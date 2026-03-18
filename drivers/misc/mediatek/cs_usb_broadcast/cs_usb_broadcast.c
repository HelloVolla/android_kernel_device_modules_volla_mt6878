// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Coosea Group.
 */

#define pr_fmt(fmt) "[cs_usb_broadcast]" fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <tcpm.h>
#include <linux/alarmtimer.h>
#include <linux/cs_notifier.h>
#if IS_ENABLED(CONFIG_TCPC_CLASS)
#include "tcpm.h"
#endif
#if IS_ENABLED(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
#endif

struct cs_usb_broadcast_info {
	struct power_supply_config psy_cfg;
	struct power_supply *psy;
	struct notifier_block pm_notifier;
	struct mtk_charger *info;
	struct tcpc_device *tcpc_dev;
	struct notifier_block pd_nb;
	int typec_attach;
	int cmd_discharging;
	int chg_type;
};

struct cs_usb_broadcast_info *g_ubi = NULL;

static int pd_tcp_notifier_call(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct cs_usb_broadcast_info *ubi = container_of(nb, struct cs_usb_broadcast_info, pd_nb);

	switch (event) {
	case TCP_NOTIFY_TYPEC_STATE:
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
		    (noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC)) {
			pr_info("%s:[CS] USB Plug in, pol = %d\n", __func__,	noti->typec_state.polarity);
			cs_usb_notifier_call_chain(USB_PLUG_IN, NULL);
			ubi->typec_attach = true;
			ubi->chg_type = 0;
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SNK ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_AUDIO)
			&& noti->typec_state.new_state == TYPEC_UNATTACHED) {
			pr_info("%s:[CS] USB Plug out\n", __func__);
			cs_usb_notifier_call_chain(USB_PLUG_OUT, NULL);
			ubi->typec_attach = false;
			ubi->chg_type = 0;
		}
		break;
	case TCP_NOTIFY_WD0_STATE:
		break;
	default:
		break;
	};
	return NOTIFY_OK;
}

static int usb_broadcast_init(void)
{
	int ret = 0;
	struct cs_usb_broadcast_info *ubi= kzalloc(sizeof(*ubi), GFP_KERNEL);
	if (!ubi)
		return -ENOMEM;;
	g_ubi = ubi;
	ubi->tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	if (ubi->tcpc_dev == NULL) {
		pr_err("%s:get tcpc device type_c_port0 fail\n", __func__);
		return -ENODEV;
	}
	ubi->pd_nb.notifier_call = pd_tcp_notifier_call;
	ret = register_tcp_dev_notifier(ubi->tcpc_dev, &ubi->pd_nb, TCP_NOTIFY_TYPE_ALL);
	if (ret < 0) {
		pr_err("%s: register tcpc notifer fail\n", __func__);
		return -EINVAL;
	}
	pr_info("cs_usb_broad_init ok !!!\n");
	return ret;
}

static int cs_usb_broadcast_remove(void)
{
	int ret = 0;
	struct cs_usb_broadcast_info *ubi = g_ubi;
	if(NULL == ubi) {
		pr_err("%s: fail\n", __func__);
		return -EINVAL;
	}
	ret = unregister_tcp_dev_notifier(ubi->tcpc_dev, &ubi->pd_nb, TCP_NOTIFY_TYPE_ALL);
	if (ret < 0) {
		pr_err("%s: unregister tcpc notifer fail\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static int __init cs_usb_broadcast_init(void)
{
	return usb_broadcast_init();
}

static void __exit cs_usb_broadcast_exit(void)
{
	cs_usb_broadcast_remove();
}

device_initcall_sync(cs_usb_broadcast_init);
module_exit(cs_usb_broadcast_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Coosea driver Team");
MODULE_DESCRIPTION("CS usb_broadcast driver");
