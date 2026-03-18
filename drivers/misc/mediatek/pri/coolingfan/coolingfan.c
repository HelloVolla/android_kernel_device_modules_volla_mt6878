#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <media/rc-core.h>

#include <mt-plat/mtk_pwm.h>
#include <mt-plat/mtk_pwm_hal.h>

#define coolingfan_DEBUG
#ifdef coolingfan_DEBUG
#define coolingfan_log(fmt,arg...) \
	do {\
		printk("<<coolingfan-drv>>[%d]"fmt"", __LINE__, ##arg);\
	} while(0)
#endif

static struct pinctrl *coolingfan_pinctrl;
static struct pinctrl_state *coolingfan_set_low;
static struct pinctrl_state *coolingfan_set_high;
static struct pinctrl_state *coolingfan_set_pwm;
static struct pinctrl_state *coolingfan_boost_en_low;
static struct pinctrl_state *coolingfan_boost_en_high;
struct class *coolingfan_class;
static int coolingfan_level = 0;
static int coolingfan_set_level_state = 1; //default 1 //1:allow thread set level; 0:not allowed thread set level
static int g_pwm_num = -1;
static int g_pwm_max_level = -1;
static struct pwm_spec_config pwm_setting;

#define COOLINGFAN_LEVEL_INIT_LEVEL 128
#define TEMP_THRESHOLD 45
#define TEMP_THRESHOLD_DIFF_COEFFICIENT 15
#define TEMP_LAST_DIFF_COEFFICIENT 5
#define TEMP_THRE_EXCEED_COUNT_COEFFICIENT 1

static atomic_t coolingfan_pwm_state = ATOMIC_INIT(0);
static struct task_struct *coolingfan_thread;
static DEFINE_MUTEX(coolingfan_lock);
static int temp_old = -1;
static int temp_cur = -1;
static int temp_threshold_diff = -1;
static int temp_last_diff = -1;

extern int g_temperature5;
extern int g_temperature6;
extern int g_temperature7;
extern atomic_t dlp_state;

static int prize_coolingfan_set_enable(int pwm_num, int level)
{
	// coolingfan_log("%s %d : level=%d, pwm_no=%d\n", __func__, __LINE__, level, pwm_num);

	pwm_setting.pwm_no = pwm_num;
	pwm_setting.mode = PWM_MODE_OLD;
	/* We won't choose 32K to be the clock src of old mode because of system performance. */
	/* The setting here will be clock src = 26MHz, CLKSEL = 26M/1625 (i.e. 16K) */
	pwm_setting.clk_src = PWM_CLK_OLD_MODE_BLOCK; //prize  PWM_CLK_OLD_MODE_32K PWM_CLK_OLD_MODE_BLOCK 26M PWM_CLK_NEW_MODE_BLOCK PWM_CLK_NEW_MODE_BLOCK_DIV_BY_1625
	pwm_setting.clk_div = CLK_DIV4;
	pwm_setting.pmic_pad = 0;

	if (level == 0) {
		if (atomic_read(&coolingfan_pwm_state)) {
			mt_pwm_disable(pwm_num, 0);
		}
		if (coolingfan_set_low) {
			pinctrl_select_state(coolingfan_pinctrl, coolingfan_set_low); //set gpio low
		}
		if (coolingfan_boost_en_low) {
			pinctrl_select_state(coolingfan_pinctrl, coolingfan_boost_en_low); //set bootst en low
		}
		atomic_set(&coolingfan_pwm_state, 0);
	} else if (level == (g_pwm_max_level+1)) {
		if (atomic_read(&coolingfan_pwm_state)) {
			mt_pwm_disable(pwm_num, 0);
		}
		if (coolingfan_set_high) {
			pinctrl_select_state(coolingfan_pinctrl, coolingfan_set_high); //set gpio hign
		}
		if (coolingfan_boost_en_high) {
			pinctrl_select_state(coolingfan_pinctrl, coolingfan_boost_en_high); //set bootst en high
		}
		atomic_set(&coolingfan_pwm_state, 0);
	} else if ((level > 0) && (level <= g_pwm_max_level)) {
		if (coolingfan_boost_en_high) {
			pinctrl_select_state(coolingfan_pinctrl, coolingfan_boost_en_high); //set bootst en high
		}
		// Freq=clk_src/clk_div/(2*(DATA_WIDTH+1))
		// Õ¼¿Õ±È=(THRESH+1)/(2*(DATA_WIDTH+1))£¬when GUARD_VALUE=0
		pinctrl_select_state(coolingfan_pinctrl, coolingfan_set_pwm);  //pwm mode
		pwm_setting.PWM_MODE_OLD_REGS.THRESH = level;
		// pwm_setting.clk_div = CLK_DIV1;
		pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 260; //25khz
		pwm_setting.PWM_MODE_OLD_REGS.IDLE_VALUE = 0;
		pwm_setting.PWM_MODE_OLD_REGS.GUARD_VALUE = 0;
		pwm_setting.PWM_MODE_OLD_REGS.GDURATION = 0;
		pwm_setting.PWM_MODE_OLD_REGS.WAVE_NUM = 0;
		pwm_set_spec_config(&pwm_setting);
		atomic_set(&coolingfan_pwm_state, 1);
	}

	// coolingfan_log("%s %d : pwm_setting.PWM_MODE_OLD_REGS.THRESH=%d,pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH=%d\n", 
		// __func__, __LINE__, pwm_setting.PWM_MODE_OLD_REGS.THRESH, pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH);
	return 0;
}

static ssize_t coolingfan_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return sprintf(buf, "%d\n", coolingfan_level);
}

static ssize_t coolingfan_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	if (sscanf(buf, "%u", &coolingfan_level) != 1) {
		coolingfan_log("[coolingfan_dev]: Invalid values\n");
		return -EINVAL;
	}

	coolingfan_log("[coolingfan_dev] %s coolingfan_level value = %d [0:OFF ; mode 1~3 ; other:Singular set thresh, Even numbers set data_width ]\n ", 
		__func__, coolingfan_level);

	if (coolingfan_level >= 0) {
		prize_coolingfan_set_enable(g_pwm_num, coolingfan_level);
	} else {
		coolingfan_log("[coolingfan_dev]: Invalid values\n");
		return -EINVAL;
	}

	return size;
}
static DEVICE_ATTR(coolingfan, 0664, coolingfan_show, coolingfan_store);

static ssize_t coolingfan_set_level_state_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return sprintf(buf, "%d\n", coolingfan_set_level_state);
}

static ssize_t coolingfan_set_level_state_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	if (sscanf(buf, "%u", &coolingfan_set_level_state) != 1) {
		coolingfan_log("[coolingfan_dev]: Invalid values\n");
		return -EINVAL;
	}

	coolingfan_log("[coolingfan_dev] %s coolingfan_set_level_state value = %d\n ", __func__, coolingfan_set_level_state);

	return size;
}
static DEVICE_ATTR(coolingfan_set_level_state, 0664, coolingfan_set_level_state_show, coolingfan_set_level_state_store);

int coolingfan_dts(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	if (of_property_read_u32(np, "pwm_num", &g_pwm_num) >= 0) {
		coolingfan_log("coolingfan of_property_read_u32 pwm_num=%d\n", g_pwm_num);
	}

	if (of_property_read_u32(np, "pwm_max_level", &g_pwm_max_level) >= 0) {
		coolingfan_log("coolingfan of_property_read_u32 pwm_max_level=%d\n", g_pwm_max_level);
	}

	coolingfan_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR(coolingfan_pinctrl)) {
		coolingfan_set_low = pinctrl_lookup_state(coolingfan_pinctrl, "coolingfan_set_low");
		if (IS_ERR(coolingfan_set_low)){
			coolingfan_log("coolingfan get pinctrl state coolingfan_set_low fail\n");
			return -EINVAL;
		}

		coolingfan_set_high = pinctrl_lookup_state(coolingfan_pinctrl, "coolingfan_set_high");
		if (IS_ERR(coolingfan_set_high)){
			coolingfan_log("coolingfan get pinctrl state coolingfan_set_high fail\n");
			return -EINVAL;
		}

		coolingfan_set_pwm = pinctrl_lookup_state(coolingfan_pinctrl, "coolingfan_set_pwm");
		if (IS_ERR(coolingfan_set_pwm)){
			coolingfan_log("coolingfan get pinctrl state coolingfan_set_pwm fail\n");
			return -EINVAL;
		}

		coolingfan_boost_en_low = pinctrl_lookup_state(coolingfan_pinctrl, "coolingfan_boost_en_low");
		if (IS_ERR(coolingfan_boost_en_low)){
			coolingfan_log("coolingfan get pinctrl state coolingfan_boost_en_low fail\n");
		}

		coolingfan_boost_en_high = pinctrl_lookup_state(coolingfan_pinctrl, "coolingfan_boost_en_high");
		if (IS_ERR(coolingfan_boost_en_high)){
			coolingfan_log("coolingfan get pinctrl state coolingfan_boost_en_high fail\n");
		}
	} else {
		coolingfan_log("coolingfan get pinctrl fail\n");
		return -EINVAL;
	}

	if (coolingfan_set_low) {
		pinctrl_select_state(coolingfan_pinctrl, coolingfan_set_low); //set gpio low
	}
	if (coolingfan_boost_en_low) {
		pinctrl_select_state(coolingfan_pinctrl, coolingfan_boost_en_low); //set bootst en low
	}
	return 0;
}

static int coolingfan_thread_fn(void *data)
{
	static int coolingfan_fn_level, temp_thre_exceed_count = 0;
	while (!kthread_should_stop()) {
		msleep(500);
		if (atomic_read(&dlp_state)) {
			mutex_lock(&coolingfan_lock);
			temp_cur = max3(g_temperature5, g_temperature6, g_temperature7);
			if (temp_old != -1) {
				// if (temp_cur > TEMP_THRESHOLD) {
					temp_threshold_diff = temp_cur - TEMP_THRESHOLD;
					if (temp_threshold_diff < 0)
						temp_threshold_diff = 0;
					if (temp_cur != temp_old)
						temp_last_diff = temp_cur - temp_old;

					if (temp_threshold_diff != 0) {
						coolingfan_fn_level = COOLINGFAN_LEVEL_INIT_LEVEL + 
							temp_threshold_diff * TEMP_THRESHOLD_DIFF_COEFFICIENT + 
							temp_last_diff * TEMP_LAST_DIFF_COEFFICIENT +
							temp_thre_exceed_count * TEMP_THRE_EXCEED_COUNT_COEFFICIENT;
					}

					if (coolingfan_fn_level > g_pwm_max_level)
						coolingfan_fn_level = g_pwm_max_level;
					else if (coolingfan_fn_level < COOLINGFAN_LEVEL_INIT_LEVEL)
						coolingfan_fn_level = COOLINGFAN_LEVEL_INIT_LEVEL;

					if (temp_last_diff > 0)
						temp_thre_exceed_count++;
					else if (temp_last_diff < 0)
						temp_thre_exceed_count--;

					if (temp_thre_exceed_count > 10)
						temp_thre_exceed_count = 10;
					else if (temp_thre_exceed_count < 0)
						temp_thre_exceed_count = 0;

					if (coolingfan_set_level_state)
						prize_coolingfan_set_enable(g_pwm_num, coolingfan_fn_level);
				// } else {
					// temp_thre_exceed_count = 0;
					// if (coolingfan_set_level_state)
						// prize_coolingfan_set_enable(g_pwm_num, 0);
				// }
			}
			temp_old = temp_cur;
			mutex_unlock(&coolingfan_lock);
		} else {
			if (coolingfan_fn_level > 0) {
				coolingfan_fn_level = 0;
				temp_thre_exceed_count = 0;
				temp_old = -1;
				temp_cur = -1;
				temp_threshold_diff = -1;
				temp_last_diff = -1;
				if (coolingfan_set_level_state)
					prize_coolingfan_set_enable(g_pwm_num, coolingfan_fn_level);
			}
		}
		// coolingfan_log("temp_cur=%d, temp_old=%d, temp_threshold_diff=%d, temp_last_diff=%d, temp_thre_exceed_count=%d, coolingfan_fn_level=%d\n", 
			// temp_cur, temp_old, temp_threshold_diff, temp_last_diff, temp_thre_exceed_count, coolingfan_fn_level);
	}
	return 0;
}

static int coolingfan_probe(struct platform_device *pdev)
{
	int ret;
	struct device *coolingfan_dev;

	coolingfan_log("%s\n", __func__);

	ret = coolingfan_dts(pdev);
	if (ret != 0) {
		coolingfan_log("coolingfan_dts failed!\n");
		return -1;
	}

	coolingfan_class = class_create(THIS_MODULE, "coolingfan");
	if (IS_ERR(coolingfan_class)) {
		coolingfan_log("Failed to create class(coolingfan_class)!");
		return PTR_ERR(coolingfan_class);
	}
	coolingfan_dev = device_create(coolingfan_class, NULL, 0, NULL, "coolingfan_data");
	if (IS_ERR(coolingfan_dev)) {
		coolingfan_log("Failed to create coolingfan_dev device");
	}

	// coolingfan file node
	if (device_create_file(coolingfan_dev, &dev_attr_coolingfan) < 0) {
		coolingfan_log("Failed to create device file(%s)!", dev_attr_coolingfan.attr.name);
	}

	if (device_create_file(coolingfan_dev, &dev_attr_coolingfan_set_level_state) < 0) {
		coolingfan_log("Failed to create device file(%s)!", dev_attr_coolingfan_set_level_state.attr.name);
	}

	atomic_set(&coolingfan_pwm_state, 0);

	// coolingfan thread
	coolingfan_thread = kthread_run(coolingfan_thread_fn, NULL, "coolingfan_thread");

	if (IS_ERR(coolingfan_thread)) {
		coolingfan_log(KERN_ERR "Failed to create coolingfan_thread\n");
		return PTR_ERR(coolingfan_thread);
	}

	coolingfan_log("%s OK\n", __func__);
	return 0;
}

static int coolingfan_remove(struct platform_device *pdev)
{
	if (coolingfan_thread) {
		kthread_stop(coolingfan_thread);
		prize_coolingfan_set_enable(g_pwm_num, 0);
	}
	return 0;
}

static const struct of_device_id coolingfan_of_match[] = {
	{ .compatible = "drv,coolingfan" },
	{ }
};
MODULE_DEVICE_TABLE(of, coolingfan_of_match);

static struct platform_driver coolingfan_driver = {
	.probe = coolingfan_probe,
	.remove = coolingfan_remove,
	.driver = {
		.name = "coolingfan",
		.of_match_table = of_match_ptr(coolingfan_of_match),
	},
};

static int __init coolingfan_init(void)
{
	int ret;
	coolingfan_log("%s\n", __func__);
	ret = platform_driver_register(&coolingfan_driver);
	if (ret) {
		coolingfan_log("****[%s] Unable to register driver (%d)\n", __func__, ret);
		return ret;
	}
	return 0;
}

static void __exit coolingfan_exit(void)
{
	coolingfan_log("%s\n", __func__);
	platform_driver_unregister(&coolingfan_driver);
}

late_initcall(coolingfan_init);
module_exit(coolingfan_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chenjiaxi@cooseagroup.com");
MODULE_DESCRIPTION("Coolingfan Driver");