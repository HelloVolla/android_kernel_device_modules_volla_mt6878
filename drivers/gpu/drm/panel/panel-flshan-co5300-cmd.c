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

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
#include <linux/cs_notifier.h>
#endif

/* pri LAX10-192 added by xuejian 20240409 begin*/
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_sub_lcm_info;
#endif
/* pri LAX10-192 added by xuejian 20240409 end*/

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
static struct panel_event_blank_data lcd_tp_event;
#endif

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
	struct gpio_desc *vdd18_gpio;
	//struct gpio_desc *dvdd_gpio;
	struct gpio_desc *vci_gpio;
	bool prepared;
	bool enabled;

	int error;
	enum panel_version version;
};

struct lcm *g_ctx;
static atomic_t current_backlight;

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
		dev_info(ctx->dev, "error %zd reading dcs seq:(%#x)\n", ret,
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
	char bl_tb[] = {0x51,0xff};
	unsigned char reg_level = atomic_read(&current_backlight);
	reg_level = 255;
	pr_err("[%s][%d]sub lcd bl_level:%d \n",__func__,__LINE__,reg_level);

	//bl_tb[1] = (u8)(reg_level & 0xFF);
	bl_tb[1] = 0xFF;
	lcm_dcs_write(ctx, bl_tb, ARRAY_SIZE(bl_tb));
}

static void lcm_panel_init(struct lcm *ctx)
{
	// ===  CMD2 password  ===
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x20);
	lcm_dcs_write_seq_static(ctx, 0xF4, 0x5A);
	lcm_dcs_write_seq_static(ctx, 0xF5, 0x59);
	// ===  AOD setting===
	lcm_dcs_write_seq_static(ctx, 0xFE, 0x40);
	lcm_dcs_write_seq_static(ctx, 0x5D, 0x24);
	lcm_dcs_write_seq_static(ctx, 0x60, 0x08);
	lcm_dcs_write_seq_static(ctx, 0x61, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x62, 0xCF);

	lcm_dcs_write_seq_static(ctx, 0xFE, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x3A, 0x77);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x20);
	lcm_dcs_write_seq_static(ctx, 0x51, 0xFF);
	lcm_dcs_write_seq_static(ctx, 0x63, 0xFF);

	lcm_dcs_write_seq_static(ctx, 0x2A, 0x00, 0x0C, 0x01, 0x5B);
	lcm_dcs_write_seq_static(ctx, 0x2B, 0x00, 0x00, 0x01, 0xDF);

	lcm_pannel_reconfig_blk(ctx);
	lcm_dcs_write_seq_static(ctx, 0x11);
	mdelay(60);
	lcm_dcs_write_seq_static(ctx, 0x29);
	mdelay(10);

	pr_info("%s-\n", __func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_POWERDOWN;
	cs_sub_panel_notifier_call_chain(CS_PANEL_EARLY_EVENT_BLANK,&lcd_tp_event);
#endif

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
	cs_sub_panel_notifier_call_chain(CS_PANEL_EVENT_BLANK,&lcd_tp_event);
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

//	ctx->dvdd_gpio = devm_gpiod_get(ctx->dev, "dvdd", GPIOD_OUT_HIGH);
//	if (IS_ERR(ctx->dvdd_gpio)) {
//		dev_info(ctx->dev, "cannot get dvdd-gpios %ld\n",
//			 PTR_ERR(ctx->dvdd_gpio));
//		return PTR_ERR(ctx->dvdd_gpio);
//	}
//	gpiod_set_value(ctx->dvdd_gpio, 0);
//	devm_gpiod_put(ctx->dev, ctx->dvdd_gpio);
//	udelay(5000);

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

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;

	ctx->vdd18_gpio = devm_gpiod_get(ctx->dev, "vdd18", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vdd18_gpio)) {
		dev_info(ctx->dev, "cannot get vdd18-gpios %ld\n",
			 PTR_ERR(ctx->vdd18_gpio));
		return PTR_ERR(ctx->vdd18_gpio);
	}
	gpiod_set_value(ctx->vdd18_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->vdd18_gpio);
	udelay(5000);
#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_UNBLANK;
	cs_sub_panel_notifier_call_chain(CS_PANEL_EARLY_EVENT_BLANK,&lcd_tp_event);
#endif

//	ctx->dvdd_gpio = devm_gpiod_get(ctx->dev, "dvdd", GPIOD_OUT_HIGH);
//	if (IS_ERR(ctx->dvdd_gpio)) {
//		dev_info(ctx->dev, "cannot get dvdd-gpios %ld\n",
//			 PTR_ERR(ctx->dvdd_gpio));
//		return PTR_ERR(ctx->dvdd_gpio);
//	}
//	gpiod_set_value(ctx->dvdd_gpio, 1);
//	devm_gpiod_put(ctx->dev, ctx->dvdd_gpio);
//	udelay(5000);

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
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_LOW);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(5);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(10);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(10);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(10);
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
#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_UNBLANK;
	cs_sub_panel_notifier_call_chain(CS_PANEL_LATE_EVENT_BLANK,&lcd_tp_event);
#endif

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

#define FRAME_WIDTH                 336
#define FRAME_HEIGHT                480
#define HFP (120)
#define HSA (10)
#define HBP (24)
#define VFP (6)
#define VSA (2)
#define VBP (4)
#define VAC (480)
#define HAC (336)


static const struct drm_display_mode switch_mode_60hz = {
	.clock = ((FRAME_WIDTH+HFP+HSA+HBP)*(FRAME_HEIGHT+VFP+VSA+VBP)*60/1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + HFP,
	.hsync_end = FRAME_WIDTH + HFP + HSA,
	.htotal = FRAME_WIDTH + HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + VFP,
	.vsync_end = FRAME_HEIGHT + VFP + VSA,
	.vtotal = FRAME_HEIGHT + VFP + VSA + VBP,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_60hz = {
	.data_rate = 370,
	.lp_perline_en = 1,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.physical_width_um = 68256,
	.physical_height_um = 151680,
	//.lcm_index = 0,

	//.lcm_degree = 180,
};

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

	ret = mipi_dsi_dcs_read(dsi, 0xdb, data, 1);
	if (ret < 0) {
		pr_err("%s error\n", __func__);
		return 0;
	}

	pr_info("[sub lcd]ATA read data %x %x %x\n", data[0], data[1], data[2]);

	if (data[0] == 0x11)
		return 1;

	return 0;
	/*pri add ata test by xuejian end 20240517*/
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	char bl_tb0[] = { 0x51, 0xff};
	unsigned char reg_level = 127;
	pr_info("[%s]sub lcd set backlight:%d\n",__func__, level);

	if (level) {
		reg_level = level;
		atomic_set(&current_backlight, level);
	} else {
		reg_level = 0;
	}

	bl_tb0[1] = (u8)(reg_level&0xFF);

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	return 0;
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

static int sub_panel_doze_enable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	struct lcm *ctx = panel_to_lcm(panel);
	unsigned int level_val = atomic_read(&current_backlight);
	unsigned char bl_aod[] = {0x51, 0xFF};

	pr_info("panel %s\n", __func__);
#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_DOZE_ENABLE;
	cs_sub_panel_notifier_call_chain(CS_PANEL_EVENT_BLANK,&lcd_tp_event);
#endif

	bl_aod[1] = (level_val)&0xff;
	lcm_dcs_write_seq_static(ctx, 0xFE,0x00);
	lcm_dcs_write(ctx, bl_aod, ARRAY_SIZE(bl_aod));
	lcm_dcs_write_seq_static(ctx, 0x39);

	return 0;
}

static int sub_panel_doze_disable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	struct lcm *ctx = panel_to_lcm(panel);
	unsigned int level_val = atomic_read(&current_backlight);
	unsigned char bl_aod[] = {0x51, 0xFF};

	pr_info("panel %s\n", __func__);

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_DOZE_DISABLE;
	cs_sub_panel_notifier_call_chain(CS_PANEL_EVENT_BLANK,&lcd_tp_event);
#endif
	bl_aod[1] = (level_val)&0xff;
	lcm_dcs_write_seq_static(ctx, 0xFE,0x00);
	lcm_dcs_write_seq_static(ctx, 0x38);
	lcm_dcs_write(ctx, bl_aod, ARRAY_SIZE(bl_aod));

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.get_virtual_heigh = lcm_get_virtual_heigh,
	.get_virtual_width = lcm_get_virtual_width,
	.doze_enable = sub_panel_doze_enable,
	.doze_disable = sub_panel_doze_disable,
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
	dsi->lanes = 1;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET//MIPI_DSI_MODE_EOT_PACKET
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

//	ctx->dvdd_gpio = devm_gpiod_get(dev, "dvdd", GPIOD_OUT_HIGH);
//	if (IS_ERR(ctx->dvdd_gpio)) {
//		dev_info(dev, "cannot get dvdd-gpios %ld\n",
//			 PTR_ERR(ctx->dvdd_gpio));
//		return PTR_ERR(ctx->dvdd_gpio);
//	}
//	devm_gpiod_put(dev, ctx->dvdd_gpio);

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
    strcpy(current_sub_lcm_info.chip,"co5300");
    strcpy(current_sub_lcm_info.vendor,"flshan");
    sprintf(current_sub_lcm_info.id,"0x%02x",0x11);
    strcpy(current_sub_lcm_info.more,"336*480");
#endif
/* pri LAX10-192 added by xuejian 20240409 end*/
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
		.compatible = "flshan,co5300,cmd",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-flshan-co5300-cmd",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("co5300 flshan CMD Panel Driver");
MODULE_LICENSE("GPL");
