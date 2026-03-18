/*
* Copyright (C) 2021 Awinic Inc.
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
#define pr_fmt(fmt)	"[aw3641e]:%s:%d " fmt, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
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
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/gpio.h>
// #include <media/soc_camera.h>
#include <linux/gpio_keys.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include "flashlight.h"
#include "flashlight-dt.h"
#include "flashlight-core.h"

#if IS_ENABLED(CONFIG_MTK_FLASHLIGHT)
#include "flashlight-core.h"

#include <linux/power_supply.h>
#endif

#define AW3641E_DRIVER_VERSION "V1.1.10"

#define CONFIG_OF				1
#define AW3641E_NSEC_TIME		1000
#define AW3641E_NAME			"aw3641e"

#define AW3641E_CHANNEL_NUM		1
#define AW3641E_CHANNEL_CH1		0
#define AW3641E_LEVEL_FLASH		16
#define AW3641E_LEVEL_TORCH		6

#define AW3641E_FLASH_BRT_MIN		300000
#define AW3641E_FLASH_BRT_STEP		100000
#define AW3641E_FLASH_BRT_MAX		1000000

#define AW3641E_FLASH_TOUT_MIN		220
#define AW3641E_FLASH_TOUT_STEP		100
#define AW3641E_FLASH_TOUT_MAX		1300

#define AW3641E_TORCH_BRT_MIN		10000
#define AW3641E_TORCH_BRT_STEP		10000
#define AW3641E_TORCH_BRT_MAX		100000

struct aw3641e_flash {
	struct device *dev;

	struct device_node *dnode;
	enum v4l2_flash_led_mode led_mode;
	struct v4l2_ctrl_handler ctrls_led;
	struct v4l2_subdev subdev_led;

#if IS_ENABLED(CONFIG_MTK_FLASHLIGHT)
	struct flashlight_device_id flash_dev_id;
#endif
};

static struct aw3641e_flash *g_flash_data;

#define to_platform_dev(d) container_of(d, struct platform_device, dev)
#define to_aw3641e_flash(_ctrl)	\
	container_of(_ctrl->handler, struct aw3641e_flash, ctrls_led)

static struct hrtimer timer_close;
static struct work_struct aw3641e_work;
static unsigned int flash_en_gpio;
static unsigned int flash_sel_gpio;
static int flash_level = -1;
static spinlock_t aw3641e_lock;

static unsigned int aw3641e_timeout_ms[AW3641E_CHANNEL_NUM];
static const unsigned char aw3641e_torch_level[AW3641E_LEVEL_TORCH+1] = {6, 7, 8, 9, 10, 11,12};
static const unsigned char aw3641e_flash_level[AW3641E_LEVEL_FLASH + 1] = {
					0, 1, 2, 3, 4, 5, 6, 7, 8,
					9, 10, 11, 12, 13, 14, 15, 16};

static void aw3641e_torch_on(void);
static void aw3641e_flash_on(void);
static void aw3641e_flash_off(void);

static int aw3641e_is_torch(int level)
{
	pr_info("%s.\n", __func__);

	if (level > AW3641E_LEVEL_TORCH)
		return -1;

	return 0;
}

static int aw3641e_verify_level(int level)
{
	pr_info("%s.\n", __func__);

	if (level <= 0)
		level = 0;
	else if (level >= AW3641E_LEVEL_FLASH)
		level = AW3641E_LEVEL_FLASH;

	return level;
}

void aw3641e_torch_on(void)
{
	int i = 0;
	pr_info("%s.\n", __func__);

	gpio_set_value(flash_sel_gpio, 0);
	
	for (i = 0; i < aw3641e_torch_level[flash_level] - 1; i++) {
		gpio_set_value(flash_en_gpio, 1);
		udelay(2);
		gpio_set_value(flash_en_gpio, 0);
		udelay(2);
	}
}

void aw3641e_flash_off(void)
{
	pr_info("%s.\n", __func__);

	gpio_set_value(flash_sel_gpio, 0);
    gpio_set_value(flash_en_gpio, 0);
	udelay(20);
}

void aw3641e_flash_on(void)
{
	int i = 0;

	pr_info("%s.flash_level = %d.\n", __func__, flash_level);

	gpio_set_value(flash_sel_gpio, 1);
	for (i = 0; i < aw3641e_flash_level[flash_level] - 1; i++) {
		gpio_set_value(flash_en_gpio, 1);
		udelay(2);
		gpio_set_value(flash_en_gpio, 0);
		udelay(2);
	}
	gpio_set_value(flash_en_gpio, 1);
}
static void aw3641e_work_disable(struct work_struct *data)
{
	pr_info("%s.\n", __func__);
	aw3641e_flash_off();
}

static enum hrtimer_restart aw3641e_timer_close(struct hrtimer *timer)
{
	schedule_work(&aw3641e_work);
	return HRTIMER_NORESTART;
}

int aw3641e_timer_start(int channel, ktime_t ktime)
{
	pr_info("%s.\n", __func__);

	if (channel == AW3641E_CHANNEL_CH1)
		hrtimer_start(&timer_close, ktime, HRTIMER_MODE_REL);
	else {
		pr_err("%s, Error channel\n", __func__);
		return -1;
	}

	return 0;
}

int aw3641e_timer_cancel(int channel)
{
	pr_info("%s.\n", __func__);

	if (channel == AW3641E_CHANNEL_CH1)
		hrtimer_cancel(&timer_close);
	else {
		pr_err("Error channel\n");
		return -EINVAL;
	}

	return 0;
}

void aw3641e_enable(int channel)
{
	pr_info("%s,flash_level = %d.\n", __func__, flash_level);

	if (!aw3641e_is_torch(flash_level)) {
		/* torch mode */
		aw3641e_torch_on();
	} else {
		/* flash mode */
		aw3641e_flash_on();
	}

}
void aw3641e_disable(int channel)
{
	pr_info("%s.\n", __func__);

	if (channel == AW3641E_CHANNEL_CH1) {
		aw3641e_flash_off();
	} else {
		pr_err("%s, Error channel\n", __func__);
		return;
	}
}
static int aw3641e_set_level(int channel, int level)
{
	pr_info("%s.\n", __func__);

	if (channel == AW3641E_CHANNEL_CH1) {
		flash_level =  aw3641e_flash_level[aw3641e_verify_level(level)];
	} else {
		pr_err("%s, Error channel\n", __func__);
		return -EINVAL;
	}

	return 0;
}
/****************************************************************
 *		v4l2 interface				*
 ****************************************************************/
static void aw3641e_v4l2_subdev_init(struct v4l2_subdev *sd,
				     struct platform_device *pdev,
				     const struct v4l2_subdev_ops *ops)
{
	int ret = 0;

	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_IS_I2C;
	sd->owner = pdev->dev.driver->owner;
	sd->dev = &pdev->dev;
	/* i2c_client and v4l2_subdev point to one another */
	v4l2_set_subdevdata(sd, pdev);
	platform_set_drvdata(pdev, sd);
	/* initialize name */
	ret = snprintf(sd->name, sizeof(sd->name), "%s",
		pdev->dev.driver->name);
	pr_info("ret = %d\n", ret);
	if (ret < 0)
		pr_info("snprintf failed\n");
}

static const struct v4l2_subdev_ops aw3641e_v4l2_subdev_ops = {
	.core = NULL,
};

static int aw3641e_v4l2_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pr_info("%s\n", __func__);

	return 0;
}

static int aw3641e_v4l2_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pr_info("%s\n", __func__);

	aw3641e_disable(AW3641E_CHANNEL_CH1);

	return 0;
}

static const struct v4l2_subdev_internal_ops aw3641e_v4l2_int_ops = {
	.open = aw3641e_v4l2_open,
	.close = aw3641e_v4l2_close,
};

static int aw3641e_mode_ctrl(struct aw3641e_flash *flash)
{
	int rval = 0;

	pr_info("%s mode:%d", __func__, flash->led_mode);
	switch (flash->led_mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		aw3641e_flash_off();
		break;
	case V4L2_FLASH_LED_MODE_TORCH:
		aw3641e_torch_on();
		break;
	case V4L2_FLASH_LED_MODE_FLASH:
		aw3641e_flash_on();
		break;
	}
	return rval;
}

static int aw3641e_get_ctrl(struct v4l2_ctrl *ctrl)
{
	s32 fault = 0;

	if (ctrl->id == V4L2_CID_FLASH_FAULT)
		ctrl->cur.val = fault;

	return 0;
}

static int aw3641e_set_ctrl(struct v4l2_ctrl *ctrl)
{
	int rval = 0;
	int level;
	ktime_t ktime;

	struct aw3641e_flash *flash = to_aw3641e_flash(ctrl);

	pr_info("%s ID:%d", __func__, ctrl->id);

	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		printk("enter V4L2_CID_FLASH_LED_MODE");
		flash->led_mode = ctrl->val;
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			rval = aw3641e_mode_ctrl(flash);
		break;

	case V4L2_CID_FLASH_STROBE_SOURCE:
		break;

	case V4L2_CID_FLASH_STROBE:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			rval = -EBUSY;
			goto err_out;
		}
		flash->led_mode = V4L2_FLASH_LED_MODE_FLASH;
		ktime = ktime_set(aw3641e_timeout_ms[AW3641E_CHANNEL_CH1] / 1000,
			(aw3641e_timeout_ms[AW3641E_CHANNEL_CH1] % 1000) * 1000000);
		aw3641e_timer_start(AW3641E_CHANNEL_CH1, ktime);
		rval = aw3641e_mode_ctrl(flash);
		break;

	case V4L2_CID_FLASH_STROBE_STOP:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			rval = -EBUSY;
			goto err_out;
		}
		flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
		rval = aw3641e_mode_ctrl(flash);
		break;

	case V4L2_CID_FLASH_TIMEOUT:
		if (ctrl->val < 1300)
			level = 1;
		else
			level = 9;
		aw3641e_set_level(AW3641E_CHANNEL_CH1, level);
		break;

	case V4L2_CID_FLASH_INTENSITY:
		if (ctrl->val > 1000000)
			level = 1;
		else
			level = 8 - ((ctrl->val - 300000) / 100000);
		aw3641e_set_level(AW3641E_CHANNEL_CH1, level);
		break;

	case V4L2_CID_FLASH_TORCH_INTENSITY:
		break;
	}

err_out:
	return rval;
}

static const struct v4l2_ctrl_ops aw3641e_v4l2_ctrl_ops = {
	.g_volatile_ctrl = aw3641e_get_ctrl,
	.s_ctrl = aw3641e_set_ctrl,
};

static int aw3641e_init_controls(struct aw3641e_flash *flash)
{
	struct v4l2_ctrl *fault;
	struct v4l2_ctrl_handler *hdl = &flash->ctrls_led;
	const struct v4l2_ctrl_ops *ops = &aw3641e_v4l2_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 8);

	/* flash mode */
	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_FLASH_LED_MODE,
			       V4L2_FLASH_LED_MODE_TORCH, ~0x7,
			       V4L2_FLASH_LED_MODE_NONE);
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;

	/* flash source */
	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_FLASH_STROBE_SOURCE,
			       0x1, ~0x3, V4L2_FLASH_STROBE_SOURCE_EXTERNAL);

	/* flash strobe */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_STROBE, 0, 0, 0, 0);

	/* flash strobe stop */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_STROBE_STOP, 0, 0, 0, 0);

	/* flash strobe timeout */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_TIMEOUT,
			  AW3641E_FLASH_TOUT_MIN, AW3641E_FLASH_TOUT_MAX,
			  AW3641E_FLASH_TOUT_STEP, AW3641E_FLASH_TOUT_MAX);

	/* flash brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_INTENSITY,
			  AW3641E_FLASH_BRT_MIN, AW3641E_FLASH_BRT_MAX,
			  AW3641E_FLASH_BRT_STEP, AW3641E_FLASH_BRT_MAX);

	/* torch brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_TORCH_INTENSITY,
			  AW3641E_TORCH_BRT_MIN, AW3641E_TORCH_BRT_MAX,
			  AW3641E_TORCH_BRT_STEP, AW3641E_TORCH_BRT_MAX);

	/* fault */
	fault = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_FAULT, 0,
				  V4L2_FLASH_FAULT_OVER_VOLTAGE
				  | V4L2_FLASH_FAULT_OVER_TEMPERATURE
				  | V4L2_FLASH_FAULT_SHORT_CIRCUIT
				  | V4L2_FLASH_FAULT_TIMEOUT, 0, 0);
	if (fault != NULL)
		fault->flags |= V4L2_CTRL_FLAG_VOLATILE;
	pr_info("hdl->error=%d\n", hdl->error);

	if (hdl->error)
		return hdl->error;

	flash->subdev_led.ctrl_handler = hdl;
	return 0;
}

static int aw3641e_subdev_init(struct aw3641e_flash *flash,
			       char *led_name)
{
	struct platform_device *pdev = to_platform_dev(flash->dev);
	struct device_node *np = flash->dev->of_node, *child;
	const char *fled_name = "flash";
	int rval = 0;

	aw3641e_v4l2_subdev_init(&flash->subdev_led, pdev, &aw3641e_v4l2_subdev_ops);
	flash->subdev_led.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	flash->subdev_led.internal_ops = &aw3641e_v4l2_int_ops;
	strscpy(flash->subdev_led.name, led_name, sizeof(flash->subdev_led.name));

	for (child = of_get_child_by_name(np, fled_name); child;
			child = of_find_node_by_name(child, fled_name)) {
		int rv;
		u32 reg = 0;

		rv = of_property_read_u32(child, "reg", &reg);
		if (rv)
			continue;

		flash->dnode = child;
		flash->subdev_led.fwnode = of_fwnode_handle(flash->dnode);
	}

	rval = aw3641e_init_controls(flash);
	if (rval)
		goto err_out;
	rval = media_entity_pads_init(&flash->subdev_led.entity, 0, NULL);
	if (rval < 0)
		goto err_out;
	flash->subdev_led.entity.function = MEDIA_ENT_F_FLASH;

	rval = v4l2_async_register_subdev(&flash->subdev_led);
	if (rval < 0)
		goto err_out;
	pr_info("v4l2_async_register_subdev finished\n");

	return rval;

err_out:
	v4l2_ctrl_handler_free(&flash->ctrls_led);
	return rval;
}

/****************************************************************
*		flashlights platform interface			*
*****************************************************************/
static int aw3641e_open(void)
{
	return 0;
}

static int aw3641e_release(void)
{
	hrtimer_cancel(&timer_close);
	return 0;
}

static int aw3641e_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;
	ktime_t ktime;

	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	pr_info("%s.\n", __func__);
	/* modify flash level of fl_arg->arg acrodding your level for test */
	/*
	if (fl_arg->arg == 1)
		fl_arg->arg = 10;
	*/
	/* verify channel */
	if (channel < 0 || channel >= AW3641E_CHANNEL_NUM) {
		pr_err("Failed with error channel\n");
		return -EINVAL;
	}

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		pr_info("%s, FLASH_IOC_SET_TIME_OUT_TIME_MS(%d): %d\n",
				__func__, channel, (int)fl_arg->arg);
		aw3641e_timeout_ms[channel] = fl_arg->arg;
		break;

	case FLASH_IOC_SET_DUTY:
		pr_info("%s,FLASH_IOC_SET_DUTY(%d): %d\n",
				__func__, channel, (int)fl_arg->arg);
		aw3641e_set_level(channel, fl_arg->arg);
		break;

	case FLASH_IOC_SET_ONOFF:
		pr_info("%s,FLASH_IOC_SET_ONOFF(%d): %d\n",
				__func__, channel, (int)fl_arg->arg);
		if (fl_arg->arg >= 1) {
			if (aw3641e_timeout_ms[channel]) {
				ktime =
				ktime_set(aw3641e_timeout_ms[channel] / 1000,
				(aw3641e_timeout_ms[channel] % 1000) * 1000000);
				aw3641e_timer_start(channel, ktime);
			}
			spin_lock(&aw3641e_lock);
			aw3641e_enable(channel);
			spin_unlock(&aw3641e_lock);
			g_flash_data->led_mode = V4L2_FLASH_LED_MODE_TORCH;
		} else {
			if (g_flash_data->led_mode != V4L2_FLASH_LED_MODE_NONE) {
				g_flash_data->led_mode = V4L2_FLASH_LED_MODE_NONE;
				aw3641e_mode_ctrl(g_flash_data);
			}
			aw3641e_disable(channel);
			aw3641e_timer_cancel(channel);
		}
		break;

	default:
		pr_info("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

int aw3641e_set_driver(int set)
{
	/* init chip and set usage count */
	return 0;
}

static ssize_t aw3641e_strobe_store(struct flashlight_arg arg)
{
	aw3641e_set_level(arg.ct, arg.level);
	return 0;
}

static struct flashlight_operations aw3641e_ops = {
	aw3641e_open,
	aw3641e_release,
	aw3641e_ioctl,
	aw3641e_strobe_store,
	aw3641e_set_driver
};

static int aw3641e_parse_dt(struct aw3641e_flash *flash)
{
	struct device_node *np, *cnp;
	struct device *dev = flash->dev;
	u32 decouple = 0;
	int i = 0;

	if (!dev || !dev->of_node)
		return -ENODEV;

	np = dev->of_node;
	for_each_child_of_node(np, cnp) {
		if (of_property_read_u32(cnp, "type",
			&flash->flash_dev_id.type))
			goto err_node_put;
		if (of_property_read_u32(cnp,
			"ct", &flash->flash_dev_id.ct))
			goto err_node_put;
		if (of_property_read_u32(cnp,
			"part", &flash->flash_dev_id.part))
			goto err_node_put;
		snprintf(flash->flash_dev_id.name, FLASHLIGHT_NAME_SIZE,
				flash->subdev_led.name);
		flash->flash_dev_id.channel = i;
		flash->flash_dev_id.decouple = decouple;

		pr_info("Parse dt (type,ct,part,name,channel,decouple)=(%d,%d,%d,%s,%d,%d).\n",
				flash->flash_dev_id.type,
				flash->flash_dev_id.ct,
				flash->flash_dev_id.part,
				flash->flash_dev_id.name,
				flash->flash_dev_id.channel,
				flash->flash_dev_id.decouple);
		if (flashlight_dev_register_by_device_id(&flash->flash_dev_id,
			&aw3641e_ops))
			return -EFAULT;
		i++;
	}

	return 0;

err_node_put:
	of_node_put(cnp);
	return -EINVAL;
}


/********************************************************************
*			driver probe
*********************************************************************/
static int aw3641e_probe(struct platform_device *dev)
{
	struct device_node *node = dev->dev.of_node;
	struct aw3641e_flash *flash;
	int rval = 0;

	pr_info("%s Probe start.\n", __func__);

	flash = devm_kzalloc(&dev->dev, sizeof(*flash), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;

	flash->dev = &dev->dev;
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
	g_flash_data = flash;

	flash_en_gpio = of_get_named_gpio(node, "flash-en-gpio", 0);
	if ((!gpio_is_valid(flash_en_gpio))) {
		pr_err("%s: dts don't provide flash_en_gpio\n", __func__);
		return -EINVAL;
	}
	pr_info("%s prase dts with flash_en_gpio success.\n", __func__);

	flash_sel_gpio = of_get_named_gpio(node, "flash-sel-gpio", 0);
	if ((!gpio_is_valid(flash_sel_gpio))) {
		pr_err("%s: dts don't provide flash_sel_gpio\n", __func__);
		return -EINVAL;
	}
	pr_info("%s prase dts with flash_sel_gpio success.\n", __func__);

	if (devm_gpio_request_one(&dev->dev, flash_en_gpio,
				  GPIOF_DIR_OUT | GPIOF_INIT_LOW,
		      "Flash-En")) {
		pr_err("%s, gpio Flash-En failed\n", __func__);
		return -1;
	}
	if (devm_gpio_request_one(&dev->dev, flash_sel_gpio,
				  GPIOF_DIR_OUT | GPIOF_INIT_LOW,
		      "Flash-SEL")) {
		pr_err("%s, gpio Flash-SEL failed\n", __func__);
		goto err_gpio;
	}

	/* init work queue */
	INIT_WORK(&aw3641e_work, aw3641e_work_disable);

	spin_lock_init(&aw3641e_lock);

	/* init timer close */
	hrtimer_init(&timer_close, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer_close.function = aw3641e_timer_close;
	aw3641e_timeout_ms[AW3641E_CHANNEL_CH1] = 100;

	aw3641e_subdev_init(flash, "aw3641e-led");

	rval = aw3641e_parse_dt(flash);
	if (rval) {
		pr_err("Failed to register flashlight device.\n");
		goto err_free;
	}

	platform_set_drvdata(dev, flash);

	pr_info("%s Probe finish.\n", __func__);

	return 0;
err_gpio:
	devm_gpio_free(&dev->dev, flash_en_gpio);
err_free:
	devm_gpio_free(&dev->dev, flash_sel_gpio);

	return -EINVAL;
}
static int aw3641e_remove(struct platform_device *dev)
{
	struct aw3641e_flash *flash = platform_get_drvdata(dev);

	flashlight_dev_unregister(AW3641E_NAME);
	devm_gpio_free(&dev->dev, flash_en_gpio);
	devm_gpio_free(&dev->dev, flash_sel_gpio);
	v4l2_device_unregister_subdev(&flash->subdev_led);
	v4l2_ctrl_handler_free(&flash->ctrls_led);
	media_entity_cleanup(&flash->subdev_led.entity);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id aw3641e_of_match[] = {
	{.compatible = "awinic,aw3641e"},
	{},
};
MODULE_DEVICE_TABLE(of, aw3641e_of_match);
#else
static struct platform_device aw3641e_platform_device[] = {
	{
		.name = AW3641E_NAME,
		.id = 0,
		.dev = {}
	},
	{}
};
MODULE_DEVICE_TABLE(platform, aw3641e_platform_device);
#endif

static struct platform_driver aw3641e_platform_driver = {
	.probe = aw3641e_probe,
	.remove = aw3641e_remove,
	.driver = {
		.name = AW3641E_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = aw3641e_of_match,
#endif
	},
};

static int __init aw3641e_flash_init(void)
{
	int ret = 0;

	pr_info("%s driver version %s.\n", __func__, AW3641E_DRIVER_VERSION);

#ifndef CONFIG_OF
	ret = platform_device_register(&aw3641e_platform_device);
	if (ret) {
		pr_err("%s,Failed to register platform device\n", __func__);
		return ret;
	}
#endif
	ret = platform_driver_register(&aw3641e_platform_driver);
	if (ret) {
		pr_err("%s,Failed to register platform driver\n", __func__);
		return ret;
	}
	return 0;
}

static void __exit aw3641e_flash_exit(void)
{
	pr_info("%s.\n", __func__);

	platform_driver_unregister(&aw3641e_platform_driver);
}

module_init(aw3641e_flash_init);
module_exit(aw3641e_flash_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("shiqiang@awinic.com");
MODULE_DESCRIPTION("GPIO Flash driver");

