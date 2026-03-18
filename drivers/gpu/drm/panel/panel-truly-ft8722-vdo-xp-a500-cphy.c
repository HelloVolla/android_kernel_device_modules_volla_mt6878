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

/* pri LAX10-192 added by xuejian 20240409 begin*/
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_lcm_info;
#endif
/* pri LAX10-192 added by xuejian 20240409 end*/

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos, *bias_neg;

	bool prepared;
	bool enabled;

	int error;
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

//static void lcm_pannel_reconfig_blk(struct lcm *ctx)
//{
//	char bl_tb[] = {0x51,0x0D,0xBB};
//	unsigned int reg_level = atomic_read(&current_backlight);
//	pr_err("[%s][%d]main lcd bl_level:%d \n",__func__,__LINE__,reg_level);
//
//	bl_tb[1] = (char)((reg_level >> 8) & 0xFF);
//	bl_tb[2] = (char)(reg_level & 0xFF);
//	lcm_dcs_write(ctx, bl_tb, ARRAY_SIZE(bl_tb));
//}

static void lcm_panel_init(struct lcm *ctx)
{

	pr_info("ILI7807S %s\n", __func__);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset-gpios %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return;
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(20);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(20);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(20);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(50);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
#if 0
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0x62);
lcm_dcs_write_seq_static(ctx,0x01,0x11);
lcm_dcs_write_seq_static(ctx,0x02,0x00);
lcm_dcs_write_seq_static(ctx,0x03,0x00);
lcm_dcs_write_seq_static(ctx,0x04,0x00);
lcm_dcs_write_seq_static(ctx,0x05,0x00);
lcm_dcs_write_seq_static(ctx,0x06,0x00);
lcm_dcs_write_seq_static(ctx,0x07,0x00);
lcm_dcs_write_seq_static(ctx,0x08,0xA9);
lcm_dcs_write_seq_static(ctx,0x09,0x0A);
lcm_dcs_write_seq_static(ctx,0x0A,0x30);
lcm_dcs_write_seq_static(ctx,0x0B,0x00);
lcm_dcs_write_seq_static(ctx,0x0C,0x01);
lcm_dcs_write_seq_static(ctx,0x0E,0x03);
lcm_dcs_write_seq_static(ctx,0x31,0x30);
lcm_dcs_write_seq_static(ctx,0x32,0x2F);
lcm_dcs_write_seq_static(ctx,0x33,0x2E);
lcm_dcs_write_seq_static(ctx,0x34,0x07);
lcm_dcs_write_seq_static(ctx,0x35,0x11);
lcm_dcs_write_seq_static(ctx,0x36,0x10);
lcm_dcs_write_seq_static(ctx,0x37,0x13);
lcm_dcs_write_seq_static(ctx,0x38,0x12);
lcm_dcs_write_seq_static(ctx,0x39,0x07);
lcm_dcs_write_seq_static(ctx,0x3A,0x40);
lcm_dcs_write_seq_static(ctx,0x3B,0x40);
lcm_dcs_write_seq_static(ctx,0x3C,0x01);
lcm_dcs_write_seq_static(ctx,0x3D,0x01);
lcm_dcs_write_seq_static(ctx,0x3E,0x07);
lcm_dcs_write_seq_static(ctx,0x3F,0x25);
lcm_dcs_write_seq_static(ctx,0x40,0x07);
lcm_dcs_write_seq_static(ctx,0x41,0x00);
lcm_dcs_write_seq_static(ctx,0x42,0x28);
lcm_dcs_write_seq_static(ctx,0x43,0x28);
lcm_dcs_write_seq_static(ctx,0x44,0x2C);
lcm_dcs_write_seq_static(ctx,0x45,0x09);
lcm_dcs_write_seq_static(ctx,0x46,0x08);
lcm_dcs_write_seq_static(ctx,0x47,0x41);
lcm_dcs_write_seq_static(ctx,0x48,0x41);
lcm_dcs_write_seq_static(ctx,0x49,0x30);
lcm_dcs_write_seq_static(ctx,0x4A,0x2F);
lcm_dcs_write_seq_static(ctx,0x4B,0x2E);
lcm_dcs_write_seq_static(ctx,0x4C,0x07);
lcm_dcs_write_seq_static(ctx,0x4D,0x11);
lcm_dcs_write_seq_static(ctx,0x4E,0x10);
lcm_dcs_write_seq_static(ctx,0x4F,0x13);
lcm_dcs_write_seq_static(ctx,0x50,0x12);
lcm_dcs_write_seq_static(ctx,0x51,0x07);
lcm_dcs_write_seq_static(ctx,0x52,0x40);
lcm_dcs_write_seq_static(ctx,0x53,0x40);
lcm_dcs_write_seq_static(ctx,0x54,0x01);
lcm_dcs_write_seq_static(ctx,0x55,0x01);
lcm_dcs_write_seq_static(ctx,0x56,0x07);
lcm_dcs_write_seq_static(ctx,0x57,0x25);
lcm_dcs_write_seq_static(ctx,0x58,0x07);
lcm_dcs_write_seq_static(ctx,0x59,0x00);
lcm_dcs_write_seq_static(ctx,0x5A,0x28);
lcm_dcs_write_seq_static(ctx,0x5B,0x28);
lcm_dcs_write_seq_static(ctx,0x5C,0x2C);
lcm_dcs_write_seq_static(ctx,0x5D,0x09);
lcm_dcs_write_seq_static(ctx,0x5E,0x08);
lcm_dcs_write_seq_static(ctx,0x5F,0x41);
lcm_dcs_write_seq_static(ctx,0x60,0x41);
lcm_dcs_write_seq_static(ctx,0x61,0x30);
lcm_dcs_write_seq_static(ctx,0x62,0x2F);
lcm_dcs_write_seq_static(ctx,0x63,0x2E);
lcm_dcs_write_seq_static(ctx,0x64,0x07);
lcm_dcs_write_seq_static(ctx,0x65,0x11);
lcm_dcs_write_seq_static(ctx,0x66,0x10);
lcm_dcs_write_seq_static(ctx,0x67,0x13);
lcm_dcs_write_seq_static(ctx,0x68,0x12);
lcm_dcs_write_seq_static(ctx,0x69,0x07);
lcm_dcs_write_seq_static(ctx,0x6A,0x40);
lcm_dcs_write_seq_static(ctx,0x6B,0x40);
lcm_dcs_write_seq_static(ctx,0x6C,0x00);
lcm_dcs_write_seq_static(ctx,0x6D,0x00);
lcm_dcs_write_seq_static(ctx,0x6E,0x07);
lcm_dcs_write_seq_static(ctx,0x6F,0x25);
lcm_dcs_write_seq_static(ctx,0x70,0x07);
lcm_dcs_write_seq_static(ctx,0x71,0x01);
lcm_dcs_write_seq_static(ctx,0x72,0x28);
lcm_dcs_write_seq_static(ctx,0x73,0x28);
lcm_dcs_write_seq_static(ctx,0x74,0x2C);
lcm_dcs_write_seq_static(ctx,0x75,0x09);
lcm_dcs_write_seq_static(ctx,0x76,0x08);
lcm_dcs_write_seq_static(ctx,0x77,0x41);
lcm_dcs_write_seq_static(ctx,0x78,0x41);
lcm_dcs_write_seq_static(ctx,0x79,0x30);
lcm_dcs_write_seq_static(ctx,0x7A,0x2F);
lcm_dcs_write_seq_static(ctx,0x7B,0x2E);
lcm_dcs_write_seq_static(ctx,0x7C,0x07);
lcm_dcs_write_seq_static(ctx,0x7D,0x11);
lcm_dcs_write_seq_static(ctx,0x7E,0x10);
lcm_dcs_write_seq_static(ctx,0x7F,0x13);
lcm_dcs_write_seq_static(ctx,0x80,0x12);
lcm_dcs_write_seq_static(ctx,0x81,0x07);
lcm_dcs_write_seq_static(ctx,0x82,0x40);
lcm_dcs_write_seq_static(ctx,0x83,0x40);
lcm_dcs_write_seq_static(ctx,0x84,0x00);
lcm_dcs_write_seq_static(ctx,0x85,0x00);
lcm_dcs_write_seq_static(ctx,0x86,0x07);
lcm_dcs_write_seq_static(ctx,0x87,0x25);
lcm_dcs_write_seq_static(ctx,0x88,0x07);
lcm_dcs_write_seq_static(ctx,0x89,0x01);
lcm_dcs_write_seq_static(ctx,0x8A,0x28);
lcm_dcs_write_seq_static(ctx,0x8B,0x28);
lcm_dcs_write_seq_static(ctx,0x8C,0x2C);
lcm_dcs_write_seq_static(ctx,0x8D,0x09);
lcm_dcs_write_seq_static(ctx,0x8E,0x08);
lcm_dcs_write_seq_static(ctx,0x8F,0x41);
lcm_dcs_write_seq_static(ctx,0x90,0x41);
lcm_dcs_write_seq_static(ctx,0xA0,0x4C);
lcm_dcs_write_seq_static(ctx,0xA1,0x4A);
lcm_dcs_write_seq_static(ctx,0xA2,0x00);
lcm_dcs_write_seq_static(ctx,0xA3,0x00);
lcm_dcs_write_seq_static(ctx,0xA7,0x10);
lcm_dcs_write_seq_static(ctx,0xAA,0x00);
lcm_dcs_write_seq_static(ctx,0xAB,0x00);
lcm_dcs_write_seq_static(ctx,0xAC,0x00);
lcm_dcs_write_seq_static(ctx,0xAE,0x00);
lcm_dcs_write_seq_static(ctx,0xB0,0x20);
lcm_dcs_write_seq_static(ctx,0xB1,0x00);
lcm_dcs_write_seq_static(ctx,0xB2,0x01);
lcm_dcs_write_seq_static(ctx,0xB3,0x04);
lcm_dcs_write_seq_static(ctx,0xB4,0x05);
lcm_dcs_write_seq_static(ctx,0xB5,0x00);
lcm_dcs_write_seq_static(ctx,0xB6,0x00);
lcm_dcs_write_seq_static(ctx,0xB7,0x00);
lcm_dcs_write_seq_static(ctx,0xB8,0x00);
lcm_dcs_write_seq_static(ctx,0xC0,0x0C);
lcm_dcs_write_seq_static(ctx,0xC1,0x5D);
lcm_dcs_write_seq_static(ctx,0xC2,0x00);
lcm_dcs_write_seq_static(ctx,0xC5,0x2B);
lcm_dcs_write_seq_static(ctx,0xCA,0x01);
lcm_dcs_write_seq_static(ctx,0xD1,0x00);
lcm_dcs_write_seq_static(ctx,0xD2,0x10);
lcm_dcs_write_seq_static(ctx,0xD3,0x41);
lcm_dcs_write_seq_static(ctx,0xD4,0x89);
lcm_dcs_write_seq_static(ctx,0xD5,0x06);
lcm_dcs_write_seq_static(ctx,0xD6,0x49);
lcm_dcs_write_seq_static(ctx,0xD7,0x40);
lcm_dcs_write_seq_static(ctx,0xD8,0x09);
lcm_dcs_write_seq_static(ctx,0xD9,0x96);
lcm_dcs_write_seq_static(ctx,0xDA,0xAA);
lcm_dcs_write_seq_static(ctx,0xDB,0xAA);
lcm_dcs_write_seq_static(ctx,0xDC,0x8A);
lcm_dcs_write_seq_static(ctx,0xDD,0xA8);
lcm_dcs_write_seq_static(ctx,0xDE,0x05);
lcm_dcs_write_seq_static(ctx,0xDF,0x42);
lcm_dcs_write_seq_static(ctx,0xE0,0x1E);
lcm_dcs_write_seq_static(ctx,0xE1,0x68);
lcm_dcs_write_seq_static(ctx,0xE2,0x07);
lcm_dcs_write_seq_static(ctx,0xE3,0x11);
lcm_dcs_write_seq_static(ctx,0xE4,0x42);
lcm_dcs_write_seq_static(ctx,0xE5,0x4F);
lcm_dcs_write_seq_static(ctx,0xE6,0x22);
lcm_dcs_write_seq_static(ctx,0xE7,0x0C);
lcm_dcs_write_seq_static(ctx,0xE8,0x00);
lcm_dcs_write_seq_static(ctx,0xE9,0x00);
lcm_dcs_write_seq_static(ctx,0xEA,0x00);
lcm_dcs_write_seq_static(ctx,0xEB,0x00);
lcm_dcs_write_seq_static(ctx,0xEC,0x80);
lcm_dcs_write_seq_static(ctx,0xED,0x55);
lcm_dcs_write_seq_static(ctx,0xEE,0x00);
lcm_dcs_write_seq_static(ctx,0xEF,0x32);
lcm_dcs_write_seq_static(ctx,0xF0,0x00);
lcm_dcs_write_seq_static(ctx,0xF1,0xC0);
lcm_dcs_write_seq_static(ctx,0xF4,0x54);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x11);
lcm_dcs_write_seq_static(ctx,0x00,0x01);
lcm_dcs_write_seq_static(ctx,0x01,0x03);
lcm_dcs_write_seq_static(ctx,0x18,0x2B);
lcm_dcs_write_seq_static(ctx,0x19,0x00);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x02);
lcm_dcs_write_seq_static(ctx,0x1B,0x00);
lcm_dcs_write_seq_static(ctx,0x19,0x44);
lcm_dcs_write_seq_static(ctx,0x24,0x16);
lcm_dcs_write_seq_static(ctx,0x46,0x21);
lcm_dcs_write_seq_static(ctx,0x47,0x03);
lcm_dcs_write_seq_static(ctx,0x4F,0x01);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x12);
lcm_dcs_write_seq_static(ctx,0x01,0x44);
lcm_dcs_write_seq_static(ctx,0x03,0x44);
lcm_dcs_write_seq_static(ctx,0x05,0x44);
lcm_dcs_write_seq_static(ctx,0x10,0x05);
lcm_dcs_write_seq_static(ctx,0x11,0x00);
lcm_dcs_write_seq_static(ctx,0x12,0x06);
lcm_dcs_write_seq_static(ctx,0x13,0x15);
lcm_dcs_write_seq_static(ctx,0x16,0x06);
lcm_dcs_write_seq_static(ctx,0x1A,0x1F);
lcm_dcs_write_seq_static(ctx,0x1B,0x25);
lcm_dcs_write_seq_static(ctx,0xC0,0x34);
lcm_dcs_write_seq_static(ctx,0xC1,0x00);
lcm_dcs_write_seq_static(ctx,0xC2,0x28);
lcm_dcs_write_seq_static(ctx,0xC3,0x28);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x04);
lcm_dcs_write_seq_static(ctx,0xBD,0x01);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x05);
lcm_dcs_write_seq_static(ctx,0x1B,0x00);
lcm_dcs_write_seq_static(ctx,0x1C,0x97);
lcm_dcs_write_seq_static(ctx,0x69,0x00);
lcm_dcs_write_seq_static(ctx,0x72,0x6A);
lcm_dcs_write_seq_static(ctx,0x74,0x42);
lcm_dcs_write_seq_static(ctx,0x76,0x79);
lcm_dcs_write_seq_static(ctx,0x7A,0x51);
lcm_dcs_write_seq_static(ctx,0x7B,0x88);
lcm_dcs_write_seq_static(ctx,0x7C,0x88);
lcm_dcs_write_seq_static(ctx,0x46,0x5E);
lcm_dcs_write_seq_static(ctx,0x47,0x7E);
lcm_dcs_write_seq_static(ctx,0xB5,0x58);
lcm_dcs_write_seq_static(ctx,0xB7,0x78);
lcm_dcs_write_seq_static(ctx,0xAE,0x28);
lcm_dcs_write_seq_static(ctx,0xB1,0x38);
lcm_dcs_write_seq_static(ctx,0x56,0xFF);
lcm_dcs_write_seq_static(ctx,0x3E,0x50);
lcm_dcs_write_seq_static(ctx,0xC6,0x1B);
lcm_dcs_write_seq_static(ctx,0x61,0xCB);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x06);
lcm_dcs_write_seq_static(ctx,0xC0,0x9C);
lcm_dcs_write_seq_static(ctx,0xC1,0x19);
lcm_dcs_write_seq_static(ctx,0xC2,0xF0);
lcm_dcs_write_seq_static(ctx,0xC3,0x06);
lcm_dcs_write_seq_static(ctx,0x13,0x13);
lcm_dcs_write_seq_static(ctx,0x12,0xBD);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x07);
lcm_dcs_write_seq_static(ctx,0x07,0x4C);
lcm_dcs_write_seq_static(ctx,0x29,0xCF);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x17);
lcm_dcs_write_seq_static(ctx,0x20,0x00,0x00,0x00,0x00,0x00,0x11,0x00,0x00,0x89,0x30,0x80,0x09,0x9C,0x04,0x38,0x00,0x0A,0x02,0x1C,0x02,0x1C,0x02,0x00,0x02,0x0E,0x00,0x20,0x00,0xED,0x00,0x07,0x00,0x0C,0x0A,0xAB,0x0A,0x2C,0x18,0x00,0x10,0xF0,0x03,0x0C,0x20,0x00,0x06,0x0B,0x0B,0x33,0x0E,0x1C,0x2A,0x38,0x46,0x54,0x62,0x69,0x70,0x77,0x79,0x7B,0x7D,0x7E,0x01,0x02,0x01,0x00,0x09,0x40,0x09,0xBE,0x19,0xFC,0x19,0xFA,0x19,0xF8,0x1A,0x38,0x1A,0x78,0x1A,0xB6,0x2A,0xF6,0x2B,0x34,0x2B,0x74,0x3B,0x74,0x6B,0xF4,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
//lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x08);
//lcm_dcs_write_seq_static(ctx,0xE0,0x00,0x00,0x1A,0x45,0x00,0x84,0xB2,0xD7,0x15,0x10,0x3B,0x7A,0x25,0xAD,0xF8,0x30,0x2A,0x69,0xA8,0xCF,0x3F,0x03,0x29,0x53,0x3F,0x6C,0x90,0xC0,0x0F,0xD8,0xD9);
//lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x00,0x1A,0x45,0x00,0x84,0xB2,0xD7,0x15,0x10,0x3B,0x7A,0x25,0xAD,0xF8,0x30,0x2A,0x69,0xA8,0xCF,0x3F,0x03,0x29,0x53,0x3F,0x6C,0x90,0xC0,0x0F,0xD8,0xD9);

//Gamma Register
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x08);
lcm_dcs_write_seq_static(ctx,0xE0,0x00,0x00,0x1A,0x45,0x00,0x84,0xB2,0xD7,0x15,0x10,0x3B,0x7A,0x25,0xAD,0xF8,0x30,0x2A,0x69,0xA8,0xCF,0x3F,0x03,0x29,0x53,0x3F,0x6C,0x90,0xC0,0x0F,0xD8,0xD9);
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x00,0x1A,0x45,0x00,0x84,0xB2,0xD7,0x15,0x10,0x3B,0x7A,0x25,0xAD,0xF8,0x30,0x2A,0x69,0xA8,0xCF,0x3F,0x03,0x29,0x53,0x3F,0x6C,0x90,0xC0,0x0F,0xD8,0xD9);

//D3G EN
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x09);
lcm_dcs_write_seq_static(ctx,0x80,0x01);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x09);
lcm_dcs_write_seq_static(ctx,0x00,0x00,0x02,0x04,0x08,0x0C,0x10,0x14,0x19,0x1C,0x20,0x24,0x28,0x2C,0x30,0x34,0x38,0x3D,0x41,0x45,0x49,0x4E,0x51,0x55,0x59,0x61,0x68,0x70,0x82,0x93,0x9A,0xA2,0xAA,0xB2,0xBB,0xC2,0xC9,0xD1,0xD9,0xE1,0xE9,0xF0,0xF8,0xFA,0xFC,0xFE,0xFF,0x54,0x06,0xBB,0xAF,0x95,0xAC,0x7E,0x09,0xD3,0x1B,0x1B,0x00,0x00,0x02,0x04,0x08,0x0C,0x0F,0x13,0x18,0x1C,0x1F,0x23,0x27,0x2B,0x2F,0x32,0x36,0x3A,0x3F,0x42,0x46,0x4A,0x4E,0x52,0x56,0x5E,0x65,0x6C,0x7D,0x8D,0x95,0x9C,0xA3,0xAB,0xB4,0xBC,0xC3,0xCA,0xD2,0xDA,0xE2,0xEA,0xF2,0xF3,0xF5,0xF8,0xF9,0x04,0x2D,0x68,0xB4,0xB3,0x6F,0x11,0xD7,0x47,0x5B,0xF1,0x00,0x00,0x02,0x03,0x07,0x0B,0x0F,0x12,0x16,0x1A,0x1E,0x21,0x25,0x28,0x2C,0x30,0x33,0x37,0x3B,0x3F,0x42,0x46,0x4A,0x4E,0x51,0x58,0x60,0x66,0x75,0x85,0x8D,0x95,0x9B,0xA3,0xAA,0xB2,0xBA,0xC1,0xC8,0xD0,0xD7,0xDF,0xE7,0xE8,0xEA,0xEC,0xED,0xB0,0x52,0x63,0x8B,0xD0,0x95,0xB7,0xCE,0xA8,0xC9,0xF1,0x0F);
//AWB Register
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x03);
lcm_dcs_write_seq_static(ctx,0xD0,0x01);
lcm_dcs_write_seq_static(ctx,0xD1,0x3F);
lcm_dcs_write_seq_static(ctx,0xD2,0xFF);
lcm_dcs_write_seq_static(ctx,0xD3,0xFF);
lcm_dcs_write_seq_static(ctx,0xD4,0xFF);
lcm_dcs_write_seq_static(ctx,0xD5,0x3F);
lcm_dcs_write_seq_static(ctx,0xD6,0xFF);
lcm_dcs_write_seq_static(ctx,0xD7,0x96);
lcm_dcs_write_seq_static(ctx,0xD8,0x29);
lcm_dcs_write_seq_static(ctx,0xD9,0x3F);
lcm_dcs_write_seq_static(ctx,0xDA,0xFB);
lcm_dcs_write_seq_static(ctx,0xDB,0xEF);
lcm_dcs_write_seq_static(ctx,0xDC,0xFF);
lcm_dcs_write_seq_static(ctx,0xDD,0x3F);
lcm_dcs_write_seq_static(ctx,0xDE,0x7B);
lcm_dcs_write_seq_static(ctx,0xDF,0x9C);
lcm_dcs_write_seq_static(ctx,0xE0,0xFF);
lcm_dcs_write_seq_static(ctx,0xE1,0x3F);
lcm_dcs_write_seq_static(ctx,0xE2,0x48);
lcm_dcs_write_seq_static(ctx,0xE3,0x7F);
lcm_dcs_write_seq_static(ctx,0xE4,0xFF);
lcm_dcs_write_seq_static(ctx,0xE5,0x3F);
lcm_dcs_write_seq_static(ctx,0xE6,0x29);
lcm_dcs_write_seq_static(ctx,0xE7,0x6A);
lcm_dcs_write_seq_static(ctx,0xE8,0xFF);
lcm_dcs_write_seq_static(ctx,0xE9,0x3F);
lcm_dcs_write_seq_static(ctx,0xEA,0x29);
lcm_dcs_write_seq_static(ctx,0xEB,0x6A);
lcm_dcs_write_seq_static(ctx,0xEC,0xFF);
lcm_dcs_write_seq_static(ctx,0xED,0x00);

lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x0B);
lcm_dcs_write_seq_static(ctx,0xC0,0x84);
lcm_dcs_write_seq_static(ctx,0xC1,0x10);
lcm_dcs_write_seq_static(ctx,0xC2,0x03);
lcm_dcs_write_seq_static(ctx,0xC3,0x03);
lcm_dcs_write_seq_static(ctx,0xC4,0x65);
lcm_dcs_write_seq_static(ctx,0xC5,0x65);
lcm_dcs_write_seq_static(ctx,0xD2,0x06);
lcm_dcs_write_seq_static(ctx,0xD3,0x8E);
lcm_dcs_write_seq_static(ctx,0xD4,0x05);
lcm_dcs_write_seq_static(ctx,0xD5,0x05);
lcm_dcs_write_seq_static(ctx,0xD6,0xA4);
lcm_dcs_write_seq_static(ctx,0xD7,0xA4);
lcm_dcs_write_seq_static(ctx,0xAA,0x12);
lcm_dcs_write_seq_static(ctx,0xAB,0xE0);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x0C);
lcm_dcs_write_seq_static(ctx,0x00,0x3F);
lcm_dcs_write_seq_static(ctx,0x01,0x6A);
lcm_dcs_write_seq_static(ctx,0x02,0x3F);
lcm_dcs_write_seq_static(ctx,0x03,0x6C);
lcm_dcs_write_seq_static(ctx,0x04,0x3F);
lcm_dcs_write_seq_static(ctx,0x05,0x6B);
lcm_dcs_write_seq_static(ctx,0x06,0x3F);
lcm_dcs_write_seq_static(ctx,0x07,0x67);
lcm_dcs_write_seq_static(ctx,0x08,0x3F);
lcm_dcs_write_seq_static(ctx,0x09,0x68);
lcm_dcs_write_seq_static(ctx,0x0A,0x3F);
lcm_dcs_write_seq_static(ctx,0x0B,0x69);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x0E);
lcm_dcs_write_seq_static(ctx,0x00,0xA3);
lcm_dcs_write_seq_static(ctx,0x02,0x0F);
lcm_dcs_write_seq_static(ctx,0x04,0x06);
lcm_dcs_write_seq_static(ctx,0x05,0x20);
lcm_dcs_write_seq_static(ctx,0x13,0x04);
lcm_dcs_write_seq_static(ctx,0x21,0x28);
lcm_dcs_write_seq_static(ctx,0x22,0x04);
lcm_dcs_write_seq_static(ctx,0x23,0x28);
lcm_dcs_write_seq_static(ctx,0x24,0x84);
lcm_dcs_write_seq_static(ctx,0x20,0x03);
lcm_dcs_write_seq_static(ctx,0x25,0x11);
lcm_dcs_write_seq_static(ctx,0x26,0x62);
lcm_dcs_write_seq_static(ctx,0x27,0x20);
lcm_dcs_write_seq_static(ctx,0x29,0x67);
lcm_dcs_write_seq_static(ctx,0xB0,0x21);
lcm_dcs_write_seq_static(ctx,0xC0,0x12);
lcm_dcs_write_seq_static(ctx,0x2D,0x59);
lcm_dcs_write_seq_static(ctx,0x30,0x00);
lcm_dcs_write_seq_static(ctx,0x2B,0x05);
lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x1E);
lcm_dcs_write_seq_static(ctx,0xAD,0x00);
lcm_dcs_write_seq_static(ctx,0xA1,0x1F);
lcm_dcs_write_seq_static(ctx,0x00,0x2D);
lcm_dcs_write_seq_static(ctx,0x02,0x2D);
lcm_dcs_write_seq_static(ctx,0x03,0x2D);
lcm_dcs_write_seq_static(ctx,0x04,0x2D);
lcm_dcs_write_seq_static(ctx,0x05,0x2D);
lcm_dcs_write_seq_static(ctx,0x06,0x2D);
lcm_dcs_write_seq_static(ctx,0x07,0x2D);
lcm_dcs_write_seq_static(ctx,0x08,0x2D);
lcm_dcs_write_seq_static(ctx,0x09,0x2D);
lcm_dcs_write_seq_static(ctx,0xA4,0x00);
lcm_dcs_write_seq_static(ctx,0xA5,0x71);
lcm_dcs_write_seq_static(ctx,0xA6,0x71);
lcm_dcs_write_seq_static(ctx,0xA7,0x54);
lcm_dcs_write_seq_static(ctx,0xAA,0x00);

//lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x02); //bist mode
//lcm_dcs_write_seq_static(ctx,0x36,0x01);

lcm_dcs_write_seq_static(ctx,0xFF,0x78,0x07,0x00);
lcm_dcs_write_seq_static(ctx,0x35,0x00);
lcm_dcs_write_seq_static(ctx, 0x11);
	msleep(80);
lcm_dcs_write_seq_static(ctx, 0x29);
	msleep(20);
#endif

lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xff,0x87,0x22,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xff,0x87,0x22);
lcm_dcs_write_seq_static(ctx,0x00,0xa3);
lcm_dcs_write_seq_static(ctx,0xb3,0x09,0x9C); //1080x2460
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xC0,0x00 ,0x98 ,0x00 ,0x24 ,0x00 ,0x14);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xC0,0x00 ,0x5E ,0x00 ,0x24 ,0x00 ,0x14);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);                           
lcm_dcs_write_seq_static(ctx,0xC0,0x00 ,0x5E ,0x00 ,0x24 ,0x00 ,0x14);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xC0,0x00 ,0xB7 ,0x00 ,0x24 ,0x14);
lcm_dcs_write_seq_static(ctx,0x00,0xC1);
lcm_dcs_write_seq_static(ctx,0xC0,0x00 ,0xE4 ,0x00 ,0xB5 ,0x00 ,0x98 ,0x01 ,0x10);
lcm_dcs_write_seq_static(ctx,0x00,0x70);
lcm_dcs_write_seq_static(ctx,0xC0,0x00 ,0x5E ,0x00 ,0x24 ,0x00 ,0x14);
lcm_dcs_write_seq_static(ctx,0x00,0xA3);
lcm_dcs_write_seq_static(ctx,0xC1,0x00, 0x46, 0x00 ,0x46 ,0x00 ,0x02);
lcm_dcs_write_seq_static(ctx,0x00,0xB7);
lcm_dcs_write_seq_static(ctx,0xC1,0x00 ,0x46);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xCE,0x01, 0x81 ,0xFF ,0xFF ,0x00 ,0x85 ,0x00 ,0x85 ,0x00 ,0xF0 ,0x00 ,0xC8 ,0x00 ,0xF0 ,0x00,0xC8);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xCE,0x00 ,0xA7 ,0x10 ,0x43 ,0x00 ,0xA7 ,0x80 ,0xFF ,0xFF ,0x00 ,0x06 ,0x40 ,0x0A ,0x05 ,0x05);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);
lcm_dcs_write_seq_static(ctx,0xCE,0x00 ,0x00 ,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xCE,0x22 ,0x00 ,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xD1);
lcm_dcs_write_seq_static(ctx,0xCE,0x00 ,0x00 ,0x01 ,0x00 ,0x00 ,0x00 ,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xE1);
lcm_dcs_write_seq_static(ctx,0xCE,0x0A ,0x02 ,0xB6 ,0x02 ,0xB6 ,0x02 ,0xB6 ,0x00 ,0x00 ,0x00 ,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xF1);
lcm_dcs_write_seq_static(ctx,0xCE,0x0E ,0x20 ,0x20 ,0x01 ,0x59 ,0x00 ,0xF2 ,0x00 ,0xF2);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xCF,0x00 ,0x00 ,0xEA ,0xEE);
lcm_dcs_write_seq_static(ctx,0x00,0xB5);
lcm_dcs_write_seq_static(ctx,0xCF,0x05 ,0x05 ,0x9A ,0x9E);
lcm_dcs_write_seq_static(ctx,0x00,0xC0);
lcm_dcs_write_seq_static(ctx,0xCF,0x09 ,0x09 ,0x97 ,0x9B);
lcm_dcs_write_seq_static(ctx,0x00,0xC5);
lcm_dcs_write_seq_static(ctx,0xCF,0x09 ,0x09 ,0x9D ,0xA1);
lcm_dcs_write_seq_static(ctx,0x00,0x60);
lcm_dcs_write_seq_static(ctx,0xCF,0x00 ,0x00 ,0xEA ,0xEE ,0x05 ,0x05 ,0x9A ,0x9E);
lcm_dcs_write_seq_static(ctx,0x00,0x70);
lcm_dcs_write_seq_static(ctx,0xCF,0x00 ,0x00 ,0xEA ,0xEE ,0x05 ,0x05 ,0x9A ,0x9E);
lcm_dcs_write_seq_static(ctx,0x00,0xD1);
lcm_dcs_write_seq_static(ctx,0xC1,0x08 ,0x44 ,0x0B ,0x8B ,0x13 ,0xBC ,0x08 ,0x44 ,0x0B ,0x8B ,0x13 ,0xBC);
lcm_dcs_write_seq_static(ctx,0x00,0xE1);
lcm_dcs_write_seq_static(ctx,0xC1,0x0B ,0x8B);
lcm_dcs_write_seq_static(ctx,0x00,0xE4);
lcm_dcs_write_seq_static(ctx,0xCF,0x0A ,0x24 ,0x0A ,0x23 ,0x0A ,0x23 ,0x0A ,0x23 ,0x0A ,0x23 ,0x0A ,0x23);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xC1,0x00 ,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xC1,0x03);
lcm_dcs_write_seq_static(ctx,0x00,0xF5);
lcm_dcs_write_seq_static(ctx,0xCF,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0xF6);
lcm_dcs_write_seq_static(ctx,0xCF,0x5A);
lcm_dcs_write_seq_static(ctx,0x00,0xF1);
lcm_dcs_write_seq_static(ctx,0xCF,0x5A);
lcm_dcs_write_seq_static(ctx,0x00,0xF7);
lcm_dcs_write_seq_static(ctx,0xCF,0x11);
lcm_dcs_write_seq_static(ctx,0x00,0x8F);
lcm_dcs_write_seq_static(ctx,0xC5,0x20);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xC5,0x77);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xC2,0x82,0x00,0x01,0x8F,0x81,0x0,0x01,0x8F);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);
lcm_dcs_write_seq_static(ctx,0xC2,0x82,0x03,0x00,0x01,0x8F,0x81,0x04,0x00,0x01,0x8F,0x80,0x05,0x00,0x01,0x8F);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xC2,0x01,0x06,0x00,0x01,0x8F,0x02,0x06,0x00,0x01,0x8F,0x03,0x07,0x00,0x01,0x8F);
lcm_dcs_write_seq_static(ctx,0x00,0xC0);
lcm_dcs_write_seq_static(ctx,0xC2,0x04,0x08,0x00,0x01,0x8F,0x05,0x09,0x00,0x01,0x8F);
lcm_dcs_write_seq_static(ctx,0x00,0xE0);
lcm_dcs_write_seq_static(ctx,0xC2,0x77,0x77,0x77,0x77);
lcm_dcs_write_seq_static(ctx,0x00,0xE8);
lcm_dcs_write_seq_static(ctx,0xC2,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xCB,0x00,0x01,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0xFD,0x01,0x00,0x00,0x00,0xFC);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0xFC,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);
lcm_dcs_write_seq_static(ctx,0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0xC0);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xCB,0x10,0x50,0x05,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xC0);
lcm_dcs_write_seq_static(ctx,0xCB,0x10,0x50,0x05,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xD5);
lcm_dcs_write_seq_static(ctx,0xCB,0x01,0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x01,0x01,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xE0);
lcm_dcs_write_seq_static(ctx,0xCB,0x01,0x01,0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xCC,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x1E,0x1D,0x1C,0x18,0x17,0x16,0x22,0x06,0x0C);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xCC,0x0A,0x08,0x02,0x29,0x29,0x29,0x29,0x29);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);
lcm_dcs_write_seq_static(ctx,0xCC,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x1E,0x1D,0x1C,0x18,0x17,0x16,0x22,0x0D,0x07);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xCC,0x09,0x0B,0x03,0x29,0x29,0x29,0x29,0x29);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xCD,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x1E,0x1D,0x1C,0x18,0x17,0x16,0x22,0x07,0x0D);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xCD,0x0B,0x09,0x03,0x29,0x29,0x29,0x29,0x29);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);
lcm_dcs_write_seq_static(ctx,0xCD,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x1E,0x1D,0x1C,0x18,0x17,0x16,0x22,0x0C,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xCD,0x08,0x0A,0x02,0x29,0x29,0x29,0x29,0x29);
lcm_dcs_write_seq_static(ctx,0x00,0x86);//Normal
lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x00,0x00,0x01,0x15,0x15,0x15,0x04);
lcm_dcs_write_seq_static(ctx,0x00,0x96);//IDLE
lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x00,0x00,0x01,0x15,0x15,0x15,0x04);
lcm_dcs_write_seq_static(ctx,0x00,0xA6);//LPF
lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x00,0x00,0x01,0x15,0x15,0x15,0x04);
lcm_dcs_write_seq_static(ctx,0x00,0xA3);//PER_DMY
lcm_dcs_write_seq_static(ctx,0xCE,0x00,0x00,0x00,0x01,0x15,0x04);
lcm_dcs_write_seq_static(ctx,0x00,0xB3);//POS_DMY
lcm_dcs_write_seq_static(ctx,0xCE,0x00,0x00,0x00,0x01,0x15,0x04);
lcm_dcs_write_seq_static(ctx,0x00,0x76);//FIFO
lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x00,0x00,0x01,0x15,0x15,0x15,0x04);
lcm_dcs_write_seq_static(ctx,0x00,0x82);
lcm_dcs_write_seq_static(ctx,0xa7,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x8d);
lcm_dcs_write_seq_static(ctx,0xa7,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x8f);
lcm_dcs_write_seq_static(ctx,0xa7,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x93);
lcm_dcs_write_seq_static(ctx,0xC5,0x33);
lcm_dcs_write_seq_static(ctx,0x00,0x97);
lcm_dcs_write_seq_static(ctx,0xC5,0x33);
lcm_dcs_write_seq_static(ctx,0x00,0x9A);
lcm_dcs_write_seq_static(ctx,0xC5,0x19);
lcm_dcs_write_seq_static(ctx,0x00,0x9C);
lcm_dcs_write_seq_static(ctx,0xC5,0x19);
lcm_dcs_write_seq_static(ctx,0x00,0xB6);
lcm_dcs_write_seq_static(ctx,0xC5,0x17,0x17,0x0A,0x0A,0x17,0x17,0x0A,0x0A);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xD8,0x2E,0x2E);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xD9,0x25,0x23,0x23,0x23);
lcm_dcs_write_seq_static(ctx,0x00,0x06);
lcm_dcs_write_seq_static(ctx,0xD9,0x23,0x23,0x23);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);
lcm_dcs_write_seq_static(ctx,0xC3,0x35,0x01,0x23,0x45,0x21,0x03,0x45,0x00,0x00,0x00,0x21,0x03,0x45,0x01,0x23,0x45);
lcm_dcs_write_seq_static(ctx,0x00,0xB1);
lcm_dcs_write_seq_static(ctx,0xF5,0x1F);
lcm_dcs_write_seq_static(ctx,0x00,0xCB);
lcm_dcs_write_seq_static(ctx,0xC0,0x11);
lcm_dcs_write_seq_static(ctx,0x00,0x88);
lcm_dcs_write_seq_static(ctx,0xC4,0x08);
lcm_dcs_write_seq_static(ctx,0x00,0x9A);
lcm_dcs_write_seq_static(ctx,0xC4,0x11);
lcm_dcs_write_seq_static(ctx,0x00,0x82);
lcm_dcs_write_seq_static(ctx,0xF5,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0x93);
lcm_dcs_write_seq_static(ctx,0xF5,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0x99);
lcm_dcs_write_seq_static(ctx,0xCF,0x50);
lcm_dcs_write_seq_static(ctx,0x00,0x9C);
lcm_dcs_write_seq_static(ctx,0xF5,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x9E);
lcm_dcs_write_seq_static(ctx,0xF5,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xC5,0xD0,0x4A,0x39,0xD0,0x4A,0x0E);
lcm_dcs_write_seq_static(ctx,0x00,0xE8);
lcm_dcs_write_seq_static(ctx,0xC0,0x40);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x02,0x07,0x0F,0x36,0x1A,0x22,0x29,0x34,0xBA,0x3D,0x44,0x4A,0x4F,0x1B,0x54,0x5D,0x65,0x6C,0xD6,0x73,0x7B,0x83,0x8C,0xD4,0x96,0x9C,0xA3,0xAA,0x93,0xB3,0xBE,0xCD,0xD5,0xF3,0xE0,0xEF,0xF9,0xFF,0x84);
lcm_dcs_write_seq_static(ctx,0x00,0x30);
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x02,0x07,0x0F,0x36,0x1A,0x22,0x29,0x34,0xBA,0x3D,0x44,0x4A,0x4F,0x1B,0x54,0x5D,0x65,0x6C,0xD6,0x73,0x7B,0x83,0x8C,0xD4,0x96,0x9C,0xA3,0xAA,0x93,0xB3,0xBE,0xCD,0xD5,0xF3,0xE0,0xEF,0xF9,0xFF,0x84);
lcm_dcs_write_seq_static(ctx,0x00,0x60);
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x02,0x07,0x0F,0x36,0x1A,0x22,0x29,0x34,0xBA,0x3D,0x44,0x4A,0x4F,0x1B,0x54,0x5D,0x65,0x6C,0xD6,0x73,0x7B,0x83,0x8C,0xD4,0x96,0x9C,0xA3,0xAA,0x93,0xB3,0xBE,0xCD,0xD5,0xF3,0xE0,0xEF,0xF9,0xFF,0x84);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x02,0x07,0x0F,0x36,0x1A,0x22,0x29,0x34,0xBA,0x3D,0x44,0x4A,0x4F,0x1B,0x54,0x5D,0x65,0x6C,0xD6,0x73,0x7B,0x83,0x8C,0xD4,0x96,0x9C,0xA3,0xAA,0x93,0xB3,0xBE,0xCD,0xD5,0xF3,0xE0,0xEF,0xF9,0xFF,0x84);
lcm_dcs_write_seq_static(ctx,0x00,0xC0);
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x02,0x07,0x0F,0x36,0x1A,0x22,0x29,0x34,0xBA,0x3D,0x44,0x4A,0x4F,0x1B,0x54,0x5D,0x65,0x6C,0xD6,0x73,0x7B,0x83,0x8C,0xD4,0x96,0x9C,0xA3,0xAA,0x93,0xB3,0xBE,0xCD,0xD5,0xF3,0xE0,0xEF,0xF9,0xFF,0x84);
lcm_dcs_write_seq_static(ctx,0x00,0xF0);
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x02,0x07,0x0F,0x36,0x1A,0x22,0x29,0x34,0xBA,0x3D,0x44,0x4A,0x4F,0x1B,0x54);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE2,0x5D,0x65,0x6C,0xD6,0x73,0x7B,0x83,0x8C,0xD4,0x96,0x9C,0xA3,0xAA,0x93,0xB3,0xBE,0xCD,0xD5,0xF3,0xE0,0xEF,0xF9,0xFF,0x84);
lcm_dcs_write_seq_static(ctx,0x00,0xE0);
lcm_dcs_write_seq_static(ctx,0xCF,0x34);
lcm_dcs_write_seq_static(ctx,0x00,0x85);
lcm_dcs_write_seq_static(ctx,0xA7,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xB3,0x22);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xB3,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x83);
lcm_dcs_write_seq_static(ctx,0xB0,0x63);
lcm_dcs_write_seq_static(ctx,0x00,0xA1);
lcm_dcs_write_seq_static(ctx,0xB0,0x02);
lcm_dcs_write_seq_static(ctx,0x00,0xA9);
lcm_dcs_write_seq_static(ctx,0xB0,0xAA,0x0A);
lcm_dcs_write_seq_static(ctx,0x1C,0x02);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xFF,0xFF,0xFF,0xFF);
lcm_dcs_write_seq_static(ctx,0x35,0x00);
lcm_dcs_write_seq_static(ctx, 0x11);
	msleep(120);
lcm_dcs_write_seq_static(ctx, 0x29);
	msleep(50);

}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;
	pr_info("%s\n", __func__);
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}
	pr_info("%s_end\n", __func__);
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
	msleep(50);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(60);
	lcm_dcs_write_seq_static(ctx,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xFF,0x87,0x22,0x01);		
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xFF,0x87,0x22);
	lcm_dcs_write_seq_static(ctx,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xF7,0x5A,0xA5,0x95,0x27);
	msleep(120);

	ctx->prepared = false;

	ctx->reset_gpio =
	devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
			dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);


#if 0
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	if (ilitek_is_gesture_wakeup_enabled() == 0) {
		ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
			"bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			dev_err(ctx->dev, "%s: cannot get bias_neg %ld\n",
				__func__, PTR_ERR(ctx->bias_neg));
			return PTR_ERR(ctx->bias_neg);
		}
		gpiod_set_value(ctx->bias_neg, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);

		udelay(1000);

		ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
			"bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			dev_err(ctx->dev, "%s: cannot get bias_pos %ld\n",
				__func__, PTR_ERR(ctx->bias_pos));
			return PTR_ERR(ctx->bias_pos);
		}
		gpiod_set_value(ctx->bias_pos, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);
	}
#endif

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_enable();  //1:?กงก่5V

	
#else
	ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
		"bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_err(ctx->dev, "%s: cannot get bias_pos %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	gpiod_set_value(ctx->bias_pos, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);

	udelay(2000);

	ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
		"bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_err(ctx->dev, "%s: cannot get bias_neg %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	gpiod_set_value(ctx->bias_neg, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
#if defined(CONFIG_PRIZE_LCD_BIAS)
	display_bias_enable_v(5800);
#endif
#endif

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

#define HFP (32)
#define HSA (20)
#define HBP (32)
#define VFP_60HZ (1460)
#define VFP_90HZ (100)
#define VSA (2)
#define VBP (18)


#define VAC (2460)
#define HAC (1080)

//static u32 fake_heigh = 2460;
//static u32 fake_width = 1080;
//static bool need_fake_resolution;

static struct drm_display_mode performance_mode_90 = {
	.clock = (HAC + HFP + HSA + HBP)*(VAC + VFP_90HZ + VSA + VBP)*90/1000,		//htotal*vtotal*vrefresh/1000   1164*3815*60/1000
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP,
	.vdisplay = VAC,
	.vsync_start = VAC + VFP_90HZ,
	.vsync_end = VAC + VFP_90HZ + VSA,
	.vtotal = VAC + VFP_90HZ + VSA + VBP, //2400+1291+10+10 = 3711
};

static struct drm_display_mode default_mode = {
	.clock = (HAC + HFP + HSA + HBP)*(VAC + VFP_60HZ + VSA + VBP)*60/1000,
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP, //= 1080+76+12+56 = 1224
	.vdisplay = VAC,
	.vsync_start = VAC + VFP_60HZ,
	.vsync_end = VAC + VFP_60HZ + VSA,
	.vtotal = VAC + VFP_60HZ + VSA + VBP, //= 2400+54+10+10 = 2474
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_90 = {
	.pll_clk = 570,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {

		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.data_rate = 1140,
	.is_cphy = 1,
	.dyn = {
		.switch_en = 0,
		.data_rate = 1140,
		.hfp = 32,
		.vfp = 64,
	},
	.lfr_enable = 1,
	.lfr_minimum_fps = 60,

	.physical_width_um = 69574,
	.physical_height_um = 157560,
};
static struct mtk_panel_params ext_params_60 = {
	.pll_clk = 570,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.is_cphy = 1,
	.data_rate = 1140,

	.dyn = {
		.switch_en = 0,
		.data_rate = 1140,
		.hfp = 32,
		.vfp = 1380,
	},
	.lfr_enable = 1,
	.lfr_minimum_fps = 60,

	.physical_width_um = 69574,
	.physical_height_um = 157560,
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
	struct lcm *ctx = panel_to_lcm(panel);
    struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
    unsigned char data[1] = {0x00};
    unsigned char id[1] = {0x9C};
    ssize_t ret;

    ret = mipi_dsi_dcs_read(dsi, 0x0a, data, 1);
    if (ret < 0) {
            pr_err("%s error\n", __func__);
            return 0;
    }

    pr_info("ATA read data %x\n", data[0]);

    if (data[0] == id[0])
            return 1;

    pr_info("ATA expect read data is %x\n",id[0]);

    return 0;

	///* Customer test by own ATA tool */
	//return 1;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	char bl_tb0[] = { 0x51, 0x0d, 0xbb};
	unsigned int reg_level = 125;
	pr_info("[%s]mian lcd set backlight:%d\n",__func__, level);

	if (level) {
		reg_level = level;
		//atomic_set(&current_backlight, level);
	} else {
		reg_level = 0;
	}

	//if (g_ctx->hbm_mode) {
	//	pr_info("[%s]hbm_mode = %d, skip backlight\n",__func__, g_ctx->hbm_mode);
	//	return 0;
	//}

	bl_tb0[1] = (u8)((reg_level>>8)&0xFF);
	bl_tb0[2] = (u8)(reg_level&0xFF);

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

//static int current_fps = 60;
static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	if (drm_mode_vrefresh(m) == 60)
		ext->params = &ext_params_60;
	else if (drm_mode_vrefresh(m) == 90)
		ext->params = &ext_params_90;
	else
		ret = 1;

	//if (!ret)
		//current_fps = drm_mode_vrefresh(m);

	return ret;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.get_virtual_heigh = lcm_get_virtual_heigh,
	.get_virtual_width = lcm_get_virtual_width,
	.ext_param_set = mtk_panel_ext_param_set,
	//.mode_switch = mode_switch,
	/*pri add aod mode 20240411 start*/
	//.doze_enable = panel_doze_enable,
	//.doze_disable = panel_doze_disable,
	/*pri add aod mode 20240411 end*/
};
#endif

static int lcm_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode_60;
	struct drm_display_mode *mode_90;
	//struct drm_display_mode *mode_120;

	mode_60 = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode_60) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 default_mode.hdisplay, default_mode.vdisplay,
			 drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_60);
	mode_60->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode_60);

	mode_90 = drm_mode_duplicate(connector->dev, &performance_mode_90);
	if (!mode_90) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_90.hdisplay, performance_mode_90.vdisplay,
			 drm_mode_vrefresh(&performance_mode_90));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_90);
	mode_90->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode_90);

#if 0
	mode_120 = drm_mode_duplicate(connector->dev, &performance_mode_120);
	if (!mode_120) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			performance_mode_120.hdisplay, performance_mode_120.vdisplay,
			drm_mode_vrefresh(&performance_mode_120));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_120);
	mode_120->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode_120);
#endif
	connector->display_info.width_mm = 70;
	connector->display_info.height_mm = 158;

	return 2;
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
	//const u32 *val;

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
	dsi->lanes = 3;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
			 | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET
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
#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_enable();
#else
	ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_err(dev, "%s: cannot get bias-pos 0 %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	devm_gpiod_put(dev, ctx->bias_pos);

	ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_err(dev, "%s: cannot get bias-neg 1 %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	devm_gpiod_put(dev, ctx->bias_neg);
#endif
	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	//val = of_get_property(dev->of_node, "reg", NULL);
	//ctx->version = val ? be32_to_cpup(val) : 1;

	//pr_info("%s: panel version 0x%x\n", __func__, ctx->version);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params_60, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;

#endif
/* pri LAX10-192 added by xuejian 20240409 begin*/
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
    strcpy(current_lcm_info.chip,"ft8722");
    strcpy(current_lcm_info.vendor,"xiapu");
    sprintf(current_lcm_info.id,"0x%02x",0x02);
    strcpy(current_lcm_info.more,"1080x2460");
#endif
/* pri LAX10-192 added by xuejian 20240409 end*/
	//ctx->hbm_mode = 0;

	//ctx->current_fps = 60;
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
	{ .compatible = "truly,ft8722,vdo-xp-a500-cphy", },
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-truly-ft8722-vdo-xp-a500-cphy",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("sc ft8722 VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");
