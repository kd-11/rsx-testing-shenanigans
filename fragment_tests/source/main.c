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
#if 1
	// fullscreen quad passthrough shader
	// MOV o[POSITION], v[POSITION]
	0x40,0x00,0x1C,0x6C, 0x00,0x40,0x00,0x0D, 0x81,0x00,0xC0,0x80, 0x60,0x41,0xE0,0x7C,
	0x40,0x1F,0x9C,0x6C, 0x00,0x40,0x00,0x0D, 0x80,0x80,0xC0,0x80, 0x60,0x41,0xFF,0x81
#endif
#if 0
	// mov o[POSITION], v[POSITION]
    // mov o[FOGC].x, vc[0]
	0x40,0x00,0x1C,0x6C, 0x00,0x40,0x00,0x0D, 0x81,0x00,0xC0,0x80, 0x60,0x41,0xE0,0x7C,
	0x40,0x1F,0x9C,0x6C, 0x00,0x40,0x00,0x0D, 0x80,0x80,0xC0,0x80, 0x60,0x41,0xFF,0x80,
	0x40,0x00,0x1C,0x6C, 0x00,0x40,0x00,0x0D, 0x81,0x80,0xC0,0x80, 0x60,0x41,0xE0,0x7C,
	0x40,0x1F,0x9C,0x6C, 0x00,0x40,0x00,0x0D, 0x80,0x80,0xC0,0x80, 0x60,0x41,0x1F,0x95,
#endif
#if 0
	// No idea what this one does, forgot
	0x40,0x00,0x1C,0x6C, 0x00,0x40,0x00,0x0D, 0x81,0x00,0xC0,0x80, 0x60,0x41,0xE0,0x7C,
	0x40,0x1F,0x9C,0x6C, 0x00,0x40,0x00,0x0D, 0x80,0x80,0xC0,0x80, 0x60,0x41,0xFF,0x80,
	0x40,0x00,0x1C,0x6C, 0x00,0x40,0x00,0x0D, 0x81,0x80,0xC0,0x80, 0x60,0x41,0xE0,0x7C,
	0x40,0x1F,0x9C,0x6C, 0x00,0x40,0x00,0x00, 0x00,0x86,0xC0,0x83, 0x60,0x41,0x1F,0x95
#endif
};

const u32 fragment_program_bits[] =
{
	// OFFSETS
	// -----WORD0-----
	// END = 0 (1)
	// FP16 = 7 (1)
	// WRITE_MASK = 9 (4)
	// INPUT_REG_NUM (4) { varying register index }
	// PREC = 22 (2)
	// INST = 24 (5)
	// -----WORD1-----
	// INPUT = 0 (2) { 0 = temp, 1 = varying in, 2 = immediate }
	// 0xE4 = 9 { do not change this }
	// COND = 18 (3)
	// -----WORD2-----
	// 0
	// -----WORD3-----
	// 0
	
#if 0
	// COLOR = IMM (RED)
	(0xF << 9) | (1 << 24), (2 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,   //MOV R0, IMM
	0x3f800000, 0, 0, 0,										          //IMM 1.f, 0.f, 0.f, 0.f
	(1 << 0), 0, 0, 0												      //END*/
#endif
#if 0
	// COLOR = TEX(TEX0, f[TEX0]);
	(0xF << 9) | /*IN_TC0*/(0x4 << 13) | /*TEX*/(0x17 << 24), /*INPUT_TYPE_INPUT*/(1 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,
	(0xF << 9) | /*MOV*/(0x1 << 24) | /*FP16(H0)*/(1 << 7), (0 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,
	(1 << 0), 0, 0, 0
#endif
#if 1
	// COLOR = NRM(IMM)
	// NOTE: REMEMBER TO REMOVE F32 FLAG FROM FPCTRL REGISTER!
	(0x1 << 7) | (0x7 << 9) | (0x39 << 24), (2 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,   //NRM H0, IMM
	0, 0, 0, 0,										                         //IMM 0.f, 0.f, 0.f, 0.f
	(1 << 0), 0, 0, 0												         //END*/
#endif
#if 0
	// COLOR = FOGC
	// (0x3 << 9) | (0x3 << 13) | (1 << 22) | (0x1 << 24), (1 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,   //MOV R0, FOGC
	// (1 << 0), 0, 0, 0												                     			   //END*/
#endif
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

	color_pitch = display_width * sizeof(u32) * 4;
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
	
	tty_write("Debug: Allocating fragment program memory.\n");
	g_fragment_program_mem = rsxMemalign(64, 1024);
	rsxAddressToOffset(g_fragment_program_mem, &g_fragment_program_offset);
	
	tty_write("Debug: Loading vertex array data...");
	g_vertex_array_mem = rsxMemalign(64, 1024);
	f32* dst = (f32*)g_vertex_array_mem;
	
	int i;
	for (i = 0; i < 4; ++i)
	{
		dst[0] = verts[i * 2];
		dst[1] = verts[i * 2 + 1];
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
	surface_info.depthFormat      = GCM_TF_ZETA_Z24S8;
	surface_info.depthLocation    = GCM_LOCATION_RSX;
	surface_info.depthOffset      = depth_offset[0];
	surface_info.depthPitch       = depth_pitch;
	
	rsxSetSurface(g_context, &surface_info);
}

void clear_transform_constants()
{
	int constant = 0;
	for (; constant < 468; ++constant)
	{
		RSX_CONTEXT_CURRENT_BEGIN(7);
		RSX_CONTEXT_CURRENTP[0] = RSX_METHOD(0x1EFC, 1);
		RSX_CONTEXT_CURRENTP[1] = constant;
		
		RSX_CONTEXT_CURRENTP[2] = RSX_METHOD(0x1F00, 4);
		RSX_CONTEXT_CURRENTP[3] = 0;
		RSX_CONTEXT_CURRENTP[4] = 0;
		RSX_CONTEXT_CURRENTP[5] = 0;
		RSX_CONTEXT_CURRENTP[6] = 0;
	}
}

void load_vp(u32* bits, u32 inst_count, u32 input_mask, u32 output_mask, u32 load_address, u32 regs)
{
	u32 i = 0;
	u32 pos = 0;
	u32* data = bits;
	
	pos = 0;
	RSX_CONTEXT_CURRENT_BEGIN(7 * inst_count);
	for (i = 0; i < inst_count; i++)
	{
		RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(NV40TCL_VP_UPLOAD_FROM_ID, 1);
		RSX_CONTEXT_CURRENTP[pos++] = load_address + i;
	
		RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(NV40TCL_VP_UPLOAD_INST(0), 4);	
		RSX_CONTEXT_CURRENTP[pos++] = data[0];
		RSX_CONTEXT_CURRENTP[pos++] = data[1];
		RSX_CONTEXT_CURRENTP[pos++] = data[2];
		RSX_CONTEXT_CURRENTP[pos++] = data[3];
		data += 4;
	}
	RSX_CONTEXT_CURRENT_END(7 * inst_count);

	pos = 0;
	RSX_CONTEXT_CURRENT_BEGIN(3)
	RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(NV40TCL_VP_ATTRIB_EN, 2);
	RSX_CONTEXT_CURRENTP[pos++] = input_mask;
	RSX_CONTEXT_CURRENTP[pos++] = output_mask;
	RSX_CONTEXT_CURRENT_END(3);
	
	rsxFinish(g_context, 0x0AC);
	
	//Xform constants
	pos = 0;
	RSX_CONTEXT_CURRENT_BEGIN(17);
	RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(0x1F00, 16);
	RSX_CONTEXT_CURRENTP[pos++] = 0x418b26bb;//0x3f800000 | 0x80000000;//0x472ac300; //0x3f800000;
	RSX_CONTEXT_CURRENTP[pos++] = 0x41c00000;//0x472ac300;
	RSX_CONTEXT_CURRENTP[pos++] = 0x41c00000;//0x472ac300;
	RSX_CONTEXT_CURRENTP[pos++] = 0x41c00000;//0x472ac300;
	
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f800000;
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f800000;
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	RSX_CONTEXT_CURRENTP[pos++] = 0x3f800000;
	RSX_CONTEXT_CURRENT_END(17);
	
	RSX_CONTEXT_CURRENT_BEGIN(2)
	RSX_CONTEXT_CURRENTP[0] = RSX_METHOD(0x00001ff8, 1); //TRANSFORM_BRANCH_BITS
	RSX_CONTEXT_CURRENTP[1] = 0xffffffff;
	RSX_CONTEXT_CURRENT_END(2);
	
	RSX_CONTEXT_CURRENT_BEGIN(2)
	RSX_CONTEXT_CURRENTP[0] = RSX_METHOD(0x00001ef8, 1); //TRANSFORM_TIMEOUT
	RSX_CONTEXT_CURRENTP[1] = 0xffff | (regs << 16);
	RSX_CONTEXT_CURRENT_END(2);
	
	rsxFinish(g_context, 0xAAB);
}

void load_fp(u32 *ucode, u32 length, bool r0_output)
{
	tty_write("Debug: Loading fragment program 0x%p, %d bytes:\n", ucode, length);
	
	u32 tmp_fp[64];
	int i;
	for (i = 0; i < (length / 4); ++i)
	{
		tmp_fp[i] = (ucode[i] << 16) | (ucode[i] >> 16);
		tty_write("FP word: 0x%x\n", tmp_fp[i]);
	}

	memcpy(g_fragment_program_mem, tmp_fp, length);
	tty_write("done.\n");
	
	u32 fp_ctrl = 0x2000400;
	if (r0_output) fp_ctrl |= 0x40;
	
	RSX_CONTEXT_CURRENT_BEGIN(2);
	RSX_CONTEXT_CURRENTP[0] = RSX_METHOD(0x1D60, 1); //FP_CTRL
	RSX_CONTEXT_CURRENTP[1] = fp_ctrl;
	RSX_CONTEXT_CURRENT_END(2);
}

void load_program()
{	
	u32 in_mask = 0x101;//(1 << 0); // POS
	u32 out_mask = /*(1 << 4) | (0x20)*/0x4030; // POS|FOGC
	
	//Write VP
	load_vp
	(
		(u32*)vertex_program_bits, 			//data
		sizeof(vertex_program_bits) / 16,   //inst count
		in_mask,					       	//in mask
		out_mask,						   	//out mask
		0,                         			//load address
		32
	);
	
	//Load FP
	RSX_CONTEXT_CURRENT_BEGIN(2);
	RSX_CONTEXT_CURRENTP[0] = RSX_METHOD(NV40TCL_FP_ADDRESS, 1);
	RSX_CONTEXT_CURRENTP[1] = ((GCM_LOCATION_RSX + 1) | g_fragment_program_offset);
	RSX_CONTEXT_CURRENT_END(2);
	
	// TEXCOORD_CTRL
	RSX_CONTEXT_CURRENT_BEGIN(2);
	RSX_CONTEXT_CURRENTP[0] = RSX_METHOD(0x00000b40, 1);
	RSX_CONTEXT_CURRENTP[1] = 0x1;
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

void render_scene(bool allow_clear)
{
	rsxFinish(g_context, 0xAAA);
	rsxSetColorMask(g_context, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
	rsxZControl(g_context, 0, 1, 1);
	rsxSetClearColor(g_context, 0x000000ff);
	rsxSetClearDepthValue(g_context, 0xffff);
	rsxSetDepthTestEnable(g_context, GCM_FALSE);
	rsxSetDepthFunc(g_context, GCM_LESS);
	rsxSetShadeModel(g_context, GCM_SHADE_MODEL_SMOOTH);
	rsxSetDepthWriteEnable(g_context, GCM_TRUE);
	rsxSetFrontFace(g_context, GCM_FRONTFACE_CCW);
	
	rsxSetReferenceCommand(g_context, 0xABB);
	
	set_viewport();
	
	RSX_CONTEXT_CURRENT_BEGIN(4);
	RSX_CONTEXT_CURRENTP[0] = RSX_METHOD(0x000008cc, 3);
	RSX_CONTEXT_CURRENTP[1] = 0x2601; // FOG_MODE_LINEAR
	RSX_CONTEXT_CURRENTP[2] = 0x40035e4a; // PARAM0
	RSX_CONTEXT_CURRENTP[3] = 0xbd57928e; // PARAM1
	RSX_CONTEXT_CURRENT_END(4);
	
	rsxSetReferenceCommand(g_context, 0xABC);
	
	if (allow_clear)
	{
		rsxClearSurface(g_context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A | GCM_CLEAR_S | GCM_CLEAR_Z);
	}
	
	rsxSetReferenceCommand(g_context, 0xABD);
	
	load_program();
	
	rsxSetReferenceCommand(g_context, 0xABE);
	
	rsxDrawVertexArray(g_context, GCM_TYPE_QUADS, 0, 4);
	rsxFinish(g_context, 0xA00);
}

void do_fill_test()
{
	render_scene(true);
}

void execute_instruction(u32 opcode, const char* name, const u32* params)
{
	u32 shader_template[] =
	{
		(0x1 << 7) | (0x7 << 9) | (0x0 << 24), (2 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,   //NOP H0, IMM
		0, 0, 0, 0,										                         			 //IMM 0.f, 0.f, 0.f, 0.f
		(1 << 0), 0, 0, 0												         			 //END*/
	};
	
	tty_write("Generating instruction %s\n", name);
		
	shader_template[0] &= ~(0x3F << 24);
	shader_template[0] |= (opcode << 24);
	
	shader_template[4] = params[0];
	shader_template[5] = params[1];
	shader_template[6] = params[2];
	shader_template[7] = params[3];
	
	load_fp(shader_template, 48, false);

	render_scene(false);
	
	tty_write("The GPU wrote [0x%x, 0x%x, 0x%x, 0x%x] to memory\n",
				color_buffer[0][0], color_buffer[0][1], color_buffer[0][2], color_buffer[0][3]);
}

void do_sample_test()
{
	const u32 instructions[] = { /*NRM*/0x39, /*RCP*/0x1A, /*RSQ*/0x1B, /*EX2*/0x1C, /*LG2*/0x1D, /*DIV*/0x3A, /*DIVSQ*/0x3B };
	const char* instruction_names[] = { "NRM", "RCP", "RSQ", "EX2", "LG2", "EX2", "LG2", "DIV", "DIVSQ" };
	u32 values[] = { 0, 0, 0, 0 };
	
	int n;
	for (n = 0; n < sizeof(instructions) / sizeof(u32); ++ n)
	{
		execute_instruction(instructions[n], instruction_names[n], values);
	}
	
	tty_write("Executing DIV with alternating XY passes\n");
	values[1] = 0x3f800000;
	execute_instruction(0x3A, "DIV (0, 1, 0, 0)", values);
	execute_instruction(0x3B, "DIVSQ (0, 1, 0, 0)", values);
	
	values[0] = 0x3f800000;
	values[1] = 0;
	execute_instruction(0x3A, "DIV (1, 0, 0, 0)", values);
	execute_instruction(0x3B, "DIVSQ (1, 0, 0, 0)", values);
	
	values[0] = 0x7fffffff; // NaN
	execute_instruction(0x02, "MUL (0 * NaN)", values);
	
	g_running = false;
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
	
	g_RTT_format = 12; //X32
	
	/*video init*/
	video_bringup();
	clear_transform_constants();
	
	rtt_setup(0, g_RTT_format);
	
	do
	{
		sysUtilCheckCallback();
		
		do_sample_test();
		flip();
		usleep(100000);
	}
	while (g_running);
	
	return 0;
}
