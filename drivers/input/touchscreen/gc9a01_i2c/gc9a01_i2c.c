#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/workqueue.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>

#include "gc9a01_i2c.h"
#include "capacitive_hynitron_CST820_update.h"
#if defined(CONFIG_PRIZE_UNDERWATER_TOUCH_CONTROL)
#include "pri_common_node.h"
extern bool underwater_report_status;
#endif

#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_sub_tp_info;
#endif

#define DEVICE_NAME  "gc9a01_iic"
#define HYN_I2C_SLAVE_ADDR                  0x15


#define HYN_EN_AUTO_UPDATE_CST78xx              1
#define HYN_EN_AUTO_UPDATE              1
#if HYN_EN_AUTO_UPDATE
unsigned char *p_cst836u_upgrade_firmware;
unsigned char  apk_upgrade_flag;
extern unsigned char app_bin[];

static struct i2c_client *client_up;
//extern unsigned char app_bin[];
static unsigned char dev_addr;
static unsigned char update_fw_flag;
static unsigned char chip_sumok_flag;

#define HYN_REG_FW_VER                      0xA9
#define REG_LEN_2B    2
#define HYN_MTK_IIC_TRANSFER_LIMIT         		1
#define PER_LEN	512
#endif

#ifndef GC9A01_SYS_TEST
#define GC9A01_SYS_TEST
#endif

/*
static unsigned int gc9a01_tp_reset = 0;
static unsigned int gc9a01_tp_int = 0;
static int tp_irq = 0;
static int tpd_flag = 0;
static struct input_dev *gc9a01_dev;
struct i2c_client *g_client = NULL;
//static struct delayed_work delay_work;
static DECLARE_WAIT_QUEUE_HEAD(waiter);
static DEFINE_MUTEX(i2c_access);
*/

static int gc9a01_i2c_is_probe_ok = false;
struct gc9a01_data * g_gc9a01_data = NULL;

static const struct of_device_id gc9a01_match_table[] = {
    {.compatible = "mediatek,gc9a01_touch",},
    { },
};

/*Define gc9a01 iic read function*/
int gc9a01_read(struct i2c_client *client, unsigned char reg, unsigned char *data)
{
	int ret = 0;
	//msleep(10);
	ret = i2c_smbus_read_i2c_block_data(client, reg, 1, data);
	printk("gc9a01 id %2x = %2x\n", reg, data[0]);
	return ret;
}

int gc9a01_read_block(struct i2c_client *client, unsigned char reg, unsigned char *data,int len)
{
	int ret = 0;
	//msleep(10);
	ret = i2c_smbus_read_i2c_block_data(client, reg, len, data);
	//cw_printk(0,"%2x = %2x\n", reg, buf[0]);
	return ret;
}

/*Define gc9a01 iic write function*/
int gc9a01_write(struct i2c_client *client, unsigned char reg, unsigned char *buf)
{
	int ret = 0;
	//msleep(10);
	ret = i2c_smbus_write_i2c_block_data(client, reg, 1, buf);
	//cw_printk(0,"%2x = %2x\n", reg, buf[0]);
	return ret;
}

//int gc9a01_write_block(struct i2c_client *client, unsigned char reg, unsigned char *data, int len)
//{
//	int ret = 0;
//	//msleep(10);
//	ret = i2c_smbus_write_i2c_block_data(client, reg, len, buf);
//	//cw_printk(0,"%2x = %2x\n", reg, buf[0]);
//	return ret;
//}

void gc9a01_irq_disable(struct gc9a01_data * gc_data)
{
    unsigned long irqflags;

    spin_lock_irqsave(&gc_data->irq_lock, irqflags);
	
	pr_err("------%s--------irq_disabled = %d\n",__func__,gc_data->irq_disabled);

    if (!gc_data->irq_disabled) {
        disable_irq_nosync(gc_data->tp_irq);
        gc_data->irq_disabled = true;
    }

    spin_unlock_irqrestore(&gc_data->irq_lock, irqflags);
    
}

void gc9a01_irq_enable(struct gc9a01_data * gc_data)
{
    unsigned long irqflags = 0;
	pr_err("------%s--------irq_disabled = %d\n",__func__,gc_data->irq_disabled);
    spin_lock_irqsave(&gc_data->irq_lock, irqflags);

    if (gc_data->irq_disabled) {
        enable_irq(gc_data->tp_irq);
        gc_data->irq_disabled = false;
    }
    spin_unlock_irqrestore(&gc_data->irq_lock, irqflags);

}


static void gc9a01_reset(struct gc9a01_data * gc_data)
{
	if(!gc_data){
		pr_err("gc_data was null,return...\n");
	}
	gpio_direction_output(gc_data->gc9a01_tp_reset,1);
	msleep(10);
	gpio_direction_output(gc_data->gc9a01_tp_reset,0);
	msleep(10);
	gpio_direction_output(gc_data->gc9a01_tp_reset,1);
	msleep(10);
}



static int gc9a01_get_gpio(struct device *dev,struct gc9a01_data * gc_data)
{
	int ret = 0;
	const struct of_device_id *match;

	if (dev->of_node){
		match = of_match_device(of_match_ptr(gc9a01_match_table), dev);
		if (!match) {
			printk("Error: No device match found\n");
			return -ENODEV;
		}
	}

	gc_data->gc9a01_tp_reset = of_get_named_gpio(dev->of_node, "reset_gpio", 0);

	printk("%s------[gezi] gc9a01_tp_reset =  %d\n", __func__, gc_data->gc9a01_tp_reset);
	if(gc_data->gc9a01_tp_reset != 0) {
		ret = gpio_request(gc_data->gc9a01_tp_reset, "gc9a01_tp_reset");
		if (ret)
			printk("%s------[gezi] gpio request gc9a01_tp_reset = 0x%x fail with %d\n", __func__, gc_data->gc9a01_tp_reset, ret);
	}

	gc_data->gc9a01_tp_int = of_get_named_gpio(dev->of_node, "irq_gpio", 0);

	printk("%s------[gezi] gc9a01_tp_int =  %d\n", __func__, gc_data->gc9a01_tp_int);
	if(gc_data->gc9a01_tp_int != 0) {
		ret = gpio_request(gc_data->gc9a01_tp_int, "gc9a01_tp_int");
		if (ret)
			printk("%s------[gezi] gpio request gc9a01_tp_int = 0x%x fail with %d\n", __func__,gc_data->gc9a01_tp_int, ret);
	}
/*
	gpio_direction_output(gc9a01_tp_reset,0);
	msleep(10);
	gpio_direction_output(gc9a01_tp_reset,1);
	msleep(50);
*/
	//gpio_direction_output(gc9a01_tp_reset,0);
	return ret;
}


static irqreturn_t cst3xx_ts_irq_handler(int irq, void *data)
{

    //printk("-------enter cst3xx_ts_irq_handler-------%d-----\n",__gpio_get_value(gc9a01_tp_int));
	struct gc9a01_data * gc_data = data;

	//disable_irq_nosync(gc_data->tp_irq);//use in interrupt,disable_irq will make dead lock.

	gc_data->tpd_flag = 1;
	wake_up_interruptible(&gc_data->waiter);

	return IRQ_HANDLED;
}

static int gc9a01_input_init(struct gc9a01_data * gc_data)
{
	int ret = 0;
	//struct input_dev *gc9a01_dev = gc_data->gc_dev;

	gc_data->gc9a01_dev = input_allocate_device();
	if (gc_data->gc9a01_dev == NULL) {
		pr_err("Failed to allocate input device for gc9a01_dev\n");
		return -1;
	}

	gc_data->gc9a01_dev->name = "gc9a01_touch";
	//gc_data->gc9a01_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	//gc_data->gc9a01_dev->keybit[BIT_WORD(BTN_TOOL_FINGER)] = BIT_MASK(BTN_TOOL_FINGER);

	__set_bit(EV_REL,gc_data->gc9a01_dev->evbit);
    __set_bit(REL_X, gc_data->gc9a01_dev->relbit);
    __set_bit(REL_Y, gc_data->gc9a01_dev->relbit);
    //__set_bit(REL_Z, gc_data->gc9a01_dev->relbit);

    __set_bit(EV_SYN, gc_data->gc9a01_dev->evbit);
    __set_bit(EV_ABS, gc_data->gc9a01_dev->evbit);
    __set_bit(EV_KEY, gc_data->gc9a01_dev->evbit);
	__set_bit(BTN_TOUCH, gc_data->gc9a01_dev->keybit);
    __set_bit(BTN_TOOL_FINGER,gc_data->gc9a01_dev->keybit);
    //__set_bit(INPUT_PROP_BUTTONPAD,gc_data->gc9a01_dev->propbit);
	__set_bit(INPUT_PROP_DIRECT, gc_data->gc9a01_dev->propbit);

//drv modify by wangwei1 CST816T for Switch from vertical screen to landscape screen start
	input_set_abs_params(gc_data->gc9a01_dev, ABS_MT_POSITION_X, 0, 320, 0, 0);
	input_set_abs_params(gc_data->gc9a01_dev, ABS_MT_POSITION_Y, 0, 172, 0, 0);
//drv modify by wangwei1 CST816T for Switch from vertical screen to landscape screen end

	//input_set_abs_params(gc_data->gc9a01_dev, ABS_MT_TOUCH_MAJOR, 0, 0xFF, 0, 0);

	//input_set_abs_params(gc_data->gc9a01_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(gc_data->gc9a01_dev, ABS_MT_TRACKING_ID, 0, 0x0F, 0, 0);
/*
	input_set_abs_params(gc9a01_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(gc9a01_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);
*/

	ret = input_register_device(gc_data->gc9a01_dev);
	if (ret) {
		pr_err("Register %s input device failed", gc_data->gc9a01_dev->name);
		return -1;
	}

	return 0;
}

static int gc9a01_eint_setup(struct gc9a01_data * gc_data)
{
	int ret;
	//struct device_node *node;

	//usb_eint_type = IRQ_TYPE_EDGE_RISING;

	gpio_direction_input(gc_data->gc9a01_tp_int);

	gc_data->tp_irq = gpio_to_irq(gc_data->gc9a01_tp_int);

	pr_err("tp_irq=%d", gc_data->tp_irq);

	ret = request_irq(gc_data->tp_irq, cst3xx_ts_irq_handler,IRQF_TRIGGER_FALLING, "gc9a01_tp_eint_func", gc_data);
	//ret = request_irq(charge_state_irq, charge_state_eint_func,IRQ_TYPE_LEVEL_LOW, "battery_exist_eint_default", NULL);
	if (ret > 0){
		pr_err("usb EINT IRQ LINE NOT AVAILABLE\n");
	}
	else {
		pr_err("usb eint set EINT finished, usb_irq=%d\n",gc_data->tp_irq);
	}

	//enable_irq_wake(gc_data->tp_irq);

	return ret;
}

static int gc9a01_get_chip_id(struct i2c_client *client)
{
	unsigned char chip_id = 0;

	gc9a01_read(client,FTS_REG_CHIP_ID,&chip_id);

	printk("[Touch sensor(gc9a01_get_chip_id) get chip id succ] = %x\n", chip_id);

	if(chip_id == 0xB6) {
		return 0;
	}

	return 0;

}

static void gc9a01_report(unsigned short x,unsigned short y,unsigned char mode,unsigned char finger_num)
{
	static unsigned char pressed = 0;
	static unsigned int tracking_id = 0;

	if(unlikely(!g_gc9a01_data)){
		return;
	}
	//if(finger_num){
	//	input_mt_report_slot_state(g_gc9a01_data->gc9a01_dev, MT_TOOL_FINGER, true);
	//}
	//input_report_abs(g_gc9a01_data->gc9a01_dev, ABS_MT_TOUCH_MAJOR, mode);
		//input_report_abs(g_gc9a01_data->gc9a01_dev, ABS_MT_PRESSURE,mode);
	input_report_abs(g_gc9a01_data->gc9a01_dev, ABS_MT_POSITION_X, x);
	input_report_abs(g_gc9a01_data->gc9a01_dev, ABS_MT_POSITION_Y, y);

	if((!pressed) && finger_num){
		tracking_id++;
		//input_mt_report_slot_state(g_gc9a01_data->gc9a01_dev, MT_TOOL_FINGER, true);
		//input_report_abs(g_gc9a01_data->gc9a01_dev, ABS_MT_TOUCH_MAJOR, mode);
		input_report_key(g_gc9a01_data->gc9a01_dev, BTN_TOUCH, 1);
		input_report_abs(g_gc9a01_data->gc9a01_dev, ABS_MT_TRACKING_ID, tracking_id);
		//input_report_key(g_gc9a01_data->gc9a01_dev, BTN_TOOL_FINGER, 1);
		pressed = 1;
	}




	if(!finger_num){
		//input_mt_report_slot_state(g_gc9a01_data->gc9a01_dev, MT_TOOL_FINGER, false);
		input_report_key(g_gc9a01_data->gc9a01_dev, BTN_TOUCH, 0);
		input_report_abs(g_gc9a01_data->gc9a01_dev, ABS_MT_TRACKING_ID, 0xffffffff);
		//input_report_key(g_gc9a01_data->gc9a01_dev, BTN_TOOL_FINGER, 0);
		pressed = 0;
	}

    input_sync(g_gc9a01_data->gc9a01_dev);

}

static int touch_event_handler(void *arg)
{
	struct gc9a01_data *gc_data = arg;
	int ret = 0;
	unsigned short pdwSampleX, pdwSampleY;
	unsigned char tp_temp[10];
	unsigned char finger_num;
	do{
		wait_event_interruptible(gc_data->waiter, gc_data->tpd_flag != 0);
		gc_data->tpd_flag = 0;
		mutex_lock(&gc_data->i2c_access);

		//pr_err("gezi---------------%s----------%d\n",__func__,__LINE__);

		ret = gc9a01_read_block(gc_data->client, FTS_REG_START,tp_temp, 7);

		//for(i = 0; i < 7; i++){
		//
		//}
		if(ret < 0){
			pr_err("gezi-------- err-------%s----------%d\n",__func__,__LINE__);
			goto exit_unlock;
		}
		finger_num = tp_temp[2]; //手指个数
//drv modify by wangwei1 CST816T for Switch from vertical screen to landscape screen start
		//pdwSampleX = ((tp_temp[3] & 0x0F) << 8) + tp_temp[4];
		//pdwSampleY = ((tp_temp[5] & 0x0F) << 8) + tp_temp[6];

		pdwSampleX = 319 - (((tp_temp[5] & 0x0F) << 8) + tp_temp[6]);
		pdwSampleY = ((tp_temp[3] & 0x0F) << 8) + tp_temp[4];
//drv modify by wangwei1 CST816T for Switch from vertical screen to landscape screen end

	//	pr_err("reg[0x03]= %x,reg[0x05] = %x\n",tp_temp[3],tp_temp[5]);

		//pr_err("mode = %d,finger_num = %d,x = %x,y = %x\n",tp_temp[1],finger_num,pdwSampleX,pdwSampleY);

#if defined(CONFIG_PRIZE_UNDERWATER_TOUCH_CONTROL)
    /* drv added by wangwei1, touch data reporting contrl, start */
    if (underwater_report_status == true) {
		gc9a01_report(pdwSampleX,pdwSampleY,tp_temp[1],finger_num);
    }
    /* drv added by wangwei1, touch data reporting contrl, end */
#endif

		//if(tp_temp[0] = 0x00) //扱点模式.要求FAE將 手勢碍清零.很多吋候手勢碍没有被清除

exit_unlock:
		//enable_irq(gc_data->tp_irq);
		mutex_unlock(&gc_data->i2c_access);

	} while (!kthread_should_stop());

	return ret;
}

static void gc9a01_suspend(void)
{
	int ret = 0;
	unsigned char enterSleep = 0x03;

	if(unlikely(!g_gc9a01_data)){
		return;
	}

	gc9a01_reset(g_gc9a01_data);
	msleep(40);
    ret = gc9a01_write(g_gc9a01_data->client,FTS_REG_LOW_POWER,&enterSleep);
	if(ret < 0){
		pr_err("gc9a01 enter suspend failed\n");
	}
	gc9a01_irq_disable(g_gc9a01_data);
	pr_err("gc9a01 enter suspend \n");

}

static void gc9a01_resume(void)
{
	//int ret = 0;
	//unsigned char QuitSleep = 0x01;

	if(unlikely(!g_gc9a01_data)){
		return;
	}

	gc9a01_reset(g_gc9a01_data);
	msleep(40);
/*
    ret = gc9a01_write(g_gc9a01_data->client,0xfe,&QuitSleep);
	if(ret < 0){
		pr_err("gc9a01 quit suspend failed\n");
	}
*/
	gc9a01_irq_enable(g_gc9a01_data);

#if defined(CONFIG_PRIZE_UNDERWATER_TOUCH_CONTROL)
	/* drv added by wangwei1, touch data reporting contrl, start */
	underwater_report_status = true;
	/* drv added by wangwei1, touch data reporting contrl, start */
#endif

	pr_err("gc9a01 enter resume \n");
}


void gc9a01_tp_power_ctrl(int value)
{
	if (gc9a01_i2c_is_probe_ok) {
		if(value){
			gc9a01_resume();
		}else{
			gc9a01_suspend();
		}
	} else {
		pr_err("gc9a01 i2c probe err\n");
	}
}
EXPORT_SYMBOL(gc9a01_tp_power_ctrl);

#ifdef GC9A01_SYS_TEST
static struct class * gc9a01_class;

static ssize_t gc9a01_test_store(struct class *class, struct class_attribute *attr,	const char *buf, size_t count)
{
	if(buf[0] == '0')
	{
		gc9a01_suspend();
	}
	else if(buf[0] == '1')
	{
		gc9a01_resume();
	}
	return count;
}

static struct class_attribute gc9a01_class_attrs[] = {
	__ATTR(test, S_IRUGO | S_IWUSR, NULL, gc9a01_test_store),
	__ATTR_NULL,
};


static int gc9a01_sysfs_create(void)
{
	int i = 0,ret = 0;
	
	gc9a01_class = class_create(THIS_MODULE, "gc9a01_tp");
	if (IS_ERR(gc9a01_class))
		return PTR_ERR(gc9a01_class);
	for (i = 0; gc9a01_class_attrs[i].attr.name; i++) {
		ret = class_create_file(gc9a01_class,&gc9a01_class_attrs[i]);
		if (ret < 0)
		{
			pr_err("gc9a01_sysfs_create error !!\n");
			return ret;
		}
	}
	return ret;
	//gc9a01_class->dev_groups = rt5509_cal_groups;
}
#endif

#if HYN_EN_AUTO_UPDATE_CST78xx
/*
 *
 */

int hyn_i2c_write(struct i2c_client *client, char *writebuf, int writelen)
{
    int ret = -1;

    if (client == NULL)
    {
        dev_err(&client_up->dev, "[IIC][%s]i2c_client==NULL!", __func__);
        return -1;
    }
	// client->addr = client->addr & I2C_MASK_FLAG;
    ret = i2c_master_send(client, writebuf, writelen);
	if(ret<0)
		dev_err(&client_up->dev, "i2c_master_send error\n");
    return ret;
}

int hctp_write_bytes(unsigned short reg,unsigned char *buf,unsigned short len,unsigned char reg_len){
	int ret;
    unsigned char mbuf[600];
    if (reg_len == 1){
        mbuf[0] = reg;
        memcpy(mbuf+1,buf,len);
    }else{
        mbuf[0] = reg>>8;
        mbuf[1] = reg;
        memcpy(mbuf+2,buf,len);    
    }

	//printk("gc9a01 HYN client->addr = 0x%x", client_up->addr);

    ret = hyn_i2c_write(client_up,mbuf,len+reg_len);
	if (ret < 0){
		dev_err(&client_up->dev, "%s i2c write error. ret = %d\n", __func__, ret);
	}
    return ret;
}

int hyn_i2c_read(struct i2c_client *client, char *writebuf, int writelen, char *readbuf, int readlen)
{
    int ret = -1;

	//dev_err(&client_up->dev, "HYN client->addr = 0x%x", client->addr);

    if (client == NULL)
    {
        dev_err(&client_up->dev, "[IIC][%s]i2c_client==NULL!", __func__);
        return -1;
    }

	// client->addr = client->addr & I2C_MASK_FLAG;
    ret = i2c_master_send(client, writebuf, writelen);
	if(ret<0)
		dev_err(&client_up->dev, "i2c_master_send error\n");


	// client->addr = (client->addr & I2C_MASK_FLAG) | I2C_WR_FLAG | I2C_RS_FLAG;
	ret = i2c_master_recv(client, readbuf, readlen);
	if(ret < 0){
		dev_err(&client_up->dev, "i2c_master_recv i2c read error.\n");
		return ret;
	}

	//printk("%s end \n",__func__);

    return ret;
}

int hyn_i2c_read_reg(struct i2c_client *client, u8 regaddr, u8 *regvalue)
{
    return hyn_i2c_read(client, &regaddr, 1, regvalue, 1);
}

int hctp_read_bytes(unsigned short reg,unsigned char* buf,unsigned short len,unsigned char reg_len){
	int ret;
    unsigned char reg_buf[2];
    if (reg_len == 1){
        reg_buf[0] = reg;
    }else{
        reg_buf[0] = reg>>8;
        reg_buf[1] = reg;
    }

	//printk("gc9a01 HYN client->addr = 0x%x", client_up->addr);

    ret = hyn_i2c_read(client_up,reg_buf,reg_len,buf,len);
	if (ret < 0){
		dev_err(&client_up->dev, "f%s: i2c read error. ret = %d\n",__func__, ret);
	}
    return ret;
}

static int cst78xx_enter_bootmode(void){
     char retryCnt = 10;

	gc9a01_reset(g_gc9a01_data);
     //mdelay(5);
     while(retryCnt--){
         u8 cmd[3];
         cmd[0] = 0xAB;
         if (-1 == hctp_write_bytes(0xA001,cmd,1,REG_LEN_2B)){  // enter program mode
             mdelay(2); // 4ms
             continue;
         }
         if (-1 == hctp_read_bytes(0xA003,cmd,1,REG_LEN_2B)) { // read flag
             mdelay(2); // 4ms
             continue;
         }else{
             if (cmd[0] != 0xC1){
                 msleep(2); // 4ms
                 continue;
             }else{
                 return 0;
             }
         }
     }
     return -1;
 }
 /*
  *
  */
static int cst78xx_update(u16 startAddr,u16 len,u8* src){
    u16 sum_len;
    u8 cmd[10];
	int ret;

	ret = 0;

    if (cst78xx_enter_bootmode() == -1){
		dev_err(&client_up->dev, "%s cst78xx_enter_bootmode error.\n", __func__);
       return -1;
    }
    sum_len = 0;

    do{
        if (sum_len >= len){
            return -1;
        }

        // send address
        cmd[1] = startAddr>>8;
        cmd[0] = startAddr&0xFF;
        hctp_write_bytes(0xA014,cmd,2,REG_LEN_2B);

#if HYN_MTK_IIC_TRANSFER_LIMIT
	{
		u8 temp_buf[8];
		u16 j,iic_addr;
		iic_addr=0;
		for(j=0; j<128; j++){

	    	temp_buf[0] = *((u8*)src+iic_addr+0);
	    	temp_buf[1] = *((u8*)src+iic_addr+1);
			temp_buf[2] = *((u8*)src+iic_addr+2);
			temp_buf[3] = *((u8*)src+iic_addr+3);

	    	hctp_write_bytes((0xA018+iic_addr),(u8* )temp_buf,4,REG_LEN_2B);
			iic_addr+=4;
			if(iic_addr==512) break;
		}

	}
#else
		hctp_write_bytes(0xA018,src,PER_LEN,REG_LEN_2B);
#endif
        cmd[0] = 0xEE;
        hctp_write_bytes(0xA004,cmd,1,REG_LEN_2B);

		if(apk_upgrade_flag==0)
			msleep(300);
		else
			msleep(100);

        {
            u8 retrycnt = 50;
            while(retrycnt--){
                cmd[0] = 0;
                hctp_read_bytes(0xA005,cmd,1,REG_LEN_2B);
                if (cmd[0] == 0x55){
                    // success
                    break;
                }
                msleep(10);
            }

			if(cmd[0]!=0x55)
			{
				ret = -1;
			}
        }
        startAddr += PER_LEN;
        src       += PER_LEN;
        sum_len   += PER_LEN;
    }while(len);

    // exit program mode
    cmd[0] = 0x00;
    hctp_write_bytes(0xA003,cmd,1,REG_LEN_2B);

	return ret;
}
/*
 *
 */
static u32 cst78xx_read_checksum(u16 startAddr,u16 len){
    union{
        u32 sum;
        u8 buf[4];
    }checksum;
    char cmd[3];
    //char readback[4] = {0};

    if (cst78xx_enter_bootmode() == -1){
       return -1;
    }

    cmd[0] = 0;
    if (-1 == hctp_write_bytes(0xA003,cmd,1,REG_LEN_2B)){
        return -1;
    }
    msleep(500);

    if (-1 == hctp_read_bytes(0xA008,checksum.buf,4,REG_LEN_2B)){
        return -1;
    }
	chip_sumok_flag  = 1;

    return checksum.sum;
}
#endif

#if HYN_EN_AUTO_UPDATE
int ctp_hynitron_update(struct i2c_client *mclient){
    unsigned short startAddr;
    unsigned short length;
    unsigned short checksum;
	unsigned short chipchecksum;
	u8 tp_fm_ver;

	update_fw_flag  = 1;
	chip_sumok_flag = 0;

    client_up = mclient;

	//if(apk_upgrade_flag==0){
	//	read_fw_version(mclient);
	//}

    dev_addr  = client_up->addr;

#if HYN_EN_AUTO_UPDATE_CST78xx

    client_up->addr = 0x6A;
	printk("gc9a01 -------------------- ctp_hynitron_update --------------------  \n");
    if (cst78xx_enter_bootmode() == 0){
		printk("gc9a01 -------------------- ctp_hynitron_update --------------------  11111111\n");
        if(sizeof(app_bin) > 10){

            startAddr = *(p_cst836u_upgrade_firmware+1);

            length =*(p_cst836u_upgrade_firmware+3);

            checksum = *(p_cst836u_upgrade_firmware+5);

            startAddr <<= 8;
			startAddr |= *(p_cst836u_upgrade_firmware+0);

            length <<= 8;
			length |= *(p_cst836u_upgrade_firmware+2);

            checksum <<= 8;
			checksum |= *(p_cst836u_upgrade_firmware+4);

			chipchecksum = cst78xx_read_checksum(startAddr, length);

			//if(update_fw_flag||chip_sumok_flag==0)
			{
				printk("\r\n gc9a01 ctp_hynitron_update:  low version,  updating!!!r\n");
				printk("\r\n  gc9a01 CTP cst78xx File, start-0x%04x len-0x%04x fileCheck-0x%04x\r\n",startAddr,length,checksum);
				if(chipchecksum != checksum){
					cst78xx_update(startAddr, length, (p_cst836u_upgrade_firmware+6));
					length = cst78xx_read_checksum(startAddr, length);
					printk("\r\n gc9a01 CTP cst78xx update %s, checksum-0x%04x",((length==checksum) ? "success" : "fail"),length);

				}else{
					printk("\r\n gc9a01 CTP cst78xx check pass...");
				}
			}
			//else
			//{
			//	printk("\r\nctp_hynitron_update:  high version  not update!!!r\n");
			//}
        }
        goto re;
    }
	else
	{
		printk("gc9a01 -------------------- ctp_hynitron_update --------------------  222222222\n");
		client_up->addr = dev_addr;
		return -1;
	}
#endif

re:
    client_up->addr = dev_addr;
	gc9a01_reset(g_gc9a01_data);
	msleep(50);
    hyn_i2c_read_reg(client_up, HYN_REG_FW_VER, &tp_fm_ver);
	printk("\r\n gc9a01 CTP fw = 0x%02x\n", tp_fm_ver);

    return 0;
}

#endif

static int gc9a01_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct task_struct *thread;
	struct gc9a01_data * gc_data = NULL;
	int err = 0;
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
	unsigned char fw_version = 0;
#endif
	pr_err("gezi-------------%s-----------------%d\n",__func__,__LINE__);

	gc_data = devm_kzalloc(&client->dev, sizeof(*gc_data), GFP_KERNEL);
	if (!gc_data){
		pr_err("gezi------ENOMEM-------%s-----------------%d\n",__func__,__LINE__);
		return -ENOMEM;
	}


	gc9a01_get_gpio(&client->dev,gc_data);
	init_waitqueue_head(&gc_data->waiter);
	mutex_init(&gc_data->i2c_access);
	spin_lock_init(&gc_data->irq_lock);
	gc_data->client = client;
	gc9a01_reset(gc_data);
	msleep(40);
	g_gc9a01_data = gc_data;
/*
	gc_data->vdd = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR_OR_NULL(gc_data->vdd))
		dev_err(&client->dev, "get regulator fail %d\n",PTR_ERR(gc_data->vdd));
	else
		regulator_enable(gc_data->vdd);
*/

    if (gc_data->client->addr != HYN_I2C_SLAVE_ADDR)
    {
        pr_err("gc9a01 i2c addr 0x%02x to 0x%02x", gc_data->client->addr,HYN_I2C_SLAVE_ADDR);
        gc_data->client->addr = HYN_I2C_SLAVE_ADDR;
    }

    pr_err("gc9a01 i2c addr 0x%02x", gc_data->client->addr);

	msleep(150);

	err = gc9a01_get_chip_id(client);
	if (err < 0){
		pr_err("gezi---gc9a01 get sensor id failed--%s---%d\n",__func__,__LINE__);
		return -1;
	}

	gc9a01_input_init(gc_data);
	thread = kthread_run(touch_event_handler, gc_data, "gc9a01_thread");
	if (IS_ERR(thread)) {
		err = PTR_ERR(thread);
		pr_err(" failed to create kernel thread: %d\n",err);
	}

	gc9a01_eint_setup(gc_data);
#ifdef GC9A01_SYS_TEST
	gc9a01_sysfs_create();
#endif
	gc9a01_irq_disable(g_gc9a01_data);

#if HYN_EN_AUTO_UPDATE
	p_cst836u_upgrade_firmware=(unsigned char *)app_bin;
	apk_upgrade_flag=0;
    err = ctp_hynitron_update(client);
	gc9a01_reset(g_gc9a01_data);
	msleep(270);
	pr_err("gc9a01 -------- cst820_probe FW update-----------\n");
	gc_data->client->addr = HYN_I2C_SLAVE_ADDR;
#endif

#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
	gc9a01_read(client,0xA9,&fw_version);
	sprintf(current_sub_tp_info.chip,"CST816T,FW_VER=0x%02x",fw_version);
    sprintf(current_sub_tp_info.id,"hynitron");
    strcpy(current_sub_tp_info.vendor,"EDO");
    sprintf(current_sub_tp_info.more,"%d*%d",320,172);
#endif

	gc9a01_i2c_is_probe_ok = true;

	gc9a01_resume();

	return 0;
}

static void gc9a01_remove(struct i2c_client *client) 
{
	return;
}



static const struct i2c_device_id gc9a01_dev_id[] = {
    {"gc9a01_touch", 0},
    {},
};
MODULE_DEVICE_TABLE(i2c,gc9a01_dev_id);

static struct i2c_driver gc9a01_driver = {
    .driver   = {
        .name           = DEVICE_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = gc9a01_match_table,
    },
    .probe    = gc9a01_probe,
    .remove   = gc9a01_remove,
    .id_table = gc9a01_dev_id,
};
//module_i2c_driver(gc9a01_driver);
static int __init gc9a01_touch_init(void)
{
	int ret = 0;
	pr_err("gezi--------%s---\n",__func__);

	ret = i2c_add_driver(&gc9a01_driver);

	return ret;
}
static void __exit gc9a01_touch_exit(void)
{
	i2c_del_driver(&gc9a01_driver);
}
late_initcall_sync(gc9a01_touch_init);
module_exit(gc9a01_touch_exit);

MODULE_AUTHOR("zhaopengge@cooseagroup.com");
MODULE_DESCRIPTION("GC9A01 TP DRIVER");
MODULE_LICENSE("GPL v2");

