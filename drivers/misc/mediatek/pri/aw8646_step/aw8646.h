/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AW8646_H_
#define _AW8646_H_

#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/platform_device.h>

#define AW8646_MAX_FREQUENCY			250000
#define AW8646_DEFAULT_FREQUENCY		1000

struct aw8646 {
	struct mutex lock;
	struct device *dev;
	struct hrtimer hrtimer;
	struct platform_device *pdev;

	ktime_t kinterval;
	uint32_t step_num;
	uint32_t timer_cnt;
	uint32_t half_period;
	uint32_t step_frequency;

	int dvr8846_boost_en_pin;
	int nen_pin;		/* driver enable control input pin */
	int dir_pin;		/* direction input pin */
	int step_pin;		/* step input pin */
	int nsleep_pin;		/* sleep mode input pin */
	int dir_pin_state;
};

#define DRIVER_NAME				"aw8646_step"

#define AW_LOGI(format, ...) \
	pr_info("[%s][%04d]%s: " format "\n", DRIVER_NAME, __LINE__, __func__, ##__VA_ARGS__)

#define AW_LOGE(format, ...) \
	pr_err("[%s][%04d]%s: " format "\n", DRIVER_NAME, __LINE__, __func__, ##__VA_ARGS__)

enum aw_gpio_state {
	AW_GPIO_LOW = 0,
	AW_GPIO_HIGH = 1,
};

#endif
