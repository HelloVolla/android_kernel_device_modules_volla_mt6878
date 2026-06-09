// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

#include <linux/extcon-provider.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/role.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>

#include <linux/sysfs.h>
#include <linux/kernfs.h>
#include <linux/kobject.h>
#include <linux/fs.h>

#include "mtk-pogo-usb.h"
#include "../../../power/supply/mtk_charger.h"

#if IS_ENABLED(CONFIG_TCPC_CLASS)
#include "tcpm.h"
#endif

int user_chosen;
int user_flag;
extern int board_id;
static int mtk_usb_pogo_extcon_set_vbus(struct mtk_pogo_extcon_info *extcon,
							bool is_on);
static const unsigned int usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static const char *data_state_str[] = {
    [DATA_STATE_NONE]         = "NONE",
    [DATA_STATE_TYPEC_DEVICE] = "TYPEC_DEVICE",
    [DATA_STATE_HUB_HOST]     = "HUB_HOST"
};

static const char *pwr_state_str[] = {
    [PWR_STATE_NONE]  = "UNATTACH",
    [PWR_STATE_TYPEC] = "TYPEC",
    [PWR_STATE_POGO] = "POGO"
};

static void mtk_usb_pogo_extcon_update_role(struct work_struct *work)
{
	struct usb_role_info *role = container_of(to_delayed_work(work),
					struct usb_role_info, dwork);
	struct mtk_pogo_extcon_info *extcon = role->extcon;
	unsigned int cur_dr, new_dr;

	cur_dr = extcon->c_role;
	new_dr = role->d_role;

	dev_info(extcon->dev, "cur_dr(%d) new_dr(%d)\n", cur_dr, new_dr);

	/* none -> device */
	if (cur_dr == USB_ROLE_NONE &&
			new_dr == USB_ROLE_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, true);
	/* none -> host */
	} else if (cur_dr == USB_ROLE_NONE &&
			new_dr == USB_ROLE_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, true);
	/* device -> none */
	} else if (cur_dr == USB_ROLE_DEVICE &&
			new_dr == USB_ROLE_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
	/* host -> none */
	} else if (cur_dr == USB_ROLE_HOST &&
			new_dr == USB_ROLE_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
	/* device -> host */
	} else if (cur_dr == USB_ROLE_DEVICE &&
			new_dr == USB_ROLE_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
		extcon_set_state_sync(extcon->edev,	EXTCON_USB_HOST, true);
	/* host -> device */
	} else if (cur_dr == USB_ROLE_HOST &&
			new_dr == USB_ROLE_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
		extcon_set_state_sync(extcon->edev,	EXTCON_USB, true);
	}

	/* usb role switch */
	if (extcon->role_sw) {
		usb_role_switch_set_role(extcon->role_sw, USB_ROLE_NONE);
		udelay(2000);
		usb_role_switch_set_role(extcon->role_sw, new_dr);
	}

	extcon->c_role = new_dr;
	kfree(role);
}

static int mtk_usb_pogo_extcon_set_role(struct mtk_pogo_extcon_info *extcon,
						unsigned int role)
{
	struct usb_role_info *role_info;
	dev_info(extcon->dev, "mtk_usb_pogo_extcon_set_role cur_dr(%d)\n", role);
	/* create and prepare worker */
	role_info = kzalloc(sizeof(*role_info), GFP_ATOMIC);
	if (!role_info)
		return -ENOMEM;

	INIT_DELAYED_WORK(&role_info->dwork, mtk_usb_pogo_extcon_update_role);

	role_info->extcon = extcon;
	role_info->d_role = role;
	/* issue connection work */
	queue_delayed_work(extcon->extcon_wq, &role_info->dwork, 0);

	return 0;
}

//add by wanwen,get port state 20260130 start
static struct usb_state mtk_pogo_usb_get_next_state(struct mtk_pogo_extcon_info *info)
{
    struct usb_state next_state;
    bool typec_src, typec_snk, pogo_src;

    typec_src = test_bit(ATTACHED_TYPEC_SRC, &info->inputs);
    typec_snk = test_bit(ATTACHED_TYPEC_SNK, &info->inputs);
    pogo_src = test_bit(ATTACHED_POGO_SRC, &info->inputs);

    if (info->state.port_states.typec_src != typec_src ||
            info->state.port_states.typec_snk != typec_snk ||
            info->state.port_states.pogo_src != pogo_src) {
        info->state.state_changed = true;
        info->state.port_states.typec_src = typec_src;
        info->state.port_states.typec_snk = typec_snk;
        info->state.port_states.pogo_src = pogo_src;
    }

    dev_info(info->dev, "[%s] typec_src:%d, typec_snk:%d, pogo_src:%d\n", __func__,
            typec_src, typec_snk, pogo_src);
			
	if (typec_src && pogo_src) {
		info->state.case_state = 1;
		next_state.data_state = DATA_STATE_HUB_HOST;
		next_state.pwr_state = PWR_STATE_NONE;
		user_flag = 3;
	} else if (typec_snk && pogo_src) {
		info->state.case_state = 2;
		gpio_direction_output(info->usb_sw, 0);
		schedule_delayed_work(&info->bc12_dwork, msecs_to_jiffies(10));
		next_state.pwr_state = PWR_STATE_TYPEC;
		next_state.data_state = DATA_STATE_HUB_HOST;
		if (info->typec_fast == true) {
			user_flag = 1;
		} else if (info->pogo_fast == true) {
			user_flag = 2;
		}
	} else {
		    if (typec_snk) {
				next_state.data_state = DATA_STATE_TYPEC_DEVICE;
				next_state.pwr_state = PWR_STATE_TYPEC;
				info->pogo_otg_device = false;
				info->typec_otg_device = false;
				user_flag = 6;
			} else if (typec_src) {
				next_state.data_state = DATA_STATE_HUB_HOST;
				next_state.pwr_state = PWR_STATE_NONE;
				info->pogo_otg_device = false;
				info->typec_otg_device = true;
				user_flag = 4;
			} else if (pogo_src) {
				next_state.data_state = DATA_STATE_HUB_HOST;
				next_state.pwr_state = PWR_STATE_NONE;
				info->pogo_otg_device = true;
				info->typec_otg_device = false;
				user_flag = 5;
			} else {
				next_state.data_state = DATA_STATE_NONE;
				next_state.pwr_state = PWR_STATE_NONE;
				info->pogo_otg_device = false;
				info->typec_otg_device = false;
				user_flag = 0;				
			}
			info->state.case_state = 0;
	}

    dev_info(info->dev, "[%s] data_state %s -> %s, pwr_state %s -> %s, port state %s\n", __func__,
            data_state_str[info->state.data_state], data_state_str[next_state.data_state],
            pwr_state_str[info->state.pwr_state], pwr_state_str[next_state.pwr_state],
            info->state.state_changed?"changed":"same");
    return next_state;
}

static void mtk_pogo_usb_set_route_by_state(struct mtk_pogo_extcon_info *info, struct usb_state state)
{
    dev_info(info->dev, "[%s] enter\n", __func__);

    if (info->state.pwr_state != state.pwr_state) {
        info->state.pwr_state = state.pwr_state;
        dev_info(info->dev, "[%s] pwr_state to %s\n",
            __func__, pwr_state_str[info->state.pwr_state]);
        switch (info->state.pwr_state) {
            case PWR_STATE_TYPEC:
                gpio_direction_output(info->usb_sw, 0);
                break;
            default:
                break;
        }
        /* we should reschedule bc12 work after power state change, whichever port plug in/out */
        schedule_delayed_work(&info->bc12_dwork, msecs_to_jiffies(10));
    }

    if (info->state.state_changed) {
        if (info->state.data_state != state.data_state) {
            info->state.data_state = state.data_state;
            dev_info(info->dev, "[%s] data_state to %s, data_state is %d\n",
                __func__, data_state_str[info->state.data_state],info->state.data_state);
        }
        switch (info->state.data_state) {
            case DATA_STATE_HUB_HOST:
                dev_info(info->dev, "[%s] data_state is %d\n", __func__, USB_ROLE_HOST);
                mtk_usb_pogo_extcon_set_role(info, USB_ROLE_HOST);
                if (info->pogo_otg_device) {
                    gpio_direction_output(info->usb_sw, 1);
					gpio_direction_output(info->pogo_otg, 1);
				}
                if (info->typec_otg_device &&
                        test_bit(ATTACHED_TYPEC_SRC, &info->inputs)) {
                    gpio_direction_output(info->usb_sw, 0);
					gpio_direction_output(info->pogo_otg, 0);
                }
				mtk_usb_pogo_extcon_set_vbus(info, true);
                break;
            case DATA_STATE_TYPEC_DEVICE:
					mtk_usb_pogo_extcon_set_role(info, USB_ROLE_DEVICE);
					gpio_direction_output(info->usb_sw, 0);
					gpio_direction_output(info->pogo_otg, 0);
					mtk_usb_pogo_extcon_set_vbus(info, false);
                break;
            default:
                gpio_direction_output(info->usb_sw, 0);
                mtk_usb_pogo_extcon_set_role(info, USB_ROLE_NONE);
                gpio_direction_output(info->pogo_otg, 0);
				mtk_usb_pogo_extcon_set_vbus(info, false);
                break;
        }
    }
    dev_info(info->dev, "[%s] done\n", __func__);
}

static void mtk_pogo_usb_set_route_by_state_chosen(struct mtk_pogo_extcon_info *info, struct usb_state state)
{
    dev_info(info->dev, "[%s] enter\n", __func__);

	if (info->state.pwr_state != state.pwr_state) {
        info->state.pwr_state = state.pwr_state;
        dev_info(info->dev, "[%s] pwr_state to %s\n",
            __func__, pwr_state_str[info->state.pwr_state]);
        switch (info->state.pwr_state) {
            case PWR_STATE_TYPEC:
                gpio_direction_output(info->usb_sw, 0);
                break;
            default:
                break;
        }
        /* we should reschedule bc12 work after power state change, whichever port plug in/out */
        schedule_delayed_work(&info->bc12_dwork, msecs_to_jiffies(10));
    }

    if (info->state.state_changed) {
        if (info->state.data_state != state.data_state) {
            info->state.data_state = state.data_state;
            dev_info(info->dev, "[%s] data_state to %s, data_state is %d\n",
                __func__, data_state_str[info->state.data_state],info->state.data_state);
        }
        switch (user_chosen) {
            case 1:
				mtk_usb_pogo_extcon_set_vbus(info, false);
				mtk_usb_pogo_extcon_set_role(info, USB_ROLE_HOST);
				gpio_direction_output(info->usb_sw, 1);
				gpio_direction_output(info->pogo_otg, 1);
				break;
            case 2:
				mtk_usb_pogo_extcon_set_vbus(info, false);
				mtk_usb_pogo_extcon_set_role(info, USB_ROLE_DEVICE);
				gpio_direction_output(info->usb_sw, 0);
				gpio_direction_output(info->pogo_otg, 0);
				break;
			case 3:
				if (info->charger_state == 1) {
					mtk_usb_pogo_extcon_set_role(info, USB_ROLE_DEVICE);
					gpio_direction_output(info->usb_sw, 0);
					gpio_direction_output(info->pogo_otg, 0);					
				} else if (info->charger_state == 2) {
					mtk_usb_pogo_extcon_set_role(info, USB_ROLE_HOST);
					gpio_direction_output(info->usb_sw, 1);
					gpio_direction_output(info->pogo_otg, 1);
				}
				break;	
            case 4:
				mtk_usb_pogo_extcon_set_role(info, USB_ROLE_HOST);
				gpio_direction_output(info->usb_sw, 1);
				gpio_direction_output(info->pogo_otg, 1);
				break;
			case 5:
				gpio_direction_output(info->usb_sw, 1);
				gpio_direction_output(info->pogo_otg, 1);
                mtk_usb_pogo_extcon_set_role(info, USB_ROLE_HOST);
                break;
            case 6:
				gpio_direction_output(info->usb_sw, 0);
				gpio_direction_output(info->pogo_otg, 0);
                mtk_usb_pogo_extcon_set_role(info, USB_ROLE_HOST);
                break;
            default:
				mtk_usb_pogo_extcon_set_vbus(info, false);
                gpio_direction_output(info->usb_sw, 1);
				gpio_direction_output(info->pogo_otg, 1);
                mtk_usb_pogo_extcon_set_role(info, USB_ROLE_HOST);
                break;
        }
    }
    dev_info(info->dev, "[%s] done\n", __func__);
}

static int mtk_pogo_usb_state_machine_thread(void *data)
{
    struct mtk_pogo_extcon_info *info = data;
    int ret = 0;
    struct usb_state next_state;

    dev_info(info->dev, "[%s] start!\n", __func__);

    while (!kthread_should_stop()) {
        ret = wait_event_interruptible(info->state_machine_wq,
                atomic_read(&info->machine_run) || kthread_should_stop());
        if (kthread_should_stop() || ret == -ERESTARTSYS) {
            break;
        } else if (ret < 0) {
            dev_dbg(info->dev, "[%s] wait event been interrupted(%d)\n",
                    __func__, ret);
            continue;
        }

        __pm_stay_awake(info->state_wakelock);
        atomic_set(&info->machine_run, 0);
        next_state = mtk_pogo_usb_get_next_state(info);
        if (!info->state.state_changed) {
            dev_info(info->dev, "[%s] same state\n", __func__);
            __pm_relax(info->state_wakelock);
            continue;
        }

        if (info->state.case_state == 0) {
			user_chosen = 0;
            mtk_pogo_usb_set_route_by_state(info, next_state);
        } else if (info->state.case_state != 0) {
            ret = wait_event_interruptible_timeout(info->state_machine_wq,
                    (user_chosen != 0) || kthread_should_stop(),
                    HZ * 10);

            if (kthread_should_stop()) {
                __pm_relax(info->state_wakelock);
                break;
            }
            if (ret == 0) {
                dev_err(info->dev, "[%s] wait user_chosen timeout!\n", __func__);
            }

            mtk_pogo_usb_set_route_by_state_chosen(info, next_state);
        }

        __pm_relax(info->state_wakelock);
    }

    return ret;
}

static void mtk_pogo_otg_irq_work_handler(struct work_struct *work)
{
    struct mtk_pogo_extcon_info *info = container_of(to_delayed_work(work),
            struct mtk_pogo_extcon_info, pogo_otg_irq_work);
    int level = 0;
	bool typec_src, typec_snk;
	struct chg_alg_device *alg = NULL;
    struct chg_alg_device *alg1 = NULL;
	typec_src = test_bit(ATTACHED_TYPEC_SRC, &info->inputs);
    typec_snk = test_bit(ATTACHED_TYPEC_SNK, &info->inputs);
	alg = get_chg_alg_by_name("pe5");
	alg1 = get_chg_alg_by_name("pe2");


    if (gpio_get_value(info->pogo_otg_int) > 0) {
        level = 1;
    }
    dev_info(info->dev, "[%s] level:%d\n", __func__, level);

	if (!level && !test_bit(ATTACHED_POGO_SRC, &info->inputs)) {
        set_bit(ATTACHED_POGO_SRC, &info->inputs);
		if (alg && chg_alg_is_algo_running(alg)) {
			dev_info(info->dev, "pogo plug in,stop pe5");
			chg_alg_stop_algo(alg);
		}
		if (alg1 && (chg_alg_is_algo_running(alg1)||(chg_alg_is_algo_ready(alg1) == 3))) {
			dev_info(info->dev, "pogo plug in,stop pe2");
			chg_alg_stop_algo(alg1);
		}
		msleep(50);
		if (typec_snk) {
			info->typec_fast = true;
		}
        atomic_set(&info->machine_run, 1);
        wake_up_interruptible(&info->state_machine_wq);
    } else if (level && test_bit(ATTACHED_POGO_SRC, &info->inputs)) {
        clear_bit(ATTACHED_POGO_SRC, &info->inputs);
		info->typec_fast = false;
		info->pogo_fast = false;
        atomic_set(&info->machine_run, 1);
        wake_up_interruptible(&info->state_machine_wq);
    }
}

static irqreturn_t mtk_pogo_usb_irq_handler(int irq, void *data)
{
    struct mtk_pogo_extcon_info *info = data;
	if (irq == info->pogo_otg_irq) {
        mod_delayed_work(system_wq, &info->pogo_otg_irq_work, msecs_to_jiffies(500));
    }

    return IRQ_HANDLED;
}
//add by wanwen,get port state 20260130 end

/*
static bool usb_is_online(struct mtk_pogo_extcon_info *extcon)
{
	union power_supply_propval pval;
	union power_supply_propval tval;
	int ret;

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_ONLINE, &pval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get online prop\n");
		return false;
	}

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_TYPE, &tval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get usb type\n");
		return false;
	}

	dev_info(extcon->dev, "online=%d, type=%d\n", pval.intval, tval.intval);

	if (pval.intval && (tval.intval == POWER_SUPPLY_TYPE_USB ||
			tval.intval == POWER_SUPPLY_TYPE_USB_CDP))
		return true;
	else
		return false;
}

static void mtk_usb_pogo_extcon_psy_detector(struct work_struct *work)
{
	struct mtk_pogo_extcon_info *extcon = container_of(to_delayed_work(work),
		struct mtk_pogo_extcon_info, wq_psy);

	pr_err("mtk_usb_pogo_extcon_psy_detector 1111\n");


	if (extcon->tcpc_dev) {
		if (usb_is_online(extcon) && extcon->c_role == USB_ROLE_NONE)
			mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_DEVICE);
	} else {
		if (usb_is_online(extcon))
			mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_DEVICE);
		else
			mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_NONE);
	}

}

static int mtk_usb_pogo_extcon_psy_notifier(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct mtk_pogo_extcon_info *extcon = container_of(nb,
					struct mtk_pogo_extcon_info, psy_nb);

	pr_err("mtk_usb_pogo_extcon_psy_notifier 1111 event:%ld psy:%d\n",event, (psy != extcon->usb_psy));

	if (event != PSY_EVENT_PROP_CHANGED || psy != extcon->usb_psy)
		return NOTIFY_DONE;

	pr_err("mtk_usb_pogo_extcon_psy_notifier 2222\n");

	queue_delayed_work(system_power_efficient_wq, &extcon->wq_psy, 0);

	return NOTIFY_DONE;
}

static int mtk_usb_pogo_extcon_psy_init(struct mtk_pogo_extcon_info *extcon)
{
	int ret = 0;
	struct device *dev = extcon->dev;

	if (!of_property_read_bool(dev->of_node, "charger")) {
		ret = -EINVAL;
		goto fail;
	}

	extcon->usb_psy = devm_power_supply_get_by_phandle(dev, "charger");
	if (IS_ERR_OR_NULL(extcon->usb_psy)) {

		extcon->usb_psy = power_supply_get_by_name("primary_chg");
		if (IS_ERR_OR_NULL(extcon->usb_psy)) {
			dev_err(dev, "fail to get usb_psy\n");
			ret = -EINVAL;
			goto fail;
		}
	}

	INIT_DELAYED_WORK(&extcon->wq_psy, mtk_usb_pogo_extcon_psy_detector);

	extcon->psy_nb.notifier_call = mtk_usb_pogo_extcon_psy_notifier;
	ret = power_supply_reg_notifier(&extcon->psy_nb);
	if (ret)
		dev_err(dev, "fail to register notifer\n");
	return ret;
fail:
	dev_err(dev, "fail to get usb_psy\n");
	return ret;
}
*/
static struct charger_device *primary_charger;
// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 start
#if IS_ENABLED(CONFIG_WIRELESS_MT5706)
struct charger_device *wlchg1_dev;
#endif /* CONFIG_WIRELESS_MT5706 */
// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 end

static bool otg_swtich = false;

static int mtk_usb_pogo_extcon_set_vbus_v1(bool is_on) {
// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 start
#if IS_ENABLED(CONFIG_WIRELESS_MT5706)
	int ret = 0;
	union charger_propval wls_work_mode = {0};
#endif /* CONFIG_WIRELESS_MT5706 */
// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 end

	if(otg_swtich == is_on){
		pr_err("gezi:----%s-----otg state same as befor:%d\n",__func__,otg_swtich);
		return 0;
	}
	
	otg_swtich = is_on;
	
	if (!primary_charger) {
		primary_charger = get_charger_by_name("primary_chg");
		if (!primary_charger) {
			pr_info("%s: get primary charger device failed\n", __func__);
			return -ENODEV;
		}
	}

// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 start
#if IS_ENABLED(CONFIG_WIRELESS_MT5706)
	if (!wlchg1_dev) {
		wlchg1_dev = get_charger_by_name("wireless_chg");
		if (!wlchg1_dev) {
			pr_info("%s: get wls charger device failed\n", __func__);
			return -ENODEV;
		}
    }
		ret = charger_dev_get_property(wlchg1_dev, CHARGER_PROP_WLS_MODE, &wls_work_mode);
		pr_err("extcon-usb wls_work_mode:%d \n", wls_work_mode.intval);
		if (!ret && wls_work_mode.intval == WLS_WORK_MODE_TX) {
			wls_work_mode.intval = 0;
			ret = charger_dev_set_property(wlchg1_dev, CHARGER_PROP_WLS_TX_ENABLE, &wls_work_mode);
			if (ret)
				pr_err("wls tx disable failed \n");
		}
#endif /* CONFIG_WIRELESS_MT5706 */
// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 end
	if (is_on) {
// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 start
#if IS_ENABLED(CONFIG_WIRELESS_MT5706)
		charger_dev_enable_otg(wlchg1_dev, true);
		msleep(15);
		#endif /* CONFIG_WIRELESS_MT5706 */
		// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 end

		charger_dev_enable_otg(primary_charger, true);
		charger_dev_set_boost_current_limit(primary_charger,1500000);
	} else {
		charger_dev_enable_otg(primary_charger, false);

		// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 start
		#if IS_ENABLED(CONFIG_WIRELESS_MT5706)
		msleep(15);
		charger_dev_enable_otg(wlchg1_dev, false);
		#endif /* CONFIG_WIRELESS_MT5706 */
		// drv add tankaikun, apply mt5706 to mtk charger class, 20250409 end
	}
	return 0;
}

static int mtk_usb_pogo_extcon_set_vbus(struct mtk_pogo_extcon_info *extcon,
							bool is_on)
{
	struct regulator *vbus = extcon->vbus;
	struct device *dev = extcon->dev;
	int ret;

	dev_err(dev, "vbus turn %s\n", is_on ? "on" : "off");
	if(1) {
		mtk_usb_pogo_extcon_set_vbus_v1(is_on);
		return 0;
	}
	/* vbus is optional */
	if (!vbus || extcon->vbus_on == is_on)
		return 0;

	dev_info(dev, "vbus turn %s\n", is_on ? "on" : "off");

	if (is_on) {
		if (extcon->vbus_vol) {
			ret = regulator_set_voltage(vbus,
					extcon->vbus_vol, extcon->vbus_vol);
			if (ret) {
				dev_err(dev, "vbus regulator set voltage failed\n");
				return ret;
			}
		}

		if (extcon->vbus_cur) {
			ret = regulator_set_current_limit(vbus,
					extcon->vbus_cur, extcon->vbus_cur);
			if (ret) {
				dev_err(dev, "vbus regulator set current failed\n");
				return ret;
			}
		}

		ret = regulator_enable(vbus);
		if (ret) {
			dev_info(dev, "vbus regulator enable failed\n");
			return ret;
		}
	} else {
		regulator_disable(vbus);
	}

	extcon->vbus_on = is_on;

	return 0;
}

static int mtk_usb_pogo_extcon_vbus_init(struct mtk_pogo_extcon_info *extcon)
{
	int ret = 0;
	struct device *dev = extcon->dev;

	if (!of_property_read_bool(dev->of_node, "vbus-supply")) {
		ret = -EINVAL;
		goto fail;
	}

	extcon->vbus =  devm_regulator_get_exclusive(dev, "vbus");
	if (IS_ERR(extcon->vbus)) {
		/* try to get by name */
		extcon->vbus =  devm_regulator_get_exclusive(dev, "usb-otg-vbus");
		if (IS_ERR(extcon->vbus)) {
			dev_err(dev, "failed to get vbus\n");
			ret = PTR_ERR(extcon->vbus);
			extcon->vbus = NULL;
			goto fail;
		}
	}

	/* sync vbus state */
	extcon->vbus_on = regulator_is_enabled(extcon->vbus);
	dev_info(dev, "vbus is %s\n", extcon->vbus_on ? "on" : "off");

	if (!of_property_read_u32(dev->of_node, "vbus-voltage",
				&extcon->vbus_vol))
		dev_info(dev, "vbus-voltage=%d", extcon->vbus_vol);

	if (!of_property_read_u32(dev->of_node, "vbus-current",
				&extcon->vbus_cur))
		dev_info(dev, "vbus-current=%d", extcon->vbus_cur);

fail:
	dev_err(dev, "mtk_usb_pogo_extcon_vbus_init failed\n");
	return ret;
}

#if IS_ENABLED(CONFIG_TCPC_CLASS)
static int mtk_pogo_extcon_tcpc_notifier(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct mtk_pogo_extcon_info *extcon =
			container_of(nb, struct mtk_pogo_extcon_info, tcpc_nb);
	struct device *dev = extcon->dev;
	bool vbus_on, pogo_src;

	switch (event) {
	case TCP_NOTIFY_SOURCE_VBUS:
		dev_info(dev, "source vbus = %dmv\n",
				 noti->vbus_state.mv);
		vbus_on = (noti->vbus_state.mv) ? true : false;
		pogo_src = test_bit(ATTACHED_POGO_SRC, &extcon->inputs);
		if (pogo_src) {
			mtk_usb_pogo_extcon_set_vbus(extcon, 1);
		} else {
			mtk_usb_pogo_extcon_set_vbus(extcon, vbus_on);
		}
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		dev_err(dev, "old_state=%d, new_state=%d\n",
				noti->typec_state.old_state,
				noti->typec_state.new_state);
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {
			dev_info(dev, "Type-C SRC plug in\n");
			set_bit(ATTACHED_TYPEC_SRC, &extcon->inputs);
			extcon->typec_otg_device = true;
		} else if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			(noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
			noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC ||
			noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			noti->typec_state.new_state == TYPEC_ATTACHED_DBGACC_SNK)) {
			dev_info(dev, "Type-C SINK plug in\n");
			gpio_direction_output(extcon->usb_sw, 0);
			set_bit(ATTACHED_TYPEC_SNK, &extcon->inputs);
			pogo_src = test_bit(ATTACHED_POGO_SRC, &extcon->inputs);
			if (pogo_src) {
				extcon->pogo_fast = true;
			}
            extcon->typec_bc12_state = BC12_STATE_REQUEST;
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_SNK ||
			noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_DBGACC_SNK) &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {
			dev_info(dev, "[%s] typec plug out\n", __func__);
            clear_bit(ATTACHED_TYPEC_SRC, &extcon->inputs);
            clear_bit(ATTACHED_TYPEC_SNK, &extcon->inputs);
            extcon->typec_usb_device = false;
            extcon->typec_otg_device = false;
            extcon->typec_bc12_state = BC12_STATE_DONE;
            extcon->typec_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			extcon->typec_fast = false;	
			extcon->pogo_fast = false;
		}
		atomic_set(&extcon->machine_run, 1);
		wake_up_interruptible(&extcon->state_machine_wq);
		break;
	case TCP_NOTIFY_DR_SWAP:
		dev_info(dev, "%s dr_swap, new role=%d\n",
				__func__, noti->swap_state.new_role);
		if (noti->swap_state.new_role == PD_ROLE_UFP &&
				extcon->c_role != USB_ROLE_DEVICE) {
			dev_info(dev, "switch role to device\n");
			mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_NONE);
			mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_DEVICE);
		} else if (noti->swap_state.new_role == PD_ROLE_DFP &&
				extcon->c_role != USB_ROLE_HOST) {
			dev_info(dev, "switch role to host\n");
			mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_NONE);
			mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_HOST);
		}
		break;
	}

	return NOTIFY_OK;
}

static int mtk_usb_pogo_extcon_tcpc_init(struct mtk_pogo_extcon_info *extcon)
{
	struct tcpc_device *tcpc_dev;
	struct device_node *np = extcon->dev->of_node;
	const char *tcpc_name;
	int ret;

	ret = of_property_read_string(np, "tcpc", &tcpc_name);
	if (ret < 0)
		return -ENODEV;

	tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (!tcpc_dev) {
		dev_err(extcon->dev, "get tcpc device fail\n");
		return -ENODEV;
	}

	extcon->tcpc_nb.notifier_call = mtk_pogo_extcon_tcpc_notifier;
	ret = register_tcp_dev_notifier(tcpc_dev, &extcon->tcpc_nb,
		TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS |
		TCP_NOTIFY_TYPE_MISC);
	if (ret < 0) {
		dev_err(extcon->dev, "register notifer fail\n");
		return -EINVAL;
	}

	extcon->tcpc_dev = tcpc_dev;

	return 0;
}
#endif

static void mtk_usb_pogo_extcon_detect_cable(struct work_struct *work)
{
	struct mtk_pogo_extcon_info *extcon = container_of(to_delayed_work(work),
		struct mtk_pogo_extcon_info, wq_detcable);
	int id;

	/* check ID and update cable state */
	id = extcon->id_gpiod ?
		gpiod_get_value_cansleep(extcon->id_gpiod) : 1;

	/* at first we clean states which are no longer active */
	if (id) {
		mtk_usb_pogo_extcon_set_vbus(extcon, false);
		mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_NONE);
	} else {
		mtk_usb_pogo_extcon_set_vbus(extcon, true);
		mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_HOST);
	}
}

static irqreturn_t mtk_usb_idpin_handle(int irq, void *dev_id)
{
	struct mtk_pogo_extcon_info *extcon = dev_id;

	/* issue detection work */
	queue_delayed_work(system_power_efficient_wq, &extcon->wq_detcable, 0);

	return IRQ_HANDLED;
}

//add by wanwen,add bc12 check 20260130 start
static void mtk_pogo_usb_bc12_work(struct work_struct *work)
{
    struct mtk_pogo_extcon_info *info = container_of(to_delayed_work(work),
            struct mtk_pogo_extcon_info, bc12_dwork);
    int ret = 0;
    union power_supply_propval attach, chg_type;
    bool need_bc12 = false, need_update_type = true;

    mutex_lock(&info->bc12_lock);
    dev_info(info->dev, "[%s] enter\n", __func__);

    if (IS_ERR_OR_NULL(info->bc12_psy)) {
        dev_warn(info->dev, "[%s] 'bc12_psy' err!\n", __func__);
        mutex_unlock(&info->bc12_lock);
        return;
    }

    if (info->state.pwr_state == PWR_STATE_NONE) {
        attach.intval = 0;
        ret = power_supply_set_property(info->bc12_psy,
                POWER_SUPPLY_PROP_ONLINE, &attach);
        if (ret) {
            dev_warn(info->dev, "[%s] fail to update attach state!\n", __func__);
            mutex_unlock(&info->bc12_lock);
            return;
        }
    } else {
        attach.intval = 2;
        /* update type */
        switch (info->state.pwr_state) {
            case PWR_STATE_TYPEC:
                if (info->typec_bc12_state == BC12_STATE_DONE) {
                    chg_type.intval = info->typec_usb_type;
                } else {
                    chg_type.intval = POWER_SUPPLY_USB_TYPE_UNKNOWN;
                }
                break;
            default:
                dev_warn(info->dev, "[%s] undefine pwr state!\n", __func__);
                need_update_type = false;
                break;
        }
        if (need_update_type) {
            ret = power_supply_set_property(info->bc12_psy,
                    POWER_SUPPLY_PROP_ONLINE, &attach);
            if (ret) {
                dev_warn(info->dev, "[%s] fail to update attach state!\n", __func__);
                mutex_unlock(&info->bc12_lock);
                return;
            }
            if (chg_type.intval != POWER_SUPPLY_USB_TYPE_UNKNOWN) {
                ret = power_supply_set_property(info->bc12_psy,
                        POWER_SUPPLY_PROP_CHARGE_TYPE, &chg_type);
                if (ret) {
                    dev_err(info->dev, "[%s] fail to update chg type\n", __func__);
                    mutex_unlock(&info->bc12_lock);
                    return;
                }
            }
        }
        /* bc12 */
        if (info->typec_bc12_state == BC12_STATE_ANALYZING) {
            dev_info(info->dev, "[%s] busy, typec_bc12_state:%d, pogo_bc12_state:%d\n",__func__, info->typec_bc12_state, info->pogo_bc12_state);
            schedule_delayed_work(&info->bc12_dwork, msecs_to_jiffies(300));
            mutex_unlock(&info->bc12_lock);
            return;
        }

        if (info->typec_bc12_state == BC12_STATE_REQUEST) {
            info->typec_bc12_state = BC12_STATE_ANALYZING;
            need_bc12 = true;
        }

        if (need_bc12) {
            ret = power_supply_set_property(info->bc12_psy,
                    POWER_SUPPLY_PROP_ONLINE, &attach);
            if (ret) {
                dev_warn(info->dev, "[%s] fail to update attach state!\n", __func__);
                mutex_unlock(&info->bc12_lock);
                return;
            }
            chg_type.intval = POWER_SUPPLY_USB_TYPE_UNKNOWN;
            ret = power_supply_set_property(info->bc12_psy,
                    POWER_SUPPLY_PROP_CHARGE_TYPE, &chg_type);
            if (ret) {
                dev_err(info->dev, "[%s] fail to update chg type\n", __func__);
                mutex_unlock(&info->bc12_lock);
                return;
            }
        }
    }
    dev_info(info->dev, "[%s] done\n", __func__);
    mutex_unlock(&info->bc12_lock);
}

static void mtk_pogo_usb_bc12_psy_notifier_work_handler(struct work_struct *work)
{
    struct mtk_pogo_extcon_info *info = container_of(to_delayed_work(work),
            struct mtk_pogo_extcon_info, bc12_psy_notifier_work);
    union power_supply_propval chg_type;
    int ret;

    ret = power_supply_get_property(info->bc12_psy, POWER_SUPPLY_PROP_CHARGE_TYPE, &chg_type);
    if (ret < 0) {
        dev_err(info->dev, "[%s] fail to get chg type\n", __func__);
        return;
    }

    if (info->typec_bc12_state == BC12_STATE_ANALYZING &&
            chg_type.intval != POWER_SUPPLY_USB_TYPE_UNKNOWN) {
        info->typec_bc12_state = BC12_STATE_DONE;
        info->typec_usb_type = chg_type.intval;
        dev_info(info->dev, "[%s] typec usb type %d\n", __func__, info->typec_usb_type);

        if (info->typec_usb_type == POWER_SUPPLY_USB_TYPE_SDP ||
                info->typec_usb_type == POWER_SUPPLY_USB_TYPE_CDP) {
            info->charger_state = 1;
        } else if (info->typec_usb_type == POWER_SUPPLY_USB_TYPE_DCP) {
			info->charger_state = 2;
        }

        /* reset chg_type to POWER_SUPPLY_USB_TYPE_UNKNOWN */
        chg_type.intval = -1;
        ret = power_supply_set_property(info->bc12_psy,
                POWER_SUPPLY_PROP_CHARGE_TYPE, &chg_type);
        if (ret) {
            dev_err(info->dev, "[%s] fail to update chg type\n", __func__);
        }
        schedule_delayed_work(&info->bc12_dwork, 0);
        atomic_set(&info->machine_run, 1);
        wake_up_interruptible(&info->state_machine_wq);
    } else {
        dev_err(info->dev, "[%s] chg_type:%d, bc12_state:%d, usb_type:%d, pogo_type:%d", __func__, chg_type.intval, info->typec_bc12_state,
                info->typec_usb_type, info->pogo_usb_type);
    }
}

static int mtk_pogo_usb_bc12_psy_notifier(struct notifier_block *nb,
        unsigned long event, void *data)
{
    struct power_supply *psy = data;
    struct mtk_pogo_extcon_info *info = container_of(nb,
            struct mtk_pogo_extcon_info, bc12_psy_nb);

    if (event == PSY_EVENT_PROP_CHANGED && psy == info->bc12_psy) {
        schedule_delayed_work(&info->bc12_psy_notifier_work, 0);
    }

    return NOTIFY_DONE;
}

static int mtk_pogo_usb_bc12_psy_init(struct mtk_pogo_extcon_info *info)
{
    int ret;

	INIT_DELAYED_WORK(&info->bc12_dwork, mtk_pogo_usb_bc12_work);

    INIT_DELAYED_WORK(&info->bc12_psy_notifier_work,
            mtk_pogo_usb_bc12_psy_notifier_work_handler);
    info->bc12_psy_nb.notifier_call = mtk_pogo_usb_bc12_psy_notifier;
    ret = power_supply_reg_notifier(&info->bc12_psy_nb);
    if (ret) {
        dev_err(info->dev, "[%s] fail to register bc12_psy notifier\n", __func__);
        return ret;
    }

    return 0;
}
//add by wanwen,add bc12 check 20260130 end

//add by wanwen,add pogo init 20260130 start
static int mtk_pogo_usb_gpio_init(struct mtk_pogo_extcon_info *info)
{
    struct device_node *node = info->dev->of_node;
    int ret;

    info->usb_sw = of_get_named_gpio(node, "usb_sw", 0);
    if (info->usb_sw < 0) {
        dev_err(info->dev, "[%s] no usb_sw provided.\n", __func__);
        return -ENODEV;
    }
    info->pogo_otg = of_get_named_gpio(node, "pogo_otg", 0);
    if (info->pogo_otg < 0) {
        dev_err(info->dev, "[%s] no pogo_otg provided.\n", __func__);
        return -ENODEV;
    }
    info->pogo_otg_int = of_get_named_gpio(node, "pogo_otg_int", 0);
    if (info->pogo_otg_int < 0) {
        dev_err(info->dev, "[%s] no pogo_otg_int provided.\n", __func__);
        return -ENODEV;
    }
    dev_err(info->dev, "pogo otg irq gpio is %d\n", info->pogo_otg_int);

    if (gpio_is_valid(info->usb_sw) && !gpio_request(info->usb_sw, "usb_sw")) {
        gpio_direction_output(info->usb_sw, 0);
    } else {
        dev_err(info->dev, "[%s] fail to get usb_sw\n", __func__);
        return -EINVAL;
    }

    if (gpio_is_valid(info->pogo_otg) &&
            !gpio_request(info->pogo_otg, "pogo_otg")) {
        gpio_direction_output(info->pogo_otg, 0);
    } else {
        dev_err(info->dev, "[%s] fail to get pogo_otg\n", __func__);
        return -EINVAL;
    }
	if (gpio_is_valid(info->pogo_otg_int) &&
            !gpio_request(info->pogo_otg_int, "pogo_otg_int")) {
        gpio_direction_input(info->pogo_otg_int);
        info->pogo_otg_irq = gpio_to_irq(info->pogo_otg_int);
        if (info->pogo_otg_irq < 0) {
            dev_err(info->dev, "[%s] fail to parse irq pogo_otg_irq!\n", __func__);
            return info->pogo_otg_irq;
        }
        ret = request_irq(info->pogo_otg_irq, mtk_pogo_usb_irq_handler,
                IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "pogo_otg_irq", info);
        if (ret < 0) {
            dev_err(info->dev, "[%s] fail to request_irq pogo_otg_irq\n", __func__);
            return ret;
        }
        enable_irq_wake(info->pogo_otg_int);
    } else {
        dev_err(info->dev, "[%s] fail to get pogo_otg_int\n", __func__);
        return -EINVAL;
    }

    INIT_DELAYED_WORK(&info->pogo_otg_irq_work,
            mtk_pogo_otg_irq_work_handler);

    return 0;
}
//add by wanwen,add pogo init 20260130 end

static int mtk_usb_pogo_extcon_id_pin_init(struct mtk_pogo_extcon_info *extcon)
{
	int ret = 0;
	int id;

	extcon->id_gpiod = devm_gpiod_get(extcon->dev, "id", GPIOD_IN);

	if (!extcon->id_gpiod || IS_ERR(extcon->id_gpiod)) {
		dev_info(extcon->dev, "failed to get id gpio\n");
		return -ENODEV;
	}

	extcon->id_irq = gpiod_to_irq(extcon->id_gpiod);
	if (extcon->id_irq < 0) {
		dev_info(extcon->dev, "failed to get ID IRQ\n");
		return extcon->id_irq;
	}

	INIT_DELAYED_WORK(&extcon->wq_detcable, mtk_usb_pogo_extcon_detect_cable);

	ret = devm_request_threaded_irq(extcon->dev, extcon->id_irq, NULL,
			mtk_usb_idpin_handle, IRQF_TRIGGER_RISING |
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			dev_name(extcon->dev), extcon);

	if (ret < 0) {
		dev_info(extcon->dev, "failed to request handler for ID IRQ\n");
		return ret;
	}

	/* get id pin value when boot on */
	id = extcon->id_gpiod ?
		gpiod_get_value_cansleep(extcon->id_gpiod) : 1;
	dev_info(extcon->dev, "id value : %d\n", id);
	if (!id) {
		mtk_usb_pogo_extcon_set_vbus(extcon, true);
		mtk_usb_pogo_extcon_set_role(extcon, USB_ROLE_HOST);
	}

	return 0;
}

#if IS_ENABLED(CONFIG_TCPC_CLASS)
#define PROC_FILE_SMT "mtk_typec"
#define FILE_SMT_U2_CC_MODE "smt_u2_cc_mode"

static int usb_cc_smt_procfs_show(struct seq_file *s, void *unused)
{
	struct mtk_pogo_extcon_info *extcon = s->private;
	struct device_node *np = extcon->dev->of_node;
	const char *tcpc_name;
	uint8_t cc1, cc2;
	int ret;

	ret = of_property_read_string(np, "tcpc", &tcpc_name);
	if (ret < 0)
		return -ENODEV;

	extcon->tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (!extcon->tcpc_dev)
		return -ENODEV;

	tcpm_inquire_remote_cc(extcon->tcpc_dev, &cc1, &cc2, false);
	dev_info(extcon->dev, "cc1=%d, cc2=%d\n", cc1, cc2);

	if (cc1 == TYPEC_CC_VOLT_OPEN || cc1 == TYPEC_CC_DRP_TOGGLING)
		seq_puts(s, "0\n");
	else if (cc2 == TYPEC_CC_VOLT_OPEN || cc2 == TYPEC_CC_DRP_TOGGLING)
		seq_puts(s, "0\n");
	else
		seq_puts(s, "1\n");

	return 0;
}

static int usb_cc_smt_procfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, usb_cc_smt_procfs_show, pde_data(inode));
}

static const struct  proc_ops usb_cc_smt_procfs_fops = {
	.proc_open = usb_cc_smt_procfs_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int mtk_usb_pogo_extcon_procfs_init(struct mtk_pogo_extcon_info *extcon)
{
	struct proc_dir_entry *file, *root;
	int ret = 0;

	root = proc_mkdir(PROC_FILE_SMT, NULL);
	if (!root) {
		dev_info(extcon->dev, "fail creating proc dir: %s\n",
			PROC_FILE_SMT);
		ret = -ENOMEM;
		goto error;
	}

	file = proc_create_data(FILE_SMT_U2_CC_MODE, 0400, root,
		&usb_cc_smt_procfs_fops, extcon);
	if (!file) {
		dev_info(extcon->dev, "fail creating proc file: %s\n",
			FILE_SMT_U2_CC_MODE);
		ret = -ENOMEM;
		goto error;
	}

	dev_info(extcon->dev, "success creating proc file: %s\n",
		FILE_SMT_U2_CC_MODE);

error:
	dev_info(extcon->dev, "%s ret:%d\n", __func__, ret);
	return ret;
}
#endif

static struct mtk_pogo_extcon_info *g_pogo_extcon_info;
static struct kobject *usb_sysfs_kobj = NULL;

static ssize_t pogo_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    if (!g_pogo_extcon_info)
        return scnprintf(buf, PAGE_SIZE, "%d\n", 0);
    return scnprintf(buf, PAGE_SIZE, "%d\n", user_flag);
}

static ssize_t pogo_state_store(struct kobject *kobj, struct kobj_attribute *attr,
                                const char *buf, size_t count)
{
    int val;
    int ret;

    if (!g_pogo_extcon_info)
        return -ENODEV;
    ret = kstrtoint(buf, 10, &val);
    if (ret < 0) {
        pr_err("pogo_state store invalid value: %s\n", buf);
        return ret;
    }

	if (g_pogo_extcon_info->state.case_state == 2 && val == 1 && g_pogo_extcon_info->typec_fast == true) {
		user_chosen = 1;
	} else if (g_pogo_extcon_info->state.case_state == 2 && val == 2 && g_pogo_extcon_info->typec_fast == true) {
		user_chosen = 2;
	} else if (g_pogo_extcon_info->state.case_state == 2 && val == 1 && g_pogo_extcon_info->pogo_fast == true) {
		user_chosen = 3;
	} else if (g_pogo_extcon_info->state.case_state == 2 && val == 2 && g_pogo_extcon_info->pogo_fast == true) {
		user_chosen = 4;
	} else if (g_pogo_extcon_info->state.case_state == 1 && val == 1) {
		user_chosen = 5;
	} else if (g_pogo_extcon_info->state.case_state == 1 && val == 2) {
		user_chosen = 6;
	} else {
		user_chosen = 0;
	}
    return count;
}

static struct kobj_attribute pogo_state_attr = __ATTR(pogo_state, 0664, pogo_state_show, pogo_state_store);

static struct attribute *pogo_state_attrs[] = {
	&pogo_state_attr.attr,
    NULL,
};

static struct attribute_group pogo_state_attr_group = {
	.attrs = pogo_state_attrs,
};

static int mtk_pogo_usb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_pogo_extcon_info *extcon;
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	const char *tcpc_name;
#endif
	int ret;

	pr_err("mtk_usb_pogo_extcon_probe begin\n");
	extcon = devm_kzalloc(&pdev->dev, sizeof(*extcon), GFP_KERNEL);
	if (!extcon && (board_id != 1)) {
		pr_err("mtk_usb_pogo_extcon_probe devm_kzalloc fail\n");
		return -ENOMEM;
	}
	extcon->dev = dev;

//add by wanwen,add pogo init 20260130 start
    extcon->typec_usb_device = false;
    extcon->typec_otg_device = false;
    extcon->pogo_otg_device = false;
    extcon->typec_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
    extcon->typec_bc12_state = BC12_STATE_DONE;
    extcon->c_role = USB_ROLE_NONE;
    extcon->inputs = 0;
//add by wanwen,add pogo init 20260130 end
	/* extcon */
	extcon->edev = devm_extcon_dev_allocate(dev, usb_extcon_cable);
	if (IS_ERR(extcon->edev)) {
		pr_err( "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(dev, extcon->edev);
	if (ret < 0) {
		pr_err("failed to register extcon device\n");
		return ret;
	}
//add by wanwen,add pogo init 20260130 start
	extcon->bc12_psy = devm_power_supply_get_by_phandle(&pdev->dev, "charger");
	if (IS_ERR(extcon->bc12_psy)) {
        	dev_err(extcon->dev, "[%s] failed to get charger psy, no device\n", __func__);
		return PTR_ERR(extcon->bc12_psy);
	} else if (!extcon->bc12_psy) {
		dev_warn(extcon->dev, "[%s] failed to get charger psy, charger psy is not ready\n", __func__);
		return -EPROBE_DEFER;
	}
//add by wanwen,add pogo init 20260130 end
	/* usb role switch */
	extcon->role_sw = usb_role_switch_get(extcon->dev);
	if (IS_ERR(extcon->role_sw)) {
		pr_err("failed to get usb role\n");
		return PTR_ERR(extcon->role_sw);
	}

	/* initial usb role */
	if (extcon->role_sw)
		extcon->c_role = USB_ROLE_NONE;

	/* vbus */
	ret = mtk_usb_pogo_extcon_vbus_init(extcon);
	if (ret < 0)
		pr_err("failed to init vbus\n");

	extcon->bypss_typec_sink =
		of_property_read_bool(dev->of_node,
			"mediatek,bypss-typec-sink");

#if IS_ENABLED(CONFIG_TCPC_CLASS)
	ret = of_property_read_string(dev->of_node, "tcpc", &tcpc_name);
	if (of_property_read_bool(dev->of_node, "mediatek,u2") && ret == 0
		&& strcmp(tcpc_name, "type_c_port0") == 0) {
		mtk_usb_pogo_extcon_procfs_init(extcon);
	}
#endif

	pr_err("extcon usb create_singlethread_workqueue...\n");

	extcon->extcon_wq = create_singlethread_workqueue("extcon_usb");
	if (!extcon->extcon_wq)
		return -ENOMEM;

	/* get id resources */
	ret = mtk_usb_pogo_extcon_id_pin_init(extcon);
	if (ret < 0)
		dev_info(dev, "failed to init id pin\n");

//add by wanwen,add pogo init 20260130 start
/*
	ret = mtk_usb_pogo_extcon_psy_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init psy\n");
*/
    init_waitqueue_head(&extcon->state_machine_wq);
    atomic_set(&extcon->machine_run, 0);
    extcon->machine_task = kthread_run(mtk_pogo_usb_state_machine_thread, extcon,
            "pogo_machine_thread");
    if (IS_ERR(extcon->machine_task)) {
        dev_err(extcon->dev, "[%s] run pogo machine kthread fail\n", __func__);
        return PTR_ERR(extcon->machine_task);
    }

    ret = mtk_pogo_usb_gpio_init(extcon);
    if (ret < 0)
        return -ENODEV;

    ret = mtk_pogo_usb_bc12_psy_init(extcon);
    if (ret < 0)
        return -ENODEV;
//add by wanwen,add pogo init 20260130 end
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	/* tcpc */
	ret = mtk_usb_pogo_extcon_tcpc_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init tcpc\n");
#endif

	pr_err("extcon usb init success\n");

    g_pogo_extcon_info = extcon;
    if (!usb_sysfs_kobj) {
        usb_sysfs_kobj = kobject_create_and_add("usb", NULL);
        if (!usb_sysfs_kobj) {
            dev_err(dev, "create /sys/usb dir failed\n");
            ret = -ENOMEM;
            goto err_sysfs;
        }
        dev_info(dev, "create /sys/usb dir success\n");
    }
    if (sysfs_create_group(usb_sysfs_kobj, &pogo_state_attr_group)) {
        dev_err(dev, "create /sys/usb/pogo_state file failed\n");
        ret = -ENOMEM;
        goto err_pogo_file;
    }
    dev_info(dev, "create /sys/usb/pogo_state node success\n");
     
	platform_set_drvdata(pdev, extcon);
	
	if (tcpm_inquire_typec_attach_state(extcon->tcpc_dev) == TYPEC_ATTACHED_SNK ||
			tcpm_inquire_typec_attach_state(extcon->tcpc_dev) == TYPEC_ATTACHED_NORP_SRC ||
			tcpm_inquire_typec_attach_state(extcon->tcpc_dev) == TYPEC_ATTACHED_CUSTOM_SRC ||
			tcpm_inquire_typec_attach_state(extcon->tcpc_dev) == TYPEC_ATTACHED_DBGACC_SNK) {
		dev_info(dev, "Type-C SINK plug in\n");
		gpio_direction_output(extcon->usb_sw, 0);
		set_bit(ATTACHED_TYPEC_SNK, &extcon->inputs);
		extcon->typec_bc12_state = BC12_STATE_REQUEST;
		atomic_set(&extcon->machine_run, 1);
		wake_up_interruptible(&extcon->state_machine_wq);
	}

	return 0;

err_pogo_file:
    kobject_put(usb_sysfs_kobj);
    usb_sysfs_kobj = NULL;
err_sysfs:
    if (!IS_ERR_OR_NULL(extcon->machine_task))
        kthread_stop(extcon->machine_task);
    if (extcon->extcon_wq)
        destroy_workqueue(extcon->extcon_wq);
    return ret;
}

static int mtk_pogo_usb_remove(struct platform_device *pdev)
{
	struct mtk_pogo_extcon_info *extcon = platform_get_drvdata(pdev);
	if (usb_sysfs_kobj) {
		sysfs_remove_group(usb_sysfs_kobj, &pogo_state_attr_group);
        kobject_put(usb_sysfs_kobj);
        usb_sysfs_kobj = NULL;
        dev_info(&pdev->dev, "remove /sys/usb and pogo_state success\n");
    }
    g_pogo_extcon_info = NULL;
	if (extcon && !IS_ERR_OR_NULL(extcon->machine_task))
		kthread_stop(extcon->machine_task);
	if (extcon && extcon->extcon_wq)
		destroy_workqueue(extcon->extcon_wq);
	return 0;
}

static void mtk_pogo_usb_shutdown(struct platform_device *pdev)
{
	struct mtk_pogo_extcon_info *extcon = platform_get_drvdata(pdev);

	dev_info(extcon->dev, "shutdown\n");

	mtk_usb_pogo_extcon_set_vbus(extcon, false);
}

static const struct of_device_id mtk_pogo_usb_of_match[] = {
    { .compatible = "mediatek,pogo-usb", },
    { },
};
MODULE_DEVICE_TABLE(of, mtk_pogo_usb_of_match);

static struct platform_driver mtk_pogo_usb_driver = {
    .probe    = mtk_pogo_usb_probe,
    .remove    = mtk_pogo_usb_remove,
    .shutdown  = mtk_pogo_usb_shutdown,
    .driver    = {
        .name  = "mtk-pogo-usb",
        .of_match_table = mtk_pogo_usb_of_match,
    },
};

static int __init mtk_pogo_usb_init(void)
{
	pr_err("mtk_usb_pogo_extcon_init in\n");
    return platform_driver_register(&mtk_pogo_usb_driver);
}
late_initcall(mtk_pogo_usb_init);

static void __exit mtk_pogo_usb_exit(void)
{
	platform_driver_unregister(&mtk_pogo_usb_driver);
}
module_exit(mtk_pogo_usb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek Pogo USB Driver");
