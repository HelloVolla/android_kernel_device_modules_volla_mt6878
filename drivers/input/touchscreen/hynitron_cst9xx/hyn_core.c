#include "hyn_core.h"
#if IS_ENABLED(CONFIG_CS_NOTIFIER)
#include <linux/cs_notifier.h>
#endif
#define HYN_DRIVER_NAME  "hyn_ts"

static struct hyn_ts_data *hyn_data = NULL;
static const struct hyn_ts_fuc* hyn_fun = NULL;
static const struct of_device_id hyn_of_match_table[] = {
    {.compatible = "hyn,92xx", .data = &cst92xx_fuc,},  /*suport 9217、9220 */
    {},
};
MODULE_DEVICE_TABLE(of, hyn_of_match_table);

static int hyn_check_ic(struct hyn_ts_data *ts_data)
{
    const struct of_device_id *of_dev;
    of_dev = of_match_device(hyn_of_match_table,ts_data->dev);
	if (!of_dev)
		return -EINVAL;
    hyn_fun = of_dev->data;
    if(IS_ERR_OR_NULL(hyn_fun) || IS_ERR_OR_NULL(hyn_fun->tp_chip_init)
        || hyn_fun->tp_chip_init(ts_data)){
        return -ENODEV;
    }
    ts_data->hyn_fuc_used = hyn_fun;
    return 0;
}

static int hyn_parse_dt(struct hyn_ts_data *ts_data)
{
    int ret = 0;
    struct device *dev = ts_data->dev;
    struct hyn_plat_data* dt = &ts_data->plat_data;
    if(dev->of_node){
        u32 buf[8];
        struct device_node *np = dev->of_node;
#if HYN_POWER_SOURCE_CUST_EN
        dt->vdd_i2c = regulator_get(dev, "vcc_i2c");
        dt->vdd_ana = regulator_get(dev, "vdd_ana");
        if(IS_ERR_OR_NULL(dt->vdd_i2c) || IS_ERR_OR_NULL(dt->vdd_ana)){
            HYN_ERROR("regulator_get failed");
            return -ENODEV;
        }
#endif
        dt->reset_gpio = of_get_named_gpio_flags(np, "reset-gpio", 0, &dt->reset_gpio_flags);
        dt->irq_gpio = of_get_named_gpio_flags(np, "irq-gpio", 0, &dt->irq_gpio_flags);
        //dt->power_gpio = of_get_named_gpio_flags(np, "power-gpio", 0, &dt->power_gpio_flags);
        if(dt->reset_gpio < 0 || dt->irq_gpio < 0){
            HYN_ERROR("dts get gpio failed");
            return -ENODEV;
        }
        else{
            HYN_INFO("reset_gpio:%d irq_gpio:%d",dt->reset_gpio,dt->irq_gpio);
        }

        dt->pinctl = devm_pinctrl_get(dev);
        if (!IS_ERR_OR_NULL(dt->pinctl)){
            dt->pin_active = pinctrl_lookup_state(dt->pinctl, "ts_active");
            dt->pin_suspend= pinctrl_lookup_state(dt->pinctl, "ts_suspend");
            if(IS_ERR_OR_NULL(dt->pin_active) || IS_ERR_OR_NULL(dt->pin_suspend)){
                HYN_ERROR("dts get \"ts-active\" \"ts_suspend\" failed");
                return -EINVAL;
            }
        }
        else{
            HYN_INFO("pinctrl not config");
            dt->pinctl = NULL;
            dt->pin_active = NULL;
            dt->pin_suspend= NULL;
        }

        ret = of_property_read_u32(np, "max-touch-number", &dt->max_touch_num);
        ret |= of_property_read_u32(np, "pos-swap", &dt->swap_xy);
        ret |= of_property_read_u32(np, "posx-reverse", &dt->reverse_x);
        ret |= of_property_read_u32(np, "posy-reverse", &dt->reverse_y);
        ret |= of_property_read_u32_array(np, "display-coords", buf, 4);
        dt->x_resolution = buf[2];
        dt->y_resolution = buf[3];
        HYN_INFO("dts x_res = %d,y_res = %d,touch-number = %d",dt->x_resolution,dt->y_resolution,dt->max_touch_num);
        if(ret < 0){
            HYN_ERROR("dts get screen failed");
            return -EINVAL;
        }

        ret = of_property_read_u32(np, "key-number", &dt->key_num);
        if(ret>=0 && dt->key_num && dt->key_num<=8){
            ret |= of_property_read_u32(np, "key-y-coord", &dt->key_y_coords);
            ret |= of_property_read_u32_array(np, "key-x-coords", dt->key_x_coords, dt->key_num);
            ret |= of_property_read_u32_array(np, "keys", dt->key_code, dt->key_num);
            if(ret < 0){
                HYN_ERROR("dts get screen failed");
                return -EINVAL;
            }
        }
        else{
            HYN_INFO("key not config");
            dt->key_num = 0;
        }
        return 0;
    }
    else{
        HYN_ERROR("dts match failed");
        return -ENODEV;
    }
}

static int hyn_power_source_ctrl(int enable)
{
    int ret = 0;
#if HYN_POWER_SOURCE_CUST_EN
    //HYN_ENTER();
    HYN_INFO("hyn_power_source_ctrl to %d\n", enable);
    if(hyn_data->power_is_on != enable){
        if(enable){
            if(regulator_is_enabled(hyn_data->plat_data.vdd_ana)==0)
                ret |= regulator_enable(hyn_data->plat_data.vdd_ana);
            if(regulator_is_enabled(hyn_data->plat_data.vdd_i2c)==0)
                ret |= regulator_enable(hyn_data->plat_data.vdd_i2c);
            gpio_set_value(hyn_data->plat_data.reset_gpio, 1);
        }
        else{
            if(regulator_is_enabled(hyn_data->plat_data.vdd_ana)>0)
                ret |= regulator_disable(hyn_data->plat_data.vdd_ana);
            if(regulator_is_enabled(hyn_data->plat_data.vdd_i2c)>0)
                ret |= regulator_disable(hyn_data->plat_data.vdd_i2c);
            gpio_set_value(hyn_data->plat_data.reset_gpio, 0);
        }
        hyn_data->power_is_on = enable;
    }
    if(ret)
        HYN_ERROR("set vdd %s regulator failed,ret=%d",enable ? "off":"on",ret);
    return ret;
#else
    //HYN_ENTER();
    HYN_INFO("hyn_power_source_ctrl to %d\n", enable);
    if(hyn_data->power_is_on != enable){
        if(enable){
            //gpio_set_value(hyn_data->plat_data.power_gpio, 1);
            gpio_set_value(hyn_data->plat_data.reset_gpio, 1);
        }
        else{
            //gpio_set_value(hyn_data->plat_data.power_gpio, 0);
            gpio_set_value(hyn_data->plat_data.reset_gpio, 0);
        }
        hyn_data->power_is_on = enable;
    }

    return ret;
#endif
}

static int hyn_poweron(struct hyn_ts_data *ts_data)
{
    int ret = 0;
    struct hyn_plat_data* dt = &ts_data->plat_data;
    if(!IS_ERR_OR_NULL(hyn_data->plat_data.pinctl)){
        if(pinctrl_select_state(dt->pinctl, dt->pin_active)){
            HYN_ERROR("pin active set failed");
        }
    }

    ret = gpio_request(dt->irq_gpio, "hyn_irq_gpio");
    ret |= gpio_request(dt->reset_gpio, "hyn_reset_gpio");
    //ret |= gpio_request(dt->power_gpio, "hyn_power_gpio");
    if(ret < 0){
        HYN_ERROR("gpio_request failed");
        goto GPIO_SET_FAILE;
    }

    ret = gpio_direction_input(dt->irq_gpio);
    ret |= gpio_direction_output(dt->reset_gpio, 0);
    //ret |= gpio_direction_output(dt->power_gpio, 1);
    if(ret < 0){
        HYN_ERROR("set gpio_direction failed");
        goto GPIO_SET_FAILE;
    }

#if HYN_POWER_SOURCE_CUST_EN
    ret = -4;
    if(regulator_count_voltages(dt->vdd_ana) > 0 && regulator_count_voltages(dt->vdd_i2c) > 0){
        ret = regulator_set_voltage(dt->vdd_ana, 2850000, 2850000);
        ret |= regulator_set_voltage(dt->vdd_i2c, 1800000, 1800000);
        if(ret ==0){
            ret = hyn_power_source_ctrl(1);
        }
    }
    if(ret){
        hyn_power_source_ctrl(0);
        regulator_put(dt->vdd_ana);
        regulator_put(dt->vdd_i2c);
        dt->vdd_i2c = NULL;
        dt->vdd_ana = NULL;
        HYN_ERROR("set regulator failed ret = %d",ret);
    }
    else{
        HYN_INFO("set regulator success");
    }
#endif
    hyn_power_source_ctrl(1);
    mdelay(5);
    gpio_set_value(dt->reset_gpio, 1);
    return 0;
GPIO_SET_FAILE:
    return ret;
}

static int hyn_input_dev_init(struct hyn_ts_data *ts_data)
{
    int key_num = 0;//,ret =0;
    struct hyn_plat_data *dt = &ts_data->plat_data;
    struct input_dev *input_dev;

     HYN_INFO("hyn_input_dev_init enter ");

    input_dev = input_allocate_device();
    if (!input_dev) {
        HYN_ERROR("Failed to allocate memory for input device");
        return -ENOMEM;
    }
    input_dev->name = HYN_DRIVER_NAME;
    input_dev->id.bustype = ts_data->bus_type;
    input_dev->dev.parent = ts_data->dev;
    input_set_drvdata(input_dev, ts_data);

    __set_bit(EV_SYN, input_dev->evbit);
    __set_bit(EV_ABS, input_dev->evbit);
    __set_bit(EV_KEY, input_dev->evbit);
    __set_bit(BTN_TOUCH, input_dev->keybit);
    __set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

    for (key_num = 0; key_num < dt->key_num; key_num++)
			input_set_capability(input_dev, EV_KEY, dt->key_code[key_num]);
#if HYN_MT_PROTOCOL_B_EN
    set_bit(BTN_TOOL_FINGER,input_dev->keybit);
    //input_mt_init_slots(input_dev, dt->max_touch_num);
    input_mt_init_slots(input_dev, dt->max_touch_num, INPUT_MT_DIRECT);
#else
    input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
#endif

	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0,  dt->max_touch_num, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, dt->x_resolution,0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, dt->y_resolution,0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 0xFF, 0, 0);

    ts_data->input_dev = input_dev;

    return 0;
}

static void release_all_finger(struct hyn_ts_data *ts_data)
{
#if HYN_MT_PROTOCOL_B_EN
	u8 i;
	for(i=0; i< ts_data->plat_data.max_touch_num; i++) {
		input_mt_slot(ts_data->input_dev, i);
		input_report_abs(ts_data->input_dev, ABS_MT_TRACKING_ID, -1);
		input_mt_report_slot_state(ts_data->input_dev, MT_TOOL_FINGER, false);
	}
	input_report_key(ts_data->input_dev, BTN_TOUCH, 0);
#else
    input_report_key(ts_data->input_dev, BTN_TOUCH, 0);
	input_mt_sync(ts_data->input_dev);
#endif
    HYN_INFO("release_all_finger");
}

static void touch_updata(u8 idx,u8 event)
{
    struct ts_frame *rep_frame = &hyn_data->rp_buf;
    struct input_dev *dev = hyn_data->input_dev;
    u16 zpress = rep_frame->pos_info[idx].pres_z;
#if HYN_MT_PROTOCOL_B_EN
    if(event){
        input_mt_slot(dev, rep_frame->pos_info[idx].pos_id);
        input_mt_report_slot_state(dev, MT_TOOL_FINGER, 1);
        input_report_abs(dev, ABS_MT_TRACKING_ID, rep_frame->pos_info[idx].pos_id);
        input_report_abs(dev, ABS_MT_POSITION_X, rep_frame->pos_info[idx].pos_x);
        input_report_abs(dev, ABS_MT_POSITION_Y, rep_frame->pos_info[idx].pos_y);
        input_report_abs(dev, ABS_MT_TOUCH_MAJOR, zpress>>3);
        input_report_abs(dev, ABS_MT_WIDTH_MAJOR, zpress>>3);
        input_report_abs(dev, ABS_MT_PRESSURE, zpress);
    }
    else{
        input_mt_slot(dev, rep_frame->pos_info[idx].pos_id);
        input_report_abs(dev, ABS_MT_TRACKING_ID, -1);
        input_mt_report_slot_state(dev, MT_TOOL_FINGER, 0);
    }
#else
    if(event){
        input_report_key(dev, BTN_TOUCH, 1);
        input_report_abs(dev, ABS_MT_PRESSURE, zpress);
        input_report_abs(dev, ABS_MT_TRACKING_ID, rep_frame->pos_info[idx].pos_id);
        input_report_abs(dev, ABS_MT_TOUCH_MAJOR, zpress>>3);
        // input_report_abs(dev, ABS_MT_WIDTH_MAJOR, zpress>>3);
        input_report_abs(dev, ABS_MT_POSITION_X, rep_frame->pos_info[idx].pos_x);
        input_report_abs(dev, ABS_MT_POSITION_Y, rep_frame->pos_info[idx].pos_y);
        input_mt_sync(dev);
    }
    else{
        input_report_key(dev, BTN_TOUCH, 0);
        input_mt_sync(dev);
    }
#endif
}

static void hyn_irq_report_work(struct work_struct *work)
{
    struct hyn_ts_data *ts_data = hyn_data;
    struct ts_frame *rep_frame = &hyn_data->rp_buf;
    struct input_dev *dev = hyn_data->input_dev;
    struct hyn_plat_data *dt = &hyn_data->plat_data;
    u16 xpos,ypos;
    hyn_fun->tp_report();
    mutex_lock(&ts_data->mutex_report);
    if(rep_frame->report_need & REPORT_KEY){ //key
#if KEY_USED_POS_REPORT
        rep_frame->pos_info[0].pos_id = 0;
        rep_frame->pos_info[0].pos_x = dt->key_x_coords[rep_frame->key_id];
        rep_frame->pos_info[0].pos_y = dt->key_y_coords;
        rep_frame->pos_info[0].pres_z = 100;
        touch_updata(0,rep_frame->key_state ? 1:0);
#else
        input_report_key(dev,dt->key_code[rep_frame->key_id],rep_frame->key_state ? 1:0);
#endif
        input_sync(dev);
        // HYN_INFO("report key");
    }

    if(rep_frame->report_need & REPORT_POS){ //pos
        u8 i;
        if(rep_frame->rep_num == 0){
            release_all_finger(ts_data);
        }
        else{
            u8 touch_down = 0;
            for(i = 0; i < rep_frame->rep_num; i++){
                if(dt->swap_xy){
                    xpos = rep_frame->pos_info[i].pos_y;
                    ypos = rep_frame->pos_info[i].pos_x;
                }
                else{
                    xpos = rep_frame->pos_info[i].pos_x;
                    ypos = rep_frame->pos_info[i].pos_y;
                }
                if(ypos > dt->y_resolution || xpos > dt->x_resolution || rep_frame->pos_info[i].pos_id >= ts_data->plat_data.max_touch_num){
                    HYN_ERROR("Please check dts or FW config !!!");
                    continue;
                }
                if(dt->reverse_x){
                    xpos = dt->x_resolution-xpos;
                }
                if(dt->reverse_y){
                    ypos = dt->y_resolution-ypos;
                }
                rep_frame->pos_info[i].pos_x = xpos;
                rep_frame->pos_info[i].pos_y = ypos;
                touch_updata(i,rep_frame->pos_info[i].event? 1:0);
                if(rep_frame->pos_info[i].event) touch_down++;
            }
#if HYN_MT_PROTOCOL_B_EN
            input_report_key(dev, BTN_TOUCH, touch_down ? 1:0);
#endif
        }
        input_sync(dev);
    }
#if (HYN_GESTURE_EN)
    else if(rep_frame->report_need & REPORT_GES){
        hyn_gesture_report(ts_data);
    }
#endif
/* pri LAX10-1106 modify by ocean 20240704 start */
    if(rep_frame->report_need == REPORT_GES){
        release_all_finger(ts_data);
    }
/* pri LAX10-1106 modify by ocean 20240704 end */
    mutex_unlock(&ts_data->mutex_report);
}

static void hyn_esdcheck_work(struct work_struct *work)
{
#if ESD_CHECK_EN
    int ret;
    //HYN_ENTER();
    ret = hyn_fun->tp_check_esd();
    HYN_INFO("esd:%04x",ret);
    if(hyn_data->esd_last_value != ret){
        hyn_data->esd_fail_cnt = 0;
        hyn_data->esd_last_value = ret;
    }
    else{
        hyn_data->esd_fail_cnt++;
        if(hyn_data->esd_fail_cnt > 2){
            hyn_data->esd_fail_cnt = 0;
            hyn_power_source_ctrl(0);
            mdelay(1);
            hyn_power_source_ctrl(1);
            hyn_fun->tp_rest();
        }
    }
    queue_delayed_work(hyn_data->hyn_workqueue, &hyn_data->esdcheck_work,
                           msecs_to_jiffies(1000));
#endif
}

/* pri KMXGK-7 added by ocean 20240803 begin*/
static void hyn_timing_esdcheck_work(struct work_struct *work)
{
    int ret;
    hyn_power_source_ctrl(0);
    mdelay(1);
    hyn_power_source_ctrl(1);
    hyn_fun->tp_rest();
/* pri LAX10-1466 added by ocean 20240907 begin*/
    release_all_finger(hyn_data);
/* pri LAX10-1466 added by ocean 20240907 end*/
    mdelay(40);
    if (hyn_data->work_mode == GESTURE_MODE) {
            HYN_INFO("gesture mode\n");
            hyn_irq_set(hyn_data,DISABLE);
            ret = enable_irq_wake(hyn_data->client->irq);
            ret |= irq_set_irq_type(hyn_data->client->irq,IRQF_TRIGGER_FALLING|IRQF_NO_SUSPEND|IRQF_ONESHOT);
            if(ret < 0) {
                HYN_ERROR("gesture irq_set_irq failed");
            }
            for (int retry =0;retry < 3; retry++) {
                ret = hyn_fun->tp_set_workmode(GESTURE_MODE,0);
                if (ret != FALSE) {
                    HYN_INFO("set gesture succeeded\n");
                    hyn_irq_set(hyn_data,ENABLE);
                    hyn_data->work_mode = GESTURE_MODE;
                    break;
                } else {
                    HYN_ERROR("set gesture fail\n");
                     msleep(10);
                }
            }
    }
}
/* pri KMXGK-7 added by ocean 20240803 end*/
static void hyn_resum(struct device *dev)
{
    int ret = 0;
    HYN_ENTER();
    if(!IS_ERR_OR_NULL(hyn_data->plat_data.pinctl)){
      pinctrl_select_state(hyn_data->plat_data.pinctl, hyn_data->plat_data.pin_active);
    }
    hyn_power_source_ctrl(1);
    hyn_fun->tp_resum();
    if(hyn_data->prox_mode_en){
        hyn_fun->tp_prox_handle(1);
    }
    else if(hyn_data->gesture_is_enable){
        hyn_irq_set(hyn_data,DISABLE);
/* pri KMXGK-7 added by ocean 20240803 begin*/
        hyn_data->work_mode = NOMAL_MODE;
/* pri KMXGK-7 added by ocean 20240803 end*/
        ret = disable_irq_wake(hyn_data->client->irq);
/* pri LAX10-530 added by ocean 20240510 start*/
        ret |= irq_set_irq_type(hyn_data->client->irq,IRQ_TYPE_EDGE_FALLING);
/* pri LAX10-530 added by ocean 20240510 end*/
        if(ret < 0){
            HYN_ERROR("gesture irq_set_irq failed");
        }
        hyn_irq_set(hyn_data,ENABLE);
    }
}

static void hyn_suspend(struct device *dev)
{
    int ret = 0;
    HYN_ENTER();
    release_all_finger(hyn_data);
    input_sync(hyn_data->input_dev);
    if(hyn_data->prox_mode_en ==1){
    }
    else if(hyn_data->gesture_is_enable){
        hyn_irq_set(hyn_data,DISABLE);
/* pri KMXGK-7 added by ocean 20240803 begin*/
        hyn_data->work_mode = GESTURE_MODE;
/* pri KMXGK-7 added by ocean 20240803 end*/
        ret = enable_irq_wake(hyn_data->client->irq);
        ret |= irq_set_irq_type(hyn_data->client->irq,IRQF_TRIGGER_FALLING|IRQF_NO_SUSPEND|IRQF_ONESHOT); 
        if(ret < 0){
            HYN_ERROR("gesture irq_set_irq failed");
        }  
        hyn_fun->tp_set_workmode(GESTURE_MODE,0);
        hyn_irq_set(hyn_data,ENABLE);
        hyn_power_source_ctrl(1);
    }
    else{
        hyn_fun->tp_supend();
        if(!IS_ERR_OR_NULL(hyn_data->plat_data.pinctl)){
            pinctrl_select_state(hyn_data->plat_data.pinctl, hyn_data->plat_data.pin_suspend);
        }
        hyn_power_source_ctrl(0);
    }
}


static void hyn_updata_fw_work(struct work_struct *work)
{
    int ret = 0;
    hyn_esdcheck_switch(hyn_data,DISABLE);
    if(!IS_ERR_OR_NULL(hyn_data->fw_updata_addr)){
        ret = hyn_fun->tp_updata_fw(hyn_data->fw_updata_addr,hyn_data->fw_updata_len);
    }
    else{
        HYN_ERROR("fw_updata_addr is erro");
    }
    hyn_esdcheck_switch(hyn_data,ENABLE);
    
}

static void hyn_resum_work(struct work_struct *work)
{
    hyn_resum(hyn_data->dev);
}

static irqreturn_t hyn_irq_handler(int irq, void *data)
{
	atomic_set(&hyn_data->hyn_irq_flg,1);
    if(hyn_data->work_mode < DIFF_MODE){
        queue_work(hyn_data->hyn_workqueue,&hyn_data->work_report);
    }
    else{
        wake_up(&hyn_data->wait_irq);
    }
    return IRQ_HANDLED;
}

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
static int cs_notifier_callback(struct notifier_block *nb,
                               unsigned long event, void *data)
{
    struct panel_event_blank_data *lcd_tp_event = data;
    int blank_value = lcd_tp_event->blank;

    HYN_INFO("notifier,event:%lu,blank:%d", event, blank_value);

    switch(event) {
    case CS_PANEL_EARLY_EVENT_BLANK:
        if (blank_value == PANEL_BLANK_UNBLANK) {
            queue_work(hyn_data->hyn_workqueue, &hyn_data->work_resume);
        }
        break;
    case CS_PANEL_EVENT_BLANK:
        switch(blank_value) {
        case PANEL_BLANK_POWERDOWN:
        case PANEL_BLANK_DOZE_ENABLE:
            cancel_work_sync(&hyn_data->work_resume);
            hyn_suspend(hyn_data->dev);
            break;
        case PANEL_BLANK_DOZE_DISABLE:
            queue_work(hyn_data->hyn_workqueue, &hyn_data->work_resume);
            break;
        }
        break;
    default:
        HYN_INFO("notifier,event:%lu,blank:%d, not care", event, blank_value);
        break;
    }

    return 0;
}
#else
/* pri LAX10-192 added by xuejian 20240418 begin */
static int fb_notifier_callback(struct notifier_block *nb,
		unsigned long event, void *v)
{
    const unsigned long event_enum[2] = {MTK_DISP_EARLY_EVENT_BLANK, MTK_DISP_EVENT_BLANK};
    const int blank_enum[2] = {MTK_DISP_BLANK_POWERDOWN, MTK_DISP_BLANK_UNBLANK};
    int blank_value = *((int *)v);

    HYN_INFO("notifier,event:%lu,blank:%d", event, blank_value);
    if ((blank_enum[1] == blank_value) && (event_enum[1] == event)) {
        queue_work(hyn_data->hyn_workqueue, &hyn_data->work_resume);
    } else if ((blank_enum[0] == blank_value) && (event_enum[0] == event)) {
        cancel_work_sync(&hyn_data->work_resume);
        hyn_suspend(hyn_data->dev);
    } else {
        HYN_INFO("notifier,event:%lu,blank:%d, not care", event, blank_value);
    }

    return 0;
}
/* pri LAX10-192 added by xuejian 20240418 end */
#endif

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
/* pri KMXGK-7 added by ocean 20240803 begin*/
static int cs_timing_esdcheck_callback(struct notifier_block *nb,
                               unsigned long event, void *data)
{
    struct panel_event_blank_data *lcd_tp_event = data;
    int blank_value = lcd_tp_event->blank;

    HYN_INFO("main panel notifier,event:%lu,blank:%d", event, blank_value);

    switch(event) {
    case CS_PANEL_EARLY_EVENT_BLANK:
        if (blank_value == PANEL_BLANK_UNBLANK) {
            queue_delayed_work(hyn_data->hyn_workqueue, &hyn_data->timing_esdcheck_work,
                msecs_to_jiffies(0));
        }
        break;
    default:
        HYN_INFO("main panel notifier,event:%lu,blank:%d, not care", event, blank_value);
        break;
    }

    return 0;
}
/* pri KMXGK-7 added by ocean 20240803 end*/
#endif

#ifdef I2C_PORT
static void hyn_ts_remove(struct i2c_client *client);
static int hyn_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
#else
static void hyn_ts_remove(struct spi_device *client);
static int hyn_ts_probe(struct spi_device *client)
#endif
{
    int ret = 0;
    u16 bus_type;
    struct hyn_ts_data *ts_data = 0;

    HYN_ENTER();
#ifdef I2C_PORT
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        HYN_INFO("I2C not supported");
        return -ENODEV;
    }
    bus_type = BUS_I2C;
#else
    client->mode = SPI_MODE;
	client->max_speed_hz = SPI_CLOCK_FREQ;
	client->bits_per_word = 8;
    if (spi_setup(client) < 0){
        HYN_INFO("SPI not supported");
        return -ENODEV;
    }
    bus_type = BUS_SPI;
#endif
    if(!IS_ERR_OR_NULL(hyn_data)){
        HYN_INFO("other dev is insmode");
        return -ENOMEM;
    }
    ts_data = kzalloc(sizeof(*ts_data), GFP_KERNEL);
    if (!ts_data) {
        HYN_ERROR("alloc ts data failed");
        return -ENOMEM;
    }
    ts_data->bus_type = bus_type;
    ts_data->rp_buf.key_id = 0xFF;
    ts_data->work_mode = NOMAL_MODE;
    
    hyn_data = ts_data;
    ts_data->client = client;
    ts_data->dev = &client->dev;
    dev_set_drvdata(ts_data->dev, ts_data);

    ret = hyn_parse_dt(ts_data);
    if(ret){
        HYN_ERROR("hyn_parse_dt failed");
        goto FREE_RESOURCE;
    }

    ret = hyn_poweron(ts_data);
    if(ret){
        HYN_ERROR("hyn_poweron failed");
        goto FREE_RESOURCE;
    }
    // spin_lock_init(&ts_data->irq_lock);
    mutex_init(&ts_data->mutex_report);
    mutex_init(&ts_data->mutex_bus);
    mutex_init(&ts_data->mutex_fs);
    init_waitqueue_head(&ts_data->wait_irq);

    ret = hyn_check_ic(ts_data);
    if(ret){
        HYN_ERROR("hyn_check_ic failed");
        goto FREE_RESOURCE;
    }

    INIT_WORK(&ts_data->work_report,hyn_irq_report_work);
    INIT_WORK(&ts_data->work_updata_fw,hyn_updata_fw_work);
    INIT_WORK(&ts_data->work_resume,hyn_resum_work);
    INIT_DELAYED_WORK(&ts_data->esdcheck_work,hyn_esdcheck_work);
/* pri KMXGK-7 added by ocean 20240803 begin*/
    INIT_DELAYED_WORK(&ts_data->timing_esdcheck_work,hyn_timing_esdcheck_work);
/* pri KMXGK-7 added by ocean 20240803 end*/
    ts_data->hyn_workqueue = create_singlethread_workqueue("hyn_wq");
    if (IS_ERR_OR_NULL(ts_data->hyn_workqueue)){
        HYN_ERROR("create work queue failed");
        goto FREE_RESOURCE;
    }
    ret = hyn_input_dev_init(ts_data);
    if(ret){
        if(!IS_ERR_OR_NULL(ts_data->input_dev)){
            input_set_drvdata(ts_data->input_dev, NULL);
            input_free_device(ts_data->input_dev);
            ts_data->input_dev = NULL;
        }
        HYN_ERROR("hyn_input_dev_init failed");
        goto FREE_RESOURCE;
    }
    ret = input_register_device(ts_data->input_dev);
    if(ret){
        HYN_ERROR("input_register_device failed");
        goto FREE_RESOURCE;
    }

#if (HYN_GESTURE_EN)
    ret = hyn_gesture_init(ts_data);
    if(ret){
        HYN_ERROR("gesture_init failed");
        goto FREE_RESOURCE;
    }
#endif

    ts_data->gpio_irq =  gpio_to_irq(ts_data->plat_data.irq_gpio);
    ret = request_threaded_irq(ts_data->gpio_irq, NULL, hyn_irq_handler,
                                (IRQF_TRIGGER_FALLING | IRQF_ONESHOT), HYN_DRIVER_NAME, ts_data);
    if(ret){
        HYN_ERROR("request_threaded_irq failed");
        goto FREE_RESOURCE;
    }
    atomic_set(&ts_data->irq_is_disable,ENABLE);
    hyn_irq_set(ts_data , DISABLE);
#if IS_ENABLED(CONFIG_CS_NOTIFIER)
    ts_data->fb_notif.notifier_call = cs_notifier_callback;
        ret = cs_sub_panel_notifier_register(&ts_data->fb_notif);
    if (ret) {
        HYN_ERROR("register cs_notifier failed: %d", ret);
    }
/* pri KMXGK-7 added by ocean 20240803 begin*/
    ts_data->panel_notif.notifier_call = cs_timing_esdcheck_callback;
    ret = cs_panel_notifier_register(&ts_data->panel_notif);
    if (ret) {
        HYN_ERROR("register panel_notifier failed: %d", ret);
    }
/* pri KMXGK-7 added by ocean 20240803 end*/
#else
    /* pri LAX10-192 added by xuejian 20240418 begin */
    ts_data->fb_notif.notifier_call = fb_notifier_callback;
    ret = mtk_disp_sub_notifier_register("hyn", &ts_data->fb_notif);
    if (ret) {
        HYN_ERROR("register fb_notifier failed: %d", ret);
    }
    /* pri LAX10-192 added by xuejian 20240418 end */
#endif
    hyn_create_sysfs(ts_data);
#if (HYN_APK_DEBUG_EN)
    hyn_tool_fs_int(ts_data);
#endif
    hyn_irq_set(ts_data,ENABLE);
    hyn_esdcheck_switch(ts_data,ENABLE);

#if HYN_POWER_ON_UPDATA
    if(ts_data->need_updata_fw){
        queue_work(ts_data->hyn_workqueue,&ts_data->work_updata_fw);
    }
#endif
    return 0;
FREE_RESOURCE:
    hyn_ts_remove(client);
    return -1;
}

#ifdef I2C_PORT
static void hyn_ts_remove(struct i2c_client *client)
#else
static void hyn_ts_remove(struct spi_device *client)
#endif
{
    struct hyn_ts_data *ts_data = hyn_data;
    HYN_ENTER();    
    if(!IS_ERR_OR_NULL(ts_data)){
        if(ts_data->gpio_irq != 0)
            free_irq(ts_data->gpio_irq, ts_data);
        if (ts_data->hyn_workqueue){
            flush_workqueue(ts_data->hyn_workqueue);
            hyn_esdcheck_switch(ts_data,DISABLE);
            destroy_workqueue(ts_data->hyn_workqueue);
        } 
        HYN_INFO("ts_remove1");
        if(!IS_ERR_OR_NULL(ts_data->input_dev)){
            input_unregister_device(ts_data->input_dev);
        }
        HYN_INFO("ts_remove2");

#if IS_ENABLED(CONFIG_CS_NOTIFIER)
    cs_sub_panel_notifier_unregister(&ts_data->fb_notif);
/* pri KMXGK-7 added by ocean 20240803 begin*/
    cs_panel_notifier_unregister(&ts_data->panel_notif);
/* pri KMXGK-7 added by ocean 20240803 end*/
#else
    mtk_disp_notifier_unregister(&ts_data->fb_notif);
#endif

#if defined(CONFIG_FB) 
        fb_unregister_client(&ts_data->fb_notif);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
        unregister_early_suspend(&ts_data->early_suspend);
#endif
        if(gpio_is_valid(ts_data->plat_data.irq_gpio))
            gpio_free(ts_data->plat_data.irq_gpio);
        if(gpio_is_valid(ts_data->plat_data.reset_gpio))
            gpio_free(ts_data->plat_data.reset_gpio);
        HYN_INFO("ts_remove3"); 
        if(!IS_ERR_OR_NULL(ts_data->plat_data.vdd_ana) && !IS_ERR_OR_NULL(ts_data->plat_data.vdd_i2c)){
            hyn_power_source_ctrl(0);
            regulator_put(ts_data->plat_data.vdd_ana);
            regulator_put(ts_data->plat_data.vdd_i2c);
        }
        HYN_INFO("ts_remove5");
#if (HYN_APK_DEBUG_EN)
        hyn_tool_fs_exit();
#endif
        HYN_INFO("ts_remove6");
#if (HYN_GESTURE_EN)
        hyn_gesture_exit(ts_data);
#endif
        hyn_release_sysfs(ts_data);
        HYN_INFO("ts_remove7");
        kfree(ts_data);
        hyn_data = NULL;
        HYN_INFO("ts_remove8");
    }
    return ;
}

#ifdef I2C_PORT
static const struct i2c_device_id hyn_id_table[] = {
    {.name = HYN_DRIVER_NAME, .driver_data = 0,},
    {},
};

static struct i2c_driver hyn_ts_driver = {
    .probe = hyn_ts_probe,
    .remove = hyn_ts_remove,
    .driver = {
        .name = HYN_DRIVER_NAME,
        .owner = THIS_MODULE,
        .of_match_table = hyn_of_match_table,
    },
    .id_table = hyn_id_table,
};
#else
static struct spi_driver hyn_ts_driver = {
	.driver = {
		   .name = HYN_DRIVER_NAME,
		   .of_match_table = hyn_of_match_table,
		   .owner = THIS_MODULE,
		   },
	.probe = hyn_ts_probe,
	.remove = hyn_ts_remove,
};
#endif

static int __init hyn_ts_init(void)
{
    int ret = 0;
    HYN_ENTER();
#ifdef I2C_PORT  
    ret = i2c_add_driver(&hyn_ts_driver);
#else
    ret = spi_register_driver(&hyn_ts_driver);
#endif
    if (ret) {
        HYN_INFO("add i2c driver failed");
        return -ENODEV;
    }
    return 0;
}

static void __exit hyn_ts_exit(void)
{
    HYN_ENTER();
#ifdef I2C_PORT  
    i2c_del_driver(&hyn_ts_driver);
#else
    spi_unregister_driver(&hyn_ts_driver);
#endif
}

late_initcall(hyn_ts_init);
// module_init(hyn_ts_init);
module_exit(hyn_ts_exit);

MODULE_AUTHOR("Hynitron Driver Team");
MODULE_DESCRIPTION("Hynitron Touchscreen Driver");
MODULE_LICENSE("GPL v2");

