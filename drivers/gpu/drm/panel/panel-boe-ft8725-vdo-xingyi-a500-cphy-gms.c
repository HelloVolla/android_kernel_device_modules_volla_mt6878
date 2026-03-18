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

	pr_info("FT8725 GMS %s\n", __func__);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset-gpios %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return;
	}
	//gpiod_set_value(ctx->reset_gpio, 0);
	//mdelay(20);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(20);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(20);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(50);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	lcm_dcs_write_seq_static(ctx,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xFF,0x87,0x25,0x01);
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xFF,0x87,0x25);

// panle size
	lcm_dcs_write_seq_static(ctx,0x00,0xA3);
	lcm_dcs_write_seq_static(ctx,0xB3,0x09,0x9C,0x00,0x18);
//TCON
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x4B,0x00,0x1E,0x00,0x14);

	lcm_dcs_write_seq_static(ctx,0x00,0x90);
	lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x4B,0x00,0x1E,0x00,0x14);

	lcm_dcs_write_seq_static(ctx,0x00,0xA0);
	lcm_dcs_write_seq_static(ctx,0xC0,0x00,0xAF,0x00,0x1E,0x00,0x14);

	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xC0,0x00,0xCA,0x00,0x1E,0x14);

	lcm_dcs_write_seq_static(ctx,0x00,0xC1);
	lcm_dcs_write_seq_static(ctx,0xC0,0x00,0xA0,0x00,0x86,0x00,0x6B,0x00,0xCA);

	lcm_dcs_write_seq_static(ctx,0x00,0x70);
	lcm_dcs_write_seq_static(ctx,0xC0,0x00,0xAF,0x00,0x1E,0x00,0x14);

	lcm_dcs_write_seq_static(ctx,0x00,0xA3);
	lcm_dcs_write_seq_static(ctx,0xC1,0x00,0x6A,0x00,0x3C,0x00,0x02);

	lcm_dcs_write_seq_static(ctx,0x00,0xB7);
	lcm_dcs_write_seq_static(ctx,0xC1,0x00,0x48);

	lcm_dcs_write_seq_static(ctx,0x00,0x7B);
	lcm_dcs_write_seq_static(ctx,0xCE,0xFF,0xFF);

	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xCE,0x01,0x81,0xFF,0xFF,0x00,0xB4,0x00,0xCC,0x00,0xC8,0x00,0xC8,0x00,0xC8,0x00,0xC8);

	lcm_dcs_write_seq_static(ctx,0x00,0x90);
	lcm_dcs_write_seq_static(ctx,0xCE,0x00,0xB7,0x0F,0x7D,0x00,0xB7,0x80,0xFF,0xFF,0x00,0x05,0xDC,0x14,0x10,0x10);

	lcm_dcs_write_seq_static(ctx,0x00,0xA0);
	lcm_dcs_write_seq_static(ctx,0xCE,0x00,0x00,0x00);

	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xCE,0x22,0x00,0x00);

	lcm_dcs_write_seq_static(ctx,0x00,0xD1);
	lcm_dcs_write_seq_static(ctx,0xCE,0x00,0x00,0x01,0x00,0x00,0x00,0x00);

	lcm_dcs_write_seq_static(ctx,0x00,0xE1);
	lcm_dcs_write_seq_static(ctx,0xCE,0x0A,0x02,0xFB,0x02,0xFB,0x02,0xFB,0x00,0x00,0x00,0x00);

	lcm_dcs_write_seq_static(ctx,0x00,0xF1);
	lcm_dcs_write_seq_static(ctx,0xCE,0x25,0x12,0x12,0x00,0xDA,0x01,0x40,0x01,0x40);

	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xCF,0x00,0x00,0xAA,0xAE);

	lcm_dcs_write_seq_static(ctx,0x00,0xB5);
	lcm_dcs_write_seq_static(ctx,0xCF,0x05,0x05,0x72,0x76);

	lcm_dcs_write_seq_static(ctx,0x00,0xC0);
	lcm_dcs_write_seq_static(ctx,0xCF,0x09,0x09,0x97,0x9B);

	lcm_dcs_write_seq_static(ctx,0x00,0xC5);
	lcm_dcs_write_seq_static(ctx,0xCF,0x09,0x09,0x9D,0xA1);

	lcm_dcs_write_seq_static(ctx,0x00,0x60);
	lcm_dcs_write_seq_static(ctx,0xCF,0x00,0x00,0xC3,0xC7,0x05,0x05,0x73,0x77);

	lcm_dcs_write_seq_static(ctx,0x00,0x70);
	lcm_dcs_write_seq_static(ctx,0xCF,0x00,0x00,0xC3,0xC7,0x05,0x05,0x73,0x77);

//Qsync Detect
	lcm_dcs_write_seq_static(ctx,0x00,0xD1);
	lcm_dcs_write_seq_static(ctx,0xC1,0x0B,0x60,0x0F,0xDC,0x1B,0x15,0x05,0xAF,0x07,0xE5,0x0D,0x81);

	lcm_dcs_write_seq_static(ctx,0x00,0xE1);
	lcm_dcs_write_seq_static(ctx,0xC1,0x0F,0xDC);

	lcm_dcs_write_seq_static(ctx,0x00,0xE4);
	lcm_dcs_write_seq_static(ctx,0xCF,0x0A,0x1E,0x0A,0x1D,0x0A,0x1D,0x0A,0x1D,0x0A,0x1D,0x0A,0x1D);

//OSC
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xC1,0x44,0x44);

	lcm_dcs_write_seq_static(ctx,0x00,0x90);
	lcm_dcs_write_seq_static(ctx,0xC1,0x03);

//Line rate for TP
	lcm_dcs_write_seq_static(ctx,0x00,0xF5);
	lcm_dcs_write_seq_static(ctx,0xCF,0x00);

//TP Frame rate
	lcm_dcs_write_seq_static(ctx,0x00,0xF6);
	lcm_dcs_write_seq_static(ctx,0xCF,0x78);

//TCON Frame rate
	lcm_dcs_write_seq_static(ctx,0x00,0xF1);
	lcm_dcs_write_seq_static(ctx,0xCF,0x78);

//Tcon clk
	lcm_dcs_write_seq_static(ctx,0x00,0x91);
	lcm_dcs_write_seq_static(ctx,0xC4,0x88);
//VDD=1.275V LVDSVDD=1.25V VDD_TP=1.2V
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xC5,0x86,0x59);

	lcm_dcs_write_seq_static(ctx,0x00,0x87);
	lcm_dcs_write_seq_static(ctx,0xC5,0x0A,0x0A);

//VDDI current 20230404
	lcm_dcs_write_seq_static(ctx,0x00,0x93);
	lcm_dcs_write_seq_static(ctx,0xC1,0x82);

//4 power VDD=1.275V LVDSVDD=1.25V
	lcm_dcs_write_seq_static(ctx,0x00,0x9E);
	lcm_dcs_write_seq_static(ctx,0xC5,0x87);
	lcm_dcs_write_seq_static(ctx,0x00,0x88);
	lcm_dcs_write_seq_static(ctx,0xC4,0x08);
		
//========================================
//STV1 & STV2 Setting
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xC2,0x83,0x01,0x01,0x86,0x82,0x01,0x01,0x86,0x8D,0x02,0x01,0x86);

//CKV1-3 setting
	lcm_dcs_write_seq_static(ctx,0x00,0xA0);
	lcm_dcs_write_seq_static(ctx,0xC2,0x8A,0x07,0x00,0x01,0x95,0x89,0x08,0x00,0x01,0x95,0x88,0x09,0x00,0x01,0x95);

//CKV4 setting
	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xC2,0x87,0x0A,0x00,0x01,0x95);

//CKV width setting
	lcm_dcs_write_seq_static(ctx,0x00,0xE0);
	lcm_dcs_write_seq_static(ctx,0xC2,0x33,0x33,0x00,0x00);

//Rst1 Setting
	lcm_dcs_write_seq_static(ctx,0x00,0xE8);
	lcm_dcs_write_seq_static(ctx,0xC2,0x12,0x00,0x0A,0x0A,0x03,0x88,0x00,0x00);

//GOFF setting
	lcm_dcs_write_seq_static(ctx,0x00,0xD0);
	lcm_dcs_write_seq_static(ctx,0xC3,0x35,0x0A,0x00,0x00,0x35,0x0A,0x00,0x00,0x35,0x0A,0x00,0x00,0x35,0x0A,0x00,0x00);

	lcm_dcs_write_seq_static(ctx,0x00,0xE0);
	lcm_dcs_write_seq_static(ctx,0xC3,0x35,0x0A,0x00,0x00,0x35,0x0A,0x00,0x00,0x35,0x0A,0x00,0x00,0x35,0x0A,0x00,0x00);

//power off enmode setting
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xCB,0xCD,0xCD,0xCD,0x00,0xCD,0xCC,0x00,0xCD,0xCE,0xFE,0xCD,0x00,0xCC,0xCC,0x00,0x00);

//power on enmode setting
	lcm_dcs_write_seq_static(ctx,0x00,0x90);
	lcm_dcs_write_seq_static(ctx,0xCB,0x0C,0x00,0x00,0x00,0x0C,0x0C,0x00,0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00);

//skip & powr on1 enmode setting
	lcm_dcs_write_seq_static(ctx,0x00,0xA0);
	lcm_dcs_write_seq_static(ctx,0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);

//power off blank enmode setting
	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xCB,0x50,0x41,0xA4,0x00);

//power on blank enmode setting
	lcm_dcs_write_seq_static(ctx,0x00,0xC0);
	lcm_dcs_write_seq_static(ctx,0xCB,0x50,0x41,0xA4,0x00);

//power on blank enmode setting
	lcm_dcs_write_seq_static(ctx,0x00,0xD5);
	lcm_dcs_write_seq_static(ctx,0xCB,0x83,0x00,0x83,0x83,0x00,0x83,0x83,0x00,0x83,0x83,0x00);

	lcm_dcs_write_seq_static(ctx,0x00,0xE0);
	lcm_dcs_write_seq_static(ctx,0xCB,0x83,0x83,0x00,0x83,0x83,0x00,0x83,0x83,0x00,0x83,0x83,0x00,0x83);

//panel mapping setting
//u2d_L
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xCC,0x18,0x17,0x16,0x2C,0x07,0x06,0x09,0x08,0x2c,0x26,0x26,0x26,0x26,0x2c,0x24,0x2c);

	lcm_dcs_write_seq_static(ctx,0x00,0x90);
	lcm_dcs_write_seq_static(ctx,0xCC,0x00,0x00,0x00,0x04,0x03,0x02,0x25,0x25);
//u2d_R
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xCD,0x18,0x17,0x16,0x2C,0x07,0x06,0x09,0x08,0x2c,0x26,0x26,0x26,0x26,0x2c,0x24,0x2c);

	lcm_dcs_write_seq_static(ctx,0x00,0x90);
	lcm_dcs_write_seq_static(ctx,0xCD,0x00,0x00,0x00,0x04,0x03,0x02,0x25,0x25);
//==============================================

//ckh
	lcm_dcs_write_seq_static(ctx,0x00,0x86);//Normal
	lcm_dcs_write_seq_static(ctx,0xC0,0x01,0x01,0x01,0x00,0x12,0x12,0x12,0x03);

	lcm_dcs_write_seq_static(ctx,0x00,0x96);//IDLE
	lcm_dcs_write_seq_static(ctx,0xC0,0x01,0x02,0x01,0x00,0x13,0x13,0x13,0x03);

	lcm_dcs_write_seq_static(ctx,0x00,0xA6);//LPF
	lcm_dcs_write_seq_static(ctx,0xC0,0x01,0x02,0x01,0x00,0x1D,0x1D,0x1D,0x03);

	lcm_dcs_write_seq_static(ctx,0x00,0xA3);//PER_DMY
	lcm_dcs_write_seq_static(ctx,0xCE,0x01,0x01,0x01,0x00,0x12,0x03);

	lcm_dcs_write_seq_static(ctx,0x00,0xB3);//POS_DMY
	lcm_dcs_write_seq_static(ctx,0xCE,0x00,0x01,0x01,0x00,0x12,0x03);

	lcm_dcs_write_seq_static(ctx,0x00,0x76);//FIFO
	lcm_dcs_write_seq_static(ctx,0xC0,0x01,0x02,0x01,0x01,0x28,0x28,0x28,0x0B);

//CKH_dummy			
	lcm_dcs_write_seq_static(ctx,0x00,0x82);			
	lcm_dcs_write_seq_static(ctx,0xa7,0x20,0x00);			
			
	lcm_dcs_write_seq_static(ctx,0x00,0x8d);			
	lcm_dcs_write_seq_static(ctx,0xa7,0x02);			
			
	lcm_dcs_write_seq_static(ctx,0x00,0x8f);			
	lcm_dcs_write_seq_static(ctx,0xa7,0x01);

//analog setting
//vgh=11V
	lcm_dcs_write_seq_static(ctx,0x00,0x93);
	lcm_dcs_write_seq_static(ctx,0xC5,0x37);

	lcm_dcs_write_seq_static(ctx,0x00,0x97);
	lcm_dcs_write_seq_static(ctx,0xC5,0x37);

//vgl=-9V
	lcm_dcs_write_seq_static(ctx,0x00,0x9A);
	lcm_dcs_write_seq_static(ctx,0xC5,0x23);

	lcm_dcs_write_seq_static(ctx,0x00,0x9C);
	lcm_dcs_write_seq_static(ctx,0xC5,0x23);

//vgho1=10V, vglo1=-8V 
	lcm_dcs_write_seq_static(ctx,0x00,0xB6);
	lcm_dcs_write_seq_static(ctx,0xC5,0x2D,0x2D,0x19,0x19);

//GVDDP=5.2V, GVDDN=-5.2V
	lcm_dcs_write_seq_static(ctx,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xD8,0x2F,0x2F);

//VCOM=-0.2V
	lcm_dcs_write_seq_static(ctx,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xD9,0x3C,0x3C,0x3C,0x3C);
	lcm_dcs_write_seq_static(ctx,0x00,0x06);
	lcm_dcs_write_seq_static(ctx,0xD9,0x23,0x23,0x23);

	lcm_dcs_write_seq_static(ctx,0x00,0x88);
	lcm_dcs_write_seq_static(ctx,0xC4,0x08);

//CKH Rotate	
//0x03:RGBBGR  0x10:RGBRGB
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xA7,0x03);		
				
	lcm_dcs_write_seq_static(ctx,0x00,0xA0);
	lcm_dcs_write_seq_static(ctx,0xC3,0x00,0x01,0x23,0x45,0x21,0x03,0x45,0x00,0x00,0x00,0x21,0x03,0x45,0x01,0x23,0x45);

	lcm_dcs_write_seq_static(ctx,0x00,0xB1);			
	lcm_dcs_write_seq_static(ctx,0xF5,0x1F);
//C0CB[7:4]=PONBLANK=1= 2 FRAME
//C0CB[3:0]=POFBLANK=1= 2 FRAME
	lcm_dcs_write_seq_static(ctx,0x00,0xCB);
	lcm_dcs_write_seq_static(ctx,0xC0,0x01);

	lcm_dcs_write_seq_static(ctx,0x00,0x88);
	lcm_dcs_write_seq_static(ctx,0xC4,0x08);

	lcm_dcs_write_seq_static(ctx,0x00,0x94);
	lcm_dcs_write_seq_static(ctx,0xE9,0x00);

	lcm_dcs_write_seq_static(ctx,0x00,0x9A);
	lcm_dcs_write_seq_static(ctx,0xC4,0x11);

	lcm_dcs_write_seq_static(ctx,0x00,0x95);
	lcm_dcs_write_seq_static(ctx,0xE9,0x10);
	lcm_dcs_write_seq_static(ctx,0x00,0x82);
	lcm_dcs_write_seq_static(ctx,0xF5,0x00);
	lcm_dcs_write_seq_static(ctx,0x00,0x93);
	lcm_dcs_write_seq_static(ctx,0xF5,0x00);

//AC MODE GIP toggle
	lcm_dcs_write_seq_static(ctx,0x00,0x99);			
	lcm_dcs_write_seq_static(ctx,0xCF,0x50);

	lcm_dcs_write_seq_static(ctx,0x00,0x9C);			
	lcm_dcs_write_seq_static(ctx,0xF5,0x00);

	lcm_dcs_write_seq_static(ctx,0x00,0x9E);			
	lcm_dcs_write_seq_static(ctx,0xF5,0x00);

	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xC5,0x10,0x4A,0x01,0x1F,0x4A,0x00);//fw q?? 8725 ????

//	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
//	lcm_dcs_write_seq_static(ctx,0xC5,0x10,0x4A,0x09,0x1F,0x4A,0x02);//8725 JDI ini

	lcm_dcs_write_seq_static(ctx,0x00,0xA0);
	lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00,0x00,0x00,0x00,0x1D,0x01); //D-phy??phy???

	lcm_dcs_write_seq_static(ctx,0x00,0x93);
	lcm_dcs_write_seq_static(ctx,0xE9,0xAA);

	lcm_dcs_write_seq_static(ctx,0x00,0x95);
	lcm_dcs_write_seq_static(ctx,0xE9,0xB0);

	lcm_dcs_write_seq_static(ctx,0x00,0x9B);
	lcm_dcs_write_seq_static(ctx,0xC4,0x08);

	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xA4,0xC8);

//mirror_x2=1
	lcm_dcs_write_seq_static(ctx,0x00,0xE8);
	lcm_dcs_write_seq_static(ctx,0xC0,0x40);

	lcm_dcs_write_seq_static(ctx,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x04,0x08,0x0F,0x3F,0x1B,0x23,0x29,0x34,0x08,0x3C,0x43,0x49,0x4E,0x40,0x52,0x5B,0x62,0x69,0xC5,0x70,0x77,0x7E,0x87,0x5C,0x90,0x96,0x9C,0xA4,0xDC,0xAC,0xB7,0xC5,0xCE,0xE1,0xDA,0xEA,0xF6,0xFF,0x18);
	lcm_dcs_write_seq_static(ctx,0x00,0x30);
	lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x04,0x08,0x0F,0x3F,0x1B,0x23,0x29,0x34,0x08,0x3C,0x43,0x49,0x4E,0x40,0x52,0x5B,0x62,0x69,0xC5,0x70,0x77,0x7E,0x87,0x5C,0x90,0x96,0x9C,0xA4,0xDC,0xAC,0xB7,0xC5,0xCE,0xE1,0xDA,0xEA,0xF6,0xFF,0x18);
	lcm_dcs_write_seq_static(ctx,0x00,0x60);
	lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x04,0x08,0x0F,0x3F,0x1B,0x23,0x29,0x34,0x08,0x3C,0x43,0x49,0x4E,0x40,0x52,0x5B,0x62,0x69,0xC5,0x70,0x77,0x7E,0x87,0x5C,0x90,0x96,0x9C,0xA4,0xDC,0xAC,0xB7,0xC5,0xCE,0xE1,0xDA,0xEA,0xF6,0xFF,0x18);
	lcm_dcs_write_seq_static(ctx,0x00,0x90);
	lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x04,0x08,0x0F,0x3F,0x1B,0x23,0x29,0x34,0x08,0x3C,0x43,0x49,0x4E,0x40,0x52,0x5B,0x62,0x69,0xC5,0x70,0x77,0x7E,0x87,0x5C,0x90,0x96,0x9C,0xA4,0xDC,0xAC,0xB7,0xC5,0xCE,0xE1,0xDA,0xEA,0xF6,0xFF,0x18);
	lcm_dcs_write_seq_static(ctx,0x00,0xC0);
	lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x04,0x08,0x0F,0x3F,0x1B,0x23,0x29,0x34,0x08,0x3C,0x43,0x49,0x4E,0x40,0x52,0x5B,0x62,0x69,0xC5,0x70,0x77,0x7E,0x87,0x5C,0x90,0x96,0x9C,0xA4,0xDC,0xAC,0xB7,0xC5,0xCE,0xE1,0xDA,0xEA,0xF6,0xFF,0x18);
	lcm_dcs_write_seq_static(ctx,0x00,0xF0);
	lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x04,0x08,0x0F,0x3F,0x1B,0x23,0x29,0x34,0x08,0x3C,0x43,0x49,0x4E,0x40,0x52);
	lcm_dcs_write_seq_static(ctx,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xE2,0x5B,0x62,0x69,0xC5,0x70,0x77,0x7E,0x87,0x5C,0x90,0x96,0x9C,0xA4,0xDC,0xAC,0xB7,0xC5,0xCE,0xE1,0xDA,0xEA,0xF6,0xFF,0x18);


	lcm_dcs_write_seq_static(ctx,0x00,0xE0);
	lcm_dcs_write_seq_static(ctx,0xCF,0x34);

	lcm_dcs_write_seq_static(ctx,0x00,0x85);
	lcm_dcs_write_seq_static(ctx,0xA7,0x00);

//ESD disable reg_21h_rev_disable
	lcm_dcs_write_seq_static(ctx,0x00,0x80);
	lcm_dcs_write_seq_static(ctx,0xB3,0x22);
 
//HS lock CMD1. B3B0h=0x01->0x00
	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xB3,0x00);

//TP TERM=48
	lcm_dcs_write_seq_static(ctx,0x00,0x82);
	lcm_dcs_write_seq_static(ctx,0xCE,0x2F,0x2F);
//CKH TOGGLE
	lcm_dcs_write_seq_static(ctx,0x00,0x90);
	lcm_dcs_write_seq_static(ctx,0xA7,0x1d);
//SD CHOP 20230510
//	lcm_dcs_write_seq_static(ctx,0x00,0x81);
//	lcm_dcs_write_seq_static(ctx,0xA4,0x83);

	lcm_dcs_write_seq_static(ctx,0x00,0xFC);
	lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x15);

//CABC PWM 21.37Khz 11bit
	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xCA,0x05,0x05,0x0B);


//Slice Height=8
	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xB4,0x00,0x08,0x02,0x00,0x00,0xbb,0x00,0x07,0x0d,0xb7,0x0c,0xb7,0x10,0xf0);

	lcm_dcs_write_seq_static(ctx,0x00,0x81);
	lcm_dcs_write_seq_static(ctx,0xA4,0x73);

	lcm_dcs_write_seq_static(ctx,0x00,0x87);
	lcm_dcs_write_seq_static(ctx,0xC4,0x08);

	lcm_dcs_write_seq_static(ctx,0x00,0xBE);
	lcm_dcs_write_seq_static(ctx,0xC5,0xC0,0xC0); //LPWG burr

	lcm_dcs_write_seq_static(ctx,0x00,0xB0);
	lcm_dcs_write_seq_static(ctx,0xB4,0x00,0x0C,0x02,0x00,0x01,0x1F,0x00,0x07,0x08,0xBB,0x08,0x7A,0x10,0xF0);//12

	lcm_dcs_write_seq_static(ctx,0x00,0x0E);
	lcm_dcs_write_seq_static(ctx,0xF3,0x80,0xFF);


	lcm_dcs_write_seq_static(ctx,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xFF,0xFF,0xFF,0xFF);
//	lcm_dcs_write_seq_static(ctx,0x35,0x01);

//	lcm_dcs_write_seq_static(ctx,0x1c,0x02);

//----------------------LCD initial code End----------------------//			
//SLPOUT and DISPON			
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


	pr_info("FT8725 GMS %s+\n", __func__);
	if (!ctx->prepared)
		return 0;

	lcm_dcs_write_seq_static(ctx, 0x28);
	msleep(50);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(150);

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
//	ctx->reset_gpio =
//		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
//	if (IS_ERR(ctx->reset_gpio)) {
//		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
//			__func__, PTR_ERR(ctx->reset_gpio));
//		return PTR_ERR(ctx->reset_gpio);
//	}
//	gpiod_set_value(ctx->reset_gpio, 1);
//	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	//if (ilitek_is_gesture_wakeup_enabled() == 0) {
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
	//}
#endif

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("FT8725 GMS%s+\n", __func__);
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

#define HFP (220)
#define HSA (8)
#define HBP (16)
#define VFP_60HZ (2700)
#define VSA (10)
#define VBP (20)


#define VAC (2460)
#define HAC (1080)

//static u32 fake_heigh = 2460;
//static u32 fake_width = 1080;
//static bool need_fake_resolution;

static struct drm_display_mode default_mode = {
	//.clock = 351185,		//htotal*vtotal*60/1000
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
static struct mtk_panel_params ext_params_60 = {
	.pll_clk = 340,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.is_cphy = 1,
	.data_rate = 680,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
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
		.slice_height = 12,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 512,
		.dec_delay = 616,
		.scale_value = 32,
		.increment_interval = 287,
		.decrement_interval = 7,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 2235,
		.slice_bpg_offset = 2170,
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
	.dyn = {
		.switch_en = 0,
		.data_rate = 680,
		.hfp = 220,
		.vfp = 2700,
	},
	.lfr_enable = 0,
	.lfr_minimum_fps = 60,
	.lp_perline_en = 1,

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
    strcpy(current_lcm_info.chip,"ft8725,gms");
    strcpy(current_lcm_info.vendor,"xingyi");
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
	{ .compatible = "boe,ft8725,vdo-xingyi-a500-cphy-gms", },
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-boe-ft8725-vdo-xingyi-a500-cphy-gms",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("sc ft8725 VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");
