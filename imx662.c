// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Sony imx662 cameras.
 *
 * Based on Sony imx477 camera driver
 * Copyright (C) 2019-2020 Raspberry Pi (Trading) Ltd
 * Modified by Will WHANG
 * Modified by sohonomura2020 in Soho Enterprise Ltd.
 * Modified by OCTOPUSCINEMA
 * Copyright (C) 2024 OCTOPUS CINEMA
 */
#include <linux/unaligned.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

// Support for rpi kernel pre git commit 314a685
//#ifndef MEDIA_BUS_FMT_SENSOR_DATA
//#define MEDIA_BUS_FMT_SENSOR_DATA 		0x7002
//#endif

/* Chip ID */
#define IMX662_REG_CHIP_ID			0x30DC
#define IMX662_CHIP_ID				0x32

/* Standby or streaming mode */
#define IMX662_REG_MODE_SELECT			0x3000
#define IMX662_MODE_STANDBY			0x01
#define IMX662_MODE_STREAMING			0x00
#define IMX662_STREAM_DELAY_US			25000
#define IMX662_STREAM_DELAY_RANGE_US		1000

/* In clk */
#define IMX662_XCLK_FREQ			24000000

/* VMAX internal VBLANK*/
#define IMX662_REG_VMAX				0x3028
#define IMX662_VMAX_MAX				0xfffff

/* HMAX internal HBLANK*/
#define IMX662_REG_HMAX				0x302C
#define IMX662_HMAX_MAX				0xffff

/* SHR internal */
#define IMX662_REG_SHR				0x3050
#define IMX662_SHR_MIN				11

/* Exposure control */
#define IMX662_EXPOSURE_MIN			52
#define IMX662_EXPOSURE_STEP			1
#define IMX662_EXPOSURE_DEFAULT			1000
#define IMX662_EXPOSURE_MAX			49865

/* HDR threshold */
#define IMX662_REG_EXP_TH_H			0x36D0
#define IMX662_REG_EXP_TH_L			0x36D4
#define IMX662_REG_EXP_BK			0x36E2

/* Gradation compression control */
#define IMX662_REG_CCMP1_EXP			0x36E8
#define IMX662_REG_CCMP2_EXP			0x36E4
#define IMX662_REG_ACMP1_EXP			0x36EE
#define IMX662_REG_ACMP2_EXP			0x36EC

/* Black level control */
#define IMX662_REG_BLKLEVEL			0x30DC
#define IMX662_BLKLEVEL_DEFAULT			50 // 0x32

/* Digital Clamp */
#define IMX662_REG_DIGITAL_CLAMP		0x3458

/* Analog gain control */
#define IMX662_REG_ANALOG_GAIN			0x306C
#define IMX662_REG_FDG_SEL0			0x3030
#define IMX662_ANA_GAIN_MIN			0
#define IMX662_ANA_GAIN_MAX			240 // x3980= 72db = 0.3db x 240
#define IMX662_ANA_GAIN_STEP			1
#define IMX662_ANA_GAIN_DEFAULT			0
#define IMX662_ANA_GAIN_HCG_LEVEL		51 // = 15.3db / 0.3db
#define IMX662_ANA_GAIN_HCG_THRESHOLD	(IMX662_ANA_GAIN_HCG_LEVEL+29)
#define IMX662_ANA_GAIN_HCG_MIN			34

/* Flip */
#define IMX662_FLIP_WINMODEH    		0x3020
#define IMX662_FLIP_WINMODEV    		0x3021

/* Embedded metadata stream structure */
#define IMX662_EMBEDDED_LINE_WIDTH 		16384
#define IMX662_NUM_EMBEDDED_LINES 		1

#define IMX662_PIXEL_RATE			74250000

enum pad_types {
	IMAGE_PAD,
	METADATA_PAD,
	NUM_PADS
};


/* Gradation compression */
enum v4l2_xfer_func_sony {
	V4L2_XFER_FUNC_GRADATION_COMPRESSION = 10
};

/* imx662 native and active pixel array size. */
#define IMX662_NATIVE_WIDTH		1936U
#define IMX662_NATIVE_HEIGHT		1100U
#define IMX662_PIXEL_ARRAY_LEFT		8U
#define IMX662_PIXEL_ARRAY_TOP		8U
#define IMX662_PIXEL_ARRAY_WIDTH	1920U
#define IMX662_PIXEL_ARRAY_HEIGHT	1080U

/* Link frequency setup */
enum {
    IMX662_LINK_FREQ_297MHZ, // 594Mbps/lane
    IMX662_LINK_FREQ_360MHZ, // 720Mbps/lane
    IMX662_LINK_FREQ_445MHZ, // 891Mbps/lane
    IMX662_LINK_FREQ_594MHZ, // 1188Mbps/lane
    IMX662_LINK_FREQ_720MHZ, // 1440Mbps/lane
    IMX662_LINK_FREQ_891MHZ, // 1782Mbps/lane
    IMX662_LINK_FREQ_1039MHZ, // 2079Mbps/lane
    IMX662_LINK_FREQ_1188MHZ, // 2376Mbps/lane
};

static const s64 link_freqs_reg_value[] = {
    [IMX662_LINK_FREQ_297MHZ]  = 0x07,
    [IMX662_LINK_FREQ_360MHZ]  = 0x06,
    [IMX662_LINK_FREQ_445MHZ]  = 0x05,
    [IMX662_LINK_FREQ_594MHZ]  = 0x04,
    [IMX662_LINK_FREQ_720MHZ]  = 0x03,
    [IMX662_LINK_FREQ_891MHZ]  = 0x02,
    [IMX662_LINK_FREQ_1039MHZ] = 0x01,
    [IMX662_LINK_FREQ_1188MHZ] = 0x00,
};

static const s64 link_freqs[] = {
    [IMX662_LINK_FREQ_297MHZ]  = 297000000,
    [IMX662_LINK_FREQ_360MHZ]  = 360000000,
    [IMX662_LINK_FREQ_445MHZ]  = 445500000,
    [IMX662_LINK_FREQ_594MHZ]  = 594000000,
    [IMX662_LINK_FREQ_720MHZ]  = 720000000,
    [IMX662_LINK_FREQ_891MHZ]  = 891000000,
    [IMX662_LINK_FREQ_1039MHZ] = 1039500000,
    [IMX662_LINK_FREQ_1188MHZ] = 1188000000,
};

//4K min HMAX for 4-lane, times 2 for 2-lane
// Wrong. Because of digital binning, HMAX and VMAX are the same in both modes.
static const s64 HMAX_table_4lane_2K[] = {
    [IMX662_LINK_FREQ_297MHZ] =  990,	// 	1584,
    [IMX662_LINK_FREQ_360MHZ] =  990,	// 	1320,
    [IMX662_LINK_FREQ_445MHZ] =  990,	// 	1100,
    [IMX662_LINK_FREQ_594MHZ] =  660,	// 	792,
    [IMX662_LINK_FREQ_720MHZ] =  660,	// 	660,
    [IMX662_LINK_FREQ_891MHZ] =  660,	// 	550,
    [IMX662_LINK_FREQ_1039MHZ] = 550,	// 	440,
    [IMX662_LINK_FREQ_1188MHZ] = 550,	// 	396,
};

struct imx662_reg {
	u16 address;
	u8 val;
};

struct IMX662_reg_list {
	unsigned int num_of_regs;
	const struct imx662_reg *regs;
};

/* Mode : resolution and related config&values */
struct imx662_mode {
	/* Frame width */
	unsigned int width;

	/* Frame height */
	unsigned int height;

	/* mode uses Clear HDR */
	bool hdr;

	/* mode has linear output (gradation compression disabled) */
	bool linear;

	/* minimum H-timing */
	uint64_t min_HMAX;

	/* minimum V-timing */
	uint64_t min_VMAX;

	/* default H-timing */
	uint64_t default_HMAX;

	/* default V-timing */
	uint64_t default_VMAX;

	/* minimum SHR */
	uint64_t min_SHR;

	/* Analog crop rectangle. */
	struct v4l2_rect crop;

	/* Default register values */
	struct IMX662_reg_list reg_list;
};

/* Common Modes */
struct imx662_reg mode_common_regs[] = {

{0x3000, 0x01}, // STANDBY
{0x3001, 0x00}, // REGHOLD
{0x3002, 0x00}, // Master mode start
{0x3014, 0x01}, // INCK_SEL[3:0] 0x01=37.125MHz, 0x04=24MHz
{0x3015, 0x02}, // DATARATE_SEL[3:0] 0x02=1782Mbps, 0x03=1440Mbps, 0x04=1188Mbps
{0x3018, 0x00}, // WINMODE[4:0] All-pixel
{0x301A, 0x00}, // WDMODE Normal mode
// {0x301B, 0x00}, // ADDMODE[1:0] full resolution, 2x2 binning
{0x301C, 0x00}, // THIN_V_EN
{0x301E, 0x01}, // VCMODE
{0x3020, 0x00}, // HREVERSE
{0x3021, 0x00}, // VREVERSE
{0x3022, 0x01}, // ADBIT[1:0]  12bit should be 0x01 for imx662
{0x3023, 0x01}, // MDBIT       12bit
{0x3030, 0x00}, // FDG_SEL0[1:0]
{0x3031, 0x00}, // FDG_SEL1[1:0]
{0x3032, 0x00}, // FDG_SEL2[1:0]
{0x303C, 0x00}, // PIX_HST[12:0]
{0x303D, 0x00}, // PIX_HST[12:0]

{0x303E, 0x90}, // PIX_HWIDTH[12:0] 1936
{0x303F, 0x07}, // PIX_HWIDTH[12:0] 1936

{0x3040, 0x01}, // LANEMODE[2:0] 2lane
// No {0x3042, 0x00}, // XSIZE_OVERLAP[10:0]
// No {0x3043, 0x00}, // XSIZE_OVERLAP[10:0]

{0x3044, 0x00}, // PIX_VST[11:0]
{0x3045, 0x00}, // PIX_VST[11:0]
{0x3046, 0x4c}, // PIX_VWIDTH[11:0] 1100
{0x3047, 0x04}, // PIX_VWIDTH[11:0] 1100

// {0x3050, 0x03}, // SHR0[19:0]
// {0x3051, 0x00}, // SHR0[19:0]
// {0x3052, 0x00}, // SHR0[19:0]
{0x3054, 0x0E}, // SHR1[19:0]
{0x3055, 0x00}, // SHR1[19:0]
{0x3056, 0x00}, // SHR1[19:0]
{0x3058, 0x8A}, // SHR2[19:0]
{0x3059, 0x01}, // SHR2[19:0]
{0x305A, 0x00}, // SHR2[19:0]
{0x3060, 0x16}, // RHS1[19:0]
{0x3061, 0x01}, // RHS1[19:0]
{0x3062, 0x00}, // RHS1[19:0]
{0x3064, 0xC4}, // RHS2[19:0]
{0x3065, 0x0C}, // RHS2[19:0]
{0x3066, 0x00}, // RHS2[19:0]

{0x3069, 0x00}, // CHDR_GAIN_EN
{0x306B, 0x00}, // Sensor_register
{0x3070, 0x00}, // GAIN[10:0]
{0x3071, 0x00}, // GAIN[10:0]
{0x3072, 0x00}, // GAIN_1[10:0]
{0x3073, 0x00}, // GAIN_1[10:0]
{0x3074, 0x00}, // GAIN_2[10:0]
{0x3075, 0x00}, // GAIN_2[10:0]
{0x3081, 0x00}, // EXP_GAIN

{0x308C, 0x00}, // CHDR_DGAIN0_HG[15:0]
{0x308D, 0x01}, // CHDR_DGAIN0_HG[15:0]
{0x3094, 0x00}, // CHDR_AGAIN0_LG[10:0]
{0x3095, 0x00}, // CHDR_AGAIN0_LG[10:0]
{0x3096, 0x00}, // CHDR_AGAIN1_LG[10:0]
{0x3097, 0x00}, // CHDR_AGAIN1_LG[10:0]

{0x309C, 0x00}, // CHDR_AGAIN0_HG[10:0]
{0x309D, 0x00}, // CHDR_AGAIN0_HG[10:0]

{0x30A4, 0xAA}, // XVSOUTSEL[1:0]
{0x30A6, 0x0F}, // XVS_DRV[1:0] HiZ

{0x30CC, 0x00}, // XVSLNG
{0x30CD, 0x00}, // XHSLNG

{0x30DC, 0x32}, // BLKLEVEL[11:0]
{0x30DD, 0x40}, // BLKLEVEL[11:0]

{0x3400, 0x01}, // GAIN_PGC_FIDMD

{0x3444, 0xAC}, // Sensor_register
{0x3460, 0x21}, // Sensor_register

        {0x3492, 0x08}, // RESERVED

/* switch by all-pixel / 2/2line binning
        {0x3A50, 0xFF}, // Normal 12bit
        {0x3A51, 0x03}, // Normal 12bit
        {0x3A52, 0x00}, // AD 12bit
*/

        {0x3B00, 0x39}, // RESERVED
        {0x3B23, 0x2D}, // RESERVED
        {0x3B45, 0x04}, // RESERVED
        {0x3C0A, 0x1F}, // RESERVED
        {0x3C0B, 0x1E}, // RESERVED
        {0x3C38, 0x21}, // RESERVED
        {0x3C40, 0x06}, // Normal mode. CHDR=05h
        {0x3C44, 0x00}, // RESERVED
        {0x3CB6, 0xD8}, // RESERVED
        {0x3CC4, 0xDA}, // RESERVED
        {0x3E24, 0x79}, // RESERVED
        {0x3E2C, 0x15}, // RESERVED
        {0x3EDC, 0x2D}, // RESERVED
        {0x4498, 0x05}, // RESERVED
        {0x449C, 0x19}, // RESERVED
        {0x449D, 0x00}, // RESERVED
        {0x449E, 0x32}, // RESERVED
        {0x449F, 0x01}, // RESERVED
        {0x44A0, 0x92}, // RESERVED
        {0x44A2, 0x91}, // RESERVED
        {0x44A4, 0x8C}, // RESERVED
        {0x44A6, 0x87}, // RESERVED
        {0x44A8, 0x82}, // RESERVED
        {0x44AA, 0x78}, // RESERVED
        {0x44AC, 0x6E}, // RESERVED
        {0x44AE, 0x69}, // RESERVED
        {0x44B0, 0x92}, // RESERVED
        {0x44B2, 0x91}, // RESERVED
        {0x44B4, 0x8C}, // RESERVED
        {0x44B6, 0x87}, // RESERVED
        {0x44B8, 0x82}, // RESERVED
        {0x44BA, 0x78}, // RESERVED
        {0x44BC, 0x6E}, // RESERVED
        {0x44BE, 0x69}, // RESERVED
        {0x44C1, 0x01}, // RESERVED
        {0x44C2, 0x7F}, // RESERVED
        {0x44C3, 0x01}, // RESERVED
        {0x44C4, 0x7A}, // RESERVED
        {0x44C5, 0x01}, // RESERVED
        {0x44C6, 0x7A}, // RESERVED
        {0x44C7, 0x01}, // RESERVED
        {0x44C8, 0x70}, // RESERVED
        {0x44C9, 0x01}, // RESERVED
        {0x44CA, 0x6B}, // RESERVED
        {0x44CB, 0x01}, // RESERVED
        {0x44CC, 0x6B}, // RESERVED
        {0x44CD, 0x01}, // RESERVED
        {0x44CE, 0x5C}, // RESERVED
        {0x44CF, 0x01}, // RESERVED
        {0x44D0, 0x7F}, // RESERVED
        {0x44D1, 0x01}, // RESERVED
        {0x44D2, 0x7F}, // RESERVED
        {0x44D3, 0x01}, // RESERVED
        {0x44D4, 0x7A}, // RESERVED
        {0x44D5, 0x01}, // RESERVED
        {0x44D6, 0x7A}, // RESERVED
        {0x44D7, 0x01}, // RESERVED
        {0x44D8, 0x70}, // RESERVED
        {0x44D9, 0x01}, // RESERVED
        {0x44DA, 0x6B}, // RESERVED
        {0x44DB, 0x01}, // RESERVED
        {0x44DC, 0x6B}, // RESERVED
        {0x44DD, 0x01}, // RESERVED
        {0x44DE, 0x5C}, // RESERVED
        {0x44DF, 0x01}, // RESERVED
        {0x4534, 0x1C}, // RESERVED
        {0x4535, 0x03}, // RESERVED
        {0x4538, 0x1C}, // RESERVED
        {0x4539, 0x1C}, // RESERVED
        {0x453A, 0x1C}, // RESERVED
        {0x453B, 0x1C}, // RESERVED
        {0x453C, 0x1C}, // RESERVED
        {0x453D, 0x1C}, // RESERVED
        {0x453E, 0x1C}, // RESERVED
        {0x453F, 0x1C}, // RESERVED
        {0x4540, 0x1C}, // RESERVED
        {0x4541, 0x03}, // RESERVED
        {0x4542, 0x03}, // RESERVED
        {0x4543, 0x03}, // RESERVED
        {0x4544, 0x03}, // RESERVED
        {0x4545, 0x03}, // RESERVED
        {0x4546, 0x03}, // RESERVED
        {0x4547, 0x03}, // RESERVED
        {0x4548, 0x03}, // RESERVED
        {0x4549, 0x03}, // RESERVED

        {0x3030, 0x00}, // FDG_SEL1
        {0x3031, 0x00}, // FDG_SEL1



// #### Streaming start ####
{0x3000, 0x00}, // STREAMING. Should be controled by App.

};

/* All pixel 2K60. 12-bit (Normal) */
static const struct imx662_reg mode_2k_regs[] = {
	{0x301A, 0x00}, // WDMODE Normal mode
	{0x301B, 0x00}, // ADDMODE non-binning
	{0x3022, 0x00}, // ADBIT 10-bit
	{0x3023, 0x01}, // MDBIT 12-bit

        {0x3A50, 0x62}, // Normal 12bit
        {0x3A51, 0x01}, // Normal 12bit
        {0x3A52, 0x19}, // AD 12bit
//        {0x3A50, 0xFF}, // Normal 12bit
//        {0x3A51, 0x03}, // Normal 12bit
//        {0x3A52, 0x00}, // AD 12bit
};

/* 2x2 binned 540p90. 12-bit (Normal) */
static const struct imx662_reg mode_540_regs[] = {
	{0x301A, 0x00}, // WDMODE Normal mode
	{0x301B, 0x01}, // ADDMODE binning
	{0x3022, 0x00}, // ADBIT 10-bit 
	{0x3023, 0x01}, // MDBIT 12-bit

        {0x3A50, 0x62}, // Normal 12bit
        {0x3A51, 0x01}, // Normal 12bit
        {0x3A52, 0x19}, // AD 12bit
};

// ######## Dummy modes ########
/* All pixel 4K30. 12-bit (HDR gradation compression) */
static const struct imx662_reg mode_4k_nonlinear_regs[] = {
	{0x301A, 0x10}, // WDMODE Clear HDR
//	{0x301B, 0x00}, // ADDMODE Non-binning
	{0x3022, 0x01}, // ADBIT 11+1bit dither
	{0x3023, 0x01}, // MDBIT 12-bit
};

/* All pixel 4K30. 16-bit (Clear HDR) */
/* Dummy */
static const struct imx662_reg mode_4k_16bit_regs[] = {
    //{0x301A, 0x10}, // WDMODE Clear HDR
//    {0x301B, 0x00}, // ADDMODE Non-binning
    {0x3022, 0x01}, // ADBIT 12-bit
};

/* 2x2 binned 1080p30. 16-bit (Clear HDR) */
/* Dummy */
static const struct imx662_reg mode_1080_16bit_regs[] = {
    //{0x301A, 0x10}, // WDMODE Clear HDR
//    {0x301B, 0x01}, // ADDMODE Binning
};
// ######## Dummy modes ########

/* Mode configs */
struct imx662_mode supported_modes_12bit[] = {
/*
	{
	//	540p90 2x2 binning
		.width = 968, // 1936,
		.height = 550, // 1096,
		.hdr = false,
		.linear = true,
		.min_HMAX = 990,
		.min_VMAX = 1250,
		.default_HMAX = 990,
		.default_VMAX = 1250,
		.min_SHR = 20,
		.crop = {
			.left = IMX662_PIXEL_ARRAY_LEFT,
			.top = IMX662_PIXEL_ARRAY_TOP,
			.width = IMX662_PIXEL_ARRAY_WIDTH,
			.height = IMX662_PIXEL_ARRAY_HEIGHT,
		},
		.reg_list = {
		.num_of_regs = ARRAY_SIZE(mode_540_regs),
		.regs = mode_540_regs,
		},
	},
*/
	{
	//	2K60 All pixel */
		.width = 1936, // 1936,
		.height = 1100, // 1096,
		.hdr = false,
		.linear = true,
		.min_HMAX = 990,
		.min_VMAX = 1250,
		.default_HMAX = 990,
		.default_VMAX = 1250,
		.min_SHR = 20,
		.crop = {
			.left = IMX662_PIXEL_ARRAY_LEFT,
			.top = IMX662_PIXEL_ARRAY_TOP,
			.width = IMX662_PIXEL_ARRAY_WIDTH,
			.height = IMX662_PIXEL_ARRAY_HEIGHT,
		},
		.reg_list = {
		.num_of_regs = ARRAY_SIZE(mode_2k_regs),
		.regs = mode_2k_regs,
		}, 
	}
};

// ######## Dummy modes ########
struct imx662_mode supported_modes_nonlinear_12bit[] = {
    {
        /* 1080P30 All pixel */
        .width = 1936,
        .height = 1096,
        .hdr = true,
        .linear = false,
        .min_HMAX = 366, // Clear HDR original
        .min_VMAX = 2250, // Clear HDR original
        .default_HMAX = 366,
        .default_VMAX = 2250,
        .min_SHR = 20,
        .crop = {
            .left = IMX662_PIXEL_ARRAY_LEFT,
            .top = IMX662_PIXEL_ARRAY_TOP,
            .width = IMX662_PIXEL_ARRAY_WIDTH,
            .height = IMX662_PIXEL_ARRAY_HEIGHT,
        },
        .reg_list = {
            .num_of_regs = ARRAY_SIZE(mode_4k_nonlinear_regs),
            .regs = mode_4k_nonlinear_regs,
        },
    },
	{
		/* 4K30 All pixel */
		.width = 3856,
		.height = 2180,
		.hdr = true,
		.linear = false,
		//.min_HMAX = 760,
		.min_HMAX = 550, // Clear HDR original
		//.min_VMAX = 2250,
		.min_VMAX = 4500, // Clear HDR original
		.default_HMAX = 550,
		.default_VMAX = 4500,
		// .default_HMAX = 550,
		// .default_VMAX = 4500,
		.min_SHR = 20,
		.crop = {
			.left = IMX662_PIXEL_ARRAY_LEFT,
			.top = IMX662_PIXEL_ARRAY_TOP,
			.width = IMX662_PIXEL_ARRAY_WIDTH,
			.height = IMX662_PIXEL_ARRAY_HEIGHT,
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4k_nonlinear_regs),
			.regs = mode_4k_nonlinear_regs,
		},
	},
};

// ######## Dummy modes ########
struct imx662_mode supported_modes_16bit[] = {
	{
		/* 1080p30 2x2 binning */
		.width = 1928,
		.height = 1090,
		.hdr = true,
		.linear = true,
		//.min_HMAX = 760,
		.min_HMAX = 550, // Clear HDR original
		//.min_VMAX = 2250,
		.min_VMAX = 4500, // Clear HDR original
		.default_HMAX = 550,
		.default_VMAX = 4500,
		// .default_HMAX = 550,
		// .default_VMAX = 4500,
		.min_SHR = 20,
		.crop = {
			.left = IMX662_PIXEL_ARRAY_LEFT,
			.top = IMX662_PIXEL_ARRAY_TOP,
			.width = IMX662_PIXEL_ARRAY_WIDTH,
			.height = IMX662_PIXEL_ARRAY_HEIGHT,
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1080_16bit_regs),
			.regs = mode_1080_16bit_regs,
		},
	},
	{
		/* 4K30 All pixel */
		.width = 3856,
		.height = 2180,
		.hdr = true,
		.linear = true,
		//.min_HMAX = 760,
		.min_HMAX = 550, // Clear HDR original
		//.min_VMAX = 2250,
		.min_VMAX = 4500, // Clear HDR original
		.default_HMAX = 550,
		.default_VMAX = 4500,
		// .default_HMAX = 550,
		// .default_VMAX = 4500,
		.min_SHR = 20,
		.crop = {
			.left = IMX662_PIXEL_ARRAY_LEFT,
			.top = IMX662_PIXEL_ARRAY_TOP,
			.width = IMX662_PIXEL_ARRAY_WIDTH,
			.height = IMX662_PIXEL_ARRAY_HEIGHT,
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4k_16bit_regs),
			.regs = mode_4k_16bit_regs,
		},
	},
};

/*
 * The supported formats.
 * This table MUST contain 4 entries per format, to cover the various flip
 * combinations in the order
 * - no flip
 * - h flip
 * - v flip
 * - h&v flips
 */
static const u32 codes[] = {
	/* 16-bit modes. */
	MEDIA_BUS_FMT_SRGGB16_1X16,
	MEDIA_BUS_FMT_SGRBG16_1X16,
	MEDIA_BUS_FMT_SGBRG16_1X16,
	MEDIA_BUS_FMT_SBGGR16_1X16,
	/* 12-bit modes. */
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SBGGR12_1X12,
};


static const u32 mono_codes[] = {
    /* 16-bit modes. */
    MEDIA_BUS_FMT_Y16_1X16,
    /* 12-bit modes. */
    MEDIA_BUS_FMT_Y12_1X12,
};


/* regulator supplies */
static const char * const imx662_supply_name[] = {
	/* Supplies can be enabled in any order */
	"VANA",  /* Analog (3.3V) supply */
	"VDIG",  /* Digital Core (1.1V) supply */
	"VDDL",  /* IF (1.8V) supply */
};

#define imx662_NUM_SUPPLIES ARRAY_SIZE(imx662_supply_name)

/*
 * Initialisation delay between XCLR low->high and the moment when the sensor
 * can start capture (i.e. can leave software standby)
 */
#define imx662_XCLR_MIN_DELAY_US	500000
#define imx662_XCLR_DELAY_RANGE_US	1000

struct imx662_compatible_data {
	unsigned int chip_id;
	struct IMX662_reg_list extra_regs;
};

struct imx662 {
	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	unsigned int fmt_code;

	struct clk *xclk;
	u32 xclk_freq;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[imx662_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *pixel_rate;
    	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	/* Current mode */
	const struct imx662_mode *mode;

    /* Mono mode */
    bool mono;

    unsigned int lane_count;
    unsigned int link_freq_idx;

	uint16_t HMAX;
	uint32_t VMAX;
	/*
	 * Mutex for serialized access:
	 * Protect sensor module set pad format and start/stop streaming safely.
	 */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;

	/* Rewrite common registers on stream on? */
	bool common_regs_written;

	/* Any extra information related to different compatible sensors */
	const struct imx662_compatible_data *compatible_data;
};

static inline struct imx662 *to_imx662(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx662, sd);
}

static inline void get_mode_table(struct imx662 *imx662, unsigned int code, enum v4l2_xfer_func transfer_function,
				  const struct imx662_mode **mode_list,
				  unsigned int *num_modes)
{

    if(imx662->mono){
        switch (code) {
        case MEDIA_BUS_FMT_Y16_1X16:
            *mode_list = supported_modes_16bit;
            *num_modes = ARRAY_SIZE(supported_modes_16bit);
            break;
        case MEDIA_BUS_FMT_Y12_1X12:
            if ( transfer_function == (enum v4l2_xfer_func)V4L2_XFER_FUNC_GRADATION_COMPRESSION ) {
                *mode_list = supported_modes_nonlinear_12bit;
                *num_modes = ARRAY_SIZE(supported_modes_nonlinear_12bit);
            } else {
                *mode_list = supported_modes_12bit;
                *num_modes = ARRAY_SIZE(supported_modes_12bit);
            }
            break;
        default:
            *mode_list = NULL;
            *num_modes = 0;
        }
    }
    else{
        switch (code) {
        /* 16-bit */
        case MEDIA_BUS_FMT_SRGGB16_1X16:
        case MEDIA_BUS_FMT_SGRBG16_1X16:
        case MEDIA_BUS_FMT_SGBRG16_1X16:
        case MEDIA_BUS_FMT_SBGGR16_1X16:
            *mode_list = supported_modes_16bit;
            *num_modes = ARRAY_SIZE(supported_modes_16bit);
            break;
        /* 12-bit */
        case MEDIA_BUS_FMT_SRGGB12_1X12:
        case MEDIA_BUS_FMT_SGRBG12_1X12:
        case MEDIA_BUS_FMT_SGBRG12_1X12:
        case MEDIA_BUS_FMT_SBGGR12_1X12:
            if ( transfer_function == (enum v4l2_xfer_func)V4L2_XFER_FUNC_GRADATION_COMPRESSION ) {
                *mode_list = supported_modes_nonlinear_12bit;
                *num_modes = ARRAY_SIZE(supported_modes_nonlinear_12bit);
            } else {
                *mode_list = supported_modes_12bit;
                *num_modes = ARRAY_SIZE(supported_modes_12bit);
            }
            break;
        default:
            *mode_list = NULL;
            *num_modes = 0;
        }
    }
}

/* Read registers up to 2 at a time */
static int imx662_read_reg(struct imx662 *imx662, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { reg >> 8, reg & 0xff };
	u8 data_buf[4] = { 0, };
	int ret;

	if (len > 4)
		return -EINVAL;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

/* Write registers 1 byte at a time */
static int imx662_write_reg_1byte(struct imx662 *imx662, u16 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	u8 buf[3];
	int ret;

	put_unaligned_be16(reg, buf);
	buf[2] = val;
	ret = i2c_master_send(client, buf, 3);
	if ( ret != 3 )
		return ret;

	return 0;
}

/* Write registers 2 byte at a time */
static int imx662_write_reg_2byte(struct imx662 *imx662, u16 reg, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	u8 buf[4];
	int ret;

	put_unaligned_be16(reg, buf);
	buf[2] = val;
	buf[3] = val>>8;
	ret = i2c_master_send(client, buf, 4);
	if ( ret != 4 )
		return ret;

	return 0;
}

/* Write registers 3 byte at a time */
static int imx662_write_reg_3byte(struct imx662 *imx662, u16 reg, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	u8 buf[5];

	put_unaligned_be16(reg, buf);
	buf[2]  = val;
	buf[3]  = val>>8;
	buf[4]  = val>>16;
	if (i2c_master_send(client, buf, 5) != 5)
		return -EIO;

	return 0;
}

/* Write a list of 1 byte registers */
static int imx662_write_regs(struct imx662 *imx662,
			     const struct imx662_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx662_write_reg_1byte(imx662, regs[i].address, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
						"Failed to write reg 0x%4.4x. error = %d\n",
						regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

/* Hold register values until hold is disabled */
static inline void imx662_register_hold(struct imx662 *imx662, bool hold)
{
	imx662_write_reg_1byte(imx662, 0x3001, hold ? 1 : 0);
}

/* Get bayer order based on flip setting. */
static u32 imx662_get_format_code(struct imx662 *imx662, u32 code)
{
	unsigned int i;
	lockdep_assert_held(&imx662->mutex);

    if(imx662->mono){
        for (i = 0; i < ARRAY_SIZE(mono_codes); i++)
            if (mono_codes[i] == code)
                break;
        return mono_codes[i];
    }
    else{
        for (i = 0; i < ARRAY_SIZE(codes); i++)
            if (codes[i] == code)
                break;
        return codes[i];
    }

	
}

static void imx662_set_default_format(struct imx662 *imx662)
{
	/* Set default mode to max resolution */
	imx662->mode = &supported_modes_12bit[0];
    if(imx662->mono){
        imx662->fmt_code = MEDIA_BUS_FMT_Y12_1X12;
    }
    else{
        imx662->fmt_code = MEDIA_BUS_FMT_SRGGB12_1X12;
    }
	
}

static int imx662_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx662 *imx662 = to_imx662(sd);
	struct v4l2_mbus_framefmt *try_fmt_img =
		v4l2_subdev_state_get_format(fh->state, IMAGE_PAD);
	struct v4l2_mbus_framefmt *try_fmt_meta =
		v4l2_subdev_state_get_format(fh->state, METADATA_PAD);
	struct v4l2_rect *try_crop;

	mutex_lock(&imx662->mutex);

	/* Initialize try_fmt for the image pad */
	try_fmt_img->width = supported_modes_12bit[0].width;
	try_fmt_img->height = supported_modes_12bit[0].height;
    if(imx662->mono){
        try_fmt_img->code = imx662_get_format_code(imx662, MEDIA_BUS_FMT_Y12_1X12);
    }
    else{
        try_fmt_img->code = imx662_get_format_code(imx662, MEDIA_BUS_FMT_SRGGB12_1X12);
    }
	
	try_fmt_img->field = V4L2_FIELD_NONE;

	/* Initialize try_fmt for the embedded metadata pad */
	try_fmt_meta->width = IMX662_EMBEDDED_LINE_WIDTH;
	try_fmt_meta->height = IMX662_NUM_EMBEDDED_LINES;
	try_fmt_meta->code = MEDIA_BUS_FMT_SENSOR_DATA;
	try_fmt_meta->field = V4L2_FIELD_NONE;

	/* Initialize try_crop */
	try_crop = v4l2_subdev_state_get_crop(fh->state, IMAGE_PAD);
	try_crop->left = IMX662_PIXEL_ARRAY_LEFT;
	try_crop->top = IMX662_PIXEL_ARRAY_TOP;
	try_crop->width = IMX662_PIXEL_ARRAY_WIDTH;
	try_crop->height = IMX662_PIXEL_ARRAY_HEIGHT;

	mutex_unlock(&imx662->mutex);

	return 0;
}


static u64 calculate_v4l2_cid_exposure(u64 hmax, u64 vmax, u64 shr, u64 svr, u64 offset) {
    u64 numerator;
    numerator = (vmax * (svr + 1) - shr) * hmax + offset;

    do_div(numerator, hmax);
    numerator = clamp_t(uint32_t, numerator, 0, 0xFFFFFFFF);
    return numerator;
}

static void calculate_min_max_v4l2_cid_exposure(u64 hmax, u64 vmax, u64 min_shr, u64 svr, u64 offset, u64 *min_exposure, u64 *max_exposure) {
    u64 max_shr = (svr + 1) * vmax - 4;
    max_shr = min_t(uint64_t, max_shr, 0xFFFF);

    *min_exposure = calculate_v4l2_cid_exposure(hmax, vmax, max_shr, svr, offset);
    *max_exposure = calculate_v4l2_cid_exposure(hmax, vmax, min_shr, svr, offset);
}


static uint32_t calculate_shr(uint32_t exposure, uint32_t hmax, uint64_t vmax, uint32_t svr, uint32_t offset)
{
    uint64_t temp;
    uint32_t shr;

    temp = ((uint64_t)exposure * hmax - offset);
    do_div(temp, hmax);
    shr = (uint32_t)(vmax * (svr + 1) - temp);

    return shr;
}

static int imx662_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx662 *imx662 = container_of(ctrl->handler, struct imx662, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	const struct imx662_mode *mode = imx662->mode;

	int ret = 0;

	/*
	 * The VBLANK control may change the limits of usable exposure, so check
	 * and adjust if necessary.
	 */
	if (ctrl->id == V4L2_CID_VBLANK){
		/* Honour the VBLANK limits when setting exposure. */
		u64 current_exposure, max_exposure, min_exposure, vmax;
		vmax = ((u64)mode->height + ctrl->val) ;
		imx662 -> VMAX = vmax;
		
		calculate_min_max_v4l2_cid_exposure(imx662 -> HMAX, imx662 -> VMAX, (u64)mode->min_SHR, 0, 209, &min_exposure, &max_exposure);
		current_exposure = clamp_t(uint32_t, current_exposure, min_exposure, max_exposure);

		//dev_info(&client->dev,"exposure_max:%lld, exposure_min:%lld, current_exposure:%lld\n",max_exposure, min_exposure, current_exposure);
		dev_info(&client->dev,"\tVMAX:%d, HMAX:%d\n",imx662->VMAX, imx662->HMAX);
		__v4l2_ctrl_modify_range(imx662->exposure, min_exposure,max_exposure, 1,current_exposure);
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		{
			u64 shr;
			dev_info(&client->dev,"V4L2_CID_EXPOSURE : %d\n",ctrl->val);
			dev_info(&client->dev,"\tvblank:%d, hblank:%d\n",imx662->vblank->val, imx662->hblank->val);
			dev_info(&client->dev,"\tVMAX:%d, HMAX:%d\n",imx662->VMAX, imx662->HMAX);
			shr = calculate_shr(ctrl->val, imx662->HMAX, imx662->VMAX, 0, 209);
			dev_info(&client->dev,"\tSHR:%lld\n",shr);
			ret = imx662_write_reg_2byte(imx662, IMX662_REG_SHR, shr);
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		{
			int gain = ctrl->val;

			// Use HCG mode when gain is over the HGC level
			// This can only be done when HDR is disabled
			bool useHGC = false;
			if (!mode->hdr && gain >= IMX662_ANA_GAIN_HCG_THRESHOLD) {
				useHGC = true;
				gain -= IMX662_ANA_GAIN_HCG_LEVEL;
				if ( gain < IMX662_ANA_GAIN_HCG_MIN )
					gain = IMX662_ANA_GAIN_HCG_MIN;
			}
			dev_info(&client->dev,"V4L2_CID_ANALOGUE_GAIN: %d, HGC: %d\n",gain, (int)useHGC);

			// Apply gain
			imx662_register_hold(imx662, true);
			ret = imx662_write_reg_2byte(imx662, IMX662_REG_ANALOG_GAIN, gain);
			if (ret)
				dev_err_ratelimited(&client->dev, "Failed to write reg 0x%4.4x. error = %d\n", IMX662_REG_ANALOG_GAIN, ret);
			
			// Set HGC/LCG channel			
			ret = imx662_write_reg_1byte(imx662, IMX662_REG_FDG_SEL0, (u16)(useHGC ? 0x01 : 0x00));
			imx662_register_hold(imx662, false);
		}
		break;
	case V4L2_CID_VBLANK:
		{
			dev_info(&client->dev,"V4L2_CID_VBLANK : %d\n",ctrl->val);
			imx662->VMAX = ((u64)mode->height + ctrl->val);
			dev_info(&client->dev,"\tVMAX : %d\n",imx662 -> VMAX);
			ret = imx662_write_reg_3byte(imx662, IMX662_REG_VMAX, imx662 -> VMAX);
		}
		break;
	case V4L2_CID_HBLANK:
		{
			u64 pixel_rate;
			u64 hmax;
			dev_info(&client->dev,"V4L2_CID_HBLANK : %d\n",ctrl->val);
			pixel_rate = (u64)mode->width * IMX662_PIXEL_RATE;
			do_div(pixel_rate,mode->min_HMAX);
			hmax = (u64)(mode->width + ctrl->val) * IMX662_PIXEL_RATE;
			do_div(hmax,pixel_rate);
			imx662 -> HMAX = hmax;
			dev_info(&client->dev,"\tHMAX : %d\n",imx662 -> HMAX);
			ret = imx662_write_reg_2byte(imx662, IMX662_REG_HMAX, hmax);
		}
		break;
    case V4L2_CID_HFLIP:
		ret = imx662_write_reg_1byte(imx662, IMX662_FLIP_WINMODEH, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = imx662_write_reg_1byte(imx662, IMX662_FLIP_WINMODEV, ctrl->val);
		break;
	default:
		dev_info(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		//ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx662_ctrl_ops = {
	.s_ctrl = imx662_set_ctrl,
};

static int imx662_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx662 *imx662 = to_imx662(sd);

	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
        if(imx662->mono){
            if (code->index >= (ARRAY_SIZE(mono_codes)))
                return -EINVAL;

            code->code = imx662_get_format_code(imx662,
                                mono_codes[code->index]);
        }
        else{
            if (code->index >= (ARRAY_SIZE(codes) / 4))
                return -EINVAL;

            code->code = imx662_get_format_code(imx662,
                                codes[code->index * 4]);
        }

	} else {
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int imx662_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx662 *imx662 = to_imx662(sd);

	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	if (fse->pad == IMAGE_PAD) {
		const struct imx662_mode *mode_list;
		unsigned int num_modes;

		get_mode_table(imx662, fse->code, V4L2_XFER_FUNC_DEFAULT, &mode_list, &num_modes);

		if (fse->index >= num_modes)
			return -EINVAL;

		if (fse->code != imx662_get_format_code(imx662, fse->code))
			return -EINVAL;

		fse->min_width = mode_list[fse->index].width;
		fse->max_width = fse->min_width;
		fse->min_height = mode_list[fse->index].height;
		fse->max_height = fse->min_height;
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = IMX662_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = IMX662_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return 0;
}

static void imx662_reset_colorspace(const struct imx662_mode *mode, struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = mode->linear ? V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace) : V4L2_XFER_FUNC_GRADATION_COMPRESSION;
}

static void imx662_update_image_pad_format(struct imx662 *imx662,
					   const struct imx662_mode *mode,
					   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	imx662_reset_colorspace(mode, &fmt->format);
}

static void imx662_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = IMX662_EMBEDDED_LINE_WIDTH;
	fmt->format.height = IMX662_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int imx662_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx662 *imx662 = to_imx662(sd);

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx662->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(sd_state,
						   fmt->pad);
		/* update the code which could change due to vflip or hflip: */
		try_fmt->code = fmt->pad == IMAGE_PAD ?
				imx662_get_format_code(imx662, try_fmt->code) :
				MEDIA_BUS_FMT_SENSOR_DATA;
		fmt->format = *try_fmt;
	} else {
		if (fmt->pad == IMAGE_PAD) {
			imx662_update_image_pad_format(imx662, imx662->mode,
						       fmt);
			fmt->format.code =
			       imx662_get_format_code(imx662, imx662->fmt_code);
		} else {
			imx662_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx662->mutex);
	return 0;
}

/* TODO */
static void imx662_set_framing_limits(struct imx662 *imx662)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	const struct imx662_mode *mode = imx662->mode;
	u64 def_hblank;
	u64 pixel_rate;


	imx662->VMAX = mode->default_VMAX;
	imx662->HMAX = mode->default_HMAX;

	pixel_rate = (u64)mode->width * IMX662_PIXEL_RATE;
	do_div(pixel_rate,mode->min_HMAX);
	dev_info(&client->dev,"Pixel Rate : %lld\n",pixel_rate);


	def_hblank = mode->default_HMAX * pixel_rate;
	do_div(def_hblank,IMX662_PIXEL_RATE);
	def_hblank = def_hblank - mode->width;
	__v4l2_ctrl_modify_range(imx662->hblank, 0,
				 IMX662_HMAX_MAX, 1, def_hblank);


	__v4l2_ctrl_s_ctrl(imx662->hblank, def_hblank);



	/* Update limits and set FPS to default */
	__v4l2_ctrl_modify_range(imx662->vblank, mode->min_VMAX - mode->height,
				 IMX662_VMAX_MAX - mode->height,
				 1, mode->default_VMAX - mode->height);
	__v4l2_ctrl_s_ctrl(imx662->vblank, mode->default_VMAX - mode->height);

	/* Setting this will adjust the exposure limits as well. */

	__v4l2_ctrl_modify_range(imx662->pixel_rate, pixel_rate, pixel_rate, 1, pixel_rate);

	dev_info(&client->dev,"Setting default HBLANK : %lld, VBLANK : %lld with PixelRate: %lld\n",def_hblank,mode->default_VMAX - mode->height, pixel_rate);
}

/* TODO */
static int imx662_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;
	const struct imx662_mode *mode;
	struct imx662 *imx662 = to_imx662(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);

	dev_info(&client->dev,"xfer_func: %d\n", (int)fmt->format.xfer_func);

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx662->mutex);

	if (fmt->pad == IMAGE_PAD) {
		const struct imx662_mode *mode_list;
		unsigned int num_modes;

		/* Bayer order varies with flips */
		fmt->format.code = imx662_get_format_code(imx662,
							  fmt->format.code);

		get_mode_table(imx662, fmt->format.code, fmt->format.xfer_func, &mode_list, &num_modes);

		mode = v4l2_find_nearest_size(mode_list,
					      num_modes,
					      width, height,
					      fmt->format.width,
					      fmt->format.height);
		imx662_update_image_pad_format(imx662, mode, fmt);
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else if (imx662->mode != mode) {
			imx662->mode = mode;
			imx662->fmt_code = fmt->format.code;
			imx662_set_framing_limits(imx662);
		}
	} else {
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			/* Only one embedded data mode is supported */
			imx662_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx662->mutex);

	return 0;
}

/* TODO */
static const struct v4l2_rect *
__imx662_get_pad_crop(struct imx662 *imx662,
		      struct v4l2_subdev_state *sd_state,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_state_get_crop(sd_state, IMAGE_PAD);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &imx662->mode->crop;
	}

	return NULL;
}

/* Start streaming */
static int imx662_start_streaming(struct imx662 *imx662)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	const struct IMX662_reg_list *reg_list;
	int ret;
	
	dev_info(&client->dev,"imx662_start_streaming\n");

	if (!imx662->common_regs_written) {
		ret = imx662_write_regs(imx662, mode_common_regs, ARRAY_SIZE(mode_common_regs));
		if (ret) {
			dev_err(&client->dev, "%s failed to set common settings\n", __func__);
			return ret;
		}
		imx662_write_reg_2byte(imx662, IMX662_REG_BLKLEVEL, IMX662_BLKLEVEL_DEFAULT);
		imx662->common_regs_written = true;
		dev_info(&client->dev,"common_regs_written\n");
	}

	/* Apply default values of current mode */
	reg_list = &imx662->mode->reg_list;
	ret = imx662_write_regs(imx662, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Apply gradation compression curve for non-linear mode */
	if ( !imx662->mode->linear ) {
		imx662_write_reg_3byte(imx662, IMX662_REG_CCMP1_EXP, 500);
		imx662_write_reg_1byte(imx662, IMX662_REG_ACMP1_EXP, 0x2);
		imx662_write_reg_3byte(imx662, IMX662_REG_CCMP2_EXP, 11500);
		imx662_write_reg_1byte(imx662, IMX662_REG_ACMP2_EXP, 0x6);
	} else {
		imx662_write_reg_3byte(imx662, IMX662_REG_CCMP1_EXP, 0);
		imx662_write_reg_1byte(imx662, IMX662_REG_ACMP1_EXP, 0);
		imx662_write_reg_3byte(imx662, IMX662_REG_CCMP2_EXP, 0);
		imx662_write_reg_1byte(imx662, IMX662_REG_ACMP2_EXP, 0);
	}
	
	/* Apply HDR combining options */
	if ( imx662->mode->hdr ) {
		imx662_write_reg_2byte(imx662, IMX662_REG_EXP_TH_H, 4095);
		imx662_write_reg_2byte(imx662, IMX662_REG_EXP_TH_L, 512);
		imx662_write_reg_1byte(imx662, IMX662_REG_EXP_BK, 0);
	}
	
	/* Disable digital clamp */
	imx662_write_reg_1byte(imx662, IMX662_REG_DIGITAL_CLAMP, 0);
	
	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imx662->sd.ctrl_handler);
	if(ret) {
		dev_err(&client->dev, "%s failed to apply user values\n", __func__);
		return ret;
	}

	/* Set stream on register */
	ret = imx662_write_reg_1byte(imx662, IMX662_REG_MODE_SELECT, IMX662_MODE_STREAMING);
	usleep_range(IMX662_STREAM_DELAY_US, IMX662_STREAM_DELAY_US + IMX662_STREAM_DELAY_RANGE_US);
	return ret;
}

/* Stop streaming */
static void imx662_stop_streaming(struct imx662 *imx662)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	int ret;
	
	dev_info(&client->dev,"imx662_stop_streaming\n");

	/* set stream off register */
	ret = imx662_write_reg_1byte(imx662, IMX662_REG_MODE_SELECT, IMX662_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to stop stream\n", __func__);
}

static int imx662_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx662 *imx662 = to_imx662(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx662->mutex);
	if (imx662->streaming == enable) {
		mutex_unlock(&imx662->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = imx662_start_streaming(imx662);
		if (ret)
			goto err_rpm_put;
	} else {
		imx662_stop_streaming(imx662);
		pm_runtime_put(&client->dev);
	}

	imx662->streaming = enable;

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(imx662->vflip, enable);

	mutex_unlock(&imx662->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx662->mutex);

	return ret;
}

/* Power/clock management functions */
static int imx662_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx662 *imx662 = to_imx662(sd);
	int ret;

	ret = regulator_bulk_enable(imx662_NUM_SUPPLIES,
				    imx662->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imx662->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx662->reset_gpio, 1);
	usleep_range(imx662_XCLR_MIN_DELAY_US,
		     imx662_XCLR_MIN_DELAY_US + imx662_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(imx662_NUM_SUPPLIES, imx662->supplies);
	return ret;
}

static int imx662_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx662 *imx662 = to_imx662(sd);

	gpiod_set_value_cansleep(imx662->reset_gpio, 0);
	regulator_bulk_disable(imx662_NUM_SUPPLIES, imx662->supplies);
	clk_disable_unprepare(imx662->xclk);

	/* Force reprogramming of the common registers when powered up again. */
	imx662->common_regs_written = false;

	return 0;
}

static int __maybe_unused imx662_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx662 *imx662 = to_imx662(sd);

	if (imx662->streaming)
		imx662_stop_streaming(imx662);

	return 0;
}

static int __maybe_unused imx662_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx662 *imx662 = to_imx662(sd);
	int ret;

	if (imx662->streaming) {
		ret = imx662_start_streaming(imx662);
		if (ret)
			goto error;
	}

	return 0;

error:
	imx662_stop_streaming(imx662);
	imx662->streaming = 0;
	return ret;
}

static int imx662_get_regulators(struct imx662 *imx662)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	unsigned int i;

	for (i = 0; i < imx662_NUM_SUPPLIES; i++)
		imx662->supplies[i].supply = imx662_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       imx662_NUM_SUPPLIES,
				       imx662->supplies);
}

/* Verify chip ID */
static int imx662_identify_module(struct imx662 *imx662, u32 expected_id)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	int ret;
	u32 val;

	ret = imx662_read_reg(imx662, IMX662_REG_CHIP_ID,
			      1, &val);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x, with error %d\n",
			expected_id, ret);
		return ret;
	}

	dev_info(&client->dev, "Device found, ID: %x\n", val);

	return 0;
}

static int imx662_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		struct imx662 *imx662 = to_imx662(sd);

		mutex_lock(&imx662->mutex);
		sel->r = *__imx662_get_pad_crop(imx662, sd_state, sel->pad,
						sel->which);
		mutex_unlock(&imx662->mutex);

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX662_NATIVE_WIDTH;
		sel->r.height = IMX662_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = IMX662_PIXEL_ARRAY_LEFT;
		sel->r.top = IMX662_PIXEL_ARRAY_TOP;
		sel->r.width = IMX662_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX662_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}


static const struct v4l2_subdev_core_ops imx662_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx662_video_ops = {
	.s_stream = imx662_set_stream,
};

static const struct v4l2_subdev_pad_ops imx662_pad_ops = {
	.enum_mbus_code = imx662_enum_mbus_code,
	.get_fmt = imx662_get_pad_format,
	.set_fmt = imx662_set_pad_format,
	.get_selection = imx662_get_selection,
	.enum_frame_size = imx662_enum_frame_size,
};

static const struct v4l2_subdev_ops imx662_subdev_ops = {
	.core = &imx662_core_ops,
	.video = &imx662_video_ops,
	.pad = &imx662_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx662_internal_ops = {
	.open = imx662_open,
};

/* Initialize control handlers */
static int imx662_init_controls(struct imx662 *imx662)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct i2c_client *client = v4l2_get_subdevdata(&imx662->sd);
	struct v4l2_fwnode_device_properties props;
	int ret;

	ctrl_hdlr = &imx662->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 16);
	if (ret)
		return ret;

	mutex_init(&imx662->mutex);
	ctrl_hdlr->lock = &imx662->mutex;

	/*
	 * Create the controls here, but mode specific limits are setup
	 * in the imx662_set_framing_limits() call below.
	 */
	/* By default, PIXEL_RATE is read only */
	imx662->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx662_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       0xffff,
					       0xffff, 1,
					       0xffff);

    /* LINK_FREQ is also read only */
    imx662->link_freq =
        v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx662_ctrl_ops,
                       V4L2_CID_LINK_FREQ, 0, 0,
                       &link_freqs[imx662->link_freq_idx]);
    if (imx662->link_freq){
        imx662->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
    }

	imx662->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx662_ctrl_ops,
					   V4L2_CID_VBLANK, 0, 0xfffff, 1, 0);
	imx662->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx662_ctrl_ops,
					   V4L2_CID_HBLANK, 0, 0xffff, 1, 0);

	imx662->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx662_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX662_EXPOSURE_MIN,
					     IMX662_EXPOSURE_MAX,
					     IMX662_EXPOSURE_STEP,
					     IMX662_EXPOSURE_DEFAULT);
/*
	v4l2_ctrl_new_std(&imx662->ctrls, &imx662_ctrl_ops,
                          V4L2_CID_ANALOGUE_GAIN, 0, 240, 1, 0);
*/
	v4l2_ctrl_new_std(ctrl_hdlr, &imx662_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX662_ANA_GAIN_MIN, IMX662_ANA_GAIN_MAX,
			  IMX662_ANA_GAIN_STEP, IMX662_ANA_GAIN_DEFAULT);


    imx662->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx662_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);

    imx662->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx662_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

/*
	if (imx662->vflip)
		imx662->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
*/

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx662_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx662->sd.ctrl_handler = ctrl_hdlr;

	/* Setup exposure and frame/line length limits. */
	imx662_set_framing_limits(imx662);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx662->mutex);

	return ret;
}

static void imx662_free_controls(struct imx662 *imx662)
{
	v4l2_ctrl_handler_free(imx662->sd.ctrl_handler);
	mutex_destroy(&imx662->mutex);
}

static const struct imx662_compatible_data imx662_compatible = {
	.chip_id = IMX662_CHIP_ID,
	.extra_regs = {
		.num_of_regs = 0,
		.regs = NULL
	}
};

static const struct of_device_id imx662_dt_ids[] = {
	{ .compatible = "sony,imx662", .data = &imx662_compatible },
	{ /* sentinel */ }
};


//from imx477.c
static int imx662_check_hwcfg(struct device *dev, struct imx662 *imx662)
{
    struct fwnode_handle *endpoint;
    struct v4l2_fwnode_endpoint ep_cfg = {
        .bus_type = V4L2_MBUS_CSI2_DPHY
    };
    int ret = -EINVAL;
    int i;

    endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
    if (!endpoint) {
        dev_err(dev, "endpoint node not found\n");
        return -EINVAL;
    }

    if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
        dev_err(dev, "could not parse endpoint\n");
        goto error_out;
    }

    
    /* Check the number of MIPI CSI2 data lanes */
    if (ep_cfg.bus.mipi_csi2.num_data_lanes != 2 && ep_cfg.bus.mipi_csi2.num_data_lanes != 4) {
        dev_err(dev, "only 2 or 4 data lanes are currently supported\n");
        goto error_out;
    }
    imx662->lane_count = ep_cfg.bus.mipi_csi2.num_data_lanes;
    dev_info(dev, "Data lanes: %d\n",imx662->lane_count);

    /* Check the link frequency set in device tree */
    if (!ep_cfg.nr_of_link_frequencies) {
        dev_err(dev, "link-frequency property not found in DT\n");
        goto error_out;
    }

    for (i = 0; i < ARRAY_SIZE(link_freqs); i++) {
        if (link_freqs[i] == ep_cfg.link_frequencies[0]) {
            imx662->link_freq_idx = i;
            break;
        }
    }

    if (i == ARRAY_SIZE(link_freqs)) {
        dev_err(dev, "Link frequency not supported: %lld\n",
            ep_cfg.link_frequencies[0]);
            ret = -EINVAL;
            goto error_out;
    }

    dev_info(dev, "Link Speed: %lld Mhz\n",ep_cfg.link_frequencies[0]);

	supported_modes_12bit[0].min_HMAX = HMAX_table_4lane_2K[imx662->link_freq_idx];
	supported_modes_12bit[0].default_HMAX = HMAX_table_4lane_2K[imx662->link_freq_idx];
	supported_modes_12bit[1].min_HMAX = HMAX_table_4lane_2K[imx662->link_freq_idx];
	supported_modes_12bit[1].default_HMAX = HMAX_table_4lane_2K[imx662->link_freq_idx];
	supported_modes_16bit[0].min_HMAX = HMAX_table_4lane_2K[imx662->link_freq_idx];
	supported_modes_16bit[0].default_HMAX = HMAX_table_4lane_2K[imx662->link_freq_idx];
	supported_modes_16bit[1].min_HMAX = HMAX_table_4lane_2K[imx662->link_freq_idx];
	supported_modes_16bit[1].default_HMAX = HMAX_table_4lane_2K[imx662->link_freq_idx];

    if (imx662->lane_count == 2){
        supported_modes_12bit[0].min_HMAX = supported_modes_12bit[0].min_HMAX;
        supported_modes_12bit[0].default_HMAX = supported_modes_12bit[0].default_HMAX;
        supported_modes_12bit[1].min_HMAX = supported_modes_12bit[1].min_HMAX;
        supported_modes_12bit[1].default_HMAX = supported_modes_12bit[1].default_HMAX;
        supported_modes_16bit[0].min_HMAX = supported_modes_16bit[0].min_HMAX;
        supported_modes_16bit[0].default_HMAX = supported_modes_16bit[0].default_HMAX;
        supported_modes_16bit[1].min_HMAX = supported_modes_16bit[1].min_HMAX;
        supported_modes_16bit[1].default_HMAX = supported_modes_16bit[1].default_HMAX;
    }

    //Update common registers for Lane / Link Speed settings
    for(i=0;i<ARRAY_SIZE(mode_common_regs);i++){
        if(mode_common_regs[i].address == 0x3040){
            mode_common_regs[i].val = (imx662->lane_count == 2) ? 0x01:0x03;
        }
        if(mode_common_regs[i].address == 0x3015){
            mode_common_regs[i].val =  link_freqs_reg_value[imx662->link_freq_idx];
        }
        if(mode_common_regs[i].address == 0x3019){
            if(imx662->mono){
                mode_common_regs[i].val = 0x01;
            }
            else{
                mode_common_regs[i].val = 0x00;
            }
        }
    }
    ret = 0;

error_out:
    v4l2_fwnode_endpoint_free(&ep_cfg);
    fwnode_handle_put(endpoint);

    return ret;
}


static int imx662_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx662 *imx662;
	const struct of_device_id *match;
	int ret;
    u32 mono;

	imx662 = devm_kzalloc(&client->dev, sizeof(*imx662), GFP_KERNEL);
	if (!imx662)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx662->sd, client, &imx662_subdev_ops);

	match = of_match_device(imx662_dt_ids, dev);
	if (!match)
		return -ENODEV;
	imx662->compatible_data =
		(const struct imx662_compatible_data *)match->data;

    /* Default the mono mode from OF to -1, which means invalid */
    ret = of_property_read_u32(dev->of_node, "mono-mode", &mono);
    imx662->mono = (ret == 0);
    dev_info(dev, "Mono: %d\n", imx662->mono);

    /* Check the hardware configuration in device tree */
    if (imx662_check_hwcfg(dev, imx662)){
        return -EINVAL;
    }

	/* Get system clock (xclk) */
	imx662->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx662->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(imx662->xclk);
	}

	imx662->xclk_freq = clk_get_rate(imx662->xclk);
	if (imx662->xclk_freq != IMX662_XCLK_FREQ) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			imx662->xclk_freq);
		return -EINVAL;
	}

	ret = imx662_get_regulators(imx662);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	/* Request optional enable pin */
	imx662->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	
	/*
	 * The sensor must be powered for imx662_identify_module()
	 * to be able to read the CHIP_ID register
	 */
	ret = imx662_power_on(dev);
	if (ret)
		return ret;

	ret = imx662_identify_module(imx662, imx662->compatible_data->chip_id);
	if (ret)
		goto error_power_off;

	/* Initialize default format */
	imx662_set_default_format(imx662);

	/* Enable runtime PM and turn off the device */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	/* This needs the pm runtime to be registered. */
	ret = imx662_init_controls(imx662);
	if (ret)
		goto error_power_off;

	/* Initialize subdev */
	imx662->sd.internal_ops = &imx662_internal_ops;
	imx662->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx662->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pads */
	imx662->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	imx662->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx662->sd.entity, NUM_PADS, imx662->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx662->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	return 0;

error_media_entity:
	media_entity_cleanup(&imx662->sd.entity);

error_handler_free:
	imx662_free_controls(imx662);

error_power_off:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	imx662_power_off(&client->dev);

	return ret;
}

static void imx662_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx662 *imx662 = to_imx662(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx662_free_controls(imx662);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx662_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

}

MODULE_DEVICE_TABLE(of, imx662_dt_ids);

static const struct dev_pm_ops imx662_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx662_suspend, imx662_resume)
	SET_RUNTIME_PM_OPS(imx662_power_off, imx662_power_on, NULL)
};

static struct i2c_driver imx662_i2c_driver = {
	.driver = {
		.name = "imx662",
		.of_match_table	= imx662_dt_ids,
		.pm = &imx662_pm_ops,
	},
	.probe = imx662_probe,
	.remove = imx662_remove,
};

module_i2c_driver(imx662_i2c_driver);

MODULE_AUTHOR("Will Whang <will@willwhang.com>");
MODULE_AUTHOR("Tetsuya NOMURA <tetsuya.nomura@soho-enterprise.com>");
MODULE_AUTHOR("Russell Newman <russellnewman@octopuscinema.com>");
MODULE_DESCRIPTION("Sony imx662 sensor driver");
MODULE_LICENSE("GPL v2");
