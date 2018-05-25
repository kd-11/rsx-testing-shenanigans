#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>

#include <rsx/rsx.h>
#include <rsx/nv40.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <sys/tty.h>

#define GCM_LABEL_INDEX	  0xFF
#define CB_SIZE           (1 * 0x100000)
#define HOST_SIZE         (32 * 0x100000)

#define RSX_CONTEXT_CURRENTP					(g_context->current)

#define RSX_CONTEXT_CURRENT_BEGIN(count)\
if ((g_context->current + (count)) > g_context->end)\
{ \
	if (rsxContextCallback(g_context, (count)) != 0)\
	{\
		tty_write("Debug: RIP\n");\
		return;\
	}\
} \

#define RSX_CONTEXT_CURRENT_END(x)				g_context->current += (x)
#define RSX_METHOD_COUNT_SHIFT					(18)
#define RSX_METHOD(method, count)				(((count) << RSX_METHOD_COUNT_SHIFT) | (method))

gcmContextData *g_context = NULL;
u8 g_running = 1;

u32 color_offset[4];
u32 depth_offset[1];
u32* color_buffer[4];
u32* depth_buffer[1];

u32 color_pitch = 0;
u32 depth_pitch = 0;
u32 display_width = 0;
u32 display_height = 0;

u32 g_current_buffer = -1;
u32 g_rsx_fence_label = 1;

u32 g_RTT_format = GCM_TF_COLOR_A8R8G8B8;

void* g_fragment_program_mem = 0;
u32 g_fragment_program_offset = 0;
void* g_vertex_array_mem = 0;
u32 g_vertex_array_offset = 0;

u32 g_num_frames = 0;
u32* commandbuffer_save;

const u8 vertex_program_bits[] =
{
0x40,0x00,0x1C,0x6C, 0x00,0x00,0x00,0x01, 0x81,0x00,0xC0,0x80, 0x60,0x41,0xE0,0x7C,
0x40,0x00,0x1C,0x6C, 0x00,0x40,0x00,0x0D, 0x81,0x00,0xC0,0x80, 0x60,0x41,0xE0,0x7C,
0x40,0x1F,0x9C,0x6C, 0x00,0x40,0x00,0x0D, 0x80,0x80,0xC0,0x80, 0x60,0x41,0xFF,0x80,
0x00,0x00,0x3C,0x6C, 0x48,0x00,0x00,0x0D, 0x80,0x80,0xC0,0x80, 0xA0,0x41,0xE0,0x7C,
0x40,0x00,0x1C,0x6C, 0x00,0x80,0x00,0x0D, 0x80,0x86,0xC0,0xC0, 0x60,0x41,0xE0,0x7C,
0x40,0x00,0x9C,0x6C, 0x00,0x40,0x00,0x0D, 0x81,0x80,0xC0,0x80, 0x60,0x41,0xE0,0xFC,
0x40,0x00,0x1C,0x6C, 0x01,0x00,0x10,0x0D, 0x80,0x86,0xC1,0x43, 0x60,0x61,0xE0,0x7C,
0x40,0x1F,0x9C,0x6C, 0x00,0x40,0x00,0x0D, 0x80,0x80,0xC0,0x80, 0x60,0x41,0xFF,0x81
};

const u32 fragment_program_bits[] =
{
	(0xF << 9) | (1 << 24), (2 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,   //MOV R0, IMM
	0x3f800000, 0, 0, 0,										          //IMM 1.f, 0.f, 0.f, 0.f
	(1 << 0), 0, 0, 0												      //END
};

static s32 __attribute__((noinline)) rsxContextCallback(gcmContextData *context, u32 count)
{
	register s32 result asm("3");
	asm volatile
	(
		"stdu	1, -128(1)\n"
		"mr		31, 2\n"
		"lwz	0, 0(%0)\n"
		"lwz	2, 4(%0)\n"
		"mtctr	0\n"
		"bctrl\n"
		"mr		2, 31\n"
		"addi	1, 1, 128\n"
		: : "b"(context->callback)
		: "r31", "r0", "r1", "r2", "lr"
	);
	
	return result;
}

static void flush_queue()
{
	rsxSetWriteBackendLabel(g_context, GCM_LABEL_INDEX, g_rsx_fence_label);
	rsxFlushBuffer(g_context);

	while (*(vu32*)gcmGetLabelAddress(GCM_LABEL_INDEX) != g_rsx_fence_label)
		usleep(30);

	++g_rsx_fence_label;
}

static void wait_for_idle()
{
	rsxSetWriteBackendLabel(g_context, GCM_LABEL_INDEX, g_rsx_fence_label);
	rsxSetWaitLabel(g_context, GCM_LABEL_INDEX, g_rsx_fence_label);

	++g_rsx_fence_label;
	flush_queue();
}

static void wait_for_vblank()
{
	while (gcmGetFlipStatus() != 0)
		usleep(200);

	gcmResetFlipStatus();
}

static void tty_write(char *fmt, ...)
{
	u32 unused;
	va_list argp;
	char out[1024];
	
	va_start(argp, fmt);
	vsprintf(out, fmt, argp);
	va_end(argp);
	
	sysTtyWrite(0, out, strlen(out), &unused);
}

static void reset_commandbuffer()
{
	rsxFinish(g_context, 0xAA);
	u32 startoffs;
	rsxAddressToOffset(commandbuffer_save, &startoffs);
	rsxSetJumpCommand(g_context, startoffs);

	gcmControlRegister volatile *ctrl = gcmGetControlRegister();
	ctrl->put = startoffs;
	while (ctrl->get != startoffs) usleep(30);
	
	g_context->current = commandbuffer_save;
	g_context->begin = commandbuffer_save;
	g_context->end = g_context->begin + 16384;
}

void video_bringup()
{
	videoState state;
	videoResolution res;
	videoConfiguration vconfig;
	f32 verts[] = {-1., -1., 1., -1., 1., 1., -1., 1.};
	
	tty_write("Video bringup begins\n");
	const void* host_addr = memalign(0x100000, HOST_SIZE);
	g_context = rsxInit(CB_SIZE, HOST_SIZE, host_addr);
	
	commandbuffer_save = g_context->current;
	
	videoGetState(0, 0, &state);
	videoGetResolution(state.displayMode.resolution, &res);

	memset(&vconfig, 0, sizeof(videoConfiguration));
	vconfig.resolution = state.displayMode.resolution;
	vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
	vconfig.pitch = res.width * sizeof(u32);

	wait_for_idle();

	videoConfigure(0, &vconfig, NULL, 0);
	videoGetState(0, 0, &state);

	gcmSetFlipMode(GCM_FLIP_VSYNC);

	display_width = res.width;
	display_height = res.height;

	color_pitch = display_width * sizeof(u32);
	color_buffer[0] = (u32*)rsxMemalign(64, (display_height * color_pitch));
	color_buffer[1] = (u32*)rsxMemalign(64, (display_height * color_pitch));

	rsxAddressToOffset(color_buffer[0], &color_offset[0]);
	rsxAddressToOffset(color_buffer[1], &color_offset[1]);

	gcmSetDisplayBuffer(0, color_offset[0], color_pitch, display_width, display_height);
	gcmSetDisplayBuffer(1, color_offset[1], color_pitch, display_width, display_height);

	depth_pitch = display_width * sizeof(u32);
	depth_buffer[0] = (u32*)rsxMemalign(64, (display_height * depth_pitch) * 2);
	rsxAddressToOffset(depth_buffer[0], &depth_offset[0]);
	
	tty_write("Video bringup finished: color_buffer[0] = 0x%X, color_buffer[1] = 0x%X\n",
				color_buffer[0], color_buffer[1]);

	tty_write("Debug: offset[0] = 0x%X, offset[1] = 0x%X\n", color_offset[0], color_offset[1]);
	
	tty_write("Debug: Loading fragment program...");
	g_fragment_program_mem = rsxMemalign(64, 1024);

	u32 tmp_fp[12];
	int i;
	for (i = 0; i < 12; ++i)
		tmp_fp[i] = (fragment_program_bits[i] << 16) | (fragment_program_bits[i] >> 16);

	memcpy(g_fragment_program_mem, tmp_fp, 48);
	rsxAddressToOffset(g_fragment_program_mem, &g_fragment_program_offset);
	tty_write("done.\n");
	
	tty_write("Debug: Loading vertex array data...");
	g_vertex_array_mem = rsxMemalign(64, 1024);
	f32* dst = (f32*)g_vertex_array_mem;
	for (i = 0; i < 4; ++i)
	{
		dst[0] = verts[i * 2] * 0.8f;
		dst[1] = verts[i * 2 + 1] * 0.8f;
		dst[2] = 0.5f;
		dst[3] = 1.f;
		dst += 4;
	}
	rsxAddressToOffset(g_vertex_array_mem, &g_vertex_array_offset);
	tty_write("done.\n");
}

void rtt_setup(u32 index, u32 format)
{
	gcmSurface surface_info;
	
	/*General*/
	surface_info.type             = GCM_TF_TYPE_LINEAR;
	surface_info.antiAlias        = GCM_TF_CENTER_1;
	surface_info.width            = display_width;
	surface_info.height           = display_height;
	surface_info.x                = 0;
	surface_info.y                = 0;
	
	/*Color attachment*/
	surface_info.colorFormat        = format;
	surface_info.colorTarget        = GCM_TF_TARGET_0;
	surface_info.colorLocation[0]   = GCM_LOCATION_RSX;
	surface_info.colorOffset[0]     = color_offset[index];
	surface_info.colorPitch[0]	    = color_pitch;
	surface_info.colorLocation[1]	= GCM_LOCATION_RSX;
	surface_info.colorLocation[2]	= GCM_LOCATION_RSX;
	surface_info.colorLocation[3]	= GCM_LOCATION_RSX;
	surface_info.colorOffset[1]	    = 0;
	surface_info.colorOffset[2]	    = 0;
	surface_info.colorOffset[3]	    = 0;
	surface_info.colorPitch[1]	    = 64;
	surface_info.colorPitch[2]	    = 64;
	surface_info.colorPitch[3]      = 64;

	/*Depth attachment*/
	surface_info.depthFormat      = GCM_TF_ZETA_Z16;
	surface_info.depthLocation    = GCM_LOCATION_RSX;
	surface_info.depthOffset      = depth_offset[0];
	surface_info.depthPitch       = depth_pitch;
	
	rsxSetSurface(g_context, &surface_info);
}

void load_vp(u32* bits, u32 inst_count, u32 input_mask, u32 output_mask, u32 load_address)
{
	u32 i = 0;
	u32 pos = 0;
	u32 startIndex = load_address;
	u32* data = bits;
	
	rsxFinish(g_context, 0x0AA);
	
	RSX_CONTEXT_CURRENT_BEGIN(3);
	RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(NV40TCL_VP_UPLOAD_FROM_ID, 1);
	RSX_CONTEXT_CURRENTP[pos++] = startIndex;
	RSX_CONTEXT_CURRENTP[pos++] = startIndex;
	RSX_CONTEXT_CURRENT_END(3);

	rsxFinish(g_context, 0x0AB);
	
	pos = 0;
	RSX_CONTEXT_CURRENT_BEGIN(5 * inst_count);
	for (i = 0; i < inst_count; i++)
	{
		RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(NV40TCL_VP_UPLOAD_INST(0), 4);	
		RSX_CONTEXT_CURRENTP[pos++] = data[0];
		RSX_CONTEXT_CURRENTP[pos++] = data[1];
		RSX_CONTEXT_CURRENTP[pos++] = data[2];
		RSX_CONTEXT_CURRENTP[pos++] = data[3];
		data += 4;
	}
	RSX_CONTEXT_CURRENT_END(5 * inst_count);

	pos = 0;
	RSX_CONTEXT_CURRENT_BEGIN(3)
	RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(NV40TCL_VP_ATTRIB_EN, 2);
	RSX_CONTEXT_CURRENTP[pos++] = input_mask;
	RSX_CONTEXT_CURRENTP[pos++] = output_mask;
	RSX_CONTEXT_CURRENT_END(3);
	
	rsxFinish(g_context, 0x0AC);
	
	//Xform constants
	pos = 0;
	RSX_CONTEXT_CURRENT_BEGIN(9);
	RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(0x1F00, 8);
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f000000;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f000000;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f800000;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f800000;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f000000;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f000000;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f800000;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f800000;
	RSX_CONTEXT_CURRENT_END(9);
	
	rsxFinish(g_context, 0xAAA);
	
	pos = 0;
	RSX_CONTEXT_CURRENT_BEGIN(2);
	RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(0x1D60, 1); //FP_CTRL
	RSX_CONTEXT_CURRENTP[pos++] = 0x2000440;
	RSX_CONTEXT_CURRENT_END(2);
	
	rsxFinish(g_context, 0xAAB);
}

void load_program()
{	
	//Write VP
	load_vp
	(
		(u32*)vertex_program_bits, 			//data
		sizeof(vertex_program_bits) / 16,   //inst count
		1,					       			//in mask (POS | TEX0)
		1,						   			//out mask (POS | TEX0)
		0                          			//load address
	);
	
	//Load FP
	RSX_CONTEXT_CURRENT_BEGIN(2);
	RSX_CONTEXT_CURRENTP[0] = RSX_METHOD(NV40TCL_FP_ADDRESS, 1);
	RSX_CONTEXT_CURRENTP[1] = ((GCM_LOCATION_RSX + 1) | g_fragment_program_offset);
	RSX_CONTEXT_CURRENT_END(2);
	
	//Bind Varray
	rsxBindVertexArrayAttrib(g_context, 0, g_vertex_array_offset, 16, 4, 2/*f32*/, GCM_LOCATION_RSX);
}

void set_viewport()
{
	f32 scale[] = {display_width / 2, display_height / 2, 0.5f, 1.f};
	f32 offset[] = {display_width / 2, display_height / 2, 0.5f, 0.f};
	
	rsxSetScissor(g_context, 0, 0, display_width, display_height);
	rsxSetViewport(g_context, 0, 0, display_width, display_height, 0.f, 1.f, scale, offset);
}

void render_scene()
{
	rsxFlushBuffer(g_context);
	
	tty_write(".... Begin scene ....\n");
	rsxSetColorMask(g_context, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
	rsxZControl(g_context, 0, 1, 1);
	rsxSetClearColor(g_context, 0x000000ff);
	rsxSetClearDepthValue(g_context, 0xffff);
	rsxSetDepthTestEnable(g_context, GCM_FALSE);
	rsxSetDepthFunc(g_context, GCM_LESS);
	rsxSetShadeModel(g_context, GCM_SHADE_MODEL_SMOOTH);
	rsxSetDepthWriteEnable(g_context, GCM_TRUE);
	rsxSetFrontFace(g_context, GCM_FRONTFACE_CCW);
	rsxFinish(g_context, 0xBAA);
	
	tty_write("Viewport...");
	set_viewport();
	rsxFinish(g_context, 0xBAB);
	tty_write("done\n");
	
	tty_write("Clear...");
	rsxClearSurface(g_context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A | GCM_CLEAR_S | GCM_CLEAR_Z);
	rsxFinish(g_context, 0xBAC);
	tty_write("done\n");
	
	//Draw stuff
	tty_write("Program...");
	load_program();
	rsxFinish(g_context, 0xBAD);
	tty_write("done\n");
	
	tty_write("draw...");
	rsxDrawVertexArray(g_context, GCM_TYPE_QUADS, 0, 4);
	rsxFinish(g_context, 0xBAE);
	tty_write("done\n");
	
	tty_write(".... End scene ....\n");
	rsxFinish(g_context, 0xBAF);
	
	if (g_num_frames == 0)
	{
		flush_queue();
		
		tty_write("Sampling first color buffer, sample value=0x%X\n", color_buffer[0][0]);
		tty_write("Sampling second color buffer, sample value=0x%X\n", color_buffer[1][0]);
		
		int n;
		for (n = 0; n < display_width * display_height; n++)
		{
			if (color_buffer[0][n] != 0xff)
			{
				tty_write("Found first colored pixel at [%d, %d] (0x%x, 0x%x)\n",
							n % display_width, n / display_width, color_buffer[0][n], color_buffer[1][n]);
				break;
			}
		}
	}
	
	g_num_frames++;
}

void flip()
{
	rsxFinish(g_context, 0x77);
	
	if (g_current_buffer < 2)
	{
		wait_for_vblank();
	}
	else
	{
		/*Init flip*/
		gcmResetFlipStatus();
		g_current_buffer = 0;
	}
	
	gcmSetFlip(g_context, g_current_buffer);
	gcmSetWaitFlip(g_context);

	g_current_buffer ^= 1;
	reset_commandbuffer();
	rtt_setup(g_current_buffer, g_RTT_format);
}

void exit_func()
{
	gcmSetWaitFlip(g_context);
	rsxFinish(g_context, 1);
}

void sysutil_exit_callback(u64 status, u64 param, void *userdata)
{
	switch (status)
	{
		case SYSUTIL_EXIT_GAME:
			g_running = 0;
			return;
		case SYSUTIL_DRAW_BEGIN:
		case SYSUTIL_DRAW_END:
			return;
		default:
			return;
	}
}

int main(int argc, const char **argv)
{
	/*Exit hooks*/
	atexit(exit_func);
	sysUtilRegisterCallback(0, sysutil_exit_callback, NULL);
	
	/*video init*/
	video_bringup();
	
	rtt_setup(0, g_RTT_format);
	
	do
	{
		sysUtilCheckCallback();
		
		render_scene();
		flip();
		usleep(100000);
	}
	while (g_running);
	
	return 0;
}