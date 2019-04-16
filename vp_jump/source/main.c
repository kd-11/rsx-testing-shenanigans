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

const u32 vertex_program_bits[] =
{
0x1C6C, 0x80102A, 0x8106C0C3, 0x6041FFFC,
0x9C6C, 0x80922A, 0x810600C3, 0x6041DFFC,
0x11C6C, 0x80502A, 0x810600C3, 0x6041DFFC,
0x21C6C, 0x400855, 0x8106C083, 0x60407FFC,
0x21C6C, 0x400908, 0x106C083, 0x60419FFC,
0x9C6C, 0x400800, 0x106C083, 0x60403FFC,
0x1C6C, 0x1000000, 0x106C0C3, 0x6021FFFC,
0x29C6C, 0x1004000, 0x10600C3, 0x121DFFC,
0x11C6C, 0x1008200, 0x10600C3, 0xA1DFFC,
0x1C6C, 0x1002055, 0x106C0C3, 0x6021FFFC,
0x9C6C, 0x80902A, 0x868600C3, 0x6041DFFC,
0x11C6C, 0x100A255, 0x10600C3, 0x121DFFC,
0x29C6C, 0x1006055, 0x10600C3, 0x2A1DFFC,
0x29C6C, 0xC0700C, 0x186C083, 0x2A1DFFC,
0x39C6C, 0xC0B00C, 0x186C0A3, 0x2A1DFFC,
0x9C6C, 0x1008000, 0x68600C3, 0xA1DFFC,
0x19C6C, 0x140000C, 0xE860743, 0x60411FFC,
0x1C6C, 0xC0300D, 0x8186C083, 0x6021FFFC,
0x11C6C, 0x2040082A, 0x8106C080, 0x1A222FC,
0x19C6C, 0x100A055, 0x68600C3, 0xA1DFFC,
0x1F9C6C, 0x1000000D, 0x8106C09F, 0xE2A202FC,
0x9C6C, 0x8840000C, 0xA86C08C, 0xE041DFFC,
0x31C6C, 0x1082407F, 0x8A8600DF, 0xE2A3C3FC,
0x31C6C, 0x9D300C, 0xCAA80C3, 0x6041DFFC,
0x39C6C, 0x7080000C, 0xEBFC740, 0x331C37C,
0x39C6C, 0x7142500C, 0xE8600CA, 0xA329037C,
0x1F9C6C, 0x7000000D, 0x8106C095, 0x4324037C,
0x31C6C, 0x1026080, 0xE9540C0, 0x603FFC,
0x49C6C, 0xDD302A, 0x8186C0A3, 0x321DFFC,
0x1F9C6C, 0x6800000D, 0x8106C09F, 0xE322037C,
0x39C6C, 0x11D3000, 0xE80074A, 0xA0611FFC,
0x31C6C, 0x9D207F, 0x8C8000C3, 0x60403FFC,
0x39C6C, 0x70822000, 0xE8600FF, 0xE323C37C,
0x31C6C, 0x82607F, 0x8CAA80C3, 0x60403FFC,
0x41C6C, 0x82307F, 0x8C8600C3, 0x6041DFFC,
0x39C6C, 0x11D300C, 0xEBFC0C3, 0x421DFFC,
0x39C6C, 0x80000C, 0x12860743, 0x6041DFFC,
0x39C6C, 0x4882700C, 0xE8600CD, 0x2041DFFC,
0x31C6C, 0x5D302A, 0x8186C083, 0x6041DFFC,
0x39C6C, 0x5D202A, 0x8186C083, 0x6041DFFC,
0x401F9C6C, 0x40000D, 0x8086C083, 0x6041FF80,
0x401F9C6C, 0x40000D, 0x8286C083, 0x6041FF9C,
0x401F9C6C, 0x40000D, 0x8486C083, 0x6041FFA0,
0x401F9C6C, 0x40000D, 0x8686C083, 0x6041FFA4,
0x401F9C6C, 0x40000D, 0x8886C083, 0x6041FFA8,
0x401F9C6C, 0x40000C, 0xC86C083, 0x6041DFAC,
0x401F9C6C, 0x40000C, 0xE86C083, 0x6041DFB0,
0x1C6C, 0x83002A, 0x8A86C0C3, 0x6041FFFC,
0x9C6C, 0x82C02A, 0x8A86C0C3, 0x6041FFFC,
0x4011C6C, 0x102107F, 0x8A9540C0, 0x611FFC,
0x9C6C, 0x102B000, 0xA86C0C3, 0x60A1FFFC,
0x1C6C, 0x102F000, 0xA86C0C3, 0x6021FFFC,
0x401F9C6C, 0xDD302A, 0x8186C0A0, 0x1203FAC,
0x1C6C, 0x1031055, 0xA86C0C3, 0x6021FFFC,
0x9C6C, 0x102D055, 0xA86C0C3, 0x60A1FFFC,
0x9C6C, 0xC2E00D, 0x8186C083, 0x60A1FFFC,
0x1C6C, 0xC3200D, 0x8186C083, 0x6021FFFC,
0x401F9C6C, 0x400009, 0x8286C083, 0x6041BFB4,
0x401F9C6C, 0xC0007F, 0x8286C0B5, 0x40A05FB4,
0x401F9C6C, 0x400009, 0x8086C083, 0x6041BFB8,
0x401F9C6C, 0xC0007F, 0x8086C0B5, 0x40205FB9,
0x9C6C, 0x80102A, 0x8106C0C3, 0x6041FFFC,
0x1C6C, 0x82922A, 0x810400C3, 0x60419FFC,
0x9C6C, 0x1000000, 0x106C0C3, 0x60A1FFFC,
0x1C6C, 0x1028200, 0x10400C2, 0x219FFC,
0x9C6C, 0x1002055, 0x106C0C3, 0x60A1FFFC,
0x1C6C, 0x102A255, 0x10400C2, 0x219FFC,
0x9C6C, 0xC0300D, 0x8186C083, 0x60A1FFFC,
0x1C6C, 0x800000, 0x80804043, 0x60407FFC,
0x1C6C, 0xC00055, 0x86C09F, 0xE0205FFC,
0x401F9C6C, 0x400055, 0x8286C083, 0x60407F80,
0x1F9C6C, 0x2000000D, 0x8106C095, 0x4022007C,
0x4001C6C, 0x102007F, 0x82BFC0D5, 0x40605FFC,
0x1C6C, 0x80007F, 0x80840043, 0x60419FFC,
0x1C6C, 0x820008, 0x8400C3, 0x60419FFC,
0x1C6C, 0x800008, 0xBFC143, 0x60419FFC,
0x401F9C6C, 0x1000008, 0xAA8042, 0xA19F81,
0x9C6C, 0x80102A, 0x8106C0C3, 0x6041FFFC,
0x1C6C, 0x82922A, 0x810400C3, 0x60419FFC,
0x9C6C, 0x1000000, 0x106C0C3, 0x60A1FFFC,
0x1C6C, 0x1028200, 0x10400C2, 0x219FFC,
0x9C6C, 0x1002055, 0x106C0C3, 0x60A1FFFC,
0x1C6C, 0x102A255, 0x10400C2, 0x219FFC,
0x9C6C, 0xC0300D, 0x8186C083, 0x60A1FFFC,
0x1C6C, 0x800000, 0x80804043, 0x60407FFC,
0x1C6C, 0xC00055, 0x86C09F, 0xE0205FFC,
0x401F9C6C, 0x400055, 0x8286C083, 0x60407F80,
0x1F9C6C, 0x2000000D, 0x8106C095, 0x4022007C,
0x4001C6C, 0x102007F, 0x82BFC0D5, 0x40605FFC,
0x1C6C, 0x80007F, 0x80840043, 0x60419FFC,
0x1C6C, 0x820008, 0x8400C3, 0x60419FFC,
0x1C6C, 0x800008, 0xBFC143, 0x60419FFC,
0x401F9C6C, 0x1000008, 0xAA8042, 0xA19F81,
0x9C6C, 0x835011, 0x12C40C3, 0x6041FFFC,
0x1C6C, 0x43600C, 0x186C083, 0x6041DFFC,
0x11C6C, 0x5D3028, 0x8186C083, 0x60415FFC,
0x21C6C, 0x5D3000, 0x186C083, 0x60409FFC,
0x1C6C, 0x833000, 0x19540C3, 0x60403FFC,
0x9C6C, 0xC00010, 0x286C08E, 0xA0A19FFC,
0x19C6C, 0x1037808, 0x8116804B, 0x4061FFFC,
0x29C6C, 0x1037800, 0x81000040, 0x607FFC,
0x9C6C, 0x103302A, 0x828000D5, 0x40609FFC,
0x1C6C, 0x1034000, 0x28000D5, 0x40611FFC,
0x11C6C, 0x8043302A, 0x8186C08A, 0xA0A4807C,
0x9C6C, 0x8083402A, 0x848000C0, 0x29007C,
0x1C6C, 0x7880007F, 0x80AA804A, 0xA0A820FC,
0x9C6C, 0x78800000, 0x2954040, 0x29017C,
0x1C6C, 0x103302A, 0x829540CA, 0xA0411FFC,
0x1C6C, 0x103402A, 0x849540C0, 0x205FFC,
0x1C6C, 0x835000, 0x2AE80C3, 0x60419FFC,
0x21C6C, 0x103507F, 0x808040C0, 0x20207FFC,
0x9C6C, 0x805055, 0x8600C3, 0x6041DFFC,
0x1C6C, 0x801055, 0x86C0C3, 0x6041FFFC,
0x1C6C, 0x1000000, 0x106C0C3, 0x6021FFFC,
0x9C6C, 0x1004000, 0x10600C3, 0xA1DFFC,
0x21C6C, 0x4000D5, 0x886C083, 0x60411FFC,
0x11C6C, 0x40007F, 0x8886C083, 0x60409FFC,
0x21C6C, 0x4000FF, 0x8886C083, 0x60405FFC,
0x9C6C, 0x140000C, 0x8860443, 0x60403FFC,
0x9C6C, 0x1006055, 0x10600C3, 0xA1DFFC,
0x1C6C, 0x1002055, 0x106C0C3, 0x6021FFFC,
0x11C6C, 0x140000C, 0x4860243, 0x60403FFC,
0x1C6C, 0xC0300D, 0x8186C083, 0x6021FFFC,
0x9C6C, 0xC0700C, 0x186C083, 0xA1DFFC,
0x1F9C6C, 0x2000000D, 0x8106C09F, 0xE0A2027C,
0x1F9C6C, 0x2000000D, 0x8106C09F, 0xE12200FC,
0x39C6C, 0xC0B00C, 0x186C0A3, 0xA1DFFC,
0x41C6C, 0x80007F, 0x88860443, 0x6041DFFC,
0x21C6C, 0x80007F, 0x82860243, 0x6041DFFC,
0x11C6C, 0x80902A, 0x908600C3, 0x6041DFFC,
0x31C6C, 0x83600C, 0x8BFC0C3, 0x6041DFFC,
0x11C6C, 0x1008000, 0x108600C3, 0x121DFFC,
0x21C6C, 0x80902A, 0x8C8600C3, 0x6041DFFC,
0x11C6C, 0x100A055, 0x108600C3, 0x121DFFC,
0x21C6C, 0x1008000, 0xC8600C3, 0x221DFFC,
0x21C6C, 0x100A055, 0xC8600C3, 0x221DFFC,
0x9C6C, 0x140000C, 0x4860243, 0x60403FFC,
0x11C6C, 0x140000C, 0x8860443, 0x60403FFC,
0x21C6C, 0x2140000C, 0xE86075F, 0xE0B022FC,
0x9C6C, 0x20400055, 0xA86C09F, 0xE122217C,
0x11C6C, 0x20800000, 0xA86025F, 0xE223C27C,
0x21C6C, 0x80007F, 0x84860443, 0x6041DFFC,
0x29C6C, 0x10800043, 0x498445F, 0xE231C37C,
0x29C6C, 0x1000030, 0x84A18463, 0x2A1DFFC,
0x11C6C, 0x8840007F, 0x8A86C09C, 0x60403FFC,
0x21C6C, 0x140000C, 0xE860743, 0x60403FFC,
0x31C6C, 0x824000, 0xC8600C3, 0x6041DFFC,
0x31C6C, 0x209D300C, 0xCAA80DF, 0xE223C27C,
0x1F9C6C, 0x7000000D, 0x8106C080, 0x330037C,
0x39C6C, 0x7080007F, 0x8886074A, 0xA329C37C,
0x21C6C, 0x7142500C, 0xE8600D5, 0x4324237C,
0x29C6C, 0x10260FF, 0x889540C0, 0x603FFC,
0x49C6C, 0xDD3000, 0x186C0A3, 0x321DFFC,
0x1F9C6C, 0x6800000D, 0x8106C09F, 0xE2A202FC,
0x31C6C, 0x11D307F, 0x88BFC440, 0x603FFC,
0x21C6C, 0x9D207F, 0x8A8000C3, 0x60403FFC,
0x39C6C, 0x7082207F, 0x8C8600FF, 0xE223C27C,
0x21C6C, 0x82607F, 0x88AA80C3, 0x60403FFC,
0x41C6C, 0x82307F, 0x888600C3, 0x6041DFFC,
0x39C6C, 0x11D300C, 0xEBFC0C3, 0x421DFFC,
0x39C6C, 0x80000C, 0x12860743, 0x6041DFFC,
0x39C6C, 0x4882700C, 0xE8600DC, 0xA041DFFC,
0x31C6C, 0x5D3000, 0x186C083, 0x6041DFFC,
0x39C6C, 0x5D302A, 0x8186C083, 0x6041DFFC,
0x401F9C6C, 0x40000D, 0x8086C083, 0x6041FF80,
0x401F9C6C, 0x40000D, 0x8286C083, 0x6041FF9C,
0x401F9C6C, 0x40000D, 0x8486C083, 0x6041FFA0,
0x401F9C6C, 0x40000D, 0x8686C083, 0x6041FFA8,
0x401F9C6C, 0x40000C, 0x886C083, 0x6041DFA4,
0x401F9C6C, 0x40000C, 0xC86C083, 0x6041DFAC,
0x401F9C6C, 0x40000C, 0xE86C083, 0x6041DFB0,
0x401F9C6C, 0x40000C, 0xA86C083, 0x6041DFB5,
0x1C6C, 0x80102A, 0x8106C0C3, 0x6041FFFC,
0x9C6C, 0x80502A, 0x810600C3, 0x6041DFFC,
0x11C6C, 0x80922A, 0x810600C3, 0x6041DFFC,
0x19C6C, 0x400855, 0x8106C083, 0x60407FFC,
0x1C6C, 0x1000000, 0x106C0C3, 0x6021FFFC,
0x11C6C, 0x1008200, 0x10600C3, 0x121DFFC,
0x9C6C, 0x1004000, 0x10600C3, 0xA1DFFC,
0x9C6C, 0x1006055, 0x10600C3, 0xA1DFFC,
0x9C6C, 0xC0700C, 0x186C083, 0xA1DFFC,
0x21C6C, 0xC0B00C, 0x186C0A3, 0xA1DFFC,
0x1C6C, 0x1002055, 0x106C0C3, 0x6021FFFC,
0x9C6C, 0x140000C, 0x8860443, 0x60403FFC,
0x11C6C, 0x100A255, 0x10600C3, 0x121DFFC,
0x19C6C, 0x20400908, 0x106C09F, 0xE0A3817C,
0x1C6C, 0xC0300D, 0x8186C083, 0x6021FFFC,
0x9C6C, 0x10400800, 0x106C09F, 0xE130227C,
0x11C6C, 0x8840082A, 0x8106C0A0, 0x80403FFC,
0x21C6C, 0x824000, 0x88600C3, 0x6041DFFC,
0x21C6C, 0x9D300C, 0x88000C3, 0x6041DFFC,
0x1F9C6C, 0x7000000D, 0x8106C080, 0x230027C,
0x1F9C6C, 0x7000000D, 0x8106C08A, 0xA228027C,
0x1F9C6C, 0x7000000D, 0x8106C095, 0x4224027C,
0x1F9C6C, 0x4800000D, 0x8106C0A0, 0xA0401FFC,
0x21C6C, 0x5D302A, 0x8186C083, 0x6041DFFC,
0x401F9C6C, 0x40000D, 0x8086C083, 0x6041FF80,
0x401F9C6C, 0x40000D, 0x8286C083, 0x6041FF9C,
0x401F9C6C, 0x40000D, 0x8486C083, 0x6041FFA0,
0x401F9C6C, 0x40000D, 0x8686C083, 0x6041FFA8,
0x401F9C6C, 0x40000C, 0x886C083, 0x6041DFAD,
0x1C6C, 0x1002055, 0x106C0C3, 0x6021FFFC,
0x401F9C6C, 0x100307F, 0x8106C0C3, 0x6021FF81,
0x401F9C6C, 0x40000D, 0x8106C083, 0x6041FF80,
0x401F9C6C, 0x400808, 0x106C083, 0x60419F9D,
0x401F9C6C, 0x40030D, 0x8106C083, 0x6041FF9C,
0x1C6C, 0x80102A, 0x8106C0C3, 0x6041FFFC,
0x401F9C6C, 0x400808, 0x106C083, 0x60419FA0,
0x1C6C, 0x1000000, 0x106C0C3, 0x6021FFFC,
0x1C6C, 0x1002055, 0x106C0C3, 0x6021FFFC,
0x401F9C6C, 0x100307F, 0x8106C0C3, 0x6021FF81,
0x401F9C6C, 0x100307F, 0x8106C0C3, 0x6021FF81,
0x401F9C6C, 0x40000D, 0x8106C083, 0x6041FF80,
0x401F9C6C, 0x400808, 0x106C083, 0x60419F9D,
0x1C6C, 0x1022055, 0xA86C0C3, 0x6021FFFC,
0x9C6C, 0x1026055, 0xA86C0C3, 0x60A1FFFC,
0x401F9C6C, 0x80007F, 0x84860243, 0x6041DFA8,
0x9C6C, 0xC2700D, 0x8186C083, 0x60A1FFFC,
0x1C6C, 0xC2300D, 0x8186C083, 0x6021FFFC,
0x11C6C, 0x2140000C, 0x8860440, 0x1B101FC,
0x401F9C6C, 0xC0007F, 0x8086C0B5, 0x40205FB8,
0x401F9C6C, 0xC0007F, 0x8286C0B5, 0x40A05FBC,
0x401F9C6C, 0x20400009, 0x8086C080, 0x125A0B8,
0x401F9C6C, 0x10400009, 0x8286C080, 0x1B1A03C,
0x401F9C6C, 0x800055, 0x2860443, 0x6041DFB4,
0x4001C6C, 0x102A080, 0xAA80CA, 0xA0609FFC,
0x441F9C6C, 0x100C000, 0x8800CE, 0xA0619FB0,
0x401F9C6C, 0x82A02A, 0x808000C3, 0x60405FB1,
0x1C6C, 0x80102A, 0x8106C0C3, 0x6041FFFC,
0x401F9C6C, 0xC20808, 0x186C082, 0x419F9C,
0x9C6C, 0x142320C, 0x10600C3, 0x60411FFC,
0x1C6C, 0x1000000, 0x106C0C3, 0x6021FFFC,
0x443F9C6C, 0x1020000, 0x2AA80DF, 0xE0605F9C,
0x1C6C, 0x1002055, 0x106C0C3, 0x6021FFFC,
0x401F9C6C, 0xC0300D, 0x8186C083, 0x6021FF81,
0x401F9C6C, 0x40030D, 0x8106C083, 0x6041FF9C,
0x1C6C, 0x80102A, 0x8106C0C3, 0x6041FFFC,
0x401F9C6C, 0x400808, 0x106C083, 0x60419FA0,
0x1C6C, 0x1000000, 0x106C0C3, 0x6021FFFC,
0x1C6C, 0x1002055, 0x106C0C3, 0x6021FFFC,
0x401F9C6C, 0x100307F, 0x8106C0C3, 0x6021FF81,
0x401F9C6C, 0xC2080D, 0x8186C082, 0x2041FF9C,
0x1C6C, 0x80102A, 0x8106C0C3, 0x6041FFFC,
0x1C6C, 0x1000000, 0x106C0C3, 0x6021FFFC,
0x1C6C, 0x1002055, 0x106C0C3, 0x6021FFFC,
0x401F9C6C, 0x100307F, 0x8106C0C3, 0x6021FF81,
0x401F9C6C, 0x40000D, 0x8106C083, 0x6041FF80,
0x401F9C6C, 0x400808, 0x106C083, 0x60419F9D
};

const u32 fragment_program_bits[] =
{
/*	(0xF << 9) | (1 << 24), (2 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,   //MOV R0, IMM
	0x3f800000, 0, 0, 0,										          //IMM 1.f, 0.f, 0.f, 0.f
	(1 << 0), 0, 0, 0												      //END*/
	(0xF << 9) | /*IN_TC0*/(0x4 << 13) | /*TEX*/(0x17 << 24), /*INPUT_TYPE_INPUT*/(1 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,
	(0xF << 9) | /*MOV*/(0x1 << 24) | /*FP16(H0)*/(1 << 7), (0 << 0) | (0xE4 << 9) | (0x7 << 18), 0, 0,
	(1 << 0), 0, 0, 0
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
	
	tty_write("Debug: Loading fragment program...\n");
	g_fragment_program_mem = rsxMemalign(64, 1024);

	u32 tmp_fp[12];
	int i;
	for (i = 0; i < 12; ++i)
	{
		tmp_fp[i] = (fragment_program_bits[i] << 16) | (fragment_program_bits[i] >> 16);
		tty_write("FP word: 0x%x\n", tmp_fp[i]);
	}

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
	
	RSX_CONTEXT_CURRENT_BEGIN(2);
	RSX_CONTEXT_CURRENTP[0] = RSX_METHOD(0x1D60, 1); //FP_CTRL
	RSX_CONTEXT_CURRENTP[1] = 0x2000440;
	RSX_CONTEXT_CURRENT_END(2);
	
	rsxFinish(g_context, 0xAAB);
}

void load_program()
{	
	u32 in_mask = 0x4305;//(1 << 0) | (1 << 2) | (1 << 3) | (1 << 8) | (1 << 9);
	u32 out_mask = 0x3fc020;//(1 << 0) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15);
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
	clear_transform_constants();
	
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