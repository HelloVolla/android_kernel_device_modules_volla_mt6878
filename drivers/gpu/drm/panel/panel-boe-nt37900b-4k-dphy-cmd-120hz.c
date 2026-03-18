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
extern struct hardware_info current_lcm_info;
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

static char GIR_ON[] = {0x5F,0x00,0x00};
static char GIR_OFF[] = {0x5F,0x02,0x00};	
static char bl_tb[] = {0x51,0x0D,0xBC};
static char aod_bl_tb[] = {0x51,0x00,0x1E};
/* static char hbm_tb[] = {0x51,0x0F,0xFF}; */

static char A9_1105_tb[] = { \
0xA9,\
0x02,0x04,0xCB,0x01,0x05,0x0E,0x1C,0x38,0x70,0xDF,\
0x02,0x04,0xCB,0x06,0x07,0x00,0x01,\
0x02,0x04,0xCB,0x08,0x0E,0xD0,0x23,0x20,0x25,0x44,0x50,0x51,\
0x02,0x04,0xEC,0x49,0x49,0x80,\
0x02,0x00,0xC7,0x01,0x01,0x30,\
0x02,0x00,0xC0,0x31,0x33,0x06,0x80,0x00,\
0x02,0x00,0xC0,0x36,0x37,0x32,0x68,\
0x02,0x04,0xEB,0x1A,0x1C,0x00,0x01,0xD0,\
0x02,0x04,0xEB,0x1D,0x1F,0x23,0x20,0x25,\
0x02,0x04,0xEB,0x20,0x22,0x44,0x50,0x51,\
0x02,0x04,0xEB,0x13,0x17,0x32,0x20,0x03,0x02,0x02,\
0x02,0x04,0xEB,0x18,0x18,0x02,\
0x02,0x04,0xEB,0x01,0x06,0x32,0x20,0x03,0x02,0x02,0x02,\
0x02,0x04,0xEB,0x07,0x07,0xB4,\
0x02,0x04,0xEB,0x08,0x0C,0x50,0x0F,0x0E,0x0E,0x0E,\
0x02,0x04,0xEB,0x25,0x27,0xDF,0x6E,0x51,\
0x02,0x04,0xEB,0x28,0x2A,0x50,0x3D,0x3C,\
0x02,0x04,0xEB,0x0D,0x0F,0xDF,0x6E,0x51,\
0x02,0x04,0xEB,0x10,0x12,0x50,0x3D,0x3C,\
0x02,0x04,0xEB,0x24,0x24,0x04,\
0x02,0x04,0xEC,0x05,0x05,0x00
};

static char A9_4095_tb[] = { \
0xA9,\
0x02,0x04,0xCB,0x01,0x05,0x0F,0x1E,0x3C,0x78,0xEF,\
0x02,0x04,0xCB,0x06,0x07,0x47,0x52,\
0x02,0x04,0xCB,0x08,0x0E,0x88,0xBD,0xCD,0xBB,0xDF,0xBC,0x0D,\
0x02,0x04,0xEC,0x49,0x49,0x80,\
0x02,0x00,0xC7,0x01,0x01,0x50,\
0x02,0x00,0xC0,0x31,0x33,0x15,0x50,0x00,\
0x02,0x00,0xC0,0x36,0x37,0x54,0x55,\
0x02,0x04,0xEB,0x1A,0x1C,0x47,0x52,0x88,\
0x02,0x04,0xEB,0x1D,0x1F,0xBD,0xCD,0xBB,\
0x02,0x04,0xEB,0x20,0x22,0xDF,0xBC,0x0D,\
0x02,0x04,0xEB,0x13,0x17,0x02,0x00,0x00,0x00,0x00,\
0x02,0x04,0xEB,0x18,0x18,0x00,\
0x02,0x04,0xEB,0x01,0x06,0x02,0x00,0x00,0x00,0x00,0x00,\
0x02,0x04,0xEB,0x07,0x07,0x0E,\
0x02,0x04,0xEB,0x08,0x0C,0x0A,0x09,0x0A,0x08,0x06,\
0x02,0x04,0xEB,0x25,0x27,0x78,0x58,0x3E,\
0x02,0x04,0xEB,0x28,0x2A,0x3C,0x3C,0x33,\
0x02,0x04,0xEB,0x0D,0x0F,0x78,0x58,0x3E,\
0x02,0x04,0xEB,0x10,0x12,0x3C,0x3C,0x33,\
0x02,0x04,0xEB,0x24,0x24,0x00,\
0x02,0x04,0xEC,0x05,0x05,0x00
};

static struct LCM_setting_table lcm_normal_to_aod_tb[] = {
    {REGFLAG_CMD, 6, {0xF0,0x55,0xAA,0x52,0x08,0x00} },
    {REGFLAG_CMD, 7, {0xC8,0x0D,0x0D,0x0D,0x0D,0x08,0x08} },
    {REGFLAG_CMD, 2, {0x6F,0x0B} },
    {REGFLAG_CMD, 9, {0xC8,0x0C,0x0C,0x04,0x04,0x0C,0x0C,0x08,0x08} },
    {REGFLAG_CMD, 6, {0xF0,0x55,0xAA,0x52,0x08,0x01} },
    {REGFLAG_CMD, 2, {0x6F,0x03} },
    {REGFLAG_CMD, 2, {0xD9,0x40} },
    {REGFLAG_CMD, 2, {0x6F,0x23} },
    {REGFLAG_CMD, 4, {0xD9,0xC1,0xC9,0x12} },
    {REGFLAG_CMD, 5, {0xFF,0xAA,0x55,0xA5,0x80} },
    {REGFLAG_CMD, 2, {0x6F,0x29} },
    {REGFLAG_CMD, 3, {0xF8,0x0E,0x36} },
    {REGFLAG_CMD, 2, {0x6F,0x0D} },
    {REGFLAG_CMD, 3, {0xF8,0x01,0x7F} },
    {REGFLAG_CMD, 5, {0xFF,0xAA,0x55,0xA5,0x81} },
    {REGFLAG_CMD, 2, {0x6F,0x02} },
    {REGFLAG_CMD, 2, {0xF9,0x04} },
    {REGFLAG_CMD, 2, {0x6F,0x0E} },
    {REGFLAG_CMD, 2, {0xF5,0x2B} },
    {REGFLAG_CMD, 6, {0xF0,0x55,0xAA,0x52,0x08,0x03} },
    {REGFLAG_CMD, 4, {0xC8,0x01,0xFF,0xFB} },
    {REGFLAG_CMD, 5, {0xFF,0xAA,0x55,0xA5,0x80} },
    {REGFLAG_CMD, 2, {0x6F,0x22} },
    {REGFLAG_CMD, 2, {0xFE,0x40} },
    {REGFLAG_CMD, 5, {0xFF,0xAA,0x55,0xA5,0x82} },
    {REGFLAG_CMD, 2, {0x6F,0x11} },
    {REGFLAG_CMD, 2, {0xF8,0x0F} },
    {REGFLAG_CMD, 6, {0xF0,0x55,0xAA,0x52,0x00,0x00} },
    {REGFLAG_CMD, 5, {0xFF,0xAA,0x55,0xA5,0x00} },
    {REGFLAG_CMD, 2, {0x6F,0x06} },
    {REGFLAG_CMD, 2, {0x6D,0x48} },
    {REGFLAG_CMD, 1, {0x35} },
    {REGFLAG_CMD, 2, {0x53,0x20} },
    {REGFLAG_CMD, 3, {0x5F,0x00,0x00} },
    {REGFLAG_CMD, 2, {0xA2,0x01} },
    {REGFLAG_CMD, 5, {0x2A,0x00,0x00,0x09,0xAF} },
    {REGFLAG_CMD, 5, {0x2B,0x00,0x00,0x08,0x97} },
    {REGFLAG_CMD, 3, {0x90,0x03,0x43} },
    {REGFLAG_CMD, 19, {0x91,0x89,0x28,0x00,0x14,0xC2,0x00,0x03,0x6D,0x02,0xA7,0x00,0x11,0x05,0x0E,0x02,0x2F,0x10,0xD0} },
    {REGFLAG_CMD, 2, {0x2F,0x02} },
    {REGFLAG_CMD, 1, {0x11} },
    {REGFLAG_DELAY, 120, {} },
    {REGFLAG_CMD, 6, {0xF0,0x55,0xAA,0x52,0x08,0x01} },
    {REGFLAG_CMD, 2, {0x6F,0xE4} },
    {REGFLAG_CMD, 9, {0xE5,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00} },
    {REGFLAG_CMD, 1, {0x29} },
    {REGFLAG_DELAY, 50, {} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static struct LCM_setting_table lcm_aod_to_normal_tb[] = {
    {REGFLAG_CMD, 6, {0xF0,0x55,0xAA,0x52,0x08,0x01} },
    {REGFLAG_CMD, 2, {0x6F,0xE4} },
    {REGFLAG_CMD, 9, {0xE5,0x7F,0x7F,0x20,0x20,0x20,0x20,0x20,0x20} },
    {REGFLAG_CMD, 2, {0x2F,0x01} },
    {REGFLAG_DELAY, 1, {} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

enum panel_version{
	PANEL_V1 = 1,
	PANEL_V2,
	PANEL_V3,
};

/*LCM_DEGREE default value*/
#define PROBE_FROM_DTS 270
struct boe_lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	bool prepared;
	bool enabled;
	unsigned int lcm_degree;
	int error;
    bool doze_en;
    bool out_doze_set_backlight;
    unsigned int out_doze_set_backlight_level;
    unsigned int current_fps;
    enum panel_version version;
};

struct boe_lcm *g_ctx;

#define boe_dcs_write_seq(ctx, seq...)                                         \
	({                                                                     \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		boe_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

#define boe_dcs_write_seq_static(ctx, seq...)                                  \
	({                                                                     \
		static const u8 d[] = { seq };                                 \
		boe_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

static inline struct boe_lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct boe_lcm, panel);
}

#ifdef PANEL_SUPPORT_READBACK
static int boe_dcs_read(struct boe_lcm *ctx, u8 cmd, void *data, size_t len)
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

static void boe_panel_get_data(struct boe_lcm *ctx)
{
	u8 buffer[3] = { 0 };
	static int ret;

	pr_info("%s+\n", __func__);

	ret = boe_dcs_read(ctx, 0x0A, buffer, 1);
	pr_info("%s  0x%08x\n", __func__, buffer[0] | (buffer[1] << 8));
	dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
		ret, buffer[0] | (buffer[1] << 8));
}
#endif

static void boe_dcs_write(struct boe_lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;

	if (len > 1)
		udelay(20);

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
static void boe_setbacklight_A9_1105_cmdq(struct boe_lcm *ctx)
{
	pr_info("%s+\n", __func__);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x04);
    boe_dcs_write_seq_static(ctx,0x6F,0x01);
    boe_dcs_write_seq_static(ctx,0xCB,0x0E,0x1C,0x38,0x70,0xDF);
    boe_dcs_write_seq_static(ctx,0x6F,0x06);
    boe_dcs_write_seq_static(ctx,0xCB,0x00,0x01,0xD0,0x23,0x20,0x25,0x44,0x50,0x51);
    boe_dcs_write_seq_static(ctx,0x6F,0x49);
    boe_dcs_write_seq_static(ctx,0xEC,0x80);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x01);
    boe_dcs_write_seq_static(ctx,0xC7,0x30);
    boe_dcs_write_seq_static(ctx,0x6F,0x31);
    boe_dcs_write_seq_static(ctx,0xC0,0x06,0x80,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x36);
    boe_dcs_write_seq_static(ctx,0xC0,0x32,0x68);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x04);
    boe_dcs_write_seq_static(ctx,0x6F,0x1A);
    boe_dcs_write_seq_static(ctx,0xEB,0x00,0x01,0xD0);
    boe_dcs_write_seq_static(ctx,0x6F,0x1D);
    boe_dcs_write_seq_static(ctx,0xEB,0x23,0x20,0x25);
    boe_dcs_write_seq_static(ctx,0x6F,0x20);
    boe_dcs_write_seq_static(ctx,0xEB,0x44,0x50,0x51);
    boe_dcs_write_seq_static(ctx,0x6F,0x13);
    boe_dcs_write_seq_static(ctx,0xEB,0x32,0x20,0x03,0x02,0x02,0x02);
    boe_dcs_write_seq_static(ctx,0x6F,0x01);
    boe_dcs_write_seq_static(ctx,0xEB,0x32,0x20,0x03,0x02,0x02,0x02);
    boe_dcs_write_seq_static(ctx,0x6F,0x07);
    boe_dcs_write_seq_static(ctx,0xEB,0xB4,0x50,0x0F,0x0E,0x0E,0x0E);
    boe_dcs_write_seq_static(ctx,0x6F,0x25);
    boe_dcs_write_seq_static(ctx,0xEB,0xDF,0x6E,0x51,0x50,0x3D,0x3C);
    boe_dcs_write_seq_static(ctx,0x6F,0x0D);
    boe_dcs_write_seq_static(ctx,0xEB,0xDF,0x6E,0x51,0x50,0x3D,0x3C);
    boe_dcs_write_seq_static(ctx,0x6F,0x24);
    boe_dcs_write_seq_static(ctx,0xEB,0x04);
    boe_dcs_write_seq_static(ctx,0x6F,0x05);
    boe_dcs_write_seq_static(ctx,0xEC,0x00);
	pr_info("%s-\n", __func__);
}

static void boe_setbacklight_A9_4095_cmdq(struct boe_lcm *ctx)
{
	pr_info("%s+\n", __func__);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x04);
    boe_dcs_write_seq_static(ctx,0x6F,0x01);
    boe_dcs_write_seq_static(ctx,0xCB,0x0F,0x1E,0x3C,0x78,0xEF);
    boe_dcs_write_seq_static(ctx,0x6F,0x06);
    boe_dcs_write_seq_static(ctx,0xCB,0x47,0x52,0x88,0xBD,0xCD,0xBB,0xDF,0xBC,0x0D);
    boe_dcs_write_seq_static(ctx,0x6F,0x49);
    boe_dcs_write_seq_static(ctx,0xEC,0x80);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x01);
    boe_dcs_write_seq_static(ctx,0xC7,0x50);
    boe_dcs_write_seq_static(ctx,0x6F,0x31);
    boe_dcs_write_seq_static(ctx,0xC0,0x15,0x50,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x36);
    boe_dcs_write_seq_static(ctx,0xC0,0x54,0x55);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x04);
    boe_dcs_write_seq_static(ctx,0x6F,0x1A);
    boe_dcs_write_seq_static(ctx,0xEB,0x47,0x52,0x88);
    boe_dcs_write_seq_static(ctx,0x6F,0x1D);
    boe_dcs_write_seq_static(ctx,0xEB,0xBD,0xCD,0xBB);
    boe_dcs_write_seq_static(ctx,0x6F,0x20);
    boe_dcs_write_seq_static(ctx,0xEB,0xDF,0xBC,0x0D);
    boe_dcs_write_seq_static(ctx,0x6F,0x13);
    boe_dcs_write_seq_static(ctx,0xEB,0x02,0x00,0x00,0x00,0x00,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x01);
    boe_dcs_write_seq_static(ctx,0xEB,0x02,0x00,0x00,0x00,0x00,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x07);
    boe_dcs_write_seq_static(ctx,0xEB,0x0E,0x0A,0x09,0x0A,0x08,0x06);
    boe_dcs_write_seq_static(ctx,0x6F,0x25);
    boe_dcs_write_seq_static(ctx,0xEB,0x78,0x58,0x3E,0x3C,0x3C,0x33);
    boe_dcs_write_seq_static(ctx,0x6F,0x0D);
    boe_dcs_write_seq_static(ctx,0xEB,0x78,0x58,0x3E,0x3C,0x3C,0x33);
    boe_dcs_write_seq_static(ctx,0x6F,0x24);
    boe_dcs_write_seq_static(ctx,0xEB,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x05);
    boe_dcs_write_seq_static(ctx,0xEC,0x00);
	pr_info("%s-\n", __func__);
}

static void boe_panel_reconfig_blk(struct boe_lcm *ctx)
{
    unsigned int level;
	if(mtk_drm_esd_check_status()){
		boe_dcs_write(ctx,bl_tb,ARRAY_SIZE(bl_tb));
        
        level = (bl_tb[1] << 8) + bl_tb[2];
        if(level > 1105){
            boe_setbacklight_A9_4095_cmdq(ctx);
        }
        else{
            boe_setbacklight_A9_1105_cmdq(ctx);
        }
		mtk_drm_esd_set_status(0);
	}
}
//modify by shenwenbin for ESD recovery not send backlight 20250903 end

static void boe_panel_init(struct boe_lcm *ctx)
{
	pr_info("%s+\n", __func__);
	
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return;
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(10);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(10);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(50);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0xC8,0x0D,0x0D,0x0D,0x0D,0x08,0x08);
    boe_dcs_write_seq_static(ctx,0x6F,0x0B);
    boe_dcs_write_seq_static(ctx,0xC8,0x0C,0x0C,0x04,0x04,0x0C,0x0C,0x08,0x08);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0x03);
    boe_dcs_write_seq_static(ctx,0xD9,0x40);
    boe_dcs_write_seq_static(ctx,0x6F,0x23);
    boe_dcs_write_seq_static(ctx,0xD9,0xC1,0xC9,0x12);
    boe_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x80);
    boe_dcs_write_seq_static(ctx,0x6F,0x29);
    boe_dcs_write_seq_static(ctx,0xF8,0x0E,0x36);
    boe_dcs_write_seq_static(ctx,0x6F,0x0D);
    boe_dcs_write_seq_static(ctx,0xF8,0x01,0x7F);
    boe_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x81);
    boe_dcs_write_seq_static(ctx,0x6F,0x02);
    boe_dcs_write_seq_static(ctx,0xF9,0x04);
    boe_dcs_write_seq_static(ctx,0x6F,0x0E);
    boe_dcs_write_seq_static(ctx,0xF5,0x2B);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x03);
    boe_dcs_write_seq_static(ctx,0xC8,0x01,0xFF,0xFB);
    boe_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x80);
    boe_dcs_write_seq_static(ctx,0x6F,0x22);
    boe_dcs_write_seq_static(ctx,0xFE,0x40);
    boe_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x82);
    boe_dcs_write_seq_static(ctx,0x6F,0x11);
    boe_dcs_write_seq_static(ctx,0xF8,0x0F);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x00,0x00);
    boe_dcs_write_seq_static(ctx,0xFF,0xAA,0x55,0xA5,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x06);
    boe_dcs_write_seq_static(ctx,0x6D,0x48);
    boe_dcs_write_seq_static(ctx,0x35,0x00);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x00);
    boe_dcs_write_seq_static(ctx,0xBE,0x54);  
    boe_dcs_write_seq_static(ctx,0x53,0x28);
    boe_dcs_write_seq_static(ctx,0x5F,0x00,0x00);
    boe_dcs_write_seq_static(ctx,0xA2,0x01);
    boe_dcs_write_seq_static(ctx,0x2A,0x00,0x00,0x09,0xAF);
    boe_dcs_write_seq_static(ctx,0x2B,0x00,0x00,0x08,0x97);
    boe_dcs_write_seq_static(ctx,0x90,0x03,0x43);
    boe_dcs_write_seq_static(ctx,0x91,0x89,0x28,0x00,0x14,0xC2,0x00,0x03,0x6D,0x02,0xA7,0x00,0x11,0x05,0x0E,0x02,0x2F,0x10,0xD0);
    boe_dcs_write_seq_static(ctx,0x2F,0x01);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x04);
    boe_dcs_write_seq_static(ctx,0xCA,0x01);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x07);
    boe_dcs_write_seq_static(ctx,0xC0,0x07);
    boe_dcs_write_seq_static(ctx,0x11);
    
    //boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    //boe_dcs_write_seq_static(ctx,0x6F,0xDE);
    //boe_dcs_write_seq_static(ctx,0xBA,0x04);
    
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x08);
    boe_dcs_write_seq_static(ctx,0xB2,0x40);
    boe_dcs_write_seq_static(ctx,0x8C,0x02);
    
    boe_dcs_write_seq_static(ctx,0x6F,0x06);
    boe_dcs_write_seq_static(ctx,0x6D,0x48);

    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0xB2,0x09);
    boe_dcs_write_seq_static(ctx,0x6F,0x05);
    boe_dcs_write_seq_static(ctx,0xB2,0x08);
    boe_dcs_write_seq_static(ctx,0x6F,0x07);
    boe_dcs_write_seq_static(ctx,0xB2,0x08);  
    msleep(120);
    boe_dcs_write_seq_static(ctx,0x29);
    msleep(50);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x6E);
    boe_dcs_write_seq_static(ctx,0xD1,0x00,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x06);
    boe_dcs_write_seq_static(ctx,0x6D,0x48);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x20);
    boe_dcs_write_seq_static(ctx,0xBA,0x00,0x00,0x00,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x24);
    boe_dcs_write_seq_static(ctx,0xBA,0x00,0x00,0x00,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x30);
    boe_dcs_write_seq_static(ctx,0xBA,0x01,0x01,0x01,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0x35);
    boe_dcs_write_seq_static(ctx,0xBA,0x01,0x01,0x01,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0x40);
    boe_dcs_write_seq_static(ctx,0xBA,0x03,0x03,0x03,0x03);
    boe_dcs_write_seq_static(ctx,0x6F,0x44);
    boe_dcs_write_seq_static(ctx,0xBA,0x03,0x03,0x03,0x03);
    boe_dcs_write_seq_static(ctx,0x6F,0x50);
    boe_dcs_write_seq_static(ctx,0xBA,0x09,0x09,0x09,0x09);
    boe_dcs_write_seq_static(ctx,0x6F,0x54);
    boe_dcs_write_seq_static(ctx,0xBA,0x09,0x09,0x09,0x09);
    boe_dcs_write_seq_static(ctx,0x6F,0x6C);
    boe_dcs_write_seq_static(ctx,0xBA,0x00,0x00,0x10);
    boe_dcs_write_seq_static(ctx,0x6F,0x74);
    boe_dcs_write_seq_static(ctx,0xBA,0x00,0x01,0x03,0x09,0x21,0x65);
    boe_dcs_write_seq_static(ctx,0x6F,0xA4);
    boe_dcs_write_seq_static(ctx,0xBA,0x01,0x01,0x01,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0xA8);
    boe_dcs_write_seq_static(ctx,0xBA,0x00,0x01,0x01,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0xAC);
    boe_dcs_write_seq_static(ctx,0xBA,0x00,0x00,0x01,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0xB0);
    boe_dcs_write_seq_static(ctx,0xBA,0x00,0x00,0x00,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0xC0);
    boe_dcs_write_seq_static(ctx,0xBA,0x11,0x11,0x11);
    boe_dcs_write_seq_static(ctx,0x6F,0x5A);
    boe_dcs_write_seq_static(ctx,0xD1,0x11,0x11,0x11);
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0x67);
    boe_dcs_write_seq_static(ctx,0xB9,0x00,0x65,0x66,0x66);
    boe_dcs_write_seq_static(ctx,0x6F,0x6F);
    boe_dcs_write_seq_static(ctx,0xB9,0x00,0x43,0x44,0x44);

    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0x6F,0x2C);
    boe_dcs_write_seq_static(ctx,0xBA,0x01,0x01,0x01,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0x3C);
    boe_dcs_write_seq_static(ctx,0xBA,0x01,0x01,0x01,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0x4C);
    boe_dcs_write_seq_static(ctx,0xBA,0x01,0x01,0x01,0x01);
    boe_dcs_write_seq_static(ctx,0x6F,0xB4);
    boe_dcs_write_seq_static(ctx,0xBA,0x90,0x0F);
    boe_dcs_write_seq_static(ctx,0x6F,0xB8);
    boe_dcs_write_seq_static(ctx,0xBA,0x11,0x11,0x11);

    boe_panel_reconfig_blk(ctx);   //modify by shenwenbin for ESD recovery not send backlight 20250903
    //bist mode
    /*
    boe_dcs_write_seq_static(ctx,0xF0,0x55,0xAA,0x52,0x08,0x00);
    boe_dcs_write_seq_static(ctx,0xEF,0x01,0x00,0xFF,0xFF,0xFF);
    boe_dcs_write_seq_static(ctx,0xEF,0x00,0x00);
    boe_dcs_write_seq_static(ctx,0xEF,0x00,0x01);
    boe_dcs_write_seq_static(ctx,0xEF,0x00,0x02);
    boe_dcs_write_seq_static(ctx,0xEF,0x00,0x03);
    boe_dcs_write_seq_static(ctx,0xEF,0x00,0x04);
    boe_dcs_write_seq_static(ctx,0xEE,0x01);
    */
	pr_info("%s-\n", __func__);
}

static int boe_panel_disable(struct drm_panel *panel)
{
	struct boe_lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int boe_panel_unprepare(struct drm_panel *panel)
{
	struct boe_lcm *ctx = panel_to_lcm(panel);

	pr_info("%s+\n", __func__);
	
	if (!ctx->prepared)
		return 0;

	boe_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(50);
	boe_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(150);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(10);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	ctx->doze_en = false;
	ctx->error = 0;
	ctx->prepared = false;
	
	pr_info("%s-\n", __func__);
	return 0;
}

static int boe_panel_prepare(struct drm_panel *panel)
{
	struct boe_lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;

	// lcd reset H -> L -> L
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(5);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	
	boe_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		boe_panel_unprepare(panel);

	ctx->prepared = true;
#ifdef PANEL_SUPPORT_READBACK
	boe_panel_get_data(ctx);
#endif

	pr_info("%s-\n", __func__);
	return ret;
}

static int boe_panel_enable(struct drm_panel *panel)
{
	struct boe_lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

#define FRAME_WIDTH                 2480
#define FRAME_HEIGHT                2200
#define PHYSICAL_WIDTH              152520
#define PHYSICAL_HEIGHT             135300

#define HFP (32)
#define HSA (4)
#define HBP (32)
#define HACT (2480)
#define VFP (88)
#define VSA (2)
#define VBP (86)
#define VACT (2200)

unsigned int nt37900b_cmd_4k_buf_thresh[14] = {
	896, 1792, 2688, 3584, 4480,
	5376, 6272, 6720, 7168, 7616,
	7744, 7872, 8000, 8064};
unsigned int nt37900b_cmd_4k_range_min_qp[15] = {
	0, 0, 1, 1, 3,
	3, 3, 3, 3, 3,
	5, 5, 5, 7, 13};
unsigned int nt37900b_cmd_4k_range_max_qp[15] = {
	4, 4, 5, 6, 7,
	7, 7, 8, 9, 10,
	11, 12, 13, 13, 15};
int nt37900b_cmd_4k_range_bpg_ofs[15] = {
	2, 0, 0, -2, -4,
	-6, -8, -8, -8, -10,
	-10, -12, -12, -12, -12};

static const struct drm_display_mode switch_mode_120hz = {
	.clock		= 726486,
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
	.clock		= 544865,
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
	.clock		= 363243,
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
	.clock		= 181622,
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
	.clock		= 60541,
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
	.clock		= 6055,
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
	.pll_clk = 876,
    .data_rate = 1752,
	.physical_width_um = PHYSICAL_WIDTH,
    .physical_height_um = PHYSICAL_HEIGHT,
    .lcm_degree = PROBE_FROM_DTS,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lp_perline_en = 1,
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
		.pic_height = 2200,
		.pic_width = 2480,
		.slice_height = 20,
		.slice_width = 1240,
		.chunk_size = 1240,
		.xmit_delay = 512,
		.dec_delay = 877,
		.scale_value = 32,
		.increment_interval = 679,
		.decrement_interval = 17,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 559,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
        .ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = nt37900b_cmd_4k_buf_thresh,
			.range_min_qp = nt37900b_cmd_4k_range_min_qp,
			.range_max_qp = nt37900b_cmd_4k_range_max_qp,
			.range_bpg_ofs = nt37900b_cmd_4k_range_bpg_ofs,
		},
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.pll_clk = 876,
    .data_rate = 1752,
	.physical_width_um = PHYSICAL_WIDTH,
    .physical_height_um = PHYSICAL_HEIGHT,
    .lcm_degree = PROBE_FROM_DTS,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lp_perline_en = 1,
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
		.pic_height = 2200,
		.pic_width = 2480,
		.slice_height = 20,
		.slice_width = 1240,
		.chunk_size = 1240,
		.xmit_delay = 512,
		.dec_delay = 877,
		.scale_value = 32,
		.increment_interval = 679,
		.decrement_interval = 17,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 559,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
        .ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = nt37900b_cmd_4k_buf_thresh,
			.range_min_qp = nt37900b_cmd_4k_range_min_qp,
			.range_max_qp = nt37900b_cmd_4k_range_max_qp,
			.range_bpg_ofs = nt37900b_cmd_4k_range_bpg_ofs,
		},
	},
};

static struct mtk_panel_params ext_params_60hz = {
	.pll_clk = 876,
    .data_rate = 1752,
	.physical_width_um = PHYSICAL_WIDTH,
    .physical_height_um = PHYSICAL_HEIGHT,
    .lcm_degree = PROBE_FROM_DTS,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lp_perline_en = 1,
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
		.pic_height = 2200,
		.pic_width = 2480,
		.slice_height = 20,
		.slice_width = 1240,
		.chunk_size = 1240,
		.xmit_delay = 512,
		.dec_delay = 877,
		.scale_value = 32,
		.increment_interval = 679,
		.decrement_interval = 17,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 559,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
        .ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = nt37900b_cmd_4k_buf_thresh,
			.range_min_qp = nt37900b_cmd_4k_range_min_qp,
			.range_max_qp = nt37900b_cmd_4k_range_max_qp,
			.range_bpg_ofs = nt37900b_cmd_4k_range_bpg_ofs,
		},
	},
};

static struct mtk_panel_params ext_params_30hz = {
	.pll_clk = 876,
    .data_rate = 1752,
	.physical_width_um = PHYSICAL_WIDTH,
    .physical_height_um = PHYSICAL_HEIGHT,
    .lcm_degree = PROBE_FROM_DTS,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lp_perline_en = 1,
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
		.pic_height = 2200,
		.pic_width = 2480,
		.slice_height = 20,
		.slice_width = 1240,
		.chunk_size = 1240,
		.xmit_delay = 512,
		.dec_delay = 877,
		.scale_value = 32,
		.increment_interval = 679,
		.decrement_interval = 17,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 559,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
        .ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = nt37900b_cmd_4k_buf_thresh,
			.range_min_qp = nt37900b_cmd_4k_range_min_qp,
			.range_max_qp = nt37900b_cmd_4k_range_max_qp,
			.range_bpg_ofs = nt37900b_cmd_4k_range_bpg_ofs,
		},
	},
};

static struct mtk_panel_params ext_params_10hz = {
	.pll_clk = 876,
    .data_rate = 1752,
	.physical_width_um = PHYSICAL_WIDTH,
    .physical_height_um = PHYSICAL_HEIGHT,
    .lcm_degree = PROBE_FROM_DTS,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lp_perline_en = 1,
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
		.pic_height = 2200,
		.pic_width = 2480,
		.slice_height = 20,
		.slice_width = 1240,
		.chunk_size = 1240,
		.xmit_delay = 512,
		.dec_delay = 877,
		.scale_value = 32,
		.increment_interval = 679,
		.decrement_interval = 17,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 559,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
        .ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = nt37900b_cmd_4k_buf_thresh,
			.range_min_qp = nt37900b_cmd_4k_range_min_qp,
			.range_max_qp = nt37900b_cmd_4k_range_max_qp,
			.range_bpg_ofs = nt37900b_cmd_4k_range_bpg_ofs,
		},
	},
};

static struct mtk_panel_params ext_params_1hz = {
	.pll_clk = 876,
    .data_rate = 1752,
	.physical_width_um = PHYSICAL_WIDTH,
    .physical_height_um = PHYSICAL_HEIGHT,
    .lcm_degree = PROBE_FROM_DTS,
 	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lp_perline_en = 1,
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
		.pic_height = 2200,
		.pic_width = 2480,
		.slice_height = 20,
		.slice_width = 1240,
		.chunk_size = 1240,
		.xmit_delay = 512,
		.dec_delay = 877,
		.scale_value = 32,
		.increment_interval = 679,
		.decrement_interval = 17,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 559,
		.initial_offset = 6144,
		.final_offset = 4304,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
        .ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = nt37900b_cmd_4k_buf_thresh,
			.range_min_qp = nt37900b_cmd_4k_range_min_qp,
			.range_max_qp = nt37900b_cmd_4k_range_max_qp,
			.range_bpg_ofs = nt37900b_cmd_4k_range_bpg_ofs,
		},
	},
};

static int panel_ata_check(struct drm_panel *panel)
{
	/* Customer test by own ATA tool */
	return 1;
}

static int boe_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
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
        pr_err("%s,nt37900b backlight err: level = %d\n", __func__, level);
        return -1;
    }
    
    bl_tb[1] =  (level >> 8 ) & 0xF;
    bl_tb[2] =  level & 0xFF;
       
    cb(dsi, handle, bl_tb, ARRAY_SIZE(bl_tb));

    if (level <= 0x0F0D) {
       cb(dsi, handle, GIR_ON, ARRAY_SIZE(GIR_ON));
    } else {
       cb(dsi, handle, GIR_OFF, ARRAY_SIZE(GIR_OFF));
    }
    
    if(level > 1105){
        cb(dsi, handle, A9_4095_tb, ARRAY_SIZE(A9_4095_tb));
    }
    else{
        cb(dsi, handle, A9_1105_tb, ARRAY_SIZE(A9_1105_tb));
    }

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
	struct boe_lcm *ctx = panel_to_lcm(panel);

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
		struct boe_lcm *ctx = panel_to_lcm(panel);
        
        boe_dcs_write_seq_static(ctx, 0xF0,0x55,0xAA,0x52,0x08,0x01);
        boe_dcs_write_seq_static(ctx, 0x6F,0xE4);
        boe_dcs_write_seq_static(ctx, 0xE5,0x7F,0x7F,0x20,0x20,0x20,0x20,0x20,0x20);
        boe_dcs_write_seq_static(ctx, 0x2F,0x30);
        boe_dcs_write_seq_static(ctx, 0x6E,0x00);
        boe_dcs_write_seq_static(ctx, 0x6D,0x80);
		pr_info("%s:%d  120 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_90(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct boe_lcm *ctx = panel_to_lcm(panel);

        boe_dcs_write_seq_static(ctx, 0xF0,0x55,0xAA,0x52,0x08,0x01);
        boe_dcs_write_seq_static(ctx, 0x6F,0xE4);
        boe_dcs_write_seq_static(ctx, 0xE5,0x7F,0x7F,0x20,0x20,0x20,0x20,0x20,0x20);
        boe_dcs_write_seq_static(ctx, 0x2F,0x30);
        boe_dcs_write_seq_static(ctx, 0x6E,0x00);
        boe_dcs_write_seq_static(ctx, 0x6D,0x81);
		pr_info("%s:%d  90 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_60(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct boe_lcm *ctx = panel_to_lcm(panel);

        boe_dcs_write_seq_static(ctx, 0xF0,0x55,0xAA,0x52,0x08,0x01);
        boe_dcs_write_seq_static(ctx, 0x6F,0xE4);
        boe_dcs_write_seq_static(ctx, 0xE5,0x7F,0x7F,0x20,0x20,0x20,0x20,0x20,0x20);
        boe_dcs_write_seq_static(ctx, 0x2F,0x30);
        boe_dcs_write_seq_static(ctx, 0x6E,0x00);
        boe_dcs_write_seq_static(ctx, 0x6D,0x82);
		pr_info("%s:%d  60 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_30(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct boe_lcm *ctx = panel_to_lcm(panel);

        boe_dcs_write_seq_static(ctx, 0xF0,0x55,0xAA,0x52,0x08,0x01);
        boe_dcs_write_seq_static(ctx, 0x6F,0xE4);
        boe_dcs_write_seq_static(ctx, 0xE5,0x7F,0x7F,0x20,0x20,0x20,0x20,0x20,0x20);
        boe_dcs_write_seq_static(ctx, 0x2F,0x30);
        boe_dcs_write_seq_static(ctx, 0x6E,0x00);
        boe_dcs_write_seq_static(ctx, 0x6D,0x83);
		pr_info("%s:%d  30 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_10(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct boe_lcm *ctx = panel_to_lcm(panel);

        boe_dcs_write_seq_static(ctx, 0xF0,0x55,0xAA,0x52,0x08,0x01);
        boe_dcs_write_seq_static(ctx, 0x6F,0xE4);
        boe_dcs_write_seq_static(ctx, 0xE5,0x7F,0x7F,0x20,0x20,0x20,0x20,0x20,0x20);
        boe_dcs_write_seq_static(ctx, 0x2F,0x30);
        boe_dcs_write_seq_static(ctx, 0x6E,0x00);
        boe_dcs_write_seq_static(ctx, 0x6D,0x84);
		pr_info("%s:%d  10 display_mode end\n", __func__, __LINE__);
	}
}

static void mode_switch_to_1(struct drm_panel *panel,
	enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	if (stage == BEFORE_DSI_POWERDOWN) {
		struct boe_lcm *ctx = panel_to_lcm(panel);

        boe_dcs_write_seq_static(ctx, 0xF0,0x55,0xAA,0x52,0x08,0x01);
        boe_dcs_write_seq_static(ctx, 0x6F,0xE4);
        boe_dcs_write_seq_static(ctx, 0xE5,0x7F,0x7F,0x20,0x20,0x20,0x20,0x20,0x20);
        boe_dcs_write_seq_static(ctx, 0x2F,0x30);
        boe_dcs_write_seq_static(ctx, 0x6E,0x00);
        boe_dcs_write_seq_static(ctx, 0x6D,0x85);
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

static int panel_doze_enable(struct drm_panel *panel, void *dsi, dcs_write_gce cb, void *handle)
{
	unsigned int i = 0;
    struct boe_lcm *ctx = panel_to_lcm(panel);

	if(IS_ERR_OR_NULL(ctx)) //(ctx == NULL)
		return 0;
	
    pr_info("panel %s +\n", __func__);
	
    if(!ctx->doze_en)
		ctx->doze_en = true;

    /* Enter AOD */  
    aod_bl_tb[1] = 0x03;
    aod_bl_tb[2] = 0x25;

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
       
    cb(dsi, handle, aod_bl_tb, ARRAY_SIZE(aod_bl_tb));
    cb(dsi, handle, GIR_ON, ARRAY_SIZE(GIR_ON));
    cb(dsi, handle, A9_1105_tb, ARRAY_SIZE(A9_1105_tb));
    
    pr_info("panel %s -\n", __func__);
	return 0;
}

static int panel_doze_disable(struct drm_panel *panel,void *dsi, dcs_write_gce cb, void *handle)
{
    unsigned int i = 0;
    unsigned int level;
	struct boe_lcm *ctx = panel_to_lcm(panel);

	if(IS_ERR_OR_NULL(ctx)) //(ctx == NULL)
		return 0;
	
    pr_info("panel %s +\n", __func__);

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

    if(ctx->doze_en){
		ctx->doze_en = false;
    }

    if(g_ctx->out_doze_set_backlight == true){
        level = g_ctx->out_doze_set_backlight_level;
        bl_tb[1] =  (level >> 8 ) & 0xF;
        bl_tb[2] =  level & 0xFF;
           
        cb(dsi, handle, bl_tb, ARRAY_SIZE(bl_tb));

        if (level <= 0x0F0D) {
           cb(dsi, handle, GIR_ON, ARRAY_SIZE(GIR_ON));
        } else {
           cb(dsi, handle, GIR_OFF, ARRAY_SIZE(GIR_OFF));
        }
        
        if(level > 1105){
            cb(dsi, handle, A9_4095_tb, ARRAY_SIZE(A9_4095_tb));
        }
        else{
            cb(dsi, handle, A9_1105_tb, ARRAY_SIZE(A9_1105_tb));
        }

       g_ctx->out_doze_set_backlight = false;
       g_ctx->out_doze_set_backlight_level = 0;
       pr_info("%s level=%d,bl_tb[1]=0x%x, bl_tb[2]=0x%x\n",__func__, level, bl_tb[1], bl_tb[2]);
    }
 
    pr_info("panel %s -\n", __func__);
	
    return 0;
}

static int panel_set_aod_light_mode(void *dsi,dcs_write_gce cb, void *handle, unsigned int mode)
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
        aod_bl_tb[2] = 0xD0;//0x01 //2nit
	}

    cb(dsi, handle, aod_bl_tb, ARRAY_SIZE(aod_bl_tb));
    cb(dsi, handle, GIR_ON, ARRAY_SIZE(GIR_ON));
    cb(dsi, handle, A9_1105_tb, ARRAY_SIZE(A9_1105_tb));
    
	pr_info("%s : %d !\n", __func__, mode);

	return 0;
}
//DRV-modify by shenwenbin for add AOD mode 20240201 end

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = boe_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.ext_param_set = mtk_panel_ext_param_set,
    .ext_param_get = mtk_panel_ext_param_get,
	.mode_switch = mode_switch,
	.doze_enable = panel_doze_enable,
	.doze_disable = panel_doze_disable,
	.set_aod_light_mode = panel_set_aod_light_mode,
};
#endif

static int boe_panel_get_modes(struct drm_panel *panel,
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

	connector->display_info.width_mm = 153;
	connector->display_info.height_mm = 135;

	return 1;
}

static const struct drm_panel_funcs boe_drm_funcs = {
	.disable = boe_panel_disable,
	.unprepare = boe_panel_unprepare,
	.prepare = boe_panel_prepare,
	.enable = boe_panel_enable,
	.get_modes = boe_panel_get_modes,
};

static int boe_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct boe_lcm *ctx;
	struct device_node *backlight;
	unsigned int lcm_degree;
	int ret;
    const u32 *val;
	int probe_ret;

	pr_info("%s+ boe,nt37900b,cmd,120hz\n", __func__);

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

	ctx = devm_kzalloc(dev, sizeof(struct boe_lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_NO_EOT_PACKET;
    //dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

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
	drm_panel_init(&ctx->panel, dev, &boe_drm_funcs, DRM_MODE_CONNECTOR_DSI);

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
	g_ctx = ctx;
	ctx->doze_en = false;
    
    ctx->current_fps = 120;

//drv add by shenwenbin for lcd hardware info 20231214 start
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
    strcpy(current_lcm_info.chip,"NT37900B");
    strcpy(current_lcm_info.vendor,"BOE");
    sprintf(current_lcm_info.id,"0x%02x",0xA1);
    strcpy(current_lcm_info.more,"2480*2200");
#endif
//drv add by shenwenbin for lcd hardware info 20231214 end

	pr_info("%s- boe,nt37900b,cmd,120hz\n", __func__);
	return ret;
}

static void boe_panel_remove(struct mipi_dsi_device *dsi)
{
	struct boe_lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	if (ext_ctx == NULL)
		return;

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif
}

static const struct of_device_id boe_panel_of_match[] = {
    {
        .compatible = "boe,nt37900b,cmd,120Hz",
    },
    {}
};

MODULE_DEVICE_TABLE(of, boe_panel_of_match);

static struct mipi_dsi_driver boe_panel_driver = {
	.probe = boe_panel_probe,
	.remove = boe_panel_remove,
	.driver = {
		.name = "nt37900b-4k-dsi-cmd-boe-dphy-120hz",
		.owner = THIS_MODULE,
		.of_match_table = boe_panel_of_match,
	},
};

module_mipi_dsi_driver(boe_panel_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("NT37900B AMOLED CMD LCD Panel Driver");
MODULE_LICENSE("GPL v2");
