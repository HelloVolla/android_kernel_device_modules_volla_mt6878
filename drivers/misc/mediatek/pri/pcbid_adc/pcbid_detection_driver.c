#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/semaphore.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/kthread.h>
#include <linux/input.h>
#if defined(CONFIG_PM_WAKELOCKS)
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <linux/time.h>

#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <linux/input.h>

#include <linux/of.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>

/*----------------------------------------------------------------------
static variable defination
----------------------------------------------------------------------*/
//#define pcbid_detection_DEVNAME    "pcbid_detection_dev"

#define EN_DEBUG

#if defined(EN_DEBUG)
		
#define TRACE_FUNC 	printk("[pcbid_detection_dev] function: %s, line: %d \n", __func__, __LINE__);

#define pcbid_detection_DEBUG  printk
#else

#define TRACE_FUNC(x,...)

#define pcbid_detection_DEBUG(x,...)
#endif

static struct pinctrl *pcbid_01_gps_pinctrl;
static struct pinctrl_state *pcbid_01_gps_default;
static struct pinctrl_state *pcbid_01_gps_lna_en_high;
static struct pinctrl_state *pcbid_01_gps_lna_en_low;


struct iio_channel *adc1_mt6363_channel = NULL;
static int pcbid_adc_vol_max_value;
static int pcbid_adc_vol_min_value;

int pcbid_detection_getadc_v(void);
int pcbid_detection_getadc(void);

int pcbid_detection_getadc_v(void){
	
	int ret = 0;
	int val = 0;
	
	if (!IS_ERR_OR_NULL(adc1_mt6363_channel)){
		ret = iio_read_channel_processed(adc1_mt6363_channel, &val);
		if (ret < 0) {
			printk("%s:Busy/Timeout, IIO ch read failed %d\n", __func__, ret);
			return ret;
		}
         printk("<psbid>-----get pcbid detect vol xx=%d\n", val);
		/*val * 1500 / 4096*/
		///ret = (val * 1450) >> 12;  //max 1.45V
	}
	return val;
}
EXPORT_SYMBOL_GPL(pcbid_detection_getadc_v);

int pcbid_detection_getadc(void){
	
	int ret = 0;
	int val = 0;
	
	if (!IS_ERR_OR_NULL(adc1_mt6363_channel)){
		ret = iio_read_channel_processed(adc1_mt6363_channel, &val);
		if (ret < 0) {
			printk("%s:Busy/Timeout, IIO ch read failed %d\n", __func__, ret);
			return ret;
		}
		//1764  0.64 
         printk("<psbid>-----get pcbid detect vol=%d\n", val);
	//	ret = (val * 1450) >> 12;  //max 1.45V
	}
	return 1800-val;
}
EXPORT_SYMBOL_GPL(pcbid_detection_getadc);

int pcbid_get(void){
	
	if ((pcbid_detection_getadc_v() < pcbid_adc_vol_max_value)&&(pcbid_detection_getadc_v() >= pcbid_adc_vol_min_value) )
	{
		printk("This is pcbid 01 board\n");
		return 1;
	}else{
		printk("This is not pcbid 01 board\n");
		return 0;
	}
}
EXPORT_SYMBOL_GPL(pcbid_get);


static int pcbid_01_gps_lna_gpio_set(struct platform_device *pdev)
{
	int ret = 0;
	pcbid_01_gps_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pcbid_01_gps_pinctrl)) {
		printk("Cannot find gps lna pinctrl!");
		ret = PTR_ERR(pcbid_01_gps_pinctrl);
	}
	//fm ant eint pin initialization 
	pcbid_01_gps_default= pinctrl_lookup_state(pcbid_01_gps_pinctrl, "default");
	if (IS_ERR(pcbid_01_gps_default)) {
		ret = PTR_ERR(pcbid_01_gps_default);
		printk("%s : init err, pcbid_01_gps_default\n", __func__);
	}

	pcbid_01_gps_lna_en_high = pinctrl_lookup_state(pcbid_01_gps_pinctrl, "lna_en_high");
	if (IS_ERR(pcbid_01_gps_lna_en_high)) {
		ret = PTR_ERR(pcbid_01_gps_lna_en_high);
		printk("%s : init err, pcbid_01_gps_lna_en_high\n", __func__);
	}

	pcbid_01_gps_lna_en_low = pinctrl_lookup_state(pcbid_01_gps_pinctrl, "lna_en_low");
	if (IS_ERR(pcbid_01_gps_lna_en_low)) {
		ret = PTR_ERR(pcbid_01_gps_lna_en_low);
		printk("%s : init err, pcbid_01_gps_lna_en_low\n", __func__);
	}

    if(1 == pcbid_get())
	{
	     pinctrl_select_state(pcbid_01_gps_pinctrl, pcbid_01_gps_lna_en_high);
	}
	else
	{
	     pinctrl_select_state(pcbid_01_gps_pinctrl, pcbid_01_gps_lna_en_low);
	} 

	return ret;
}

static int pcbid_detection_probe(struct platform_device *pdev)
{
   int ret = 0;
   struct device *dev = &pdev->dev;
   struct device_node *np = dev->of_node;
	
   printk("pcbid_detection_probe start\n");
	   
	adc1_mt6363_channel = iio_channel_get(&pdev->dev, "adc1-mt6363");
	if (IS_ERR(adc1_mt6363_channel)) {
		ret = PTR_ERR(adc1_mt6363_channel);
		printk("[%s] <psbid> fail to get auxadc iioadc1-mt6363: %d, %p\n", __func__, ret, adc1_mt6363_channel);
		return ret;
	}
	
	ret = of_property_read_u32(np,"pcbid_adc_vol_max",&pcbid_adc_vol_max_value);
	if(ret < 0){
		printk(" <psbid> fail to get auxadc pcbid_adc_vol_max failed\n");
	}else{
		printk("<psbid>  get auxadc pcbid_adc_vol_max = %d\n",pcbid_adc_vol_max_value);
	}
	ret = of_property_read_u32(np,"pcbid_adc_vol_min",&pcbid_adc_vol_min_value);
	if(ret < 0){
		printk(" <psbid> fail to get auxadc pcbid_adc_vol_min failed\n");
	}else{
		printk("<psbid>  get auxadc pcbid_adc_vol_min = %d\n",pcbid_adc_vol_min_value);
	}	
	
	pcbid_01_gps_lna_gpio_set(pdev);

	printk("pcbid_detection_probe end\n");
	return 0;
}

static int pcbid_detection_remove(struct platform_device *dev)	
{
	pcbid_detection_DEBUG("[pcbid_detection_dev]:pcbid_detection_remove start!\n");
	pcbid_detection_DEBUG("[pcbid_detection_dev]:pcbid_detection_remove end!\n");
	return 0;
}
static const struct of_device_id pcbid_detection_dt_match[] = {
	{.compatible = "prize,pcbid_detection"},
	{},
};

static struct platform_driver pcbid_detection_driver = {
	.probe	= pcbid_detection_probe,
	.remove  = pcbid_detection_remove,
	.driver    = {
		.name       = "pcbid_detection_driver",
		.of_match_table = of_match_ptr(pcbid_detection_dt_match),
	},
};

static int __init pcbid_detection_init(void)
{
    int retval = 0;
    printk("pcbid_detection_init, retval=%d \n!",retval);
	  if (retval != 0) {
		  return retval;
	  }
    platform_driver_register(&pcbid_detection_driver);
    return 0;
}

static void __exit pcbid_detection_exit(void)
{
    printk("pcbid_detection_exit start\n");
    platform_driver_unregister(&pcbid_detection_driver);
}

module_init(pcbid_detection_init);
module_exit(pcbid_detection_exit);
MODULE_DESCRIPTION("AIR QUALITY driver");
MODULE_AUTHOR("moshuya <moshuya@szprize.com>");
MODULE_LICENSE("GPL");
//MODULE_SUPPORTED_DEVICE("AIRQUALITYDEVICE");

