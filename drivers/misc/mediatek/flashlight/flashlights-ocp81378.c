/*
* Copyright (C) 2022 Awinic Inc.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/pinctrl/consumer.h>
#include "flashlight-core.h"
#include "flashlight-dt.h"

/* device tree should be defined in flashlight-dt.h */
#ifndef OCP81378_DTNAME
#define OCP81378_DTNAME "mediatek,flashlights_ocp81378"
#endif
#ifndef OCP81378_DTNAME_I2C
#define OCP81378_DTNAME_I2C "mediatek,flashlights_ocp81378"
#endif
#define OCP81378_NAME "flashlights-ocp81378"

#define OCP81378_DRIVER_VERSION "V1.1.0"

/* define registers */
#define OCP81378_REG_ENABLE           (0x01)
#define OCP81378_MASK_ENABLE_LED1     (0x01)
#define OCP81378_MASK_ENABLE_LED2     (0x02)
#define OCP81378_DISABLE              (0x00)
#define OCP81378_ENABLE_LED1          (0x01)
#define OCP81378_ENABLE_LED1_TORCH    (0x0B) //0x09
#define OCP81378_ENABLE_LED1_FLASH    (0x0F) //0x0D
#define OCP81378_ENABLE_LED2          (0x02)
#define OCP81378_ENABLE_LED2_TORCH    (0x0A)
#define OCP81378_ENABLE_LED2_FLASH    (0x0E)

#define OCP81378_REG_TORCH_LEVEL_LED1 (0x05)
#define OCP81378_REG_FLASH_LEVEL_LED1 (0x03)
#define OCP81378_REG_TORCH_LEVEL_LED2 (0x06)
#define OCP81378_REG_FLASH_LEVEL_LED2 (0x04)

#define OCP81378_REG_TIMING_CONF      (0x08)
#define OCP81378_TORCH_RAMP_TIME      (0x10)
#define OCP81378_FLASH_TIMEOUT        (0x0F)
#define OCP81378_REG_DEVICE_ID        (0x0C)
#define OCP81378_DEVICE_ID            (0x3A)

/* define channel, level */
#define OCP81378_CHANNEL_NUM          2
#define OCP81378_CHANNEL_CH1          0
#define OCP81378_CHANNEL_CH2          1
#define OCP81378_LEVEL_NUM            26
#define OCP81378_LEVEL_TORCH          7


#define CONFIG_PRIZE_SOFT_LED_ENABLE 0
#define OCP81378_DEBUG(format, args...) printk(KERN_WARNING format, ##args)

/* define pinctrl */
#define OCP81378_PINCTRL_PIN_HWEN 8
#define OCP81378_PINCTRL_PINSTATE_LOW 0
#define OCP81378_PINCTRL_PINSTATE_HIGH 1
#define OCP81378_PINCTRL_STATE_HWEN_HIGH "ocp81378_hwen_1"
#define OCP81378_PINCTRL_STATE_HWEN_LOW  "ocp81378_hwen_0"
static struct pinctrl *ocp81378_pinctrl;
static struct pinctrl_state *ocp81378_hwen_high;
static struct pinctrl_state *ocp81378_hwen_low;

/* define mutex and work queue */
static DEFINE_MUTEX(ocp81378_mutex);
static struct work_struct ocp81378_work_ch1;
static struct work_struct ocp81378_work_ch2;

struct i2c_client *ocp81378_flashlight_client;

/* define usage count */
static int use_count;

/* define i2c */
static struct i2c_client *ocp81378_i2c_client;

/* platform data
* torch_pin_enable: TX1/TORCH pin isa hardware TORCH enable
* pam_sync_pin_enable: TX2 Mode The ENVM/TX2 is a PAM Sync. on input
* thermal_comp_mode_enable: LEDI/NTC pin in Thermal Comparator Mode
* strobe_pin_disable: STROBE Input disabled
* vout_mode_enable: Voltage Out Mode enable
*/
struct ocp81378_platform_data {
	u8 torch_pin_enable;
	u8 pam_sync_pin_enable;
	u8 thermal_comp_mode_enable;
	u8 strobe_pin_disable;
	u8 vout_mode_enable;
	
	int channel_num;
	struct flashlight_device_id *dev_id;
};

/* ocp81378 chip data */
struct ocp81378_chip_data {
	struct i2c_client *client;
	struct ocp81378_platform_data *pdata;
	struct mutex lock;
	u8 last_flag;
	u8 no_pdata;
};

static struct platform_device *g_ocp81378_pdev;
static int led_num = -1;


/******************************************************************************
 * Pinctrl configuration
 *****************************************************************************/
int flashlight_gpio_init(struct platform_device *pdev)
{
	int ret = 0;

	/* get pinctrl */
	ocp81378_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(ocp81378_pinctrl)) {
		pr_err("Failed to get flashlight pinctrl.\n");
		ret = PTR_ERR(ocp81378_pinctrl);
	}

	/* Flashlight HWEN pin initialization */
	ocp81378_hwen_high = pinctrl_lookup_state(ocp81378_pinctrl, OCP81378_PINCTRL_STATE_HWEN_HIGH);
	if (IS_ERR(ocp81378_hwen_high)) {
		pr_err("Failed to init (%s)\n", OCP81378_PINCTRL_STATE_HWEN_HIGH);
		ret = PTR_ERR(ocp81378_hwen_high);
	}
	ocp81378_hwen_low = pinctrl_lookup_state(ocp81378_pinctrl, OCP81378_PINCTRL_STATE_HWEN_LOW);
	if (IS_ERR(ocp81378_hwen_low)) {
		pr_err("Failed to init (%s)\n", OCP81378_PINCTRL_STATE_HWEN_LOW);
		ret = PTR_ERR(ocp81378_hwen_low);
	}

	return ret;
}

int flashlight_pinctrl_set(int pin, int state)
{
	int ret = 0;

	if (IS_ERR(ocp81378_pinctrl)) {
		pr_err("pinctrl is not available\n");
		return -1;
	}

	switch (pin) {
	case OCP81378_PINCTRL_PIN_HWEN:
		if (state == OCP81378_PINCTRL_PINSTATE_LOW && !IS_ERR(ocp81378_hwen_low))
			pinctrl_select_state(ocp81378_pinctrl, ocp81378_hwen_low);
		else if (state == OCP81378_PINCTRL_PINSTATE_HIGH && !IS_ERR(ocp81378_hwen_high))
			pinctrl_select_state(ocp81378_pinctrl, ocp81378_hwen_high);
		else
			pr_err("set err, pin(%d) state(%d)\n", pin, state);
		break;
	default:
		pr_err("set err, pin(%d) state(%d)\n", pin, state);
		break;
	}
	OCP81378_DEBUG("pin(%d) state(%d)\n", pin, state);

	return ret;
}


/******************************************************************************
 * ocp81378 operations
 *****************************************************************************/
/* duty * 3.84 + 2.9 */
/* 34, 67, 95, 126, 157, 188, 219 */
static const unsigned char ocp81378_torch_level[OCP81378_LEVEL_NUM] = {
	0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* duty * 15.65 + 15 */
/*  78, 109, 140, 171, 203, 234, 265, 297, 328, 359 */
/* 390, 422, 453, 484, 516, 547, 578, 609, 641, 657 */
/* 672, 688, 704, 719, 735, 750 */
static const unsigned char ocp81378_flash_level[OCP81378_LEVEL_NUM] = {
	0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16,
	0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22, 0x24, 0x26, 0x28, 0x29,
	0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F};

static volatile unsigned char ocp81378_reg_enable;
static volatile int ocp81378_level_ch1 = -1;
static volatile int ocp81378_level_ch2 = -1;

static int ocp81378_is_torch(int level)
{
	if (level >= OCP81378_LEVEL_TORCH)
		return -1;

	return 0;
}

static int ocp81378_verify_level(int level)
{
	if (level < 0)
		level = 0;
	else if (level >= OCP81378_LEVEL_NUM)
		level = OCP81378_LEVEL_NUM - 1;

	return level;
}

/* i2c wrapper function */
static int ocp81378_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;
	struct ocp81378_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	mutex_unlock(&chip->lock);

	if (ret < 0)
		pr_err("failed writing at 0x%02x\n", reg);

	return ret;
}

static int ocp81378_read_reg(struct i2c_client *client, u8 reg)
{
	int val;
	struct ocp81378_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	val = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&chip->lock);

	if (val < 0)
		pr_err("failed read at 0x%02x\n", reg);

	return val;
}

/* flashlight enable function */
static int ocp81378_enable_ch1(void)
{
	unsigned char reg, val;

	reg = OCP81378_REG_ENABLE;
	if (!ocp81378_is_torch(ocp81378_level_ch1)) {
		/* torch mode */
		ocp81378_reg_enable |= OCP81378_ENABLE_LED1_TORCH;
	} else {
		/* flash mode */
		ocp81378_reg_enable |= OCP81378_ENABLE_LED1_FLASH;
	}

	val = ocp81378_reg_enable;

	return ocp81378_write_reg(ocp81378_i2c_client, reg, val);
}

static int ocp81378_enable_ch2(void)
{
	unsigned char reg, val;

	reg = OCP81378_REG_ENABLE;
	if (!ocp81378_is_torch(ocp81378_level_ch2)) {
		/* torch mode */
		ocp81378_reg_enable |= OCP81378_ENABLE_LED2_TORCH;
	} else {
		/* flash mode */
		ocp81378_reg_enable |= OCP81378_ENABLE_LED2_FLASH;
	}
	val = ocp81378_reg_enable;

	return ocp81378_write_reg(ocp81378_i2c_client, reg, val);
}

/* flashlight disable function */
static int ocp81378_disable_ch1(void)
{
	unsigned char reg, val;

	reg = OCP81378_REG_ENABLE;
	val = ocp81378_reg_enable & 0xF0;//11110000

	return ocp81378_write_reg(ocp81378_i2c_client, reg, val);
}

static int ocp81378_disable_ch2(void)
{
	unsigned char reg, val;

	reg = OCP81378_REG_ENABLE;
	if (ocp81378_reg_enable & OCP81378_MASK_ENABLE_LED1) {
		/* if LED 1 is enable, disable LED 2 */
		ocp81378_reg_enable &= (~OCP81378_ENABLE_LED2);
	} else {
		/* if LED 1 is enable, disable LED 2 and clear mode */
		ocp81378_reg_enable &= (~OCP81378_ENABLE_LED2_FLASH);
	}
	val = ocp81378_reg_enable;

	return ocp81378_write_reg(ocp81378_i2c_client, reg, val);
}

static int ocp81378_enable(int channel)
{
	if (channel == OCP81378_CHANNEL_CH1)
		ocp81378_enable_ch1();
	else if (channel == OCP81378_CHANNEL_CH2)
		ocp81378_enable_ch2();
	else {
		pr_err("%s, Error channel\n", __func__);
		return -1;
	}

	return 0;
}

static int ocp81378_disable(int channel)
{
	if (channel == OCP81378_CHANNEL_CH1)
		ocp81378_disable_ch1();
	else if (channel == OCP81378_CHANNEL_CH2)
		ocp81378_disable_ch2();
	else {
		pr_err("Error channel\n");
		return -1;
	}

	return 0;
}

/* set flashlight level */
static int ocp81378_set_level_ch1(int level)
{
	int ret;
	unsigned char reg, val;

	level = ocp81378_verify_level(level);

	/* set torch brightness level */
	reg = OCP81378_REG_TORCH_LEVEL_LED1;
	val = ocp81378_torch_level[level] | 0x80;
	ret = ocp81378_write_reg(ocp81378_i2c_client, reg, val);

	ocp81378_level_ch1 = level;

	/* set flash brightness level */
	reg = OCP81378_REG_FLASH_LEVEL_LED1;
	val = ocp81378_flash_level[level] | 0x80;
	ret = ocp81378_write_reg(ocp81378_i2c_client, reg, val);

	return ret;
}

int ocp81378_set_level_ch2(int level)
{
	int ret;
	unsigned char reg, val;

	level = ocp81378_verify_level(level);

	/* set torch brightness level */
	reg = OCP81378_REG_TORCH_LEVEL_LED2;
	val = ocp81378_torch_level[level];
	ret = ocp81378_write_reg(ocp81378_i2c_client, reg, val);

	ocp81378_level_ch2 = level;

	/* set flash brightness level */
	reg = OCP81378_REG_FLASH_LEVEL_LED2;
	val = ocp81378_flash_level[level];
	ret = ocp81378_write_reg(ocp81378_i2c_client, reg, val);

	return ret;
}

static int ocp81378_set_level(int channel, int level)
{
	if (channel == OCP81378_CHANNEL_CH1)
		ocp81378_set_level_ch1(level);
	else if (channel == OCP81378_CHANNEL_CH2)
		ocp81378_set_level_ch2(level);
	else {
		pr_err("%s, Error channel\n", __func__);
		return -1;
	}

	return 0;
}

/* flashlight init */
int ocp81378_init(void)
{
	int ret;
	unsigned char reg, val;

	flashlight_pinctrl_set(OCP81378_PINCTRL_PIN_HWEN, OCP81378_PINCTRL_PINSTATE_HIGH);
	
	usleep_range(2000, 2500);

	/* clear enable register */
	reg = OCP81378_REG_ENABLE;
	val = OCP81378_DISABLE;
	ret = ocp81378_write_reg(ocp81378_i2c_client, reg, val);

	ocp81378_reg_enable = val;

	/* set torch current ramp time and flash timeout */
	reg = OCP81378_REG_TIMING_CONF;
	val = OCP81378_TORCH_RAMP_TIME | OCP81378_FLASH_TIMEOUT;
	ret = ocp81378_write_reg(ocp81378_i2c_client, reg, val);

	return ret;
}

/* flashlight uninit */
int ocp81378_uninit(void)
{
	ocp81378_disable(OCP81378_CHANNEL_CH1);
	ocp81378_disable(OCP81378_CHANNEL_CH2);
	
	flashlight_pinctrl_set(OCP81378_PINCTRL_PIN_HWEN, OCP81378_PINCTRL_PINSTATE_LOW);

	return 0;
}

/******************************************************************************
 * Timer and work queue
 *****************************************************************************/
static struct hrtimer ocp81378_timer_ch1;
static struct hrtimer ocp81378_timer_ch2;
static unsigned int ocp81378_timeout_ms[OCP81378_CHANNEL_NUM];

static void ocp81378_work_disable_ch1(struct work_struct *data)
{
	OCP81378_DEBUG("ht work queue callback\n");
	ocp81378_disable_ch1();
}

static void ocp81378_work_disable_ch2(struct work_struct *data)
{
	printk("lt work queue callback\n");
	ocp81378_disable_ch2();
}

static enum hrtimer_restart ocp81378_timer_func_ch1(struct hrtimer *timer)
{
	schedule_work(&ocp81378_work_ch1);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart ocp81378_timer_func_ch2(struct hrtimer *timer)
{
	schedule_work(&ocp81378_work_ch2);
	return HRTIMER_NORESTART;
}


int ocp81378_timer_start(int channel, ktime_t ktime)
{
	if (channel == OCP81378_CHANNEL_CH1)
		hrtimer_start(&ocp81378_timer_ch1, ktime, HRTIMER_MODE_REL);
	else if (channel == OCP81378_CHANNEL_CH2)
		hrtimer_start(&ocp81378_timer_ch2, ktime, HRTIMER_MODE_REL);
	else {
		pr_err("%s, Error channel\n", __func__);
		return -1;
	}

	return 0;
}

int ocp81378_timer_cancel(int channel)
{
	if (channel == OCP81378_CHANNEL_CH1)
		hrtimer_cancel(&ocp81378_timer_ch1);
	else if (channel == OCP81378_CHANNEL_CH2)
		hrtimer_cancel(&ocp81378_timer_ch2);
	else {
		pr_err("Error channel\n");
		return -1;
	}

	return 0;
}


/******************************************************************************
 * Flashlight operations
 *****************************************************************************/
static int ocp81378_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;
	ktime_t ktime;

	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	/* verify channel */
	if (channel < 0 || channel >= OCP81378_CHANNEL_NUM) {
		pr_err("%s, Failed with error channel\n", __func__);
		return -EINVAL;
	}

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		pr_info("%s, FLASH_IOC_SET_TIME_OUT_TIME_MS(%d): %d\n",
				__func__, channel, (int)fl_arg->arg);
		ocp81378_timeout_ms[channel] = fl_arg->arg;
		break;

	case FLASH_IOC_SET_DUTY:
		pr_info("%s. FLASH_IOC_SET_DUTY(%d): %d\n",
				__func__, channel, (int)fl_arg->arg);
		ocp81378_set_level(channel, fl_arg->arg);
		break;

	case FLASH_IOC_SET_ONOFF:
		pr_info("%s. FLASH_IOC_SET_ONOFF(%d): %d\n",
				__func__, channel, (int)fl_arg->arg);
		if (fl_arg->arg == 1) {
			if (ocp81378_timeout_ms[channel]) {
				ktime =
				ktime_set(ocp81378_timeout_ms[channel] / 1000,
				(ocp81378_timeout_ms[channel] % 1000) * 1000000);
				ocp81378_timer_start(channel, ktime);
			}
			ocp81378_enable(channel);
		} else {
			ocp81378_disable(channel);
			ocp81378_timer_cancel(channel);
		}
		break;

	default:
		pr_info("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

static int ocp81378_open(void)
{
	/* Actual behavior move to set driver function */
	/*since power saving issue */
	return 0;
}

static int ocp81378_release(void)
{
	/* uninit chip and clear usage count */
	mutex_lock(&ocp81378_mutex);
	use_count--;
	if (!use_count)
		ocp81378_uninit();
	if (use_count < 0)
		use_count = 0;
	mutex_unlock(&ocp81378_mutex);

	pr_info("Release: %d\n", use_count);

	return 0;
}

static int ocp81378_set_driver(int set)
{
	/* init chip and set usage count */
	mutex_lock(&ocp81378_mutex);
	if (!use_count)
		ocp81378_init();
	use_count++;
	mutex_unlock(&ocp81378_mutex);

	pr_info("Set driver: %d\n", use_count);

	return 0;
}

static ssize_t ocp81378_strobe_store(struct flashlight_arg arg)
{
	ocp81378_set_driver(1);
	ocp81378_set_level(arg.ct, arg.level);
	ocp81378_enable(arg.ct);
	msleep(arg.dur);
	ocp81378_disable(arg.ct);
	ocp81378_set_driver(0);

	return 0;
}

static struct flashlight_operations ocp81378_ops = {
	ocp81378_open,
	ocp81378_release,
	ocp81378_ioctl,
	ocp81378_strobe_store,
	ocp81378_set_driver
};

/******************************************************************************
 * I2C device and driver
 *****************************************************************************/
static int ocp81378_chip_init(struct ocp81378_chip_data *chip)
{
	/* NOTE: Chip initialication move to
	*"set driver" operation for power saving issue.
	*/
	int val;
	
	flashlight_pinctrl_set(OCP81378_PINCTRL_PIN_HWEN, OCP81378_PINCTRL_PINSTATE_HIGH);
	usleep_range(2000, 2500);
	
	val = ocp81378_read_reg(ocp81378_i2c_client, OCP81378_REG_DEVICE_ID);
	printk("%s:ocp81378 chip id:0x%x\n", __func__, val);
	if (val == OCP81378_DEVICE_ID) {
		printk("%s:ocp81378 get chip id ok\n", __func__);
	} else {
		printk("%s:ocp81378 get chip id fail\n", __func__);
	}
	
	return 0;
}

/***************************************************************************/
/*OCP81378 Debug file */
/***************************************************************************/
static ssize_t
ocp81378_get_reg(struct device *cd, struct device_attribute *attr, char *buf)
{
	unsigned char reg_val;
	unsigned char i;
	ssize_t len = 0;

	for (i = 0; i < 0x0E; i++) {
		reg_val = ocp81378_read_reg(ocp81378_i2c_client, i);
		len += snprintf(buf+len, PAGE_SIZE-len,
			"reg0x%2X = 0x%2X\n", i, reg_val);
	}
	len += snprintf(buf+len, PAGE_SIZE-len, "\r\n");
	return len;
}

static ssize_t ocp81378_set_reg(struct device *cd,
		struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int databuf[2];

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2)
		ocp81378_write_reg(ocp81378_i2c_client, databuf[0], databuf[1]);
	return len;
}


static ssize_t ocp81378_store(struct device *cd,
		struct device_attribute *attr, const char *buf, size_t len)
{
	if(buf[0] == '0')
	{
		ocp81378_enable(OCP81378_CHANNEL_CH1);
	}
	else if(buf[0] == '1')
	{
		ocp81378_disable(OCP81378_CHANNEL_CH1);
	}
	else if(buf[0] == '2')
	{
		ocp81378_enable(OCP81378_CHANNEL_CH2);
	}
	else if(buf[0] == '3')
	{
		ocp81378_disable(OCP81378_CHANNEL_CH2);
	}

	return len;
}


static DEVICE_ATTR(reg, 0660, ocp81378_get_reg, ocp81378_set_reg);
static DEVICE_ATTR(set, 0660, NULL, ocp81378_store);

static int ocp81378_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);

	err = device_create_file(dev, &dev_attr_reg);
	
	err = device_create_file(dev, &dev_attr_set);
	
	return err;
}

static int ocp81378_parse_dt(struct device *dev,
		struct ocp81378_platform_data *pdata)
{
	struct device_node *np, *cnp;
	u32 decouple = 0;
	int i = 0;

	if (!dev || !dev->of_node || !pdata)
		return -ENODEV;

	np = dev->of_node;

	pdata->channel_num = of_get_child_count(np);
	if (!pdata->channel_num) {
		OCP81378_DEBUG("Parse no dt, node.\n");
		return 0;
	}
	OCP81378_DEBUG("Channel number(%d).\n", pdata->channel_num);

	if (of_property_read_u32(np, "decouple", &decouple))
		OCP81378_DEBUG("Parse no dt, decouple.\n");
	
	if (of_property_read_u32(np, "led_num", &led_num))
		OCP81378_DEBUG("Parse no dt, led_num.\n");

	pdata->dev_id = devm_kzalloc(dev,
			pdata->channel_num *
			sizeof(struct flashlight_device_id),
			GFP_KERNEL);
	if (!pdata->dev_id)
		return -ENOMEM;

	for_each_child_of_node(np, cnp) {
		if (of_property_read_u32(cnp, "type", &pdata->dev_id[i].type))
			goto err_node_put;
		if (of_property_read_u32(cnp, "ct", &pdata->dev_id[i].ct))
			goto err_node_put;
		if (of_property_read_u32(cnp, "part", &pdata->dev_id[i].part))
			goto err_node_put;
		snprintf(pdata->dev_id[i].name, FLASHLIGHT_NAME_SIZE,
				OCP81378_NAME);
		pdata->dev_id[i].channel = i;
		pdata->dev_id[i].decouple = decouple;

		OCP81378_DEBUG("Parse dt (type,ct,part,name,channel,decouple)=(%d,%d,%d,%s,%d,%d).\n",
				pdata->dev_id[i].type, pdata->dev_id[i].ct,
				pdata->dev_id[i].part, pdata->dev_id[i].name,
				pdata->dev_id[i].channel,
				pdata->dev_id[i].decouple);
		i++;
	}
	
	return 0;

err_node_put:
	of_node_put(cnp);
	return -EINVAL;
}

static int
ocp81378_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ocp81378_chip_data *chip;
	struct ocp81378_platform_data *pdata = client->dev.platform_data;
	int err;
	int i;

	OCP81378_DEBUG("%s Probe start.\n", __func__);

	/* check i2c */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("Failed to check i2c functionality.\n");
		err = -ENODEV;
		goto err_out;
	}

	/* init chip private data */
	chip = kzalloc(sizeof(struct ocp81378_chip_data), GFP_KERNEL);
	if (!chip) {
		err = -ENOMEM;
		goto err_out;
	}
	chip->client = client;

	/* init platform data */
	if (!pdata) {
		pr_err("Platform data does not exist\n");
		pdata =
		kzalloc(sizeof(struct ocp81378_platform_data), GFP_KERNEL);
		if (!pdata) {
			err = -ENOMEM;
			goto err_init_pdata;
		}
		chip->no_pdata = 1;
	}
	chip->pdata = pdata;

	err = ocp81378_parse_dt(&g_ocp81378_pdev->dev, chip->pdata);
	if (err)
		goto err_init_pdata;
	
	i2c_set_clientdata(client, chip);
	ocp81378_i2c_client = client;

	/* init mutex and spinlock */
	mutex_init(&chip->lock);

	/* init work queue */
	INIT_WORK(&ocp81378_work_ch1, ocp81378_work_disable_ch1);
	INIT_WORK(&ocp81378_work_ch2, ocp81378_work_disable_ch2);

	/* init timer */
	hrtimer_init(&ocp81378_timer_ch1, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ocp81378_timer_ch1.function = ocp81378_timer_func_ch1;
	hrtimer_init(&ocp81378_timer_ch2, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ocp81378_timer_ch2.function = ocp81378_timer_func_ch2;
	ocp81378_timeout_ms[OCP81378_CHANNEL_CH1] = 100;
	ocp81378_timeout_ms[OCP81378_CHANNEL_CH2] = 100;

	/* init chip hw */
	ocp81378_chip_init(chip);

	/* register flashlight device */
	if (pdata->channel_num) {
		for (i = 0; i < pdata->channel_num; i++) {
			if (flashlight_dev_register_by_device_id(&pdata->dev_id[i], &ocp81378_ops)) {
				err = -EFAULT;
				goto err_free;
			}
		}
	} else {
		if (flashlight_dev_register(OCP81378_NAME, &ocp81378_ops)) {
			err = -EFAULT;
			goto err_free;
		}
	}

	/* clear usage count */
	use_count = 0;

	ocp81378_create_sysfs(client);

	OCP81378_DEBUG("%s Probe done.\n", __func__);

	return 0;

err_free:
	kfree(chip->pdata);
err_init_pdata:
	i2c_set_clientdata(client, NULL);
	kfree(chip);
err_out:
	return err;
}

static void ocp81378_i2c_remove(struct i2c_client *client)
{
	struct ocp81378_chip_data *chip = i2c_get_clientdata(client);

	OCP81378_DEBUG("Remove start.\n");

	/* flush work queue */
	flush_work(&ocp81378_work_ch1);
	flush_work(&ocp81378_work_ch2);

	/* unregister flashlight operations */
	flashlight_dev_unregister(OCP81378_NAME);

	/* free resource */
	if (chip->no_pdata)
		kfree(chip->pdata);
	kfree(chip);

	OCP81378_DEBUG("Remove done.\n");
}

static const struct i2c_device_id ocp81378_i2c_id[] = {
	{OCP81378_NAME, 0},
	{}
};

#ifdef CONFIG_OF
static const struct of_device_id ocp81378_i2c_of_match[] = {
	{.compatible = OCP81378_DTNAME_I2C},
	{},
};
#endif

static struct i2c_driver ocp81378_i2c_driver = {
	.driver = {
		   .name = OCP81378_NAME,
#ifdef CONFIG_OF
		   .of_match_table = ocp81378_i2c_of_match,
#endif
		   },
	.probe = ocp81378_i2c_probe,
	.remove = ocp81378_i2c_remove,
	.id_table = ocp81378_i2c_id,
};

#if CONFIG_PRIZE_SOFT_LED_ENABLE
static volatile int cur_soft_torch_status = 0;

static ssize_t device_soft_torch_value_show(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
	printk("ocp81378 cur_soft_torch_status=%d\n", cur_soft_torch_status);
	return sprintf(buf, "%u\n", cur_soft_torch_status);
}
static ssize_t device_soft_torch_value_store(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t size)
{
	//ktime_t ktime;
	
	printk("ocp81378 %s ON/OFF value = %d:\n ", __func__, cur_soft_torch_status);
	if(sscanf(buf, "%u", &cur_soft_torch_status) != 1)
	{
		printk("ocp81378Invalid values\n");
		return -EINVAL;
	}
    
	if(1==cur_soft_torch_status)
	{
		//ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]=100;
		
		//ocp81378_set_level(OCP81378_CHANNEL_CH1, 1);
	  //printk("ocp81378_set_level[OCP81378_CHANNEL_CH1]=%d\n",ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]);
		//if (ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]) {
		//	printk("ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]=%d\n",ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]);
		//	ktime =ktime_set(ocp81378_timeout_ms[OCP81378_CHANNEL_CH1] / 1000,
		//	(ocp81378_timeout_ms[OCP81378_CHANNEL_CH1] % 1000) * 1000000);
		//	ocp81378_timer_start(OCP81378_CHANNEL_CH1, ktime);
			
		//   }
		   ocp81378_enable(OCP81378_CHANNEL_CH1);
		   ocp81378_set_level_ch1(6);
		
	}else{
			ocp81378_disable(OCP81378_CHANNEL_CH1);
			ocp81378_timer_cancel(OCP81378_CHANNEL_CH1);
			ocp81378_set_level_ch1(0);
		
	}
	return size;
}

static DEVICE_ATTR(soft_torch_value, 0664, device_soft_torch_value_show, device_soft_torch_value_store);

static volatile int cur_soft_torch2_status = 0;

static ssize_t device_soft_torch2_value_show(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
	printk("ocp81378 cur_soft_torch2_status=%d\n", cur_soft_torch2_status);
	return sprintf(buf, "%u\n", cur_soft_torch2_status);
}
static ssize_t device_soft_torch2_value_store(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t size)
{
	//ktime_t ktime;
	
	printk("ocp81378 %s ON/OFF value = %d:\n ", __func__, cur_soft_torch2_status);
	if(sscanf(buf, "%u", &cur_soft_torch2_status) != 1)
	{
		printk("ocp81378Invalid values\n");
		return -EINVAL;
	}
    
	if(1==cur_soft_torch2_status)
	{
		//ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]=100;
		
		//ocp81378_set_level(OCP81378_CHANNEL_CH1, 1);
	  //printk("ocp81378_set_level[OCP81378_CHANNEL_CH1]=%d\n",ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]);
		//if (ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]) {
		//	printk("ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]=%d\n",ocp81378_timeout_ms[OCP81378_CHANNEL_CH1]);
		//	ktime =ktime_set(ocp81378_timeout_ms[OCP81378_CHANNEL_CH1] / 1000,
		//	(ocp81378_timeout_ms[OCP81378_CHANNEL_CH1] % 1000) * 1000000);
		//	ocp81378_timer_start(OCP81378_CHANNEL_CH1, ktime);
			
		//   }
		   ocp81378_enable(OCP81378_CHANNEL_CH2);
		   ocp81378_set_level_ch2(6);
		
	}else{
			ocp81378_disable(OCP81378_CHANNEL_CH2);
			ocp81378_timer_cancel(OCP81378_CHANNEL_CH2);
			ocp81378_set_level_ch2(0);
		
	}
	return size;
}

static DEVICE_ATTR(soft_torch2_value, 0664, device_soft_torch2_value_show, device_soft_torch2_value_store);
#endif
 
/******************************************************************************
 * Platform device and driver
 *****************************************************************************/
#if CONFIG_PRIZE_SOFT_LED_ENABLE
 struct class *flash_soft_torch_class;
 struct class *flash_soft_torch2_class;
#endif 
 
static int ocp81378_probe(struct platform_device *dev)
{
    #if CONFIG_PRIZE_SOFT_LED_ENABLE
	struct device *flash_soft_torch_dev;
	struct device *flash_soft_torch2_dev;
	//struct device *flash_torch_dev;
    #endif
	g_ocp81378_pdev = dev;
	printk(" ocp81378 Probe start.\n");
	
	flashlight_gpio_init(g_ocp81378_pdev);

    #if CONFIG_PRIZE_SOFT_LED_ENABLE
	flash_soft_torch_class = class_create("flash_soft_torch");
    
	if (IS_ERR(flash_soft_torch_class)) {
		printk("Failed to create class(flash_soft_torch_class)!");
		return 0;
	}
	
	flash_soft_torch_dev = device_create(flash_soft_torch_class, NULL, 0, NULL, "flash_soft_torch_data");
	if (IS_ERR(flash_soft_torch_dev))
		printk("Failed to create flash_soft_torch_dev device");
	
	if (device_create_file(flash_soft_torch_dev, &dev_attr_soft_torch_value) < 0)
		printk("Failed to create device file(%s)!",dev_attr_soft_torch_value.attr.name);

	flash_soft_torch2_class = class_create("flash_soft_torch2");
    
	if (IS_ERR(flash_soft_torch2_class)) {
		printk("Failed to create class(flash_soft_torch2_class)!");
		return 0;
	}
	
	flash_soft_torch2_dev = device_create(flash_soft_torch2_class, NULL, 0, NULL, "flash_soft_torch2_data");
	if (IS_ERR(flash_soft_torch2_dev))
		printk("Failed to create flash_soft_torch2_dev device");
	
	if (device_create_file(flash_soft_torch2_dev, &dev_attr_soft_torch2_value) < 0)
		printk("Failed to create device file(%s)!",dev_attr_soft_torch2_value.attr.name);
	#endif
	
	if (i2c_add_driver(&ocp81378_i2c_driver)) {
		pr_err("Failed to add i2c driver.\n");
		return -1;
	}

	pr_info("%s Probe done.\n", __func__);

	return 0;
}

static int ocp81378_remove(struct platform_device *dev)
{
	pr_info("Remove start.\n");

	i2c_del_driver(&ocp81378_i2c_driver);

	pr_info("Remove done.\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ocp81378_of_match[] = {
	{.compatible = OCP81378_DTNAME},
	{},
};
MODULE_DEVICE_TABLE(of, ocp81378_of_match);
#else
static struct platform_device ocp81378_platform_device[] = {
	{
		.name = OCP81378_NAME,
		.id = 0,
		.dev = {}
	},
	{}
};
MODULE_DEVICE_TABLE(platform, ocp81378_platform_device);
#endif

static struct platform_driver ocp81378_platform_driver = {
	.probe = ocp81378_probe,
	.remove = ocp81378_remove,
	.driver = {
		.name = OCP81378_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = ocp81378_of_match,
#endif
	},
};

static int __init flashlight_ocp81378_init(void)
{
	int ret;

	pr_info("%s driver version %s.\n", __func__, OCP81378_DRIVER_VERSION);

#ifndef CONFIG_OF
	ret = platform_device_register(&ocp81378_platform_device);
	if (ret) {
		pr_err("Failed to register platform device\n");
		return ret;
	}
#endif

	ret = platform_driver_register(&ocp81378_platform_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		return ret;
	}

	pr_info("flashlight_ocp81378 Init done.\n");

	return 0;
}

static void __exit flashlight_ocp81378_exit(void)
{
	pr_info("flashlight_ocp81378-Exit start.\n");

	platform_driver_unregister(&ocp81378_platform_driver);

	pr_info("flashlight_ocp81378 Exit done.\n");
}

module_init(flashlight_ocp81378_init);
module_exit(flashlight_ocp81378_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Awinic");
MODULE_DESCRIPTION("OCP81378 Flashlight Driver");

