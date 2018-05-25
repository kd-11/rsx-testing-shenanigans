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
	depth_buffer[0] = (u32*)rsxMemalign(64, (display_height * depth_pitch) * 2);
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
	surface_info.depthFormat      = GCM_TF_ZETA_Z16;
	surface_info.depthLocation    = GCM_LOCATION_RSX;
	surface_info.depthOffset      = depth_offset[0];
	surface_info.depthPitch       = depth_pitch;
	
	rsxSetSurface(g_context, &surface_info);
}

void render_scene()
{
	rsxSetColorMask(g_context, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
	rsxZControl(g_context, 0, 1, 1);
	rsxSetClearColor(g_context, 0);
	rsxSetClearDepthValue(g_context,0xffff);
	rsxSetDepthTestEnable(g_context, GCM_TRUE);
	rsxSetDepthFunc(g_context, GCM_LESS);
	rsxSetShadeModel(g_context, GCM_SHADE_MODEL_SMOOTH);
	rsxSetDepthWriteEnable(g_context, GCM_TRUE);
	rsxSetFrontFace(g_context, GCM_FRONTFACE_CCW);
	
	rsxClearSurface(g_context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A | GCM_CLEAR_S | GCM_CLEAR_Z);
	
	//Draw stuff
}

void do_g8b8_test()
{
	if (g_current_buffer > 2)
		return;
	
	color_buffer[g_current_buffer][0] = 0xAAFF;
	rsxSetClearColor(g_context, 0);
	rsxSetClearDepthValue(g_context,0xffff);
	rsxClearSurface(g_context, GCM_CLEAR_G | GCM_CLEAR_S | GCM_CLEAR_Z);
	
	flush_queue();
	wait_for_idle();
	
	tty_write("Removed color channel G...\n")
	tty_write("Reading sample at maddr=0x%X\n", color_buffer[g_current_buffer]);
	tty_write("Sample result was 0x%x\n", sample);

	g_running = false;
}

void do_rgba16f_test()
{
	if (g_current_buffer > 2)
		return;
	
	color_buffer[g_current_buffer][0] = 0xAAFF;
	rsxSetClearColor(g_context, 0);
	rsxSetClearDepthValue(g_context,0xffff);
	rsxClearSurface(g_context, GCM_CLEAR_B | GCM_CLEAR_G | GCM_CLEAR_R | GCM_CLEAR_A | GCM_CLEAR_S | GCM_CLEAR_Z);
	
	flush_queue();
	wait_for_idle();
	
	tty_write("Removed color channel G...\n")
	tty_write("Reading sample at maddr=0x%X\n", color_buffer[g_current_buffer]);
	tty_write("Sample result was 0x%x\n", sample);

	g_running = false;
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

int main(int argc, const char **argv)
{
	/*Exit hooks*/
	atexit(exit_func);
	sysUtilRegisterCallback(0, sysutil_exit_callback, NULL);
	
	/*video init*/
	video_bringup();
	
	g_RTT_format = 10; //G8B8
	rtt_setup(0, g_RTT_format);
	
	while (g_running)
	{
		sysUtilCheckCallback();
		
		//render_scene();
		do_g8b8_test();
		flip();
	}
	
	return 0;
}