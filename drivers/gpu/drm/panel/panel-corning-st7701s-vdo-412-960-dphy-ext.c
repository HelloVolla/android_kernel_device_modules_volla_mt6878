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
#include "../mediatek/mediatek_v2/mtk_disp_notify.h"
#endif

//drv added by chenjiaxi, hardware_info, begin
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_sub_lcm_info;
#endif
//drv added by chenjiaxi, hardware_info, end

//drv added by chenjiaxi, disp sub notifier, begin
static BLOCKING_NOTIFIER_HEAD(drv_disp_sub_notifier_list);

int drv_mtk_disp_sub_notifier_register(const char *source, struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&drv_disp_sub_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(drv_mtk_disp_sub_notifier_register);

int drv_mtk_disp_sub_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&drv_disp_sub_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(drv_mtk_disp_sub_notifier_unregister);

int drv_mtk_disp_sub_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&drv_disp_sub_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(drv_mtk_disp_sub_notifier_call_chain);
//drv added by chenjiaxi, disp sub notifier, end

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

	int error;
	unsigned int hbm_mode;
	unsigned int dc_mode;
	unsigned int current_bl;
	unsigned int current_fps;
	enum panel_version version;
};

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

static void lcm_panel_init(struct lcm *ctx)
{
lcm_dcs_write_seq_static(ctx,0xFF,0x77,0x01,0x00,0x00,0x13);
lcm_dcs_write_seq_static(ctx,0xEF,0x08);
lcm_dcs_write_seq_static(ctx,0xFF,0x77,0x01,0x00,0x00,0x10);
lcm_dcs_write_seq_static(ctx,0xC0,0x77,0x03);
lcm_dcs_write_seq_static(ctx,0xC1,0x10,0x0C);
lcm_dcs_write_seq_static(ctx,0xC2,0x07,0x0A);
lcm_dcs_write_seq_static(ctx,0xCC,0x30);
lcm_dcs_write_seq_static(ctx,0xB0,0x06,0x12,0x13,0x0E,0x11,0x06,0x09,0x09,0x0A,0x25,0x06,0x12,0x12,0x28,0x32,0x1F);
lcm_dcs_write_seq_static(ctx,0xB1,0x0F,0x12,0x1D,0x08,0x0D,0x04,0x07,0x09,0x07,0x24,0x02,0x0F,0x0C,0x29,0x2F,0x1F);
lcm_dcs_write_seq_static(ctx,0xFF,0x77,0x01,0x00,0x00,0x11);
lcm_dcs_write_seq_static(ctx,0xB0,0x4D);
lcm_dcs_write_seq_static(ctx,0xB1,0x52);
lcm_dcs_write_seq_static(ctx,0xB2,0x81);
lcm_dcs_write_seq_static(ctx,0xB3,0x80);
lcm_dcs_write_seq_static(ctx,0xB5,0x4E);
lcm_dcs_write_seq_static(ctx,0xB7,0x85);
lcm_dcs_write_seq_static(ctx,0xB8,0x33);
lcm_dcs_write_seq_static(ctx,0xC1,0x78);
lcm_dcs_write_seq_static(ctx,0xC2,0x78);
lcm_dcs_write_seq_static(ctx,0xD0,0x88);
lcm_dcs_write_seq_static(ctx,0xE0,0x00,0x00,0x02);
lcm_dcs_write_seq_static(ctx,0xE1,0x06,0x0C,0x09,0x0C,0x05,0x0C,0x08,0x0C,0x0E,0x44,0x44);
lcm_dcs_write_seq_static(ctx,0xE2,0x30,0x30,0x33,0x33,0xD5,0x00,0x00,0x00,0xD5,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE3,0x00,0x00,0x33,0x33);
lcm_dcs_write_seq_static(ctx,0xE4,0x44,0x44);
lcm_dcs_write_seq_static(ctx,0xE5,0x0D,0xD3,0x2C,0x8C,0x0F,0xD5,0x2C,0x8C,0x09,0xCF,0x2C,0x8C,0x0B,0xD1,0x2C,0x8C);
lcm_dcs_write_seq_static(ctx,0xE6,0x00,0x00,0x33,0x33);
lcm_dcs_write_seq_static(ctx,0xE7,0x44,0x44);
lcm_dcs_write_seq_static(ctx,0xE8,0x0C,0xD2,0x2C,0x8C,0x0E,0xD4,0x2C,0x8C,0x08,0xCE,0x2C,0x8C,0x0A,0xD0,0x2C,0x8C);
lcm_dcs_write_seq_static(ctx,0xE9,0x36,0x00);
lcm_dcs_write_seq_static(ctx,0xEB,0x00,0x01,0xE4,0xE4,0x44,0x88,0x33);
lcm_dcs_write_seq_static(ctx,0xED,0xFF,0xFF,0xF7,0x65,0x4C,0x10,0x2F,0xFF,0xFF,0xF2,0x01,0xC4,0x56,0x7F,0xFF,0xFF);
lcm_dcs_write_seq_static(ctx,0xEF,0x10,0x0D,0x04,0x08,0x3F,0x1F);
lcm_dcs_write_seq_static(ctx,0xFF,0x77,0x01,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0x11);
mdelay(120);
lcm_dcs_write_seq_static(ctx,0x29);
mdelay(50);

	pr_info("%s-\n", __func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int data = MTK_DISP_BLANK_POWERDOWN;

	drv_mtk_disp_sub_notifier_call_chain(MTK_DISP_EARLY_EVENT_BLANK, &data);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int panel_ext_init_power(struct drm_panel *panel);
static int panel_ext_powerdown(struct drm_panel *panel);

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int data = MTK_DISP_BLANK_POWERDOWN;

	pr_info("%s+\n", __func__);
	if (!ctx->prepared)
		return 0;

	lcm_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	lcm_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(120);

	ctx->error = 0;
	ctx->prepared = false;

	panel_ext_powerdown(panel);
	pr_info("%s-\n", __func__);

	drv_mtk_disp_sub_notifier_call_chain(MTK_DISP_EVENT_BLANK, &data);

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;
	int data = MTK_DISP_BLANK_UNBLANK;

	drv_mtk_disp_sub_notifier_call_chain(MTK_DISP_EARLY_EVENT_BLANK, &data);

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;

	panel_ext_init_power(panel);

	// lcd reset L->H -> L -> L
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_LOW);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(11000, 11001);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(1000, 1001);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(1000, 1001);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(11000, 11001);
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
	int data = MTK_DISP_BLANK_UNBLANK;

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	drv_mtk_disp_sub_notifier_call_chain(MTK_DISP_EVENT_BLANK, &data);

	return 0;
}

#define HFP (50)
#define HSA (10)
#define HBP (50)
#define HACT (412)
#define VFP (20)
#define VSA (10)
#define VBP (20)
#define VACT (960)

#define FHD_HTOTAL         (HACT + HFP + HSA + HBP)
#define FHD_VTOTAL         (VACT + VFP + VSA + VBP)
#define FHD_FRAME_TOTAL    (FHD_HTOTAL * FHD_VTOTAL)
#define FHD_VREFRESH_60    (60)

#define FHD_CLK_60_X10     ((FHD_FRAME_TOTAL * FHD_VREFRESH_60) / 100)
#define FHD_CLK_60		(((FHD_CLK_60_X10 % 10) != 0) ?              \
			(FHD_CLK_60_X10 / 10 + 1) : (FHD_CLK_60_X10 / 10))

static const struct drm_display_mode switch_mode_60hz = {
	.clock = FHD_CLK_60,
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
static struct mtk_panel_params ext_params_60hz = {
	.data_rate = 400,

	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.ssc_enable = 0,
	.physical_width_um = 68256,
	.physical_height_um = 151680,
	.lcm_index = 0,

	//.lcm_degree = 180,

	//.output_mode = MTK_PANEL_DSC_SINGLE_PORT,

	//.max_bl_level = 3514,
	//.hbm_type = HBM_MODE_DCS_ONLY,
	//.te_delay = 1,
};

static int panel_ata_check(struct drm_panel *panel)
{
	/* Customer test by own ATA tool */
	return 1;
}

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


	if (drm_mode_vrefresh(m) == 60)
		ext->params = &ext_params_60hz;
	else
		ret = 1;

	return ret;
}

static void mode_switch_to_60(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct lcm *ctx = panel_to_lcm(panel);

		lcm_dcs_write_seq_static(ctx, 0x2F, 0x01);

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

	if (drm_mode_vrefresh(m) == 60) { /*switch to 1 */
		mode_switch_to_60(panel, stage);
	} else
		ret = 1;

	return ret;
}

static int panel_ext_init_power(struct drm_panel *panel)
{
	pr_info("%s\n", __func__);

	return 0;
}

static int panel_ext_powerdown(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s\n", __func__);
	if (ctx->prepared)
		return 0;

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	usleep_range(5000, 5001);

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	//.init_power = panel_ext_init_power,
	//.power_down = panel_ext_powerdown,
	.ata_check = panel_ata_check,
	.ext_param_set = mtk_panel_ext_param_set,
	.mode_switch = mode_switch,
};
#endif

static int lcm_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;

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
	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET//MIPI_DSI_MODE_EOT_PACKET
			 | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS;

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

	/* Set backlight default value */

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
	ctx->hbm_mode = 0;
	ctx->dc_mode = 0;

	ctx->current_fps = 60;

//drv added by chenjiaxi, hardware_info, begin
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
    strcpy(current_sub_lcm_info.chip,"st7701s,dphy,vdo,hknd");
    strcpy(current_sub_lcm_info.vendor,"sitronix,boe");
    sprintf(current_sub_lcm_info.id,"0x%x",0x7701);
    strcpy(current_sub_lcm_info.more,"412*960");
#endif
//drv added by chenjiaxi, hardware_info, end

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
		.compatible = "corning,st7701s,vdo,412x960",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-corning-st7701s-vdo-412-960-dphy-ext",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("ST7701S AMOLED VDO Panel Driver");
MODULE_LICENSE("GPL");
