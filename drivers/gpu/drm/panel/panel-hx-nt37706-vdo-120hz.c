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
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 start */
#include <linux/kthread.h>
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 end */

#include "include/panel-hx-nt37706-vdo-120hz.h"

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
extern unsigned int lcm_set_page(unsigned int case_num);
extern unsigned int lcm_set_register(unsigned char val1, unsigned char val2);
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 start */
extern int lcm_ddic_dsi_read_cmd(unsigned char cmd_addr, unsigned char read_len,
	unsigned char *rx_data, unsigned char rx_data_len);
extern void lcm_ddic_dsi_send_cmd_lhbm(unsigned char *tx_data, unsigned char tx_len);
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 end */

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
	/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 start */
	struct delayed_work reflash_work;
	struct workqueue_struct *reflash_workqueue;
	/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 end */
};

struct lcm *g_ctx;
static atomic_t current_backlight;
/* pri added by xuejian 20240415 begin */
static struct kobject *kobj = NULL;
extern void lcm_set_hbm_backlight(unsigned int case_num, unsigned char val1, unsigned char val2);

/* pri added by xuejian 20240415 begin */
static bool lcm_aod_status = false;

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
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 start */
static char bl_tbD1[] = {0xD1,0x26,0xC0,0x23,0x68,0x2C,0xE4};

static void lcm_get_lhbm_info(struct lcm *ctx)
{
	int ret = 0;
	u8 buffer_B2[3] = { 0 };
	u8 buffer_B5[3] = { 0 };
	u8 buffer_B8[3] = { 0 };
	unsigned int data_R = 0;
	unsigned int data_G = 0;
	unsigned int data_B = 0;
	int i = 0;

	char bl_tbF0[] = {0xF0,0x55,0xAA,0x52,0x08,0x02};
	char bl_tbBF[] = {0xBF,0x0B};
	char bl_tb6F[] = {0x6F,0x08};

	pr_info("%s+\n", __func__);

	lcm_ddic_dsi_send_cmd_lhbm(bl_tbF0,ARRAY_SIZE(bl_tbF0));
	lcm_ddic_dsi_send_cmd_lhbm(bl_tbBF,ARRAY_SIZE(bl_tbBF));
	lcm_ddic_dsi_send_cmd_lhbm(bl_tb6F,ARRAY_SIZE(bl_tb6F));
	ret = lcm_ddic_dsi_read_cmd(0xB2, 2, buffer_B2, 2);
	if (ret != 0) {
		pr_err("[%s] read 0xB2 error", __func__);
	}
	printk("[%s] read buffer_B2:0x%x, 0x%x\n", __func__, buffer_B2[0], buffer_B2[1]);
	lcm_ddic_dsi_send_cmd_lhbm(bl_tb6F,ARRAY_SIZE(bl_tb6F));
	/* dataG_H, dataG_L*/
	ret = lcm_ddic_dsi_read_cmd(0xB5, 2, buffer_B5, 2);
	if (ret != 0) {
		pr_err("[%s] read 0xB5 error", __func__);
	}
	printk("[%s] read buffer_B5:0x%x, 0x%x\n", __func__, buffer_B5[0], buffer_B5[1]);
	lcm_ddic_dsi_send_cmd_lhbm(bl_tb6F,ARRAY_SIZE(bl_tb6F));
	ret = lcm_ddic_dsi_read_cmd(0xB8, 2, buffer_B8, 2);
	if (ret != 0) {
		pr_err("[%s] read 0xB8 error", __func__);
	}
	printk("[%s] read buffer_B8:0x%x, 0x%x\n", __func__, buffer_B8[0], buffer_B8[1]);
    if (ret >= 0) {
		/* dataR1_H,dataR1_L,dataG1_H,dataG1_L,dataB1_H, dataB1_L */
		/* dataR1 = dataR*4 dataG1 = dataG*4 dataB1 = dataB*4 */
		data_R = buffer_B2[0] << 8 | buffer_B2[1];
		data_G = buffer_B5[0] << 8 | buffer_B5[1];
		data_B = buffer_B8[0] << 8 | buffer_B8[1];
		printk("[%s] data_R:0x%x, data_G:0x%x, data_B:0x%x \n", __func__, data_R, data_G, data_B);
		bl_tbD1[1] = (char)(data_R*4 >> 8 & 0xFF);
		bl_tbD1[2] = (char)(data_R*4 & 0xFF);
		bl_tbD1[3] = (char)(data_G*4 >> 8 & 0xFF);
		bl_tbD1[4] = (char)(data_G*4 & 0xFF);
		bl_tbD1[5] = (char)(data_B*4 >> 8 & 0xFF);
		bl_tbD1[6] = (char)(data_B*4 & 0xFF);
	}
	for (i = 0; i < sizeof(bl_tbD1); i++) {
		printk("[%s]: bl_tbD1[%d] = 0x%x \n", __func__, i, bl_tbD1[i]);
	}
}
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 end */
/*
static void lcm_pannel_reconfig_blk(struct lcm *ctx)
{
	char bl_tb[] = {0x51,0x07,0xFF};
	unsigned int reg_level = atomic_read(&current_backlight);
	pr_err("[%s][%d]main lcd bl_level:%d \n",__func__,__LINE__,reg_level);

	bl_tb[1] = (char)((reg_level >> 8) & 0xFF);
	bl_tb[2] = (char)(reg_level & 0xFF);
	lcm_dcs_write(ctx, bl_tb, ARRAY_SIZE(bl_tb));
}*/
static void lcm_lhbm_init(struct lcm *ctx)
{
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x04);
	lcm_dcs_write_seq_static(ctx,0xCB,0x66);
	lcm_dcs_write_seq_static(ctx,0xDD,0x05,0x08,0x1F,0x3E,0x9B);
	lcm_dcs_write_seq_static(ctx,0x6F,0x05);
	lcm_dcs_write_seq_static(ctx,0xDD,0x05,0x08,0x1F,0x3E,0x9B);
	lcm_dcs_write_seq_static(ctx,0x6F,0x0A);
	lcm_dcs_write_seq_static(ctx,0xDD,0x05,0x08,0x1F,0x3E,0x9B);
	lcm_dcs_write_seq_static(ctx,0x6F,0x0F);
	lcm_dcs_write_seq_static(ctx,0xDD,0x05,0x08,0x1F,0x3E,0x9B);
	lcm_dcs_write_seq_static(ctx,0x6F,0x14);
	lcm_dcs_write_seq_static(ctx,0xDD,0x05,0x08,0x1F,0x3E,0x9B);
	lcm_dcs_write_seq_static(ctx,0x6F,0x19);
	lcm_dcs_write_seq_static(ctx,0xDD,0x05,0x08,0x1F,0x3E,0x9B);
	lcm_dcs_write_seq_static(ctx,0x6F,0x5A);
	lcm_dcs_write_seq_static(ctx,0xD2,0x10,0x10,0x10,0x10,0x10,0x14);
	lcm_dcs_write_seq_static(ctx,0x6F,0x60);
	lcm_dcs_write_seq_static(ctx,0xD2,0x10,0x10,0x10,0x10,0x10,0x0C);
	lcm_dcs_write_seq_static(ctx,0x6F,0x66);
	lcm_dcs_write_seq_static(ctx,0xD2,0x12,0x12,0x12,0x12,0x12,0x0C);
	lcm_dcs_write_seq_static(ctx,0x6F,0x6C);
	lcm_dcs_write_seq_static(ctx,0xD2,0x12,0x12,0x12,0x12,0x12,0x0A);
	lcm_dcs_write_seq_static(ctx,0x6F,0x72);
	lcm_dcs_write_seq_static(ctx,0xD2,0x12,0x12,0x12,0x12,0x12,0x08);
	lcm_dcs_write_seq_static(ctx,0x6F,0x78);
	lcm_dcs_write_seq_static(ctx,0xD2,0x28,0x28,0x28,0x28,0x28,0x14);
	lcm_dcs_write_seq_static(ctx,0x6F,0x7E);
	lcm_dcs_write_seq_static(ctx,0xD2,0x1A,0x1A,0x1A,0x1A,0x1A,0x10);
	lcm_dcs_write_seq_static(ctx,0x6F,0x84);
	lcm_dcs_write_seq_static(ctx,0xD2,0x1A,0x1A,0x1A,0x1A,0x1A,0x10);
	lcm_dcs_write_seq_static(ctx,0x6F,0x8A);
	lcm_dcs_write_seq_static(ctx,0xD2,0x1A,0x1A,0x1A,0x1A,0x1A,0x10);
	lcm_dcs_write_seq_static(ctx,0x6F,0x90);
	lcm_dcs_write_seq_static(ctx,0xD2,0x1A,0x1A,0x1A,0x1A,0x1A,0x08);
	lcm_dcs_write_seq_static(ctx,0x6F,0x96);
	lcm_dcs_write_seq_static(ctx,0xD2,0x14,0x14,0x14,0x14,0x14,0x10);
	lcm_dcs_write_seq_static(ctx,0x6F,0x9C);
	lcm_dcs_write_seq_static(ctx,0xD2,0x14,0x14,0x14,0x14,0x14,0x08);
	lcm_dcs_write_seq_static(ctx,0x6F,0xA2);
	lcm_dcs_write_seq_static(ctx,0xD2,0x0D,0x0D,0x0D,0x0D,0x0D,0x08);
	lcm_dcs_write_seq_static(ctx,0x6F,0xA8);
	lcm_dcs_write_seq_static(ctx,0xD2,0x0D,0x0D,0x0D,0x0D,0x0D,0x08);
	lcm_dcs_write_seq_static(ctx,0x6F,0xAE);
	lcm_dcs_write_seq_static(ctx,0xD2,0x0B,0x0B,0x0B,0x0B,0x0B,0x08);
}
static void lcm_panel_init(struct lcm *ctx)
{
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
	//--------------- CW8792---------//
	lcm_dcs_write_seq_static(ctx,0x6F,0x06);
	lcm_dcs_write_seq_static(ctx,0xB5,0x7F,0x48,0x29,0x4F,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0x6F,0x0C);
	lcm_dcs_write_seq_static(ctx,0xB5,0x48,0x29,0x50,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0x6F,0x11);
	lcm_dcs_write_seq_static(ctx,0xB5,0x29,0x29,0x29,0x29);
	lcm_dcs_write_seq_static(ctx,0x6F,0x18);
	lcm_dcs_write_seq_static(ctx,0xB5,0x00,0x00,0x00,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0x6F,0x1D);
	lcm_dcs_write_seq_static(ctx,0xB5,0x00,0x00,0x00,0x00,0x00);

	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
	lcm_dcs_write_seq_static(ctx,0xDF,0x09);
	lcm_dcs_write_seq_static(ctx,0x6F,0x01);
	lcm_dcs_write_seq_static(ctx,0xDF,0x40);
	lcm_dcs_write_seq_static(ctx,0x6F,0x31);
	lcm_dcs_write_seq_static(ctx,0xDF,0x00,0x1A);
	lcm_dcs_write_seq_static(ctx,0x6F,0x34);
	lcm_dcs_write_seq_static(ctx,0xDF,0x29);  /* X100S2-467 LHBM DBV */
	lcm_dcs_write_seq_static(ctx,0x6F,0x2D);
	lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x32,0x00,0x04,0x50);
	lcm_dcs_write_seq_static(ctx,0x6F,0x01);
	lcm_dcs_write_seq_static(ctx,0xC7,0x01);
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x01);
	lcm_dcs_write_seq_static(ctx,0x6F,0x2A);
	lcm_dcs_write_seq_static(ctx,0xB9,0x10);
	lcm_dcs_write_seq_static(ctx,0x6F,0x0C);
	lcm_dcs_write_seq_static(ctx,0xB9,0x1A);
	lcm_dcs_write_seq_static(ctx,0x6F,0x05);
	lcm_dcs_write_seq_static(ctx,0xBB,0xA2);
	lcm_dcs_write_seq_static(ctx,0x6F,0x1C);
	lcm_dcs_write_seq_static(ctx,0xBB,0xA2);
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x08);
	lcm_dcs_write_seq_static(ctx,0x6F,0xCE);
	lcm_dcs_write_seq_static(ctx,0xBC,0x01);
	lcm_dcs_write_seq_static(ctx,0x6F,0xCF);
	lcm_dcs_write_seq_static(ctx,0xBC,0x00,0x88,0x00,0xB6,0x01,0x11,0x00,0x44);
	lcm_dcs_write_seq_static(ctx,0xBE,0x03);
	lcm_dcs_write_seq_static(ctx,0x6F,0x0C);
	lcm_dcs_write_seq_static(ctx,0xE9,0x1D,0x36,0x0F,0xD6,0x1C,0x51,0x0F,0xAB);
	lcm_dcs_write_seq_static(ctx,0x88,0x81,0x02,0x61,0x09,0x85);
	/* pri add for X100S2-160 20241028 start */
	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x07);
	//SPR setting
	lcm_dcs_write_seq_static(ctx,0xB0,0x8C,0xC0,0x78,0x70,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xB1,0x1C,0x0C,0x00,0x0C,0x1C,0x00);
	lcm_dcs_write_seq_static(ctx,0xB2,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28);
	lcm_dcs_write_seq_static(ctx,0x6F,0x36);
	lcm_dcs_write_seq_static(ctx,0xB2,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C);
	lcm_dcs_write_seq_static(ctx,0x6F,0x09);
	lcm_dcs_write_seq_static(ctx,0xB2,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28);
	lcm_dcs_write_seq_static(ctx,0x6F,0x48);
	lcm_dcs_write_seq_static(ctx,0xB2,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C);
	lcm_dcs_write_seq_static(ctx,0x6F,0x1B);
	lcm_dcs_write_seq_static(ctx,0xB2,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28);
	lcm_dcs_write_seq_static(ctx,0x6F,0x6C);
	lcm_dcs_write_seq_static(ctx,0xB2,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C);
	lcm_dcs_write_seq_static(ctx,0x6F,0x24);
	lcm_dcs_write_seq_static(ctx,0xB2,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28);
	lcm_dcs_write_seq_static(ctx,0x6F,0x7E);
	lcm_dcs_write_seq_static(ctx,0xB2,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C);
	lcm_dcs_write_seq_static(ctx,0xB3,0x6C);
	lcm_dcs_write_seq_static(ctx,0xB4,0xC0,0x80,0x80,0x80,0x80,0x40,0x80,0x80,0x80,0x80,0x80,0x40,0x80);
	lcm_dcs_write_seq_static(ctx,0xB7,0x00,0x00);
	/* pri add for X100S2-160 20241028 end */

	lcm_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x80);
	lcm_dcs_write_seq_static(ctx,0x6F,0x2A);
	lcm_dcs_write_seq_static(ctx,0xF4,0x08);
	lcm_dcs_write_seq_static(ctx,0x6F,0x46);
	lcm_dcs_write_seq_static(ctx,0xF4,0x07,0x09);
	lcm_dcs_write_seq_static(ctx,0x6F,0x4A);
	lcm_dcs_write_seq_static(ctx,0xF4,0x08,0x0A);
	lcm_dcs_write_seq_static(ctx,0x6F,0x56);
	lcm_dcs_write_seq_static(ctx,0xF4,0x44,0x44);
	lcm_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x81);
	lcm_dcs_write_seq_static(ctx,0x6F,0x3C);
	lcm_dcs_write_seq_static(ctx,0xF5,0x84);
	lcm_dcs_write_seq_static(ctx,0x17,0x03);
	lcm_dcs_write_seq_static(ctx,0x71,0x00);
	lcm_dcs_write_seq_static(ctx,0x8D,0x00,0x00,0x04,0xC3,0x00,0x00,0x0A,0x97);
	lcm_dcs_write_seq_static(ctx,0x2A,0x00,0x00,0x04,0xC3);
	lcm_dcs_write_seq_static(ctx,0x2B,0x00,0x00,0x0A,0x97);
	lcm_dcs_write_seq_static(ctx,0x03,0x00);
	lcm_dcs_write_seq_static(ctx,0x90,0x03,0x43);
	lcm_dcs_write_seq_static(ctx,0x91,0xAB,0x28,0x00,0x0C,0xC2,0x00,0x02,0x32,0x01,0x31,0x00,0x08,0x08,0xBB,0x07,0x7B,0x10,0xF0);
	/* Set0 :120Hz Set1:90Hz Set2:60Hz */
	/* VBP_FSET0_H, VBP_FSET0_L, VFP_FSET0_H, VFP_FSET0_L, VBP_FSET1_H, VBP_FSET1_L,VFP_FSET1_H, VFP_FSET1_L, */
	/* VBP_FSET2_H, VBP_FSET2_L,VFP_FSET2_H, VFP_FSET2_L */
	lcm_dcs_write_seq_static(ctx,0x3B,0x00,0x16,0x00,0x34,0x00,0x16,0x03,0xDC,0x00,0x16,0x0B,0x14);
	lcm_dcs_write_seq_static(ctx,0x6F,0x10);
	/* VBP_FSET0_H, VBP_FSET0_L, VFP_FSET0_H, VFP_FSET0_L */
	lcm_dcs_write_seq_static(ctx,0x3B,0x00,0x16,0x00,0x34);
	lcm_dcs_write_seq_static(ctx,0x35,0x00);
	//lcm_dcs_write_seq_static(ctx,0x51,0x07,0xFF);
	lcm_dcs_write_seq_static(ctx,0x6F,0x04);
	lcm_dcs_write_seq_static(ctx,0x51,0x0F,0xFF);
	lcm_dcs_write_seq_static(ctx,0x53,0x28); /* dimming on */
	lcm_dcs_write_seq_static(ctx,0x57,0x00);
	// 120Hz
	lcm_dcs_write_seq_static(ctx,0x2F,0x00);
	lcm_dcs_write_seq_static(ctx,0x26,0x00);
	lcm_dcs_write_seq_static(ctx,0x5F,0x00,0x40);  /* X100S2-467 LHBM DBV */

	lcm_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
	lcm_dcs_write_seq_static(ctx,0xB2,0x09); /* dimming */
	lcm_dcs_write_seq_static(ctx,0x6F,0x07); /* dimming */
	lcm_dcs_write_seq_static(ctx,0xB2,0x20,0x20); /* dimming 32 frames */

	//backlight
	//lcm_pannel_reconfig_blk(ctx);
	lcm_dcs_write_seq_static(ctx,0x11,0x00);
	mdelay(120);
	lcm_dcs_write_seq_static(ctx,0x29,0x00);
	mdelay(20);

	/* LHBM */
	lcm_lhbm_init(ctx);

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
	udelay(10000);

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
	lcm_aod_status = false;

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
	udelay(10000);

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
	mdelay(5);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(20);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	// end

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		goto error;

	ctx->prepared = true;
	//#ifdef PANEL_SUPPORT_READBACK
		//lcm_panel_get_data(ctx);
	//#endif

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

#define VAC (2712)
#define HAC (1220)
static const struct drm_display_mode switch_mode_90hz = {
	.clock = ((FRAME_WIDTH+MODE_2_HFP+HSA+HBP)*(FRAME_HEIGHT+MODE_2_VFP+VSA+VBP)*(MODE_2_FPS)/1000),
	.hdisplay = FRAME_WIDTH,
	.hsync_start = FRAME_WIDTH + MODE_2_HFP,
	.hsync_end = FRAME_WIDTH + MODE_2_HFP + HSA,
	.htotal = FRAME_WIDTH + MODE_2_HFP + HSA + HBP,
	.vdisplay = FRAME_HEIGHT,
	.vsync_start = FRAME_HEIGHT + MODE_2_VFP,
	.vsync_end = FRAME_HEIGHT + MODE_2_VFP + VSA,
	.vtotal = FRAME_HEIGHT + MODE_2_VFP + VSA + VBP,
};
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

static const struct drm_display_mode switch_mode_60hz = {
	.clock = ((FRAME_WIDTH+MODE_0_HFP+HSA+HBP)*(FRAME_HEIGHT+MODE_0_VFP+VSA+VBP)*(MODE_0_FPS)/1000),
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
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2, {0x2F, 0x02}},
	},
	.pll_clk = DATA_RATE / 2,
	.vdo_per_frame_lp_enable = 0,
	.ssc_enable = 0,
	/* pri modify by zhanghuimin for X100S2-124 start */
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	//.lcm_esd_check_table[0] = {
	//	.cmd = 0x0a,
	//	.count = 1,
	//	.para_list[0] = 0x9c,
	//},
	//.lcm_esd_check_table[1] = {
	//	.cmd = 0xab,
	//	.count = 1,
	//	.para_list[0] = 0x00,
	//},
	/* pri modify by zhanghuimin for X100S2-124 end */
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lcm_index = 0,
	.wait_before_hbm = true,
	.dsc_param_load_mode = 2,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	/* pri hbm added by xuejian 20240410 begin*/
	.hbm_en_time = 0,
	.hbm_dis_time = 0,
	/* pri hbm added by xuejian 20240410 end*/
	.dsc_params = {
		.enable                =  DSC_ENABLE,
		.ver                   =  DSC_VER,
		.slice_mode            =  DSC_SLICE_MODE,
		.rgb_swap              =  DSC_RGB_SWAP,
		.dsc_cfg               =  DSC_DSC_CFG,
		.rct_on                =  DSC_RCT_ON,
		.bit_per_channel       =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth    =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable             =  DSC_BP_ENABLE,
		.bit_per_pixel         =  DSC_BIT_PER_PIXEL,
		.pic_height            =  FRAME_HEIGHT,
		.pic_width             =  FRAME_WIDTH,
		.slice_height          =  DSC_SLICE_HEIGHT,
		.slice_width           =  DSC_SLICE_WIDTH,
		.chunk_size            =  DSC_CHUNK_SIZE,
		.xmit_delay            =  DSC_XMIT_DELAY,
		.dec_delay             =  DSC_DEC_DELAY,
		.scale_value           =  DSC_SCALE_VALUE,
		.increment_interval    =  DSC_INCREMENT_INTERVAL,
		.decrement_interval    =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset       =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset        =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset      =  DSC_SLICE_BPG_OFFSET,
		.initial_offset        =  DSC_INITIAL_OFFSET,
		.final_offset          =  DSC_FINAL_OFFSET,
		.flatness_minqp        =  DSC_FLATNESS_MINQP,
		.flatness_maxqp        =  DSC_FLATNESS_MAXQP,
		.rc_model_size         =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor        =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi      =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo      =  DSC_RC_TGT_OFFSET_LO,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = nt37706_vdo_fhd_buf_thresh,
			.range_min_qp = nt37706_vdo_fhd_range_min_qp,
			.range_max_qp = nt37706_vdo_fhd_range_max_qp,
			.range_bpg_ofs = nt37706_vdo_fhd_range_bpg_ofs,
		},
	},
};

static struct mtk_panel_params ext_params_120hz = {
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2, {0x2F, 0x00}},
	},
	.pll_clk = DATA_RATE / 2,
	.vdo_per_frame_lp_enable = 0,
	.ssc_enable = 0,

	/* pri modify by zhanghuimin for X100S2-124 start */
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	//.lcm_esd_check_table[0] = {
	//	.cmd = 0x0a,
	//	.count = 1,
	//	.para_list[0] = 0x9c,
	//},
	//.lcm_esd_check_table[1] = {
	//	.cmd = 0xab,
	//	.count = 1,
	//	.para_list[0] = 0x00,
	//},
	/* pri modify by zhanghuimin for X100S2-124 end */

	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lcm_index = 0,
	.wait_before_hbm = true,
	.dsc_param_load_mode = 2,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	/* pri hbm added by xuejian 20240410 begin*/
	.hbm_en_time = 0,
	.hbm_dis_time = 0,
	/* pri hbm added by xuejian 20240410 end*/
	//.lfr_enable = 1,
	//.lfr_minimum_fps = 60,
	.dsc_params = {
		.enable                =  DSC_ENABLE,
		.ver                   =  DSC_VER,
		.slice_mode            =  DSC_SLICE_MODE,
		.rgb_swap              =  DSC_RGB_SWAP,
		.dsc_cfg               =  DSC_DSC_CFG,
		.rct_on                =  DSC_RCT_ON,
		.bit_per_channel       =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth    =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable             =  DSC_BP_ENABLE,
		.bit_per_pixel         =  DSC_BIT_PER_PIXEL,
		.pic_height            =  FRAME_HEIGHT,
		.pic_width             =  FRAME_WIDTH,
		.slice_height          =  DSC_SLICE_HEIGHT,
		.slice_width           =  DSC_SLICE_WIDTH,
		.chunk_size            =  DSC_CHUNK_SIZE,
		.xmit_delay            =  DSC_XMIT_DELAY,
		.dec_delay             =  DSC_DEC_DELAY,
		.scale_value           =  DSC_SCALE_VALUE,
		.increment_interval    =  DSC_INCREMENT_INTERVAL,
		.decrement_interval    =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset       =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset        =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset      =  DSC_SLICE_BPG_OFFSET,
		.initial_offset        =  DSC_INITIAL_OFFSET,
		.final_offset          =  DSC_FINAL_OFFSET,
		.flatness_minqp        =  DSC_FLATNESS_MINQP,
		.flatness_maxqp        =  DSC_FLATNESS_MAXQP,
		.rc_model_size         =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor        =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi      =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo      =  DSC_RC_TGT_OFFSET_LO,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = nt37706_vdo_fhd_buf_thresh,
			.range_min_qp = nt37706_vdo_fhd_range_min_qp,
			.range_max_qp = nt37706_vdo_fhd_range_max_qp,
			.range_bpg_ofs = nt37706_vdo_fhd_range_bpg_ofs,
		},
	},
};
static struct mtk_panel_params ext_params_90hz = {
	.change_fps_by_vfp_send_cmd = 1,
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2, {0x2F, 0x01}},
	},
	.pll_clk = DATA_RATE / 2,
	.vdo_per_frame_lp_enable = 0,
	.ssc_enable = 0,

	/* pri modify by zhanghuimin for X100S2-124 start */
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	//.lcm_esd_check_table[0] = {
	//	.cmd = 0x0a,
	//	.count = 1,
	//	.para_list[0] = 0x9c,
	//},
	//.lcm_esd_check_table[1] = {
	//	.cmd = 0xab,
	//	.count = 1,
	//	.para_list[0] = 0x00,
	//},
	/* pri modify by zhanghuimin for X100S2-124 end */
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.lcm_index = 0,
	.wait_before_hbm = true,
	.dsc_param_load_mode = 2,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	/* pri hbm added by xuejian 20240410 begin*/
	.hbm_en_time = 0,
	.hbm_dis_time = 0,
	/* pri hbm added by xuejian 20240410 end*/
	//.lfr_enable = 1,
	//.lfr_minimum_fps = 60,

	.dsc_params = {
		.enable = DSC_ENABLE,
		.ver                   =  DSC_VER,
		.slice_mode            =  DSC_SLICE_MODE,
		.rgb_swap              =  DSC_RGB_SWAP,
		.dsc_cfg               =  DSC_DSC_CFG,
		.rct_on                =  DSC_RCT_ON,
		.bit_per_channel       =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth    =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable             =  DSC_BP_ENABLE,
		.bit_per_pixel         =  DSC_BIT_PER_PIXEL,
		.pic_height            =  FRAME_HEIGHT,
		.pic_width             =  FRAME_WIDTH,
		.slice_height          =  DSC_SLICE_HEIGHT,
		.slice_width           =  DSC_SLICE_WIDTH,
		.chunk_size            =  DSC_CHUNK_SIZE,
		.xmit_delay            =  DSC_XMIT_DELAY,
		.dec_delay             =  DSC_DEC_DELAY,
		.scale_value           =  DSC_SCALE_VALUE,
		.increment_interval    =  DSC_INCREMENT_INTERVAL,
		.decrement_interval    =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset       =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset        =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset      =  DSC_SLICE_BPG_OFFSET,
		.initial_offset        =  DSC_INITIAL_OFFSET,
		.final_offset          =  DSC_FINAL_OFFSET,
		.flatness_minqp        =  DSC_FLATNESS_MINQP,
		.flatness_maxqp        =  DSC_FLATNESS_MAXQP,
		.rc_model_size         =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor        =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi      =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo      =  DSC_RC_TGT_OFFSET_LO,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = nt37706_vdo_fhd_buf_thresh,
			.range_min_qp = nt37706_vdo_fhd_range_min_qp,
			.range_max_qp = nt37706_vdo_fhd_range_max_qp,
			.range_bpg_ofs = nt37706_vdo_fhd_range_bpg_ofs,
		},
	},
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

	ret = mipi_dsi_dcs_read(dsi, 0xa7, data, 1);
	if (ret < 0) {
		pr_err("%s error\n", __func__);
		return 0;
	}
	pr_info("[panel][%s]ATA read data %x \n", __func__, data[0]);

	if (data[0] == 0x06)
		return 1;

	return 0;
	/*pri add ata test by xuejian end 20240517*/
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	char bl_tb0[] = { 0x51, 0x0F, 0xFF};
	unsigned int reg_level = 125;
	//atomic_set(&current_backlight, level);



	if (level && level <= 2047) {
		reg_level = level;
	} else if (level > 2047) {
		reg_level = 4095;
	} else {
		reg_level = 0;
	}
	atomic_set(&current_backlight, reg_level);
	pr_info("[%s]level: %d\n",__func__, reg_level);

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
	//pr_err("[panel][%s]: name: %s, level : %d",__func__, name, trans_level);
	return trans_level;
}
EXPORT_SYMBOL(led_level_disp_get);
/* pri LAX10-347 added by xiaweigogn 20240407 end */

/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 start */
static void lcm_enter_lhbm(void *dsi, dcs_write_gce cb, void *handle)
{
	unsigned int level_normal = 0;
	char bl_tb0[]  = {0xF0,0x55,0xAA,0x52,0x08,0x07};
	char bl_tb1[]  = {0xC0,0x87};
	char bl_tb2[] = {0xF0,0x55,0xAA,0x52,0x08,0x02};
	char bl_tb3[] = {0xBF,0x0B};
	//char bl_tbD1[] = {0xD1,0x28,0x8C,0x26,0x18,0x2f,0xc8};
	level_normal = atomic_read(&current_backlight);
	if (level_normal > 2047) {
		level_normal = 2047;
	}

	printk("lcm_enter_lhbm Enter! bl_lhbm_enter[%d]:0x%x,0x%x\n", level_normal,bl_lhbm_enter[level_normal][13], bl_lhbm_enter[level_normal][14]);

    cb(dsi, handle, bl_tb0 ,ARRAY_SIZE(bl_tb0 ));
	cb(dsi, handle, bl_tb1 ,ARRAY_SIZE(bl_tb1 ));
	cb(dsi, handle, bl_tb2,ARRAY_SIZE(bl_tb2));
	cb(dsi, handle, bl_tb3,ARRAY_SIZE(bl_tb3));
	cb(dsi, handle, bl_tbD1,ARRAY_SIZE(bl_tbD1));
	cb(dsi, handle, bl_lhbm_enter[level_normal],ARRAY_SIZE(bl_lhbm_enter[level_normal]));

	printk("lcm_enter_lhbm End!\n");
}
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 end */

static void lcm_exit_lhbm(void *dsi, dcs_write_gce cb, void *handle)
{
	char bl_tb0[]  = {0x8B,0x00};
	char bl_tb1[]  = {0xF0,0x55,0xAA,0x52,0x08,0x08};
	char bl_tb2[]  = {0xB6,0x08,0x0B,0x08,0x0B,0x09,0x13,0x09,0x13,0x09,0x76,0x09,0x76,0x09,0xB8,0x09,0xB8};
	char bl_tb3[]  = {0x6F,0x10};
	char bl_tb4[]  = {0xB6,0x0A,0x23,0x0A,0x23,0x0A,0x44,0x0A,0x44,0x08,0xF2,0x08,0xF2,0x0A,0x65,0x0A,0x65};
	char bl_tb5[]  = {0x6F,0x20};
	char bl_tb6[]  = {0xB6,0x0B,0x8D,0x0B,0x8D,0x0C,0x32,0x0C,0x32,0x0F,0xFF,0x0F,0xFF,0x0F,0xFF};
	char bl_tb7[]  = {0x6F,0x55};
	char bl_tb8[]  = {0xB8,0x1F,0xD0,0x1F,0xD0,0x1C,0x33,0x1C,0x33,0x1B,0x0C,0x1B};
	char bl_tb9[]  = {0x6F,0x60};
	char bl_tb10[] = {0xB8,0x0C,0x1A,0x54,0x1A,0x54,0x19,0x3E,0x19,0x3E,0x18,0xED,0x18,0xED,0x1C,0x9B,0x1C};
	char bl_tb11[] = {0x6F,0x70};
	char bl_tb12[] = {0xB8,0x9B,0x18,0x9E,0x18,0x9E,0x16,0x27,0x16,0x27,0x14,0xFC,0x14,0xFC,0x10,0x00,0x10};
	char bl_tb13[] = {0x6F,0x80};
	char bl_tb14[] = {0xB8,0x00,0x10,0x00};
	char bl_tb15[] = {0xA9,0x02,0x00,0xB5,0x2C,0x2C,0x03,0x01,0x00,0x87,0x00,0x00,0x20};
	printk("lcm_exit_lhbm Enter!\n");
	cb(dsi, handle, bl_tb0 ,ARRAY_SIZE(bl_tb0 ));
	cb(dsi, handle, bl_tb1 ,ARRAY_SIZE(bl_tb1 ));
	cb(dsi, handle, bl_tb2 ,ARRAY_SIZE(bl_tb2 ));
	cb(dsi, handle, bl_tb3 ,ARRAY_SIZE(bl_tb3 ));
	cb(dsi, handle, bl_tb4 ,ARRAY_SIZE(bl_tb4 ));
	cb(dsi, handle, bl_tb5 ,ARRAY_SIZE(bl_tb5 ));
	cb(dsi, handle, bl_tb6 ,ARRAY_SIZE(bl_tb6 ));
	cb(dsi, handle, bl_tb7 ,ARRAY_SIZE(bl_tb7 ));
	cb(dsi, handle, bl_tb8 ,ARRAY_SIZE(bl_tb8 ));
	cb(dsi, handle, bl_tb9 ,ARRAY_SIZE(bl_tb9 ));
	cb(dsi, handle, bl_tb10,ARRAY_SIZE(bl_tb10));
	cb(dsi, handle, bl_tb11,ARRAY_SIZE(bl_tb11));
	cb(dsi, handle, bl_tb12,ARRAY_SIZE(bl_tb12));
	cb(dsi, handle, bl_tb13,ARRAY_SIZE(bl_tb13));
	cb(dsi, handle, bl_tb14,ARRAY_SIZE(bl_tb14));
	cb(dsi, handle, bl_tb15,ARRAY_SIZE(bl_tb15));
	printk("lcm_exit_lhbm Exit!\n");
}

static int panel_hbm_set_cmdq(struct drm_panel *panel, void *dsi,
			      dcs_write_gce cb, void *handle, bool en)
{
	unsigned int level_normal = atomic_read(&current_backlight);
	struct lcm *ctx = panel_to_lcm(panel);

	if (!cb)
		return -1;

	if (ctx->hbm_en == en)
		goto done;

	if (en)	{
		printk("[panel] %s : set HBM, lcm_aod_status=%d level_normal:%d\n", __func__, lcm_aod_status, level_normal);
		g_ctx->hbm_mode = true;
		//exit doze
		if (lcm_aod_status) {
			//panel_doze_disable(panel, dsi, cb, handle);
		}
		lcm_enter_lhbm(dsi, cb, handle);

	} else	{
		printk("[panel] %s : set normal = %d, lcm_aod_status=%d \n",
			__func__,level_normal, lcm_aod_status);
		g_ctx->hbm_mode = false;

		lcm_exit_lhbm(dsi, cb, handle);
		printk("[panel] %s : out hbm\n",__func__);

		//enter doze
		if (lcm_aod_status) {
			//lcm_hbm_doze_change_state(dsi, cb, handle, 1);
		}

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
	struct lcm *ctx = panel_to_lcm(panel);
	struct drm_display_mode *m = get_mode_by_id(connector, mode);
	printk("[panel] %s : drm_mode_vrefresh(m) = %d, mode :%d\n", __func__, drm_mode_vrefresh(m), mode);

	if (drm_mode_vrefresh(m) == MODE_0_FPS) {
		ctx->current_fps = 60;
		ext->params = &ext_params_60hz;
	} else if (drm_mode_vrefresh(m) == MODE_1_FPS) {
		ctx->current_fps = 120;
		ext->params = &ext_params_120hz;
	} else if (drm_mode_vrefresh(m) == MODE_2_FPS) {
		ctx->current_fps = 90;
		ext->params = &ext_params_90hz;
	} else
		ret = 1;
	return ret;
}

/*pri add aod mode 20240411 start*/
static int panel_doze_enable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	pr_info("panel %s\n", __func__);
	lcm_aod_status = true;

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_DOZE_ENABLE;
	cs_panel_notifier_call_chain(CS_PANEL_EVENT_BLANK,&lcd_tp_event);
#endif
	return 0;
}

static int panel_doze_disable(struct drm_panel *panel,
	void *dsi, dcs_write_gce cb, void *handle)
{
	struct lcm *ctx = panel_to_lcm(panel);
	lcm_aod_status = false;
#if IS_ENABLED(CONFIG_CS_NOTIFIER)
	lcd_tp_event.blank  = PANEL_BLANK_DOZE_DISABLE;
	cs_panel_notifier_call_chain(CS_PANEL_EVENT_BLANK,&lcd_tp_event);
#endif
	pr_info("[%s]get_current_fps:%d\n",__func__, ctx->current_fps);
	/* Prize modify for Aod Mode X100S2-177 end */
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

	mode = drm_mode_duplicate(connector->dev, &switch_mode_60hz);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			switch_mode_60hz.hdisplay, switch_mode_60hz.vdisplay,
			drm_mode_vrefresh(&switch_mode_60hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode);

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

	mode_1 = drm_mode_duplicate(connector->dev, &switch_mode_120hz);
	if (!mode_1) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 switch_mode_120hz.hdisplay, switch_mode_120hz.vdisplay,
			 drm_mode_vrefresh(&switch_mode_120hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode_1);
	mode_1->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode_1);
	connector->display_info.width_mm = 69;
	connector->display_info.height_mm = 154;

	return 3;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

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
		lcm_set_hbm_backlight(1, 0, 0);
	} else {
		val1 = (u8)(state>>8)&0xf;
		val2 = (u8)(state)&0xff;
		lcm_set_hbm_backlight(2, val1, val2);
	}

err:
	return count;
}

/* pri added by xuejian 20240624 begin */
static ssize_t dimming_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
	int ret;
	unsigned int cmd;

	ret = kstrtouint(buf, 10, &cmd);
	if (ret < 0) {
		goto err;
	}
	pr_info("[%s]  cmd = %d\n", __func__, cmd);

	if (cmd == 1) {
		lcm_set_register(0x53, 0x28);
	} else {
		lcm_set_register(0x53, 0x20);
	}

err:
	return count;
}

static struct kobj_attribute hbm_backlight_attr = __ATTR(hbm_backlight, 0664, hbm_backlight_show, hbm_backlight_store);
static struct kobj_attribute dimming_attr = __ATTR(dimming, 0664, NULL, dimming_store);

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
	ret = sysfs_create_file(kobj, &dimming_attr.attr);
	if (ret < 0) {
		printk("[%s] sysfs_create_group failed\n",__func__);
		return -1;
	}

	printk("[%s] is OK!!!\n", __func__);
	return 0;
}
/* pri added by xuejian 20240625 end */
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 start */
static void lhbm_read_reg_work(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct lcm *ctx;
	printk("%s enter \n", __func__);
	delayed_work = container_of(work, struct delayed_work, work);
	ctx = container_of(delayed_work, struct lcm, reflash_work);
	lcm_get_lhbm_info(ctx);
}
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 end */

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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
		| MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_CLOCK_NON_CONTINUOUS;

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
	ret = mtk_panel_ext_create(dev, &ext_params_120hz, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;

#endif
/* pri LAX10-192 added by xuejian 20240409 begin*/
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
    strcpy(current_lcm_info.chip,"nt37706");
    strcpy(current_lcm_info.vendor,"Novatek");
    sprintf(current_lcm_info.id,"0x06");
    strcpy(current_lcm_info.more,"1220*2712");
#endif
/* pri LAX10-192 added by xuejian 20240409 end*/
	ctx->hbm_mode = 0;

	ctx->current_fps = 120;

	/* pri added by xuejian 20240415 begin */
	sys_node_init();
	/* pri added by xuejian 20240415 end */
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 start */
	ctx->reflash_workqueue =
			create_singlethread_workqueue("lhbm_read");
	if (!ctx->reflash_workqueue) {
		printk("%s :create_singlethread_workqueue fail\n", __func__);
	}
	INIT_DELAYED_WORK(&ctx->reflash_work, lhbm_read_reg_work);
	queue_delayed_work(ctx->reflash_workqueue, &ctx->reflash_work,
			msecs_to_jiffies(2000));
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 end */

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
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 start */
	cancel_delayed_work_sync(&ctx->reflash_work);
	flush_workqueue(ctx->reflash_workqueue);
	destroy_workqueue(ctx->reflash_workqueue);
/* pri add for X100S2-467 LHBM by zhanghuimin 20241021 end */
}

static const struct of_device_id lcm_of_match[] = {
	{
		.compatible = "hx,nt37706,vdo",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-hx-nt37706-vdo-120hz",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("nt37706 rayle CMD Panel Driver");
MODULE_LICENSE("GPL");
