// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_sub_lcm_info;
#endif

#define REGFLAG_CMD             0xFFFA
#define REGFLAG_DELAY           0xFFFC
#define REGFLAG_UDELAY          0xFFFB
#define REGFLAG_END_OF_TABLE    0xFFFD

//modify by shenwenbin for ESD recovery not send backlight 20250903 start
extern int mtk_drm_esd_check_status(void);
extern void mtk_drm_esd_set_status(int status);
//modify by shenwenbin for ESD recovery not send backlight 20250903 end

struct LCM_setting_table {
	unsigned cmd;
	unsigned char count;
	unsigned char para_list[64];
};

/*LCM_DEGREE default value*/
#define PROBE_FROM_DTS 0
static char bl_tb[] = {0x51,0x0D,0xBC};
static char aod_bl_tb[] = {0x51,0x00,0x1E};

static struct LCM_setting_table lcm_normal_to_aod_tb[] = {
    {REGFLAG_CMD, 2, {0xF1,0xA6} },
    {REGFLAG_CMD, 2, {0xFE,0x9B} },
    {REGFLAG_CMD, 2, {0x53,0x18} },
    {REGFLAG_CMD, 2, {0xFE,0x9C} },
    {REGFLAG_CMD, 2, {0x71,0x05} },
    {REGFLAG_CMD, 2, {0x72,0x05} },
    {REGFLAG_CMD, 2, {0x73,0x05} },
    {REGFLAG_CMD, 2, {0x74,0x06} },
    {REGFLAG_CMD, 2, {0x75,0x06} },
    {REGFLAG_CMD, 2, {0x76,0x05} },
    {REGFLAG_CMD, 2, {0x77,0x05} },
    {REGFLAG_CMD, 2, {0x78,0x05} },
    {REGFLAG_CMD, 2, {0x79,0x05} },
    {REGFLAG_CMD, 2, {0x7A,0x05} },
    {REGFLAG_CMD, 2, {0x7B,0x06} },
    {REGFLAG_CMD, 2, {0xFE,0x53} },
    {REGFLAG_CMD, 2, {0x7B,0x44} },
    {REGFLAG_CMD, 2, {0x7C,0x80} },
    {REGFLAG_CMD, 2, {0x7D,0x80} },
    {REGFLAG_CMD, 2, {0x7E,0x44} },
    {REGFLAG_CMD, 2, {0x7F,0x80} },
    {REGFLAG_CMD, 2, {0x80,0x80} },
    {REGFLAG_CMD, 2, {0x81,0x44} },
    {REGFLAG_CMD, 2, {0x82,0x80} },
    {REGFLAG_CMD, 2, {0x83,0x80} },
    {REGFLAG_CMD, 2, {0x84,0x44} },
    {REGFLAG_CMD, 2, {0x85,0x80} },
    {REGFLAG_CMD, 2, {0x86,0x80} },
    {REGFLAG_CMD, 2, {0x87,0x44} },
    {REGFLAG_CMD, 2, {0x88,0x80} },
    {REGFLAG_CMD, 2, {0x89,0x80} },
    {REGFLAG_CMD, 2, {0x8A,0x44} },
    {REGFLAG_CMD, 2, {0x8B,0x80} },
    {REGFLAG_CMD, 2, {0x8C,0x80} },
    {REGFLAG_CMD, 2, {0x8D,0x44} },
    {REGFLAG_CMD, 2, {0x8E,0x80} },
    {REGFLAG_CMD, 2, {0x8F,0x80} },
    {REGFLAG_CMD, 2, {0x90,0x44} },
    {REGFLAG_CMD, 2, {0x91,0x80} },
    {REGFLAG_CMD, 2, {0x92,0x80} },
    {REGFLAG_CMD, 2, {0x93,0x40} },
    {REGFLAG_CMD, 2, {0x94,0x80} },
    {REGFLAG_CMD, 2, {0xFE,0x00} },
    {REGFLAG_CMD, 3, {0x53,0x20} },
    {REGFLAG_CMD, 3, {0x51,0x03,0x25} },
    {REGFLAG_CMD, 2, {0x6A,0x40} },
    {REGFLAG_CMD, 2, {0x8A,0x13} },
    {REGFLAG_CMD, 2, {0xFB,0xAA} },
    {REGFLAG_DELAY, 40, {} },

	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static struct LCM_setting_table lcm_aod_to_normal_tb[] = {
    {REGFLAG_CMD, 2, {0xF1,0xA6} },
    {REGFLAG_CMD, 2, {0xFE,0x45} },
    {REGFLAG_CMD, 2, {0xB8,0x5C} },
    {REGFLAG_CMD, 2, {0xB9,0x44} },
    {REGFLAG_CMD, 2, {0xBC,0x00} },
    {REGFLAG_CMD, 2, {0xFE,0x44} },
    {REGFLAG_CMD, 2, {0x47,0x01} },
    {REGFLAG_CMD, 2, {0xFE,0x00} },
    {REGFLAG_CMD, 2, {0x6A,0x10} },
    {REGFLAG_CMD, 2, {0x8A,0x10} },
    {REGFLAG_CMD, 2, {0xFB,0xAA} },
    {REGFLAG_CMD, 2, {0xFE,0x00} },
    {REGFLAG_CMD, 3, {0x53,0x28} },
    {REGFLAG_DELAY, 40, {} },

	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

enum panel_version{
	PANEL_V1 = 1,
	PANEL_V2,
	PANEL_V3,
};

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	bool prepared;
	bool enabled;
	bool doze_en;
	int error;
    bool out_doze_set_backlight;
    unsigned int out_doze_set_backlight_level;
    unsigned int lcm_degree;
    unsigned int current_fps;
    enum panel_version version;
};

unsigned int rawlevel;
struct lcm *g_ctx;

#define lcm_dcs_write_seq(ctx, seq...)                                         \
	({                                                                     \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

#define lcm_dcs_write_seq_static(ctx, seq...)  \
({\
	static const u8 d[] = { seq };\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret,
			 cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = { 0 };
	static int ret;

	pr_info("%s+\n", __func__);

	if (ret == 0) {
		ret = lcm_dcs_read(ctx, 0x0A, buffer, 1);
		pr_info("%s  0x%08x\n", __func__, buffer[0] | (buffer[1] << 8));
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

//modify by shenwenbin for ESD recovery not send backlight 20250903 start
static void lcm_panel_reconfig_blk(struct lcm *ctx)
{
	if(mtk_drm_esd_check_status()){
		lcm_dcs_write(ctx,bl_tb,ARRAY_SIZE(bl_tb));
		mtk_drm_esd_set_status(0);
	}
}
//modify by shenwenbin for ESD recovery not send backlight 20250903 end

static void lcm_panel_init(struct lcm *ctx)
{
	pr_err("%s+\n", __func__);
    lcm_dcs_write_seq_static(ctx,0xFE,0xA1);
    lcm_dcs_write_seq_static(ctx,0x50,0x20);
    lcm_dcs_write_seq_static(ctx,0xFE,0xA3);
    lcm_dcs_write_seq_static(ctx,0x00,0x01);
    lcm_dcs_write_seq_static(ctx,0xFE,0x72);
    lcm_dcs_write_seq_static(ctx,0x9B,0x02);
    lcm_dcs_write_seq_static(ctx,0xFE,0xD0);
    lcm_dcs_write_seq_static(ctx,0x86,0x23);
    lcm_dcs_write_seq_static(ctx,0x84,0x23);
    lcm_dcs_write_seq_static(ctx,0xFE,0X99);
    lcm_dcs_write_seq_static(ctx,0x1B,0x23);
    lcm_dcs_write_seq_static(ctx,0xFE,0x44);
    lcm_dcs_write_seq_static(ctx,0x41,0x10);
    lcm_dcs_write_seq_static(ctx,0x3D,0x08);
    lcm_dcs_write_seq_static(ctx,0x08,0x32);
    lcm_dcs_write_seq_static(ctx,0x17,0x00);
    lcm_dcs_write_seq_static(ctx,0x18,0x06);
    lcm_dcs_write_seq_static(ctx,0x19,0x0F);
    lcm_dcs_write_seq_static(ctx,0x1F,0x44);
    lcm_dcs_write_seq_static(ctx,0x20,0x41);
    lcm_dcs_write_seq_static(ctx,0xFE,0xFF);
    lcm_dcs_write_seq_static(ctx,0x63,0x1C);
    lcm_dcs_write_seq_static(ctx,0x64,0x1C);
    lcm_dcs_write_seq_static(ctx,0x65,0x1C);
    lcm_dcs_write_seq_static(ctx,0x66,0x1C);
    lcm_dcs_write_seq_static(ctx,0x6B,0x00);
    lcm_dcs_write_seq_static(ctx,0x6C,0x00);
    lcm_dcs_write_seq_static(ctx,0x6D,0x00);
    lcm_dcs_write_seq_static(ctx,0x6E,0x00);
    lcm_dcs_write_seq_static(ctx,0xFE,0xE3);
    lcm_dcs_write_seq_static(ctx,0x73,0x80);
    
    lcm_dcs_write_seq_static(ctx,0xFE,0x70);//BL dimming
    lcm_dcs_write_seq_static(ctx,0x0D,0x32);//steps
    lcm_dcs_write_seq_static(ctx,0x0E,0x02);//dimming steps   
    
    lcm_dcs_write_seq_static(ctx,0xFE,0x00);
    lcm_dcs_write_seq_static(ctx,0x2F,0x0C);
    lcm_dcs_write_seq_static(ctx,0x6A,0x10);
    lcm_dcs_write_seq_static(ctx,0x8A,0x10);
    lcm_dcs_write_seq_static(ctx,0xFA,0x01);
    lcm_dcs_write_seq_static(ctx,0xC2,0x08); 
    lcm_dcs_write_seq_static(ctx,0x53,0x28); //dimming by shenwenbin 20250917
    lcm_dcs_write_seq_static(ctx,0x35,0x00);
    lcm_dcs_write_seq_static(ctx,0x51,0x00,0x00);
    lcm_panel_reconfig_blk(ctx);    //modify by shenwenbin for ESD recovery not send backlight 20250903
    lcm_dcs_write_seq_static(ctx,0x11);
    msleep(120);
    lcm_dcs_write_seq_static(ctx,0x29);
    msleep(50);

	pr_err("%s-\n", __func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);


	pr_info("rm692k0.%s+\n", __func__);
	if (!ctx->prepared)
		return 0;

    lcm_dcs_write_seq_static(ctx, 0xFE, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x28);
	msleep(70);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(100);
    lcm_dcs_write_seq_static(ctx, 0xFE, 0xFD);
    lcm_dcs_write_seq_static(ctx, 0x84, 0xA5);
    lcm_dcs_write_seq_static(ctx, 0x85, 0x5A);
    lcm_dcs_write_seq_static(ctx, 0xFE, 0x00);
    lcm_dcs_write_seq_static(ctx, 0x4F, 0x01);
	
	// enter deep standby mode
	// lcm_dcs_write_seq_static(ctx, 0x4f, 0x01);
	// msleep(120);
	
	// lcd reset L
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(10);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	
	ctx->error = 0;
	ctx->prepared = false;
	
	ctx->doze_en = false;

	pr_err("%s-\n", __func__);
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("rm692k0.%s+\n", __func__);
	if (ctx->prepared)
		return 0;
	
	// lcd reset L->H -> L -> L
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(10);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(10);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(50);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	// end
	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;
#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

	pr_info("%s-\n", __func__);
	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

#define HFP (36)
#define HSA (4)
#define HBP (36)
#define HACT (1172)
#define VFP (64)
#define VSA (4)
#define VBP (2)
#define VACT (2748)

static const struct drm_display_mode switch_mode_120hz = {
	.clock		= ((HACT+HFP+HSA+HBP)*(VACT+VFP+VSA+VBP)*(120)/1000) + 1,
	.hdisplay	= HACT,
	.hsync_start	= HACT + HFP,
	.hsync_end	= HACT + HFP + HSA,
	.htotal		= HACT + HFP + HSA + HBP,
	.vdisplay	= VACT,
	.vsync_start	= VACT + VFP,
	.vsync_end	= VACT + VFP + VSA,
	.vtotal		= VACT + VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_90hz = {
	.clock		= ((HACT+HFP+HSA+HBP)*(VACT+VFP+VSA+VBP)*(90)/1000) +1,
	.hdisplay	= HACT,
	.hsync_start	= HACT + HFP,
	.hsync_end	= HACT + HFP + HSA,
	.htotal		= HACT + HFP + HSA + HBP,
	.vdisplay	= VACT,
	.vsync_start	= VACT + VFP,
	.vsync_end	= VACT + VFP + VSA,
	.vtotal		= VACT + VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_60hz = {
	.clock		= ((HACT+HFP+HSA+HBP)*(VACT+VFP+VSA+VBP)*(60)/1000) +1,
	.hdisplay	= HACT,
	.hsync_start	= HACT + HFP,
	.hsync_end	= HACT + HFP + HSA,
	.htotal		= HACT + HFP + HSA + HBP,
	.vdisplay	= VACT,
	.vsync_start	= VACT + VFP,
	.vsync_end	= VACT + VFP + VSA,
	.vtotal		= VACT + VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_30hz = {
	.clock		= ((HACT+HFP+HSA+HBP)*(VACT+VFP+VSA+VBP)*(30)/1000) +1,
	.hdisplay	= HACT,
	.hsync_start	= HACT + HFP,
	.hsync_end	= HACT + HFP + HSA,
	.htotal		= HACT + HFP + HSA + HBP,
	.vdisplay	= VACT,
	.vsync_start	= VACT + VFP,
	.vsync_end	= VACT + VFP + VSA,
	.vtotal		= VACT + VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_10hz = {
	.clock		= ((HACT+HFP+HSA+HBP)*(VACT+VFP+VSA+VBP)*(10)/1000) +1,
	.hdisplay	= HACT,
	.hsync_start	= HACT + HFP,
	.hsync_end	= HACT + HFP + HSA,
	.htotal		= HACT + HFP + HSA + HBP,
	.vdisplay	= VACT,
	.vsync_start	= VACT + VFP,
	.vsync_end	= VACT + VFP + VSA,
	.vtotal		= VACT + VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_1hz = {
	.clock		= ((HACT+HFP+HSA+HBP)*(VACT+VFP+VSA+VBP)*(1)/1000) +1,
	.hdisplay	= HACT,
	.hsync_start	= HACT + HFP,
	.hsync_end	= HACT + HFP + HSA,
	.htotal		= HACT + HFP + HSA + HBP,
	.vdisplay	= VACT,
	.vsync_start	= VACT + VFP,
	.vsync_end	= VACT + VFP + VSA,
	.vtotal		= VACT + VFP + VSA + VBP,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_120hz = {
	.data_rate = 1118,
	.pll_clk = 559,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	/*.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
		.para_list[1] = 0xdc, //aod
	}, */
	.physical_width_um = 65116,
	.physical_height_um = 152679,
	.lcm_degree = PROBE_FROM_DTS,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lp_perline_en = 1,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2748,
		.pic_width = 1172,
		.slice_height = 12,
		.slice_width = 586,
		.chunk_size = 586,
		.xmit_delay = 512,
		.dec_delay = 550,
		.scale_value = 32,
		.increment_interval = 300,
		.decrement_interval = 8,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 2235,
		.slice_bpg_offset = 1993,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.data_rate = 1118,
	.pll_clk = 559,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	/*.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
		.para_list[1] = 0xdc, //aod
	}, */
	.physical_width_um = 65116,
	.physical_height_um = 152679,
	.lcm_degree = PROBE_FROM_DTS,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lp_perline_en = 1,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2748,
		.pic_width = 1172,
		.slice_height = 12,
		.slice_width = 586,
		.chunk_size = 586,
		.xmit_delay = 512,
		.dec_delay = 550,
		.scale_value = 32,
		.increment_interval = 300,
		.decrement_interval = 8,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 2235,
		.slice_bpg_offset = 1993,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
};

static struct mtk_panel_params ext_params_60hz = {
	.data_rate = 1118,
	.pll_clk = 559,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	/*.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
		.para_list[1] = 0xdc, //aod
	}, */
	.physical_width_um = 65116,
	.physical_height_um = 152679,
	.lcm_degree = PROBE_FROM_DTS,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lp_perline_en = 1,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2748,
		.pic_width = 1172,
		.slice_height = 12,
		.slice_width = 586,
		.chunk_size = 586,
		.xmit_delay = 512,
		.dec_delay = 550,
		.scale_value = 32,
		.increment_interval = 300,
		.decrement_interval = 8,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 2235,
		.slice_bpg_offset = 1993,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
};

static struct mtk_panel_params ext_params_30hz = {
	.data_rate = 1118,
	.pll_clk = 559,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	/*.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
		.para_list[1] = 0xdc, //aod
	}, */
	.physical_width_um = 65116,
	.physical_height_um = 152679,
	.lcm_degree = PROBE_FROM_DTS,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lp_perline_en = 1,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2748,
		.pic_width = 1172,
		.slice_height = 12,
		.slice_width = 586,
		.chunk_size = 586,
		.xmit_delay = 512,
		.dec_delay = 550,
		.scale_value = 32,
		.increment_interval = 300,
		.decrement_interval = 8,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 2235,
		.slice_bpg_offset = 1993,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
};

static struct mtk_panel_params ext_params_10hz = {
	.data_rate = 1118,
	.pll_clk = 559,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	/*.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
		.para_list[1] = 0xdc, //aod
	}, */
	.physical_width_um = 65116,
	.physical_height_um = 152679,
	.lcm_degree = PROBE_FROM_DTS,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lp_perline_en = 1,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2748,
		.pic_width = 1172,
		.slice_height = 12,
		.slice_width = 586,
		.chunk_size = 586,
		.xmit_delay = 512,
		.dec_delay = 550,
		.scale_value = 32,
		.increment_interval = 300,
		.decrement_interval = 8,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 2235,
		.slice_bpg_offset = 1993,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
};

static struct mtk_panel_params ext_params_1hz = {
	.data_rate = 1118,
	.pll_clk = 559,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	/*.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
		.para_list[1] = 0xdc, //aod
	}, */
	.physical_width_um = 65116,
	.physical_height_um = 152679,
	.lcm_degree = PROBE_FROM_DTS,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lp_perline_en = 1,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2748,
		.pic_width = 1172,
		.slice_height = 12,
		.slice_width = 586,
		.chunk_size = 586,
		.xmit_delay = 512,
		.dec_delay = 550,
		.scale_value = 32,
		.increment_interval = 300,
		.decrement_interval = 8,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 2235,
		.slice_bpg_offset = 1993,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
};

static int panel_ata_check(struct drm_panel *panel)
{
	/* Customer test by own ATA tool */
	return 1;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	if (!cb){
		return -1;
    }

    if(g_ctx->doze_en == true){
       pr_info("%s g_ctx->doze_en = %d;setbacklight state error need flag state\n", __func__,g_ctx->doze_en);
       g_ctx->out_doze_set_backlight = true;
       g_ctx->out_doze_set_backlight_level = level;
       return 0;       
    }

    //level = level*0x0DBC/0xFFF;   //drv-mod shenwenbin for underLight sensor calc 20250813
    
    if (level > 0xFFF){
        pr_err("%s,rm692k0 backlight err: level = %d\n", __func__, level);
        return -1;
    }
    
    bl_tb[1] =  (level >> 8 ) & 0xF;
    bl_tb[2] =  level & 0xFF;
       
    cb(dsi, handle, bl_tb, ARRAY_SIZE(bl_tb));
	pr_info("%s level=%d,bl_tb[1]=0x%x, bl_tb[2]=0x%x\n",__func__, level, bl_tb[1], bl_tb[2]);

	return 0;
}
struct drm_display_mode *get_mode_by_id(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(10);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(10);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(50);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	
	return 0;
}

static void mode_switch_to_120(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);
        
        lcm_dcs_write_seq_static(ctx,0xF1,0xA6);
        lcm_dcs_write_seq_static(ctx,0xFE,0x45);
        lcm_dcs_write_seq_static(ctx,0xB8,0x5C);
        lcm_dcs_write_seq_static(ctx,0xB9,0x44);
        lcm_dcs_write_seq_static(ctx,0xBC,0x00);
        lcm_dcs_write_seq_static(ctx,0xFE,0x44);
        lcm_dcs_write_seq_static(ctx,0x47,0x01);
        lcm_dcs_write_seq_static(ctx,0xFE,0x00);
        lcm_dcs_write_seq_static(ctx,0x6A,0x10);
        lcm_dcs_write_seq_static(ctx,0x8A,0x10);
        lcm_dcs_write_seq_static(ctx,0xFB,0xAA);
        msleep(40);	
		pr_info("%s:%d  120 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_90(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);

        lcm_dcs_write_seq_static(ctx,0xF1,0xA2);
        lcm_dcs_write_seq_static(ctx,0xFE,0x44);
        lcm_dcs_write_seq_static(ctx,0x3D,0x48);
        lcm_dcs_write_seq_static(ctx,0x08,0x32);
        lcm_dcs_write_seq_static(ctx,0x0B,0x01);
        lcm_dcs_write_seq_static(ctx,0x0C,0x01);
        lcm_dcs_write_seq_static(ctx,0x0D,0x01);
        lcm_dcs_write_seq_static(ctx,0x0E,0x01);
        lcm_dcs_write_seq_static(ctx,0x98,0x00);
        lcm_dcs_write_seq_static(ctx,0x13,0x11);
        lcm_dcs_write_seq_static(ctx,0x14,0x11);
        lcm_dcs_write_seq_static(ctx,0x4C,0x01);
        lcm_dcs_write_seq_static(ctx,0x5C,0x00);
        lcm_dcs_write_seq_static(ctx,0xA2,0x11);
        lcm_dcs_write_seq_static(ctx,0xA3,0x11);
        lcm_dcs_write_seq_static(ctx,0xFE,0X00);
        lcm_dcs_write_seq_static(ctx,0x8A,0x11);
        lcm_dcs_write_seq_static(ctx,0xFB,0xAA);
        msleep(1);
		pr_info("%s:%d  90 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_60(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);

        lcm_dcs_write_seq_static(ctx,0xF1,0xA2);
        lcm_dcs_write_seq_static(ctx,0xFE,0x44);
        lcm_dcs_write_seq_static(ctx,0x3D,0x48);
        lcm_dcs_write_seq_static(ctx,0x08,0x32);
        lcm_dcs_write_seq_static(ctx,0x0B,0x01);
        lcm_dcs_write_seq_static(ctx,0x0C,0x03);
        lcm_dcs_write_seq_static(ctx,0x0D,0x03);
        lcm_dcs_write_seq_static(ctx,0x0E,0x03);
        lcm_dcs_write_seq_static(ctx,0x98,0x00);
        lcm_dcs_write_seq_static(ctx,0x13,0x11);
        lcm_dcs_write_seq_static(ctx,0x14,0x11);
        lcm_dcs_write_seq_static(ctx,0x4C,0x03);
        lcm_dcs_write_seq_static(ctx,0x5C,0x00);
        lcm_dcs_write_seq_static(ctx,0xA2,0x11);
        lcm_dcs_write_seq_static(ctx,0xA3,0x11);
        lcm_dcs_write_seq_static(ctx,0xFE,0X00);
        lcm_dcs_write_seq_static(ctx,0x8A,0x11);
        lcm_dcs_write_seq_static(ctx,0xFB,0xAA);
        msleep(1);
		pr_info("%s:%d  60 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_30(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);

        lcm_dcs_write_seq_static(ctx,0xF1,0xA2);
        lcm_dcs_write_seq_static(ctx,0xFE,0x44);
        lcm_dcs_write_seq_static(ctx,0x3D,0x48);
        lcm_dcs_write_seq_static(ctx,0x08,0x32);
        lcm_dcs_write_seq_static(ctx,0x0B,0x01);
        lcm_dcs_write_seq_static(ctx,0x0C,0x03);
        lcm_dcs_write_seq_static(ctx,0x0D,0x09);
        lcm_dcs_write_seq_static(ctx,0x0E,0x09);
        lcm_dcs_write_seq_static(ctx,0x98,0x00);
        lcm_dcs_write_seq_static(ctx,0x13,0x11);
        lcm_dcs_write_seq_static(ctx,0x14,0x11);
        lcm_dcs_write_seq_static(ctx,0x4C,0x09);
        lcm_dcs_write_seq_static(ctx,0x5C,0x00);
        lcm_dcs_write_seq_static(ctx,0xA2,0x11);
        lcm_dcs_write_seq_static(ctx,0xA3,0x11);
        lcm_dcs_write_seq_static(ctx,0xFE,0X00);
        lcm_dcs_write_seq_static(ctx,0x8A,0x11);
        lcm_dcs_write_seq_static(ctx,0xFB,0xAA);
        msleep(1);
		pr_info("%s:%d  30 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_10(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);

        lcm_dcs_write_seq_static(ctx,0xF1,0xA2);
        lcm_dcs_write_seq_static(ctx,0xFE,0x44);
        lcm_dcs_write_seq_static(ctx,0x3D,0x48);
        lcm_dcs_write_seq_static(ctx,0x08,0x32);
        lcm_dcs_write_seq_static(ctx,0x0B,0x01);
        lcm_dcs_write_seq_static(ctx,0x0C,0x03);
        lcm_dcs_write_seq_static(ctx,0x0D,0x09);
        lcm_dcs_write_seq_static(ctx,0x0E,0x21);
        lcm_dcs_write_seq_static(ctx,0x98,0x00);
        lcm_dcs_write_seq_static(ctx,0x13,0x11);
        lcm_dcs_write_seq_static(ctx,0x14,0x11);
        lcm_dcs_write_seq_static(ctx,0x4C,0x21);
        lcm_dcs_write_seq_static(ctx,0x5C,0x00);
        lcm_dcs_write_seq_static(ctx,0xA2,0x11);
        lcm_dcs_write_seq_static(ctx,0xA3,0x11);
        lcm_dcs_write_seq_static(ctx,0xFE,0X00);
        lcm_dcs_write_seq_static(ctx,0x8A,0x11);
        lcm_dcs_write_seq_static(ctx,0xFB,0xAA);
        msleep(1);
		pr_info("%s:%d  10 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_1(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);

        lcm_dcs_write_seq_static(ctx,0xF1,0xA2);
        lcm_dcs_write_seq_static(ctx,0xFE,0x44);
        lcm_dcs_write_seq_static(ctx,0x3D,0x48);
        lcm_dcs_write_seq_static(ctx,0x08,0x32);
        lcm_dcs_write_seq_static(ctx,0x0B,0x01);
        lcm_dcs_write_seq_static(ctx,0x0C,0x03);
        lcm_dcs_write_seq_static(ctx,0x0D,0x09);
        lcm_dcs_write_seq_static(ctx,0x0E,0x21);
        lcm_dcs_write_seq_static(ctx,0x98,0x00);
        lcm_dcs_write_seq_static(ctx,0x13,0x11);
        lcm_dcs_write_seq_static(ctx,0x14,0x11);
        lcm_dcs_write_seq_static(ctx,0x4C,0x65);
        lcm_dcs_write_seq_static(ctx,0x5C,0x01);
        lcm_dcs_write_seq_static(ctx,0xA2,0x11);
        lcm_dcs_write_seq_static(ctx,0xA3,0x11);
        lcm_dcs_write_seq_static(ctx,0xFE,0X00);
        lcm_dcs_write_seq_static(ctx,0x8A,0x11);
        lcm_dcs_write_seq_static(ctx,0xFB,0xAA);
        msleep(1);
		pr_info("%s:%d  1 display_mode end\n", __func__, __LINE__);
	}
}

 static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	int dst_fps = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	dst_fps = m ? drm_mode_vrefresh(m) : -EINVAL;

	if (dst_fps == 120) {
        ext_params_120hz.skip_vblank = 0;
		ext->params = &ext_params_120hz;
	} else if (dst_fps == 90) {
		ext_params_90hz.skip_vblank = 0;
        ext->params = &ext_params_90hz;
	} else if (dst_fps == 60) {
		ext_params_60hz.skip_vblank = 0;
		ext->params = &ext_params_60hz;
	} else if (dst_fps == 30) {
		ext_params_30hz.skip_vblank = 0;
		ext->params = &ext_params_30hz;
	}  else if (dst_fps == 10) {
		ext_params_10hz.skip_vblank = 0;
		ext->params = &ext_params_10hz;
	}  else if (dst_fps == 1) {
		ext_params_1hz.skip_vblank = 0;
		ext->params = &ext_params_1hz;
	}  else {
		pr_info("%s, dst_fps %d\n", __func__, dst_fps);
		ret = 1;
	}

	return ret;
}

static int mtk_panel_ext_param_get(struct drm_panel *panel,
	struct drm_connector *connector,
	struct mtk_panel_params **ext_param,
	unsigned int mode)
{
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	if (drm_mode_vrefresh(m) == 120)
		*ext_param = &ext_params_120hz;
	else if (drm_mode_vrefresh(m) == 90)
		*ext_param = &ext_params_90hz;
	else if (drm_mode_vrefresh(m) == 60)
		*ext_param = &ext_params_60hz;
	else if (drm_mode_vrefresh(m) == 30)
		*ext_param = &ext_params_30hz;
	else if (drm_mode_vrefresh(m) == 10)
		*ext_param = &ext_params_10hz;
	else if (drm_mode_vrefresh(m) == 1)
		*ext_param = &ext_params_1hz;
	else
		ret = 1;

	return ret;
}

static int mode_switch(struct drm_panel *panel,
		struct drm_connector *connector, unsigned int cur_mode,
		unsigned int dst_mode, enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	int ret = 0;
	int dst_fps = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, dst_mode);

	pr_info("%s cur_mode = %d dst_mode %d\n", __func__, cur_mode, dst_mode);

	dst_fps = m ? drm_mode_vrefresh(m) : -EINVAL;

	if (dst_fps == 120) { //switch to 120 
		mode_switch_to_120(panel,stage);
	} else if (dst_fps == 90) { //switch to 90
		mode_switch_to_90(panel,stage);
	} else if (dst_fps == 60) { //switch to 60
		mode_switch_to_60(panel,stage);
	} else if (dst_fps == 30) { //switch to 30
		mode_switch_to_30(panel,stage);
	} else if (dst_fps == 10) { //switch to 10
		mode_switch_to_10(panel,stage);
	} else if (dst_fps == 1) { //switch to 1
		mode_switch_to_1(panel,stage);
	} else {
		pr_info("%s, dst_fps %d\n", __func__, dst_fps);
		ret = 1;
	}

	return ret;
}

static int panel_doze_enable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
    unsigned int i = 0;
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s+", __func__);
	/* Enter AOD */
	for (i = 0; i < (sizeof(lcm_normal_to_aod_tb) /
			sizeof(struct LCM_setting_table)); i++) {
		unsigned int cmd;

		cmd = lcm_normal_to_aod_tb[i].cmd;

		switch (cmd) {
		case REGFLAG_DELAY:
			msleep(lcm_normal_to_aod_tb[i].count);
			break;
		case REGFLAG_UDELAY:
			udelay(lcm_normal_to_aod_tb[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			cb(dsi, handle, lcm_normal_to_aod_tb[i].para_list,
				lcm_normal_to_aod_tb[i].count);
		}
	}
	
	ctx->doze_en = true;
	pr_info("%s-\n", __func__);
	return 0;
}

static int panel_doze_disable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
    unsigned int i = 0;
    unsigned int level;
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s+", __func__);

	/* Exit AOD */
	for (i = 0; i < (sizeof(lcm_aod_to_normal_tb) /
			sizeof(struct LCM_setting_table)); i++) {
		unsigned int cmd;

		cmd = lcm_aod_to_normal_tb[i].cmd;

		switch (cmd) {
		case REGFLAG_DELAY:
				msleep(lcm_aod_to_normal_tb[i].count);
			break;
		case REGFLAG_UDELAY:
			udelay(lcm_aod_to_normal_tb[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			cb(dsi, handle, lcm_aod_to_normal_tb[i].para_list,
				lcm_aod_to_normal_tb[i].count);
		}
	}
    
    if (g_ctx->out_doze_set_backlight == true) {
       level = g_ctx->out_doze_set_backlight_level;
       bl_tb[1] =  (level >> 8 ) & 0xF;
       bl_tb[2] =  level & 0xFF;
           
       cb(dsi, handle, bl_tb, ARRAY_SIZE(bl_tb));

       g_ctx->out_doze_set_backlight = false;
       g_ctx->out_doze_set_backlight_level = 0;
       pr_info("%s level=%d,bl_tb[1]=0x%x, bl_tb[2]=0x%x\n",__func__, level, bl_tb[1], bl_tb[2]);
    }

	ctx->doze_en = false;
	pr_info("%s-\n", __func__);
	return 0;
}

static int panel_set_aod_light_mode(void *dsi, dcs_write_gce cb,void *handle, unsigned int mode)
{
	pr_info("panel %s\n", __func__);

	if (mode == 1) {
		/*Enter AOD 50nit*/
		pr_info("panel %s Enter AOD 50nit\n", __func__);
        aod_bl_tb[1] = 0x03;
        aod_bl_tb[2] = 0x25;
	}else if(mode == 2) {
       	/*Enter AOD 24nit*/
        pr_info("panel %s Enter AOD 24nit\n", __func__);
        aod_bl_tb[1] = 0x02;
        aod_bl_tb[2] = 0x20;
    }else {
		/*Enter AOD 6nit*/
        pr_info("panel %s Enter AOD 6nit\n", __func__);
        aod_bl_tb[1] = 0x00;
        aod_bl_tb[2] = 0xD0;
	}

    cb(dsi, handle, aod_bl_tb, ARRAY_SIZE(aod_bl_tb));
    
	pr_info("%s : %d !\n", __func__, mode);

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.ext_param_set = mtk_panel_ext_param_set,
	.ext_param_get = mtk_panel_ext_param_get,
	.mode_switch = mode_switch,
	.doze_enable = panel_doze_enable,
	.doze_disable = panel_doze_disable,
	.set_aod_light_mode = panel_set_aod_light_mode,
};
#endif

static int lcm_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode_1;
	struct drm_display_mode *mode_2;
    struct drm_display_mode *mode_3;
    struct drm_display_mode *mode_4;
    struct drm_display_mode *mode_5;
	
    mode = drm_mode_duplicate(connector->dev, &switch_mode_120hz);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_120hz.hdisplay, switch_mode_120hz.vdisplay,
			drm_mode_vrefresh(&switch_mode_120hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);
	
	mode_1 = drm_mode_duplicate(connector->dev, &switch_mode_90hz);
	if (!mode_1) {
		dev_info(connector->dev->dev, "failed to add mode_1 %ux%ux@%u\n",
			 switch_mode_90hz.hdisplay, switch_mode_90hz.vdisplay,
			 drm_mode_vrefresh(&switch_mode_90hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode_1);
	mode_1->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_1);

    mode_2 = drm_mode_duplicate(connector->dev, &switch_mode_60hz);
	if (!mode_2) {
		dev_info(connector->dev->dev, "failed to add mode_2 %ux%ux@%u\n",
			 switch_mode_60hz.hdisplay, switch_mode_60hz.vdisplay,
			 drm_mode_vrefresh(&switch_mode_60hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode_2);
	mode_2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_2);

    mode_3 = drm_mode_duplicate(connector->dev, &switch_mode_30hz);
	if (!mode_3) {
		dev_info(connector->dev->dev, "failed to add mode_3 %ux%ux@%u\n",
			switch_mode_30hz.hdisplay, switch_mode_30hz.vdisplay,
			drm_mode_vrefresh(&switch_mode_30hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode_3);
	mode_3->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode_3);
	
	mode_4 = drm_mode_duplicate(connector->dev, &switch_mode_10hz);
	if (!mode_4) {
		dev_info(connector->dev->dev, "failed to add mode_4 %ux%ux@%u\n",
			 switch_mode_10hz.hdisplay, switch_mode_10hz.vdisplay,
			 drm_mode_vrefresh(&switch_mode_10hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode_4);
	mode_4->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_4);

    mode_5 = drm_mode_duplicate(connector->dev, &switch_mode_1hz);
	if (!mode_5) {
		dev_info(connector->dev->dev, "failed to add mode_5 %ux%ux@%u\n",
			 switch_mode_1hz.hdisplay, switch_mode_1hz.vdisplay,
			 drm_mode_vrefresh(&switch_mode_1hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode_5);
	mode_5->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_5);

	connector->display_info.width_mm = 65;
	connector->display_info.height_mm = 153;

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct lcm *ctx;
	struct device_node *backlight;
	unsigned int lcm_degree;
    int ret;
    const u32 *val;
	int probe_ret;

	pr_info("%s+\n", __func__);

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm(node: %s)\n", __func__, dev->of_node->name);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	g_ctx = ctx;
	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);
	
	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	val = of_get_property(dev->of_node, "reg", NULL);
	ctx->version = val ? be32_to_cpup(val) : 1;

	pr_info("%s: panel version 0x%x\n", __func__, ctx->version);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params_120hz, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
	probe_ret = of_property_read_u32(dev->of_node, "lcm-degree", &lcm_degree);
	if (probe_ret < 0)
		lcm_degree = 0;
	else
		ext_params_120hz.lcm_degree = lcm_degree;
	pr_info("lcm_degree: %d\n", ext_params_120hz.lcm_degree);
#endif

	ctx->doze_en = false;
    ctx->current_fps = 120;

#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
    strcpy(current_sub_lcm_info.chip,"rm692k0");
    strcpy(current_sub_lcm_info.vendor,"Raydium");
    sprintf(current_sub_lcm_info.id,"0x%02x",0xA1);
    strcpy(current_sub_lcm_info.more,"1172*2748");
#endif

    pr_info("%s- lcm,rm692e0,cmd\n", __func__);
	return ret;
}

static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif

}

static const struct of_device_id lcm_of_match[] = {
	{
	    .compatible = "boe,rm692k0,cmd,120Hz",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-boe-rm692k0-fhdp-dphy-cmd-120hz",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("RM692K0 AMOLED CMD LCD Panel Driver");
MODULE_LICENSE("GPL");
