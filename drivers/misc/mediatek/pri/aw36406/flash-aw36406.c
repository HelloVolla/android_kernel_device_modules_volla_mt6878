#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <media/rc-core.h>

#include <mt-plat/mtk_pwm.h>
#include <mt-plat/mtk_pwm_hal.h>

#define aw36406_DEBUG
#ifdef aw36406_DEBUG
#define aw36406_log(fmt,arg...) \
	do {\
		printk("<<aw36406-drv>>[%d]"fmt"", __LINE__, ##arg);\
	} while(0)
#endif

static struct pinctrl *aw36406_pinctrl;
static struct pinctrl_state *aw36406_set_low1;
static struct pinctrl_state *aw36406_set_pwm1;
static struct pinctrl_state *aw36406_set_higt1;
static struct pinctrl_state *aw36406_set_low2;
static struct pinctrl_state *aw36406_set_pwm2;
static struct pinctrl_state *aw36406_set_higt2;
struct class *aw36406_class;
static int aw36406_white_level = 0;
static int aw36406_yellow_level = 0;
static int g_pwm_num1 = -1;
static int g_pwm_num2 = -1;
static struct pwm_spec_config pwm_setting;
//drv add by lipengpeng 20241012 start
static int pwm_enabled_yellow = 0; // 0: PWM禁用, 1: PWM启用 
static int pwm_enabled_white = 0; // 0: PWM禁用, 1: PWM启用
//drv add by lipengpeng 20241012 end

static int prize_aw36406_set_enable_yellow(int pwm_num2, int level)//by lipengpeng 
{
	aw36406_log("%s %d : level=%d, pwm_no=%d\n", __func__, __LINE__, level, pwm_num2);//by lipengpeng 

	pwm_setting.pwm_no = pwm_num2;//by lipengpeng 
	pwm_setting.mode = PWM_MODE_OLD;
	/* We won't choose 32K to be the clock src of old mode because of system performance. */
	/* The setting here will be clock src = 26MHz, CLKSEL = 26M/1625 (i.e. 16K) */
	pwm_setting.clk_src = PWM_CLK_OLD_MODE_BLOCK; //prize  PWM_CLK_OLD_MODE_32K PWM_CLK_OLD_MODE_BLOCK 26M PWM_CLK_NEW_MODE_BLOCK PWM_CLK_NEW_MODE_BLOCK_DIV_BY_1625
	pwm_setting.clk_div = CLK_DIV4;
	pwm_setting.pmic_pad = 0;

//drv add by lipengpeng 20241012 start 	
    // 检查PWM是否已经禁用  
    if (level == 0 && pwm_enabled_yellow) { 
//drv add by lipengpeng 20241012 end 
		mt_pwm_disable(pwm_num2, 0);//by lipengpeng 
		pwm_enabled_yellow = 0; // 更新PWM状态
		if (aw36406_set_low2) {
			pinctrl_select_state(aw36406_pinctrl, aw36406_set_low2); //set gpio low //by lipengpeng 
		}
//drv add by lipengpeng 20241012 start
	} else if (level != 0 && !pwm_enabled_yellow) {  
//drv add by lipengpeng 20241012 end 
		// Freq=clk_src/clk_div/(2*(DATA_WIDTH+1))
		// 占空比=(THRESH+1)/(2*(DATA_WIDTH+1))，when GUARD_VALUE=0
		pinctrl_select_state(aw36406_pinctrl, aw36406_set_pwm2);  //pwm mode //by lipengpeng 
		pwm_setting.PWM_MODE_OLD_REGS.THRESH = level;
		// pwm_setting.clk_div = CLK_DIV1;
		pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 260; //25khz
		pwm_setting.PWM_MODE_OLD_REGS.IDLE_VALUE = 0;
		pwm_setting.PWM_MODE_OLD_REGS.GUARD_VALUE = 0;
		pwm_setting.PWM_MODE_OLD_REGS.GDURATION = 0;
		pwm_setting.PWM_MODE_OLD_REGS.WAVE_NUM = 0;
		pwm_set_spec_config(&pwm_setting);
//drv add by lipengpeng 20241012 start
		pwm_enabled_yellow = 1; // 更新PWM状态
//drv add by lipengpeng 20241012 end 
	}

	aw36406_log("%s %d : pwm_setting.PWM_MODE_OLD_REGS.THRESH=%d,pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH=%d\n", 
		__func__, __LINE__, pwm_setting.PWM_MODE_OLD_REGS.THRESH, pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH);
	return 0;
}

static ssize_t aw36406_yellow_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return sprintf(buf, "%d\n", aw36406_yellow_level);
}

static ssize_t aw36406_yellow_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	if (sscanf(buf, "%u", &aw36406_yellow_level) != 1) {
		aw36406_log("[aw36406_yellow]: Invalid values\n");
		return -EINVAL;
	}

	aw36406_log("[aw36406_yellow] %s aw36406_level value = %d [0:OFF ; mode 1~3 ; other:Singular set thresh, Even numbers set data_width ]\n ", 
		__func__, aw36406_yellow_level);

	if (aw36406_yellow_level >= 0) {
		prize_aw36406_set_enable_yellow(g_pwm_num2, aw36406_yellow_level); //by lipengpeng 
	} else {
		aw36406_log("[aw36406_yellow]: Invalid values\n");
		return -EINVAL;
	}

	return size;
}
static DEVICE_ATTR(yellow_light, 0664, aw36406_yellow_show, aw36406_yellow_store);


static int prize_aw36406_set_enable_white(int pwm_num1, int level)//by lipengpeng 
{
	aw36406_log("%s %d : level=%d, pwm_no=%d\n", __func__, __LINE__, level, pwm_num1);//by lipengpeng 

	pwm_setting.pwm_no = pwm_num1;//by lipengpeng 
	pwm_setting.mode = PWM_MODE_OLD;
	/* We won't choose 32K to be the clock src of old mode because of system performance. */
	/* The setting here will be clock src = 26MHz, CLKSEL = 26M/1625 (i.e. 16K) */
	pwm_setting.clk_src = PWM_CLK_OLD_MODE_BLOCK; //prize  PWM_CLK_OLD_MODE_32K PWM_CLK_OLD_MODE_BLOCK 26M PWM_CLK_NEW_MODE_BLOCK PWM_CLK_NEW_MODE_BLOCK_DIV_BY_1625
	pwm_setting.clk_div = CLK_DIV4;
	pwm_setting.pmic_pad = 0;
	
//drv add by lipengpeng 20241012 start 	
    // 检查PWM是否已经禁用  
    if (level == 0 && pwm_enabled_white) { 
//drv add by lipengpeng 20241012 end 
		mt_pwm_disable(pwm_num1, 0);//by lipengpeng 
		pwm_enabled_white = 0; // 更新PWM状态
		if (aw36406_set_low1) {
			pinctrl_select_state(aw36406_pinctrl, aw36406_set_low1); //set gpio low//by lipengpeng 
		}
//drv add by lipengpeng 20241012 start
	} else if (level != 0 && !pwm_enabled_white) {  
//drv add by lipengpeng 20241012 end 
		// Freq=clk_src/clk_div/(2*(DATA_WIDTH+1))
		// 占空比=(THRESH+1)/(2*(DATA_WIDTH+1))，when GUARD_VALUE=0
		pinctrl_select_state(aw36406_pinctrl, aw36406_set_pwm1);  //pwm mode//by lipengpeng 
		pwm_setting.PWM_MODE_OLD_REGS.THRESH = level;
		// pwm_setting.clk_div = CLK_DIV1;
		pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 260; //25khz
		pwm_setting.PWM_MODE_OLD_REGS.IDLE_VALUE = 0;
		pwm_setting.PWM_MODE_OLD_REGS.GUARD_VALUE = 0;
		pwm_setting.PWM_MODE_OLD_REGS.GDURATION = 0;
		pwm_setting.PWM_MODE_OLD_REGS.WAVE_NUM = 0;
		pwm_set_spec_config(&pwm_setting);
//drv add by lipengpeng 20241012 start
		pwm_enabled_white = 1; // 更新PWM状态
//drv add by lipengpeng 20241012 end 
	}

	aw36406_log("%s %d : pwm_setting.PWM_MODE_OLD_REGS.THRESH=%d,pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH=%d\n", 
		__func__, __LINE__, pwm_setting.PWM_MODE_OLD_REGS.THRESH, pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH);
	return 0;
}

static ssize_t aw36406_white_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return sprintf(buf, "%d\n", aw36406_white_level);
}

static ssize_t aw36406_white_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	if (sscanf(buf, "%u", &aw36406_white_level) != 1) {
		aw36406_log("[aw36406_white]: Invalid values\n");
		return -EINVAL;
	}

	aw36406_log("[aw36406_white] %s aw36406_white value = %d [0:OFF ; mode 1~3 ; other:Singular set thresh, Even numbers set data_width ]\n ", 
		__func__, aw36406_white_level);

	if (aw36406_white_level >= 0) {
		prize_aw36406_set_enable_white(g_pwm_num1, aw36406_white_level);
	} else {
		aw36406_log("[aw36406_white]: Invalid values\n");
		return -EINVAL;
	}

	return size;
}
static DEVICE_ATTR(white_light, 0664, aw36406_white_show, aw36406_white_store);

int aw36406_dts(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	if (of_property_read_u32(np, "pwm_num1", &g_pwm_num1) >= 0) {
		aw36406_log("aw36406 of_property_read_u32 pwm_num=%d\n", g_pwm_num1);
	}

	if (of_property_read_u32(np, "pwm_num2", &g_pwm_num2) >= 0) {
		aw36406_log("aw36406 of_property_read_u32 pwm_num=%d\n", g_pwm_num2);
	}
	aw36406_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR(aw36406_pinctrl)) {
		aw36406_set_low1 = pinctrl_lookup_state(aw36406_pinctrl, "aw36406_set_low1");
		if (IS_ERR(aw36406_set_low1)){
			aw36406_log("aw36406 get pinctrl state aw36406_set_low1 fail\n");
			return -EINVAL;
		}
		
		aw36406_set_higt1 = pinctrl_lookup_state(aw36406_pinctrl, "aw36406_set_higt1");
		if (IS_ERR(aw36406_set_higt1)){
			aw36406_log("aw36406 get pinctrl state aw36406_set_higt1 fail\n");
			return -EINVAL;
		}

		aw36406_set_pwm1 = pinctrl_lookup_state(aw36406_pinctrl, "aw36406_set_pwm1");
		if (IS_ERR(aw36406_set_pwm1)){
			aw36406_log("aw36406 get pinctrl state aw36406_set_pwm1 fail\n");
			return -EINVAL;
		}
		
		aw36406_set_low2 = pinctrl_lookup_state(aw36406_pinctrl, "aw36406_set_low2");
		if (IS_ERR(aw36406_set_low2)){
			aw36406_log("aw36406 get pinctrl state aw36406_set_low2 fail\n");
			return -EINVAL;
		}
		
		aw36406_set_higt2 = pinctrl_lookup_state(aw36406_pinctrl, "aw36406_set_higt2");
		if (IS_ERR(aw36406_set_higt2)){
			aw36406_log("aw36406 get pinctrl state aw36406_set_low fail\n");
			return -EINVAL;
		}

		aw36406_set_pwm2 = pinctrl_lookup_state(aw36406_pinctrl, "aw36406_set_pwm2");
		if (IS_ERR(aw36406_set_pwm2)){
			aw36406_log("aw36406 get pinctrl state aw36406_set_pwm2 fail\n");
			return -EINVAL;
		}
	} else {
		printk("aw36406 get pinctrl fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(aw36406_pinctrl, aw36406_set_low1);
	pinctrl_select_state(aw36406_pinctrl, aw36406_set_low2);
	return 0;
}

static int aw36406_probe(struct platform_device *pdev)
{
	int ret;
	struct device *aw36406_dev;

	aw36406_log("aw36406_probe start");

	ret = aw36406_dts(pdev);
	if (ret != 0) {
		aw36406_log("aw36406_dts failed!\n");
		return -1;
	}

	aw36406_class = class_create(THIS_MODULE, "flash_filltouch");
	if (IS_ERR(aw36406_class)) {
		aw36406_log("Failed to create class(aw36406_class)!");
		return PTR_ERR(aw36406_class);
	}
	aw36406_dev = device_create(aw36406_class, NULL, 0, NULL, "flash_filltouch_data");
	if (IS_ERR(aw36406_dev)) {
		aw36406_log("Failed to create aw36406_dev device");
	}

	// test
	if (device_create_file(aw36406_dev, &dev_attr_white_light) < 0) {
		printk("Failed to create white light device file(%s)!", dev_attr_white_light.attr.name);
	}
	
	if (device_create_file(aw36406_dev, &dev_attr_yellow_light) < 0) {
		printk("Failed to create white yellow device file(%s)!", dev_attr_yellow_light.attr.name);
	}

	printk("aw36406_probe OK\n");
	return 0;
}

static int aw36406_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id aw36406_of_match[] = {
	{ .compatible = "drv,aw36406" },
	{ }
};
MODULE_DEVICE_TABLE(of, aw36406_of_match);

static struct platform_driver aw36406_driver = {
	.probe = aw36406_probe,
	.remove = aw36406_remove,
	.driver = {
		.name = "aw36406",
		.of_match_table = of_match_ptr(aw36406_of_match),
	},
};

static int __init aw36406_init(void)
{
	int ret;
	aw36406_log("%s\n", __func__);
	ret = platform_driver_register(&aw36406_driver);
	if (ret) {
		aw36406_log("****[%s] Unable to register driver (%d)\n", __func__, ret);
		return ret;
	}
	return 0;
}

static void __exit aw36406_exit(void)
{
	aw36406_log("%s\n", __func__);
	platform_driver_unregister(&aw36406_driver);
}

late_initcall(aw36406_init);
module_exit(aw36406_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chenjiaxi@cooseagroup.com");
MODULE_DESCRIPTION("Coolingfan Driver");