#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>

struct scr_rtl_info {
	struct device *dev;
	int pwr_gpio;
	int rst_gpio;
};

static ssize_t device_scr_rtl_pwr_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
	struct scr_rtl_info *scr = dev_get_drvdata(dev);
	int ret = -1;
	
	if (gpio_is_valid(scr->pwr_gpio)){
		ret = gpio_get_value(scr->pwr_gpio);
	}

	return sprintf(buf, "%d\n", ret);
}
static ssize_t device_scr_rtl_pwr_store(struct device *dev,
                struct device_attribute *attr, const char *buf, size_t size)
{
	struct scr_rtl_info *scr = dev_get_drvdata(dev);
	int pwr = 0;
	
	if(sscanf(buf, "%u", &pwr) != 1){
		dev_err(dev,"Invalid values\n");
		return -EINVAL;
	}
	dev_info(dev,"%s value = %d:\n", __func__, pwr);
	
	if (gpio_is_valid(scr->pwr_gpio)){
		if (pwr){
			gpio_direction_output(scr->pwr_gpio, 1);
		}else{
			gpio_direction_output(scr->pwr_gpio, 0);
		}
	}
	return size;
}
static DEVICE_ATTR(pwr, 0644, device_scr_rtl_pwr_show, device_scr_rtl_pwr_store);


static ssize_t device_scr_rtl_rst_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
	struct scr_rtl_info *scr = dev_get_drvdata(dev);
	int ret = -1;
	
	if (gpio_is_valid(scr->rst_gpio)){
		ret = gpio_get_value(scr->rst_gpio);
	}

	return sprintf(buf, "%d\n", ret);
}
static ssize_t device_scr_rtl_rst_store(struct device *dev,
                struct device_attribute *attr, const char *buf, size_t size)
{
	struct scr_rtl_info *scr = dev_get_drvdata(dev);
	int rst = 0;
	
	if(sscanf(buf, "%u", &rst) != 1){
		dev_err(dev,"Invalid values\n");
		return -EINVAL;
	}
	dev_info(dev,"%s value = %d:\n", __func__, rst);
	
	if (gpio_is_valid(scr->rst_gpio)){
		if (rst){
			gpio_direction_output(scr->rst_gpio, 1);
		}else{
			gpio_direction_output(scr->rst_gpio, 0);
		}
	}
	return size;
}
static DEVICE_ATTR(rst, 0644, device_scr_rtl_rst_show, device_scr_rtl_rst_store);


static int scr_rtl_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct scr_rtl_info *scr = NULL;
	int ret = 0;
	dev_info(&pdev->dev, "%s enter probe\n", __func__);
	if (IS_ERR_OR_NULL(np)){
		dev_err(&pdev->dev, "Get device node fail\n");
		return -EINVAL;
	}
	
	scr = devm_kzalloc(&pdev->dev, sizeof(*scr), GFP_KERNEL);
	if (!scr){
		dev_err(&pdev->dev, "No mem\n");
		return -ENOMEM;
	}
	
	scr->dev = &pdev->dev;
	platform_set_drvdata(pdev, scr);
	
	scr->pwr_gpio = of_get_named_gpio(np, "pwr-gpio", 0);
	if (scr->pwr_gpio < 0){
		dev_err(&pdev->dev, "Get rst gpio fail %d\n",scr->pwr_gpio);
		goto free_mem;
	}
	ret = devm_gpio_request(&pdev->dev, scr->pwr_gpio, "scr_rtl_pwr");
	if (ret){
		dev_err(&pdev->dev, "GPIO request failed\n");
		goto free_mem;
	}
	
	scr->rst_gpio = of_get_named_gpio(np, "rst-gpio", 0);
	if (scr->rst_gpio < 0){
		dev_err(&pdev->dev, "Get rst gpio fail %d\n",scr->rst_gpio);
		goto free_gpio_pwr;
	}
	ret = devm_gpio_request(&pdev->dev, scr->rst_gpio, "scr_rtl_rst");
	if (ret){
		dev_err(&pdev->dev, "GPIO request failed\n");
		goto free_gpio_pwr;
	}

	if (device_create_file(scr->dev, &dev_attr_pwr) < 0){
		dev_err(&pdev->dev, "Create device pwr file fail\n");
		goto free_gpio_rst;
	}
	
	if (device_create_file(scr->dev, &dev_attr_rst) < 0){
		dev_err(&pdev->dev, "Create device rst file fail\n");
		goto free_gpio_rst;
	}
	dev_info(&pdev->dev, "%s exit probe\n", __func__);
	return 0;

free_gpio_rst:
	gpio_free(scr->rst_gpio);
free_gpio_pwr:
	gpio_free(scr->pwr_gpio);
free_mem:
	devm_kfree(&pdev->dev, scr);

	return ret;
}

static int scr_rtl_remove(struct platform_device *pdev)
{
	struct scr_rtl_info *scr = platform_get_drvdata(pdev);
	
	//devm_gpio_free(&pdev->dev, scr->pwr_gpio);
	//devm_gpio_free(&pdev->dev, scr->rst_gpio);
	device_remove_file(&pdev->dev, &dev_attr_pwr);
	device_remove_file(&pdev->dev, &dev_attr_rst);
	devm_kfree(&pdev->dev, scr);
	
	return 0;
}
static const struct of_device_id scr_rtl_dt_match[] = {
	{.compatible = "pri,scr_rtl"},
	{},
};

static struct platform_driver scr_rtl_driver = {
	.probe	= scr_rtl_probe,
	.remove  = scr_rtl_remove,
	.driver    = {
		.name       = "scr_rtl",
		.of_match_table = of_match_ptr(scr_rtl_dt_match),
	},
};

static int __init scr_rtl_init(void)
{
    platform_driver_register(&scr_rtl_driver);
    return 0;
}

static void __exit scr_rtl_exit(void)
{
    platform_driver_unregister(&scr_rtl_driver);
}

module_init(scr_rtl_init);
module_exit(scr_rtl_exit);
MODULE_LICENSE("GPL");

