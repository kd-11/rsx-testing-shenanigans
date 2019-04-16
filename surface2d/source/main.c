#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>

#include <rsx/rsx.h>
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

void video_bringup()
{
	const void* host_addr = memalign(0x100000, HOST_SIZE);
	g_context = rsxInit(CB_SIZE, HOST_SIZE, host_addr);

	videoState state;
	videoResolution res;
	videoConfiguration vconfig;
	
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
	depth_buffer[0] = (u32*)rsxMemalign(64, (display_height * depth_pitch));
	rsxAddressToOffset(depth_buffer[0], &depth_offset[0]);
	
	tty_write("Video bringup finished: color_buffer[0] = 0x%X, color_buffer[1] = 0x%X\n",
				color_buffer[0], color_buffer[1]);
	tty_write("Debug: offset[0] = 0x%X, offset[1] = 0x%X\n", color_offset[0], color_offset[1]);
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

void flip()
{
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
	rsxFlushBuffer(g_context);
	gcmSetWaitFlip(g_context);

	g_current_buffer ^= 1;
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

void write_inline(void* block, u32 block_offset, u32 X, u32 Y, u32 Sz, u32 format)
{
	memset(block, 0, 4096);
	u32 pos = 0;
	
	// Set up 3062
	RSX_CONTEXT_CURRENT_BEGIN(5);
	RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(0x00006300, 4);
	RSX_CONTEXT_CURRENTP[pos++] = format;
	RSX_CONTEXT_CURRENTP[pos++] = (4096) | (4096 << 16); // src_pitch | dst_pitch << 16
	RSX_CONTEXT_CURRENTP[pos++] = 0;
	RSX_CONTEXT_CURRENTP[pos++] = block_offset;
	RSX_CONTEXT_CURRENT_END(5);
	
	pos = 0;
	RSX_CONTEXT_CURRENT_BEGIN(4);
	RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(0x0000A304, 3); //nv308A_POINT
	RSX_CONTEXT_CURRENTP[pos++] = (Y << 16) | X;
	RSX_CONTEXT_CURRENTP[pos++] = (1 << 16) | Sz;
	RSX_CONTEXT_CURRENTP[pos++] = (1 << 16) | Sz;
	RSX_CONTEXT_CURRENT_END(4);
	
	pos = 0;
	RSX_CONTEXT_CURRENT_BEGIN(5);
	RSX_CONTEXT_CURRENTP[pos++] = RSX_METHOD(0x0000A400, 4); //nv308A_COLOR
	RSX_CONTEXT_CURRENTP[pos++] = 0xCAFEBABE;
	RSX_CONTEXT_CURRENTP[pos++] = 0xDEADBEEF;
	RSX_CONTEXT_CURRENTP[pos++] = 0xABADBABE;
	RSX_CONTEXT_CURRENTP[pos++] = 0xAABBCCDD;
	RSX_CONTEXT_CURRENT_END(5);
	
	rsxFinish(g_context, 0xA0A0);
	
	// write the 1024 words, 16 at a time
	u32 *data = (u32*)block;
	int n;
	for (n = 0; n < 1024; ++n)
	{
		if (data[n] != 0)
		{
			tty_write("[%d] 0x%x\n", n, data[n]);
		}
	}
}

void do_image_in_test()
{
	rsxFlushBuffer(g_context);
	
	tty_write(".... Clear ....\n");

	u32  dst_offset;
	u32* dst = (u32*)rsxMemalign(64, 10240);
	rsxAddressToOffset(dst, &dst_offset);
}

void do_test()
{
	tty_write("Starting test...\n");
	
	// Allocate some memory
	void* block = rsxMemalign(64, 4096);
	u32 block_offset = 0;
	rsxAddressToOffset(block, &block_offset);
	
	// Clear
	flush_queue();
	
	// Do transfers
	tty_write("------------ FORMAT Y32 --------------\n");
	write_inline(block, block_offset, 16, 0, 4, 11);
	
	tty_write("------------ FORMAT ARGB8 --------------\n");
	write_inline(block, block_offset, 16, 0, 4, 10);
	
	tty_write("------------ FORMAT RGB565 --------------\n");
	write_inline(block, block_offset, 16, 0, 4, 4);
	
	tty_write("Test finished\n");
	g_running = false;
}

int main(int argc, const char **argv)
{
	/*Exit hooks*/
	atexit(exit_func);
	sysUtilRegisterCallback(0, sysutil_exit_callback, NULL);
	
	/*video init*/
	video_bringup();
	rtt_setup(0, g_RTT_format);
	
	while (g_running)
	{
		sysUtilCheckCallback();
		
		do_test();
	}
	
	return 0;
}