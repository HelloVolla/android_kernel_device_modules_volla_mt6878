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
#include "include/panel-ky-nt37701-cmd-120hz.h"

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
#include <linux/cs_notifier.h>
#endif

/* pri LAX10-192 added by xuejian 20240409 begin*/
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_lcm_info;
#endif
/* pri LAX10-192 added by xuejian 20240409 end*/

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
static struct panel_event_blank_data lcd_tp_event;
#endif

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vdd18_gpio;
	struct gpio_desc *dvdd_gpio;
	struct gpio_desc *vci_gpio;
	bool prepared;
	bool enabled;

	int error;
	/* pri hbm added by xuejian 20240410 begin*/
	bool hbm_en;
	bool hbm_mode;
	bool hbm_wait;
	/* pri hbm added by xuejian 20240410 end*/
	unsigned int current_fps;
	enum panel_version version;
};

struct lcm *g_ctx;
static atomic_t current_backlight;
/* pri added by xuejian 20240415 begin */
//static struct kobject *kobj = NULL;
//extern void lcm_set_hbm_backlight(unsigned int case_num, unsigned char val1, unsigned char val2);
/* pri added by xuejian 20240415 begin */

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

static void lcm_pannel_reconfig_blk(struct lcm *ctx)
{
	char bl_tb[] = {0x51,0x0D,0xBB};
	unsigned int reg_level = atomic_read(&current_backlight);
	pr_err("[%s][%d]main lcd bl_level:%d \n",__func__,__LINE__,reg_level);

	bl_tb[1] = (char)((reg_level >> 8) & 0xFF);
	bl_tb[2] = (char)(reg_level & 0xFF);
	lcm_dcs_write(ctx, bl_tb, ARRAY_SIZE(bl_tb));
}

static void lcm_panel_init(struct lcm *ctx)
{
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
	lcm_dcs_write_seq_static(ctx,0xb5,0x90,0xc2,0x65,0x00,0x00,0x7f,0x48,0x25,0x51,0x00,0x00,0x48,0x1F,0x00,0x00,0x00,0x25,0x25,0x25,0x25,0x25);

	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x01);
	lcm_dcs_write_seq_static(ctx,0x6F,0x51);
	lcm_dcs_write_seq_static(ctx,0xBB,0x2F);
	lcm_dcs_write_seq_static(ctx,0x6F,0x64);
	lcm_dcs_write_seq_static(ctx,0xBB,0x2F);
	lcm_dcs_write_seq_static(ctx,0x6F,0x77);
	lcm_dcs_write_seq_static(ctx,0xBB,0x2F);
	lcm_dcs_write_seq_static(ctx,0x6F,0x8A);
	lcm_dcs_write_seq_static(ctx,0xBB,0x2F);
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x05);
	lcm_dcs_write_seq_static(ctx,0xB8,0x03);
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x01);
	lcm_dcs_write_seq_static(ctx,0xC7,0x78);
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x09);
	lcm_dcs_write_seq_static(ctx,0xD5,0x80);
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
	lcm_dcs_write_seq_static(ctx,0x6F,0x01);
	lcm_dcs_write_seq_static(ctx,0xCA,0xAB);
	lcm_dcs_write_seq_static(ctx,0x6F,0x02);
	lcm_dcs_write_seq_static(ctx,0xB5,0x47);
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x04);
	lcm_dcs_write_seq_static(ctx,0xD0,0x00);

	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
	//lcm_dcs_write_seq_static(ctx,0xC0,0x02);

	lcm_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x80); 
	lcm_dcs_write_seq_static(ctx,0x6F,0x1D);
	lcm_dcs_write_seq_static(ctx,0xF2,0x05);
	lcm_dcs_write_seq_static(ctx,0x6F,0x20);
	lcm_dcs_write_seq_static(ctx,0xF7,0x32);
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x81);
	lcm_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x00);
	lcm_dcs_write_seq_static(ctx,0x5F,0x00);
	lcm_dcs_write_seq_static(ctx,0x35,0x00);
	lcm_dcs_write_seq_static(ctx,0x53,0x20);
	lcm_dcs_write_seq_static(ctx,0x2A,0x00,0x00,0x04,0xC7);
	lcm_dcs_write_seq_static(ctx,0x2B,0x00,0x00,0x0A,0x7F);
	lcm_dcs_write_seq_static(ctx,0x82,0xAE);
	lcm_dcs_write_seq_static(ctx,0x03,0x01);
	lcm_dcs_write_seq_static(ctx,0x90,0x11);
	lcm_dcs_write_seq_static(ctx,0x91,0x89,0x28,0x00,0x0C,0xC2,0x00,0x02,0x32,0x01,0x31,0x00,0x08,0x08,0xBB,0x07,0x7B,0x10,0xF0);
	lcm_dcs_write_seq_static(ctx,0x51,0x05,0xFF);
	lcm_dcs_write_seq_static(ctx,0x6F,0x05);
	lcm_dcs_write_seq_static(ctx,0x51,0x8E,0xCA);
	lcm_dcs_write_seq_static(ctx,0x88,0x01);
	lcm_dcs_write_seq_static(ctx,0x6F,0x01);
	lcm_dcs_write_seq_static(ctx,0x88,0x02,0x5F,0x09,0x88);
	lcm_dcs_write_seq_static(ctx,0x6F,0x11);
	lcm_dcs_write_seq_static(ctx,0x87,0x0F,0xFF);
	lcm_dcs_write_seq_static(ctx,0x2F,0x03);
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x09);
	lcm_dcs_write_seq_static(ctx,0x6F,0x6B);
	lcm_dcs_write_seq_static(ctx,0xE0,0x00);
	lcm_dcs_write_seq_static(ctx,0x2C,0x00);

	//backlight
	lcm_pannel_reconfig_blk(ctx);
	lcm_dcs_write_seq_static(ctx,0x11,0x00);
	mdelay(120);
	lcm_dcs_write_seq_static(ctx,0x29,0x00);
	mdelay(10);

	pr_info("%s-\n", __func__);
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


	pr_info("%s+\n", __func__);
	if (!ctx->prepared)
		return 0;

	lcm_dcs_write_seq_static(ctx, 0x28);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(100);

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_POWERDOWN;
	cs_panel_notifier_call_chain(CS_PANEL_EVENT_BLANK,&lcd_tp_event);
#endif

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	udelay(5000);

	ctx->vci_gpio = devm_gpiod_get(ctx->dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_info(ctx->dev, "cannot get vci-gpios %ld\n",
			 PTR_ERR(ctx->vci_gpio));
		return PTR_ERR(ctx->vci_gpio);
	}
	gpiod_set_value(ctx->vci_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->vci_gpio);
	udelay(5000);

	ctx->dvdd_gpio = devm_gpiod_get(ctx->dev, "dvdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->dvdd_gpio)) {
		dev_info(ctx->dev, "cannot get dvdd-gpios %ld\n",
			 PTR_ERR(ctx->dvdd_gpio));
		return PTR_ERR(ctx->dvdd_gpio);
	}
	gpiod_set_value(ctx->dvdd_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->dvdd_gpio);
	udelay(5000);

	ctx->vdd18_gpio = devm_gpiod_get(ctx->dev, "vdd18", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vdd18_gpio)) {
		dev_info(ctx->dev, "cannot get vdd18-gpios %ld\n",
			 PTR_ERR(ctx->vdd18_gpio));
		return PTR_ERR(ctx->vdd18_gpio);
	}
	gpiod_set_value(ctx->vdd18_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->vdd18_gpio);
	udelay(5000);

	ctx->error = 0;
	ctx->prepared = false;
	ctx->hbm_mode = 0;

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_UNBLANK;
	cs_panel_notifier_call_chain(CS_PANEL_EARLY_EVENT_BLANK,&lcd_tp_event);
#endif

	ctx->vdd18_gpio = devm_gpiod_get(ctx->dev, "vdd18", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vdd18_gpio)) {
		dev_info(ctx->dev, "cannot get vdd18-gpios %ld\n",
			 PTR_ERR(ctx->vdd18_gpio));
		return PTR_ERR(ctx->vdd18_gpio);
	}
	gpiod_set_value(ctx->vdd18_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->vdd18_gpio);
	udelay(5000);

	ctx->dvdd_gpio = devm_gpiod_get(ctx->dev, "dvdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->dvdd_gpio)) {
		dev_info(ctx->dev, "cannot get dvdd-gpios %ld\n",
			 PTR_ERR(ctx->dvdd_gpio));
		return PTR_ERR(ctx->dvdd_gpio);
	}
	gpiod_set_value(ctx->dvdd_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->dvdd_gpio);
	udelay(5000);

	ctx->vci_gpio = devm_gpiod_get(ctx->dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_info(ctx->dev, "cannot get vci-gpios %ld\n",
			 PTR_ERR(ctx->vci_gpio));
		return PTR_ERR(ctx->vci_gpio);
	}
	gpiod_set_value(ctx->vci_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->vci_gpio);
	udelay(5000);

	// lcd reset L->H -> L -> L
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(5);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(20);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(30);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(30);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	// end

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		goto error;

	ctx->prepared = true;
#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

	pr_info("%s-\n", __func__);
	return ret;
error:
	lcm_unprepare(panel);
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

#define VAC (2688)
#define HAC (1224)
#if 1
static const struct drm_display_mode switch_mode_120hz = {
	.clock = ((FRAME_WIDTH+MODE_1_HFP+HSA+HBP)*(FRAME_HEIGHT+MODE_1_VFP+VSA+VBP)*(MODE_1_FPS)/1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_1_HFP,
	.hsync_end = FRAME_WIDTH + MODE_1_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_1_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_1_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_1_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_1_VFP + VSA + VBP,
};

static const struct drm_display_mode switch_mode_90hz = {
	.clock = ((FRAME_WIDTH+MODE_0_HFP+HSA+HBP)*(FRAME_HEIGHT+MODE_0_VFP+VSA+VBP)*90/1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_0_HFP,
	.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
};
#endif
static const struct drm_display_mode switch_mode_60hz = {
	.clock = ((FRAME_WIDTH+MODE_0_HFP+HSA+HBP)*(FRAME_HEIGHT+MODE_0_VFP+VSA+VBP)*(MODE_1_FPS)/1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_0_HFP,
	.hsync_end = FRAME_WIDTH + MODE_0_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_0_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_0_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_0_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_60hz = {
	.dyn_fps = {
		.switch_en = 0,
		.vact_timing_fps = 120,
		.data_rate = MODE_0_DATA_RATE,
	},
	.data_rate = MODE_0_DATA_RATE,
	.lp_perline_en = 1,

	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0x90,
		.count = 1,
		.para_list[0] = 0x11,
	},
//	.lcm_esd_check_table[2] = {
//		.cmd = 0xf6,
//		.count = 1,
//		.para_list[0] = 0x00,
//	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lcm_index = 0,
	//.wait_before_hbm = true,
	//.dsc_param_load_mode = 2,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	/* pri hbm added by xuejian 20240410 begin*/
	.hbm_en_time = 0,
	.hbm_dis_time = 0,
	/* pri hbm added by xuejian 20240410 end*/
	.dsc_params = {
		.enable = 1,
		.ver                   =  17,
		.slice_mode            =  1,
		.rgb_swap              =  0,
		.dsc_cfg               =  34,
		.rct_on                =  1,
		.bit_per_channel       =  8,
		.dsc_line_buf_depth    =  9,
		.bp_enable             =  1,
		.bit_per_pixel         =  128,
		.pic_height            =  2688,
		.pic_width             =  1224,
		.slice_height          =  12,
		.slice_width           =  612,
		.chunk_size            =  612,
		.xmit_delay            =  512,
		.dec_delay             =  562,
		.scale_value           =  32,
		.increment_interval    =  305,
		.decrement_interval    =  8,
		.line_bpg_offset       =  12,
		.nfl_bpg_offset        =  2235,
		.slice_bpg_offset      =  1915,
		.initial_offset        =  6144,
		.final_offset          =  4336,
		.flatness_minqp        =  3,
		.flatness_maxqp        =  12,
		.rc_model_size         =  8192,
		.rc_edge_factor        =  6,
		.rc_quant_incr_limit0  =  11,
		.rc_quant_incr_limit1  =  11,
		.rc_tgt_offset_hi      =  3,
		.rc_tgt_offset_lo      =  3,

//		.ext_pps_cfg = {
//			.enable = 1,
//			.rc_buf_thresh = nt37701_cmd_fhd_buf_thresh,
//			.range_min_qp = nt37701_cmd_fhd_range_min_qp,
//			.range_max_qp = nt37701_cmd_fhd_range_max_qp,
//			.range_bpg_ofs = nt37701_cmd_fhd_range_bpg_ofs,
//		},
	},
};
#if 1
static struct mtk_panel_params ext_params_90hz = {
	.dyn_fps = {
		.switch_en = 0,
		.vact_timing_fps = 90,
		.data_rate = MODE_0_DATA_RATE,
	},
	.data_rate = MODE_0_DATA_RATE,
	.lp_perline_en = 1,

	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0x90,
		.count = 1,
		.para_list[0] = 0x11,
	},
//	.lcm_esd_check_table[2] = {
//		.cmd = 0xf6,
//		.count = 1,
//		.para_list[0] = 0x00,
//	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lcm_index = 0,
	//.wait_before_hbm = true,
	//.dsc_param_load_mode = 2,
	//.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	/* pri hbm added by xuejian 20240410 begin*/
	.hbm_en_time = 0,
	.hbm_dis_time = 0,
	/* pri hbm added by xuejian 20240410 end*/
	.dsc_params = {
		.enable = 1,
		.ver                   =  17,
		.slice_mode            =  1,
		.rgb_swap              =  0,
		.dsc_cfg               =  34,
		.rct_on                =  1,
		.bit_per_channel       =  8,
		.dsc_line_buf_depth    =  9,
		.bp_enable             =  1,
		.bit_per_pixel         =  128,
		.pic_height            =  2688,
		.pic_width             =  1224,
		.slice_height          =  12,
		.slice_width           =  612,
		.chunk_size            =  612,
		.xmit_delay            =  512,
		.dec_delay             =  562,
		.scale_value           =  32,
		.increment_interval    =  305,
		.decrement_interval    =  8,
		.line_bpg_offset       =  12,
		.nfl_bpg_offset        =  2235,
		.slice_bpg_offset      =  1915,
		.initial_offset        =  6144,
		.final_offset          =  4336,
		.flatness_minqp        =  3,
		.flatness_maxqp        =  12,
		.rc_model_size         =  8192,
		.rc_edge_factor        =  6,
		.rc_quant_incr_limit0  =  11,
		.rc_quant_incr_limit1  =  11,
		.rc_tgt_offset_hi      =  3,
		.rc_tgt_offset_lo      =  3,

//		.ext_pps_cfg = {
//			.enable = 1,
//			.rc_buf_thresh = nt37701_cmd_fhd_buf_thresh,
//			.range_min_qp = nt37701_cmd_fhd_range_min_qp,
//			.range_max_qp = nt37701_cmd_fhd_range_max_qp,
//			.range_bpg_ofs = nt37701_cmd_fhd_range_bpg_ofs,
//		},
	},
};



static struct mtk_panel_params ext_params_120hz = {
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
		.data_rate = MODE_1_DATA_RATE,
	},
	.data_rate = MODE_1_DATA_RATE,
	.lp_perline_en = 1,

	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0x90,
		.count = 1,
		.para_list[0] = 0x11,
	},
//	.lcm_esd_check_table[2] = {
//		.cmd = 0xf6,
//		.count = 1,
//		.para_list[0] = 0x00,
//	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lcm_index = 0,
	//.wait_before_hbm = true,
	//.dsc_param_load_mode = 2,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	/* pri hbm added by xuejian 20240410 begin*/
	.hbm_en_time = 0,
	.hbm_dis_time = 0,
	/* pri hbm added by xuejian 20240410 end*/
	.dsc_params = {
		.enable = 1,
		.ver                   =  17,
		.slice_mode            =  1,
		.rgb_swap              =  0,
		.dsc_cfg               =  34,
		.rct_on                =  1,
		.bit_per_channel       =  8,
		.dsc_line_buf_depth    =  9,
		.bp_enable             =  1,
		.bit_per_pixel         =  128,
		.pic_height            =  2688,
		.pic_width             =  1224,
		.slice_height          =  12,
		.slice_width           =  612,
		.chunk_size            =  612,
		.xmit_delay            =  512,
		.dec_delay             =  562,
		.scale_value           =  32,
		.increment_interval    =  305,
		.decrement_interval    =  8,
		.line_bpg_offset       =  12,
		.nfl_bpg_offset        =  2235,
		.slice_bpg_offset      =  1915,
		.initial_offset        =  6144,
		.final_offset          =  4336,
		.flatness_minqp        =  3,
		.flatness_maxqp        =  12,
		.rc_model_size         =  8192,
		.rc_edge_factor        =  6,
		.rc_quant_incr_limit0  =  11,
		.rc_quant_incr_limit1  =  11,
		.rc_tgt_offset_hi      =  3,
		.rc_tgt_offset_lo      =  3,

		//.ext_pps_cfg = {
		//	.enable = 1,
		//	.rc_buf_thresh = nt37701_cmd_fhd_buf_thresh,
		//	.range_min_qp = nt37701_cmd_fhd_range_min_qp,
		//	.range_max_qp = nt37701_cmd_fhd_range_max_qp,
		//	.range_bpg_ofs = nt37701_cmd_fhd_range_bpg_ofs,
		//},
	},
};
#endif
static int lcm_get_virtual_heigh(void)
{
	return VAC;
}

static int lcm_get_virtual_width(void)
{
	return HAC;
}

static int panel_ata_check(struct drm_panel *panel)
{
	/*pri add ata test by xuejian start 20240517*/
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	unsigned char data[3] = {0x00, 0x00, 0x00};
	ssize_t ret;

	ret = mipi_dsi_dcs_read(dsi, 0xda, data, 1);
	if (ret < 0) {
		pr_err("%s error\n", __func__);
		return 0;
	}

	pr_info("[main lcd]ATA read data %x %x %x\n", data[0], data[1], data[2]);

	if (data[0] == 0xd2)
		return 1;

	return 0;
	/*pri add ata test by xuejian end 20240517*/
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	char bl_tb0[] = { 0x51, 0x0d, 0xbb};
	unsigned int reg_level = 125;
	/* pri LAX10-962 modified max backlight 93%(480nit) 20240615 start */
	if (level) {
		reg_level = (level * 92) / 100;
		atomic_set(&current_backlight, reg_level);
	} else {
		reg_level = 0;
	}
	pr_info("[%s]mian lcd set backlight:%d, actual backlight:%d\n",__func__, level, reg_level);
	/* pri LAX10-962 modified max backlight 93%(480nit) 20240615 end */

	if (g_ctx->hbm_mode) {
		pr_info("[%s]hbm_mode = %d, skip backlight\n",__func__, g_ctx->hbm_mode);
		return 0;
	}

	bl_tb0[1] = (u8)((reg_level>>8)&0xFF);
	bl_tb0[2] = (u8)(reg_level&0xFF);

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	return 0;
}

/* pri LAX10-347 added by xiaweigogn 20240407 begin */
unsigned short led_level_disp_get(char *name)
{
	int trans_level = 0;
	trans_level = atomic_read(&current_backlight);
	pr_err("[%s]: name: %s, level : %d",__func__, name, trans_level);
	return trans_level;
}
EXPORT_SYMBOL(led_level_disp_get);
/* pri LAX10-347 added by xiaweigogn 20240407 end */
/* pri hbm added by xuejian 20240410 begin*/
static int panel_hbm_set_cmdq(struct drm_panel *panel, void *dsi,
			      dcs_write_gce cb, void *handle, bool en)
{
	unsigned int level_normal = atomic_read(&current_backlight);
	char normal_tb0[] = {0x51, 0x07,0xFF};
	char hbm_tb[] = {0x51,0x0F,0xFF,0x0F,0xFF};
	struct lcm *ctx = panel_to_lcm(panel);

	if (!cb)
		return -1;

	if (ctx->hbm_en == en)
		goto done;

	if (en)
	{
		pr_err("[panel] %s : set HBM\n",__func__);
		g_ctx->hbm_mode = true;
		cb(dsi, handle, hbm_tb, ARRAY_SIZE(hbm_tb));
	}
	else
	{
		pr_err("[panel] %s : set normal = %d\n",__func__,level_normal);
		normal_tb0[1] = (level_normal>>8)&0xff;
		normal_tb0[2] = (level_normal)&0xff;

		g_ctx->hbm_mode = false;
		cb(dsi, handle, normal_tb0, ARRAY_SIZE(normal_tb0));

	}

	ctx->hbm_en = en;
	ctx->hbm_wait = true;
done:
	return 0;
}

static void panel_hbm_get_state(struct drm_panel *panel, bool *state)
{
	struct lcm *ctx = panel_to_lcm(panel);

	*state = ctx->hbm_en;
}

static void panel_hbm_get_wait_state(struct drm_panel *panel, bool *wait)
{
	struct lcm *ctx = panel_to_lcm(panel);

	*wait = ctx->hbm_wait;
}

static bool panel_hbm_set_wait_state(struct drm_panel *panel, bool wait)
{
	struct lcm *ctx = panel_to_lcm(panel);
	bool old = ctx->hbm_wait;

	ctx->hbm_wait = wait;
	return old;
}
/* pri hbm added by xuejian 20240410 end*/
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

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

static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

		pr_info("[panel] %s drm_mode_vrefresh(m) = %d\n",__func__, drm_mode_vrefresh(m));

	if (drm_mode_vrefresh(m) == MODE_0_FPS)
		ext->params = &ext_params_60hz;
	else if (drm_mode_vrefresh(m) == 90)
		ext->params = &ext_params_90hz;
	else if (drm_mode_vrefresh(m) == MODE_1_FPS)
		ext->params = &ext_params_120hz;
	else
		ret = 1;

	return ret;
}

static void mode_switch_to_120(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);
		pr_info("[panel] %s\n",__func__);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x01);
		ctx->current_fps = 120;
	}
}

static void mode_switch_to_90(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);
		pr_info("[panel] %s\n",__func__);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x02);
		ctx->current_fps = 90;
	}
}

static void mode_switch_to_60(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);
		pr_info("[panel] %s\n",__func__);
		lcm_dcs_write_seq_static(ctx, 0x2F, 0x03);
		ctx->current_fps = 60;
	}
}


static int mode_switch(struct drm_panel *panel,
		struct drm_connector *connector, unsigned int cur_mode,
		unsigned int dst_mode, enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, dst_mode);

	if (cur_mode == dst_mode)
		return ret;

		pr_info("[panel] %s\n",__func__);

	if (drm_mode_vrefresh(m) == MODE_0_FPS) { /*switch to 60 */
		mode_switch_to_60(panel, stage);
	} else if (drm_mode_vrefresh(m) == 90) { /*switch to 120 */
		mode_switch_to_90(panel, stage);
	} else if (drm_mode_vrefresh(m) == MODE_1_FPS) { /*switch to 120 */
		mode_switch_to_120(panel, stage);
	} else
		ret = 1;

	return ret;
}

/*pri add aod mode 20240411 start*/
static int panel_doze_enable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	struct lcm *ctx = panel_to_lcm(panel);
	unsigned int bl_val = atomic_read(&current_backlight);

	pr_info("panel %s\n", __func__);

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_DOZE_ENABLE;
	cs_panel_notifier_call_chain(CS_PANEL_EVENT_BLANK,&lcd_tp_event);
#endif

	lcm_dcs_write_seq_static(ctx, 0x39);
	lcm_dcs_write_seq_static(ctx, 0x2C);

	if (bl_val >= 1200) {
		lcm_dcs_write_seq_static(ctx, 0x51, 0x0D, 0xBB, 0x0F, 0xFE);
	} else if (bl_val >= 800 && bl_val < 1200) {
		lcm_dcs_write_seq_static(ctx, 0x51, 0x0D, 0xBB, 0x07, 0xFF);
	} else {
		lcm_dcs_write_seq_static(ctx, 0x51, 0x0D, 0xBB, 0x01, 0x55);
	}

	return 0;
}

static int panel_doze_disable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	struct lcm *ctx = panel_to_lcm(panel);
	unsigned int level_val = atomic_read(&current_backlight);
	unsigned char bl_aod[] = {0x51, 0x07, 0xFF};

	pr_info("panel %s\n", __func__);

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_DOZE_DISABLE;
	cs_panel_notifier_call_chain(CS_PANEL_EVENT_BLANK,&lcd_tp_event);
#endif

	bl_aod[1] = (level_val>>8)&0xf;
	bl_aod[2] = (level_val)&0xff;

	lcm_dcs_write_seq_static(ctx, 0x38);
	lcm_dcs_write_seq_static(ctx, 0x2C);
	lcm_dcs_write(ctx, bl_aod, ARRAY_SIZE(bl_aod));

	return 0;
}
/*pri add aod mode 20240411 end*/

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	/* pri hbm added by xuejian 20240410 begin*/
	.hbm_set_cmdq = panel_hbm_set_cmdq,
	.hbm_get_state = panel_hbm_get_state,
	.hbm_get_wait_state = panel_hbm_get_wait_state,
	.hbm_set_wait_state = panel_hbm_set_wait_state,
	/* pri hbm added by xuejian 20240410 end*/
	.get_virtual_heigh = lcm_get_virtual_heigh,
	.get_virtual_width = lcm_get_virtual_width,
	.ext_param_set = mtk_panel_ext_param_set,
	.mode_switch = mode_switch,
	/*pri add aod mode 20240411 start*/
	.doze_enable = panel_doze_enable,
	.doze_disable = panel_doze_disable,
	/*pri add aod mode 20240411 end*/
};
#endif

static int lcm_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode_1;
	struct drm_display_mode *mode_2;

	mode_1 = drm_mode_duplicate(connector->dev, &switch_mode_120hz);
	if (!mode_1) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 switch_mode_120hz.hdisplay, switch_mode_120hz.vdisplay,
			 drm_mode_vrefresh(&switch_mode_120hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_1);
	mode_1->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_1);
	
	mode_2 = drm_mode_duplicate(connector->dev, &switch_mode_90hz);
	if (!mode_2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 switch_mode_90hz.hdisplay, switch_mode_90hz.vdisplay,
			 drm_mode_vrefresh(&switch_mode_90hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_2);
	mode_2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_2);

	mode = drm_mode_duplicate(connector->dev, &switch_mode_60hz);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_60hz.hdisplay, switch_mode_60hz.vdisplay,
			drm_mode_vrefresh(&switch_mode_60hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 68;
	connector->display_info.height_mm = 152;

	return 2;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};
#if 0
/* pri added by xuejian 20240415 begin */
static ssize_t hbm_backlight_show(struct kobject* kodjs,struct kobj_attribute *attr,char *buf)
{
	return 0;
}

static ssize_t hbm_backlight_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
	int ret;
	unsigned int state;
	unsigned char val1, val2;

	ret = kstrtouint(buf, 10, &state);
	if (ret < 0) {
		goto err;
	}
	printk("[%s]  hbm state:%d\n", __func__, state);

	if (state == 4095) {
		//lcm_set_hbm_backlight(1, 0, 0);
	} else {
		val1 = (u8)(state>>8)&0xf;
		val2 = (u8)(state)&0xff;
		//lcm_set_hbm_backlight(2, val1, val2);
	}

err:
	return count;
}


/* pri added by xuejian 20240624 begin */
extern unsigned char get_reg_val(unsigned int case_num, unsigned char val);
extern unsigned int lcm_set_page(unsigned int case_num);
extern unsigned int lcm_set_register(unsigned char val1, unsigned char val2);

static ssize_t read_panel_show(struct kobject* kodjs,struct kobj_attribute *attr,char *buf)
{
	int count = 0;
	unsigned char val1 = 0;
	unsigned char val2 = 0;
	val1 =  get_reg_val(1, 0x03);

	val2 =  get_reg_val(1, 0x90);
	get_reg_val(2, 0x91);
	count = sprintf(buf, "03-0x%x|90-0x%x\n",val1,val2);
	return count;
}

static ssize_t read_panel_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
	printk("[%s] \n", __func__);
	return 0;
}

static ssize_t esdcheck_show(struct kobject* kodjs,struct kobj_attribute *attr,char *buf)
{
	int count = 0;
	unsigned char val1 = 0;

	lcm_set_page(4);
	lcm_set_register(0x6F, 0x77);
	val1 =  get_reg_val(1, 0xf6);

	count = sprintf(buf, "f6-0x%x\n",val1);
	return count;
}

static ssize_t esdcheck_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
	printk("[%s] \n", __func__);
	return 0;
}

static struct kobj_attribute hbm_backlight_attr = __ATTR(hbm_backlight, 0664, hbm_backlight_show, hbm_backlight_store);
static struct kobj_attribute read_panel_attr = __ATTR(read_panel, 0664, read_panel_show, read_panel_store);
static struct kobj_attribute esdcheck_attr = __ATTR(esdcheck, 0664, esdcheck_show, esdcheck_store);

int sys_node_init(void)
{
	int ret = 0;

	kobj = kobject_create_and_add("panel_feature", NULL);
	if (kobj == NULL) {
		return -ENOMEM;
	}

	ret = sysfs_create_file(kobj, &hbm_backlight_attr.attr);
	if (ret < 0) {
		printk("[%s] sysfs_create_group failed\n",__func__);
		return -1;
	}

	ret = sysfs_create_file(kobj, &read_panel_attr.attr);
	if (ret < 0) {
		printk("[%s] sysfs_create_group failed\n",__func__);
		return -1;
	}
	ret = sysfs_create_file(kobj, &esdcheck_attr.attr);
	if (ret < 0) {
		printk("[%s] sysfs_create_group failed\n",__func__);
		return -1;
	}

	printk("[%s] is OK!!!\n", __func__);
	return 0;
}
/* pri added by xuejian 20240625 end */
#endif

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;
	const u32 *val;

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
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET// MIPI_DSI_MODE_EOT_PACKET
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

	ctx->vdd18_gpio = devm_gpiod_get(dev, "vdd18", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vdd18_gpio)) {
		dev_info(dev, "cannot get vdd18-gpios %ld\n",
			 PTR_ERR(ctx->vdd18_gpio));
		return PTR_ERR(ctx->vdd18_gpio);
	}
	devm_gpiod_put(dev, ctx->vdd18_gpio);

	ctx->dvdd_gpio = devm_gpiod_get(dev, "dvdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->dvdd_gpio)) {
		dev_info(dev, "cannot get dvdd-gpios %ld\n",
			 PTR_ERR(ctx->dvdd_gpio));
		return PTR_ERR(ctx->dvdd_gpio);
	}
	devm_gpiod_put(dev, ctx->dvdd_gpio);

	ctx->vci_gpio = devm_gpiod_get(dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_info(dev, "cannot get vci-gpios %ld\n",
			 PTR_ERR(ctx->vci_gpio));
		return PTR_ERR(ctx->vci_gpio);
	}
	devm_gpiod_put(dev, ctx->vci_gpio);

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
	ret = mtk_panel_ext_create(dev, &ext_params_60hz, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;

#endif
/* pri LAX10-192 added by xuejian 20240409 begin*/
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
    strcpy(current_lcm_info.chip,"nt37701");
    strcpy(current_lcm_info.vendor,"kangyuan");
    sprintf(current_lcm_info.id,"0x%02x",0xd2);
    strcpy(current_lcm_info.more,"1224*2688");
#endif
/* pri LAX10-192 added by xuejian 20240409 end*/
	ctx->hbm_mode = 0;

	ctx->current_fps = 60;

	/* pri added by xuejian 20240415 begin */
	//sys_node_init();
	/* pri added by xuejian 20240415 end */

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
		.compatible = "ky,nt37701,cmd",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-ky-nt37701-cmd",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("nt37701 ky CMD Panel Driver");
MODULE_LICENSE("GPL");
