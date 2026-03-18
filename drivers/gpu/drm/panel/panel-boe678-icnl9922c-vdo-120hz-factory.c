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

#include "../../../misc/mediatek/gate_ic/gate_i2c.h"

//drv added by chenjiaxi, hardware_info, begin
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_lcm_info;
#endif
//drv added by chenjiaxi, hardware_info, end

static char bl_tb0[] = { 0x51, 0xff };

/*LCM_DEGREE default value*/
#define PROBE_FROM_DTS 0

static int current_fps = 120;

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;
	bool prepared;
	bool enabled;

	unsigned int gate_ic;
	unsigned int lcm_degree;

	int error;
};

#define lcm_dcs_write_seq(ctx, seq...)                                         \
	({                                                                     \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

#define lcm_dcs_write_seq_static(ctx, seq...)                                  \
	({                                                                     \
		static const u8 d[] = { seq };                                 \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
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
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10 * 1000, 15 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	lcm_dcs_write_seq_static(ctx,0xF0,0x99,0x22,0x0C);
	lcm_dcs_write_seq_static(ctx,0x70,0xC1,0x12,0x00,0x05,0x00,0x5A,0x00,0x6F,0x00,0x77,0x00,0x31,0x02,0x00,0x20);
	lcm_dcs_write_seq_static(ctx,0x71,0x11,0x00,0x00,0x89,0x30,0x80,0x09,0x9C,0x04,0x38,0x00,0x14,0x02,0x1C,0x02,0x1C,0x02,0x00,0x02,0x25,0x00,0x20,0x01,0xD5,0x00,0x07,0x00,0x0D,0x05,0x7A,0x05,0x16);
	lcm_dcs_write_seq_static(ctx,0x72,0x18,0x00,0x10,0xF0,0x03,0x0C,0x20,0x00,0x06,0x0B,0x0B,0x33,0x0E,0x1C,0x2A,0x38,0x46,0x54,0x62,0x69,0x70,0x77,0x79,0x7B,0x7D,0x7E,0x01,0x02,0x01,0x00,0x09,0x40);
	lcm_dcs_write_seq_static(ctx,0x73,0x09,0xBE,0x19,0xFC,0x19,0xFA,0x19,0xF8,0x1A,0x38,0x1A,0x78,0x1A,0xB6,0x2A,0xF6,0x2B,0x34,0x2B,0x74,0x3B,0x74,0x6B,0xF4,0x00,0x00,0x00,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xC7,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x10,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x10,0x34,0x00,0x01,0xFF,0xFF,0x00,0xF0,0xF0,0x00);
	lcm_dcs_write_seq_static(ctx,0x80,0xFF,0xF7,0xEB,0xE2,0xDA,0xD3,0xCD,0xC8,0xC3,0xB3,0xA7,0x9D,0x95,0x8D,0x87,0x7C,0x72,0x6A,0x62,0x62,0x5A,0x52,0x49,0x40,0x3B,0x35,0x2E,0x26,0x1C,0x12,0x0F,0x0D);
	lcm_dcs_write_seq_static(ctx,0x81,0xFF,0xF7,0xEB,0xE2,0xDA,0xD3,0xCD,0xC8,0xC3,0xB3,0xA7,0x9D,0x95,0x8D,0x87,0x7C,0x72,0x6A,0x62,0x62,0x5A,0x52,0x49,0x40,0x3B,0x35,0x2E,0x26,0x1C,0x12,0x0F,0x0D);
	lcm_dcs_write_seq_static(ctx,0x82,0xFF,0xF7,0xEB,0xE2,0xDA,0xD3,0xCD,0xC8,0xC3,0xB3,0xA7,0x9D,0x95,0x8D,0x87,0x7C,0x72,0x6A,0x62,0x62,0x5A,0x52,0x49,0x40,0x3B,0x35,0x2E,0x26,0x1C,0x12,0x0F,0x0D);
	lcm_dcs_write_seq_static(ctx,0x83,0x09,0x0B,0x09,0x07,0x05,0x03,0x02,0x0B,0x09,0x07,0x05,0x03,0x02,0x0B,0x09,0x07,0x05,0x03,0x02,0x12,0x0E,0x0A,0x06,0x02,0x00,0x12,0x0E,0x0A,0x06,0x02,0x00,0x12);
	lcm_dcs_write_seq_static(ctx,0x84,0x0E,0x0A,0x06,0x02,0x00,0x2F,0xBD,0xFF,0x7B,0xFD,0x0B,0xDA,0xDB,0xFE,0x32,0xFB,0xDF,0xF7,0xBF,0xD0,0xBD,0xAD,0xBF,0xE3,0x2F,0xBD,0xFF,0x7B,0xFD,0x0B,0xDA,0xDB);
	lcm_dcs_write_seq_static(ctx,0x85,0xFE,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xB2,0x0D,0x06,0x05,0x04,0xF2,0x22,0x03,0x00,0x22,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x61,0x5B,0x00,0x00,0x00,0x00,0x00,0x00,0x55,0x55,0x05,0x05,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xB3,0x31,0x0B,0x01,0x0B,0x81,0x61,0x00,0x00,0x5B,0x00,0x00,0x00,0x00,0x00,0x02,0xFF,0xBC,0x22,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
	lcm_dcs_write_seq_static(ctx,0xB4,0x30,0x04,0x01,0x05,0x81,0x02,0x00,0x00,0x47,0x00,0x00,0x00,0x00,0x00,0x02,0xFF,0xBC,0x22,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
	lcm_dcs_write_seq_static(ctx,0xB5,0x00,0x0B,0x06,0x0D,0x10,0x26,0x34,0x91,0xA2,0x33,0x44,0x00,0x26,0x00,0xBF,0x3C,0x02,0x08,0x20,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x5C,0x35);
	lcm_dcs_write_seq_static(ctx,0xB6,0x1E,0x1D,0x1C,0x82,0x0D,0x0C,0x0F,0x0E,0x82,0x3A,0x3A,0x3A,0x3A,0x82,0xC0,0x82,0x00,0x00,0x00,0x28,0x05,0x04,0x01,0x01,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x3C,0x00);
	lcm_dcs_write_seq_static(ctx,0xB7,0x1E,0x1D,0x1C,0x82,0x0D,0x0C,0x0F,0x0E,0x82,0x3A,0x3A,0x3A,0x3A,0x82,0xC0,0x82,0x00,0x00,0x00,0x28,0x05,0x04,0x01,0x01,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x3C,0x00);
	lcm_dcs_write_seq_static(ctx,0xB8,0x03,0x01,0x01,0x82,0x00,0x80,0x00,0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0x9D,0x00,0x00,0x00,0x12,0x1D,0x39,0x44,0x5E,0x54,0x61,0x6B,0x79,0x83,0x92,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xB9,0x12,0x33,0x21,0x12,0x33,0x21,0x12,0x33,0x21,0x12,0x33,0x21,0x12,0x33,0x21,0x12,0x33,0x21,0x12,0x33,0x21,0x12,0x33,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02);
	lcm_dcs_write_seq_static(ctx,0xBA,0x01,0xFE,0xFF,0xBF,0xEE,0xFF,0xFF,0xFE,0xFF,0xBF,0xEE,0xFF,0xFF,0xFE,0x00,0x80,0x2E,0x00,0x00,0xFE,0x00,0x80,0x2E,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xBB,0x01,0x02,0x03,0x0A,0x04,0x13,0x14,0x52,0x16,0x5C,0x00,0x15,0x16,0x00);
	lcm_dcs_write_seq_static(ctx,0xBC,0x00,0x00,0x00,0x00,0x04,0x00,0xFF,0xF8,0x0B,0x11,0x50,0x5E,0x55,0x99,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xBD,0xA1,0xA2,0x52,0x2E,0x00,0x8F,0x0D,0x09,0xC1,0x04,0x01,0xAE,0x14);
	lcm_dcs_write_seq_static(ctx,0xBE,0x28,0x1E,0x0B,0xAA,0x43,0x35,0x33,0x32,0x1E,0x00,0x00,0x3A);
	lcm_dcs_write_seq_static(ctx,0xC0,0x40,0x93,0xFF,0xFF,0xFF,0x3F,0xFF,0x00,0xFF,0x00,0xCC,0x04,0x12,0x35,0x67,0x89,0xA0,0xFF,0xFF,0xF0,0x0B,0xEB);
	lcm_dcs_write_seq_static(ctx,0xC1,0x00,0x00,0x20,0x26,0x26,0x04,0x08,0x10,0x04,0x9C,0x19,0x22,0x50,0x01,0x11,0x07,0x63,0x08,0xA0,0x00,0x93);
	lcm_dcs_write_seq_static(ctx,0xC2,0x00);
	lcm_dcs_write_seq_static(ctx,0xC3,0x00,0x00,0x00,0x00,0x00,0x00,0x15,0x28,0x23,0x16,0x16,0x16,0x00,0xFF,0x40,0x40,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x00,0x00,0x18,0x00,0x00,0x10,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xC4,0x0C,0x93,0xA8,0x28,0x00,0x3C,0x02,0x00,0x00,0x0A,0x26,0x48,0x91,0xB3,0x75,0x00,0xF0,0xEF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xC5,0x02,0x4F,0xF0,0xA8,0x64,0x04,0x02,0x02,0x19,0x02,0x10,0x4F,0x05,0x06,0x00,0x20,0x0D,0x0A,0x06,0x12,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xC6,0x65,0x08,0x18,0x48,0x48,0x20,0x3F,0x03,0x16,0x16,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xC8,0x06,0x09);
	lcm_dcs_write_seq_static(ctx,0xC9,0x62,0x62,0x5C,0x5C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xD0,0x0C,0x23,0x18,0xFF,0xFF,0x00,0x80,0x0C,0xFF,0x0F,0x40);
	lcm_dcs_write_seq_static(ctx,0x9A,0x11,0x7A,0x00,0x00,0xFF,0x00,0x0A,0x00,0x17,0x00,0x22);
	lcm_dcs_write_seq_static(ctx,0x99,0x91,0xB5,0x00,0x3F,0x00,0x7A,0x00,0x30,0x22,0x01);
	lcm_dcs_write_seq_static(ctx,0xE0,0x0C,0x00,0xB0,0x10,0x00,0x15,0x7C);
	lcm_dcs_write_seq_static(ctx,0xF0,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0x35,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0x51,0x0F,0xF0);
	lcm_dcs_write_seq_static(ctx,0x53,0x2C,0x00);
	// SLEEP OUT + DISPLAY ON
	lcm_dcs_write_seq_static(ctx,0x11,0x00);
	mdelay(120);
	lcm_dcs_write_seq_static(ctx,0x29,0x00);
	mdelay(10);
	lcm_dcs_write_seq_static(ctx,0xAC,0x05 ,0x00);

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

	lcm_dcs_write_seq_static(ctx, 0xac, 0x0a, 0x00);

	lcm_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF); //0x28
	msleep(50);
	lcm_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE); //0x10
	msleep(150);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	if (ctx->gate_ic == 0) {
		ctx->bias_neg =
			devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->bias_neg, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);

		usleep_range(2000, 2001);

		ctx->bias_pos =
			devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->bias_pos, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);
	} else if (ctx->gate_ic == 4831) {
		_gate_ic_i2c_panel_bias_enable(0);
		_gate_ic_Power_off();
	}
	ctx->error = 0;
	ctx->prepared = false;
	pr_info("%s-\n", __func__);

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;

	// lcd reset H -> L -> L
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10000, 10001);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value(ctx->reset_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	// end
	if (ctx->gate_ic == 0) {
		ctx->bias_pos =
			devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->bias_pos, 1);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);

		usleep_range(2000, 2001);
		ctx->bias_neg =
			devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->bias_neg, 1);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);
	} else if (ctx->gate_ic == 4831) {
		_gate_ic_Power_on();
		_gate_ic_i2c_panel_bias_enable(1);
	}

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;
#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

#ifdef VENDOR_EDIT
	// shifan@bsp.tp 20191226 add for loading tp fw when screen lighting on
	lcd_queue_load_tp_fw();
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

#define FRAME_WIDTH    (1080)
#define HFP            (16)
#define HSA            (4)
#define HBP            (8)
#define FRAME_HEIGHT   (2460)
#define VFP_120        (32)
#define VFP_90         (884)
#define VFP_60         (2572)
#define VSA            (4)
#define VBP            (32)
#define CLK_120_X10    ((((FRAME_HEIGHT + VFP_120 + VSA + VBP) * (FRAME_WIDTH + HFP + HSA + HBP)) * 120) / 100)
#define CLK_90_X10     ((((FRAME_HEIGHT + VFP_90 + VSA + VBP) * (FRAME_WIDTH + HFP + HSA + HBP)) * 90) / 100)
#define CLK_60_X10     ((((FRAME_HEIGHT + VFP_60 + VSA + VBP) * (FRAME_WIDTH + HFP + HSA + HBP)) * 60) / 100)
#define CLK_120		   (((CLK_120_X10 % 10) != 0) ? (CLK_120_X10 / 10 + 1) : (CLK_120_X10 / 10))
#define CLK_90		   (((CLK_90_X10 % 10) != 0) ? (CLK_90_X10 / 10 + 1) : (CLK_90_X10 / 10))
#define CLK_60		   (((CLK_60_X10 % 10) != 0) ? (CLK_60_X10 / 10 + 1) : (CLK_60_X10 / 10))
				
static const struct drm_display_mode default_mode = {
	.clock = CLK_60,
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + HFP,
	.hsync_end = FRAME_WIDTH + HFP + HSA,
	.htotal = FRAME_WIDTH + HFP+ HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + VFP_60,
	.vsync_end = FRAME_HEIGHT + VFP_60 + VSA,
	.vtotal = FRAME_HEIGHT + VFP_60 + VSA + VBP,
};

static const struct drm_display_mode performance_mode_90hz = {
	.clock = CLK_90,
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + HFP,
	.hsync_end = FRAME_WIDTH + HFP + HSA,
	.htotal = FRAME_WIDTH + HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + VFP_90,
	.vsync_end = FRAME_HEIGHT + VFP_90 + VSA,
	.vtotal = FRAME_HEIGHT + VFP_90 + VSA + VBP,
};

static const struct drm_display_mode performance_mode_120hz = {
	.clock = CLK_120,
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + HFP,
	.hsync_end = FRAME_WIDTH + HFP + HSA,
	.htotal = FRAME_WIDTH + HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + VFP_120,
	.vsync_end = FRAME_HEIGHT + VFP_120 + VSA,
	.vtotal = FRAME_HEIGHT + VFP_120 + VSA + VBP,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.pll_clk = 426,
	// .vfp_low_power = 4180,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_degree = PROBE_FROM_DTS,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.ssc_enable = 0,
	.lane_swap_en = 0,
	.lane_swap[0][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[0][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[0][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[0][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[0][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[0][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[1][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[1][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[1][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[1][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_param_load_mode = 0, //0: default flow; 1: key param only; 2: full control
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
		.pic_height = 2460,
		.pic_width = 1080,
		.slice_height = 20,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 512,
		.dec_delay = 549,
		.scale_value = 32,
		.increment_interval = 469,
		.decrement_interval = 7,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 1302,
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
	.data_rate = 852,
	.lfr_enable = 0,
	.lfr_minimum_fps = 60,
	.dyn_fps = {
		.switch_en = 0,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2, {0xFF, 0x25} },
		.dfps_cmd_table[1] = {0, 2, {0xFB, 0x01} },
		.dfps_cmd_table[2] = {0, 2, {0x18, 0x21} },
		/*switch page for esd check*/
		.dfps_cmd_table[3] = {0, 2, {0xFF, 0x10} },
		.dfps_cmd_table[4] = {0, 2, {0xFB, 0x01} },
	},
	/* following MIPI hopping parameter might cause screen mess */
	.dyn = {
		.switch_en = 1,
		.pll_clk = 556,
		.vfp_lp_dyn = 4178,
		.hfp = 76,
		.vfp = 2590,
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.pll_clk = 426,
	// .vfp_low_power = 2578,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_degree = PROBE_FROM_DTS,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.ssc_enable = 0,
	.lane_swap_en = 0,
	.lane_swap[0][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[0][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[0][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[0][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[0][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[0][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[1][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[1][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[1][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[1][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_param_load_mode = 0, //0: default flow; 1: key param only; 2: full control
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
		.pic_height = 2460,
		.pic_width = 1080,
		.slice_height = 20,//20
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 512,
		.dec_delay = 549,
		.scale_value = 32,
		.increment_interval = 469,
		.decrement_interval = 7,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 1302,
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
	.data_rate = 852,
	.lfr_enable = 0,
	.lfr_minimum_fps = 60,
	.dyn_fps = {
		.switch_en = 0,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2, {0xFF, 0x25} },
		.dfps_cmd_table[1] = {0, 2, {0xFB, 0x01} },
		.dfps_cmd_table[2] = {0, 2, {0x18, 0x20} },
		/*switch page for esd check*/
		.dfps_cmd_table[3] = {0, 2, {0xFF, 0x10} },
		.dfps_cmd_table[4] = {0, 2, {0xFB, 0x01} },
	},
	/* following MIPI hopping parameter might cause screen mess */
	.dyn = {
		.switch_en = 1,
		.pll_clk = 556,
		.vfp_lp_dyn = 2578,
		.hfp = 76,
		.vfp = 940,
	},
};

static struct mtk_panel_params ext_params_120hz = {
	.pll_clk = 426,
	// .vfp_low_power = 2578,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_degree = PROBE_FROM_DTS,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.ssc_enable = 0,
	.lane_swap_en = 0,
	.lane_swap[0][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[0][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[0][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[0][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[0][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[0][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[1][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[1][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[1][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[1][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_param_load_mode = 0, //0: default flow; 1: key param only; 2: full control
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
		.pic_height = 2460,
		.pic_width = 1080,
		.slice_height = 20,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 512,
		.dec_delay = 549,
		.scale_value = 32,
		.increment_interval = 469,
		.decrement_interval = 7,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 1302,
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
	.data_rate = 852,
	.lfr_enable = 0,
	.lfr_minimum_fps = 60,
	.dyn_fps = {
		.switch_en = 0,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2, {0xFF, 0x25} },
		.dfps_cmd_table[1] = {0, 2, {0xFB, 0x01} },
		.dfps_cmd_table[2] = {0, 2, {0x18, 0x22} },
		/*switch page for esd check*/
		.dfps_cmd_table[3] = {0, 2, {0xFF, 0x10} },
		.dfps_cmd_table[4] = {0, 2, {0xFB, 0x01} },
	},
	/* following MIPI hopping parameter might cause screen mess */
	.dyn = {
		.switch_en = 1,
		.pll_clk = 556,
		.vfp_lp_dyn = 2578,
		.hfp = 76,
		.vfp = 116,
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

	if (level > 255)
		level = 255;
	pr_info("%s backlight = -%d\n", __func__, level);
	bl_tb0[1] = (u8)level;

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
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
	int dst_fps = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	dst_fps = m ? drm_mode_vrefresh(m) : -EINVAL;

	if (dst_fps == 60) {
		ext_params.skip_vblank = 0;
		ext->params = &ext_params;
	} else if (dst_fps == 90) {
		ext_params_90hz.skip_vblank = 0;
		ext->params = &ext_params_90hz;
	} else if (dst_fps == 120) {
		ext_params_120hz.skip_vblank = 0;
		ext->params = &ext_params_120hz;
	} else {
		pr_err("%s, dst_fps %d\n", __func__, dst_fps);
		ret = -EINVAL;
	}

	if (!ret)
		current_fps = drm_mode_vrefresh(m);

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
		*ext_param = &ext_params;
	else
		ret = 1;

	if (!ret)
		current_fps = drm_mode_vrefresh(m);

	return ret;
}

#define REAL_MODE_NUM           (3)
int mtk_scaling_mode_mapping(int mode_idx)
{
	return (mode_idx % REAL_MODE_NUM);
}

static void mode_switch_to_120(struct drm_panel *panel)
{
	// struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s\n", __func__);

	// lcm_dcs_write_seq_static(ctx, 0xFF, 0x25);
	// lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	// lcm_dcs_write_seq_static(ctx, 0x18, 0x22);//120hz
	// lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
	// lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	//cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
}

static void mode_switch_to_90(struct drm_panel *panel)
{
	// struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s\n", __func__);

	// lcm_dcs_write_seq_static(ctx, 0xFF, 0x25);
	// lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	// lcm_dcs_write_seq_static(ctx, 0x18, 0x20);//90hz
	// lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
	// lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
}

static void mode_switch_to_60(struct drm_panel *panel)
{
	// struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s\n", __func__);

	// lcm_dcs_write_seq_static(ctx, 0xFF, 0x25);
	// lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	// lcm_dcs_write_seq_static(ctx, 0x18, 0x21);
	// lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
	// lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
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

	if (dst_fps == 60) { /* 60 switch to 120 */
		mode_switch_to_60(panel);
	} else if (dst_fps == 90) { /* 1200 switch to 60 */
		mode_switch_to_90(panel);
	} else if (dst_fps == 120) { /* 1200 switch to 60 */
		mode_switch_to_120(panel);
	} else {
		pr_err("%s, dst_fps %d\n", __func__, dst_fps);
		ret = -EINVAL;
	}

	return ret;
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

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	.ext_param_get = mtk_panel_ext_param_get,
	.mode_switch = mode_switch,
	.ata_check = panel_ata_check,
	.scaling_mode_mapping = mtk_scaling_mode_mapping,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	/**
	 * @prepare: the time (in milliseconds) that it takes for the panel to
	 *	   become ready and start receiving video data
	 * @enable: the time (in milliseconds) that it takes for the panel to
	 *	  display the first valid frame after starting to receive
	 *	  video data
	 * @disable: the time (in milliseconds) that it takes for the panel to
	 *	   turn the display off (no content is visible)
	 * @unprepare: the time (in milliseconds) that it takes for the panel
	 *		 to power itself down completely
	 */
	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lcm_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode2;
	struct drm_display_mode *mode3;

	mode = drm_mode_duplicate(connector->dev, &performance_mode_120hz);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_120hz.hdisplay, performance_mode_120hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_120hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	mode2 = drm_mode_duplicate(connector->dev, &performance_mode_90hz);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_90hz.hdisplay, performance_mode_90hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_90hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2);

	mode3 = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode3) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 default_mode.hdisplay, default_mode.vdisplay,
			 drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode3);
	mode3->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode3);

	connector->display_info.width_mm = 70;
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
	unsigned int value;
	unsigned int lcm_degree;
	int ret;
	int probe_ret;
	struct mtk_panel_params *ext_param = NULL;

	pr_info("%s+ boe,icnl9922c,vdo,120hz,factory\n", __func__);

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
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ret = of_property_read_u32(dev->of_node, "gate-ic", &value);
	if (ret < 0)
		value = 0;
	else
		ctx->gate_ic = value;

	value = 0;
	ret = of_property_read_u32(dev->of_node, "rc-enable", &value);
	if (ret < 0)
		value = 0;
	else {
		ext_params.round_corner_en = value;
		ext_params_90hz.round_corner_en = value;
		ext_params_120hz.round_corner_en = value;
	}
	pr_info("%s+ round_corner_en %d\n", __func__,ext_params.round_corner_en);

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
	if (ctx->gate_ic == 0) {
		ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			dev_info(dev, "cannot get bias-gpios 0 %ld\n",
				 PTR_ERR(ctx->bias_pos));
			return PTR_ERR(ctx->bias_pos);
		}
		devm_gpiod_put(dev, ctx->bias_pos);

		ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			dev_info(dev, "cannot get bias-gpios 1 %ld\n",
				 PTR_ERR(ctx->bias_neg));
			return PTR_ERR(ctx->bias_neg);
		}
		devm_gpiod_put(dev, ctx->bias_neg);
	}
	ctx->prepared = true;
	ctx->enabled = true;

	if (of_property_read_bool(dsi_node, "init-panel-off")) {
		ctx->prepared = false;
		ctx->enabled = false;
		pr_info("icnl9922c,120hz dsi_node:%s set prepared = enabled = false\n",
					dsi_node->full_name);
	}
	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);

	if (current_fps == 120)
		ext_param = &ext_params_120hz;
	else if (current_fps == 90)
		ext_param = &ext_params_90hz;
	else
		ext_param = &ext_params;

	ret = mtk_panel_ext_create(dev, ext_param, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
	probe_ret = of_property_read_u32(dev->of_node, "lcm-degree", &lcm_degree);
	if (probe_ret < 0)
		lcm_degree = 0;
	else
		ext_param->lcm_degree = lcm_degree;
	pr_info("lcm_degree: %d\n", ext_param->lcm_degree);
#endif
	pr_info("%s- boe,icnl9922c,vdo,120hz,factory\n", __func__);

//drv added by chenjiaxi, hardware_info, begin
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
    strcpy(current_lcm_info.chip,"icn9922c,factory");
    strcpy(current_lcm_info.vendor,"chipone");
    sprintf(current_lcm_info.id,"0x%02x",0x01);
    strcpy(current_lcm_info.more,"1080*2460");
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
	    .compatible = "boe,icnl9922c,vdo,120hz,factory",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-boe-icnl9922c-vdo-120hz-factory",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("shaohua deng <shaohua.deng@mediatek.com>");
MODULE_DESCRIPTION("BOE ICNL9922C VDO 120HZ AMOLED Panel Driver");
MODULE_LICENSE("GPL v2");
