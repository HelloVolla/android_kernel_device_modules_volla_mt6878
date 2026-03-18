/*
 *   copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/notifier.h>

static BLOCKING_NOTIFIER_HEAD(panel_notifier_list);
static BLOCKING_NOTIFIER_HEAD(sub_panel_notifier_list);
static BLOCKING_NOTIFIER_HEAD(usb_notifier_list);
static BLOCKING_NOTIFIER_HEAD(touch_notifier_list);
static BLOCKING_NOTIFIER_HEAD(sensor_notifier_list);

/**
 *	cs_panel_notifier_register - register a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_panel_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&panel_notifier_list, nb);
}
EXPORT_SYMBOL(cs_panel_notifier_register);

/**
 *	cs_panel_notifier_unregister - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_panel_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&panel_notifier_list, nb);
}
EXPORT_SYMBOL(cs_panel_notifier_unregister);

/**
 * cs_panel_notifier_call_chain - notify clients
 *
 */
int cs_panel_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&panel_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(cs_panel_notifier_call_chain);

/**
 *	cs_sub_panel_notifier_register - register a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_sub_panel_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&sub_panel_notifier_list, nb);
}
EXPORT_SYMBOL(cs_sub_panel_notifier_register);

/**
 *	cs_sub_panel_notifier_unregister - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_sub_panel_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&sub_panel_notifier_list, nb);
}
EXPORT_SYMBOL(cs_sub_panel_notifier_unregister);

/**
 * cs_sub_panel_notifier_call_chain - notify clients
 *
 */
int cs_sub_panel_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&sub_panel_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(cs_sub_panel_notifier_call_chain);

/**
 *	cs_usb_notifier_register - register a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_usb_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&usb_notifier_list, nb);
}
EXPORT_SYMBOL(cs_usb_notifier_register);

/**
 *	cs_usb_notifier_unregister - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_usb_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&usb_notifier_list, nb);
}
EXPORT_SYMBOL(cs_usb_notifier_unregister);

/**
 * cs_usb_notifier_call_chain - notify clients
 *
 */
int cs_usb_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&usb_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(cs_usb_notifier_call_chain);

/**
 *	cs_touch_notifier_unregister - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_touch_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&touch_notifier_list, nb);
}
EXPORT_SYMBOL(cs_touch_notifier_register);

/**
 *	cs_touch_notifier_unregister - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_touch_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&touch_notifier_list, nb);
}
EXPORT_SYMBOL(cs_touch_notifier_unregister);

/**
 * cs_touch_notifier_call_chain - notify clients
 *
 */
int cs_touch_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&touch_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(cs_touch_notifier_call_chain);

/**
 *	cs_sensor_notifier_unregister - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_sensor_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&sensor_notifier_list, nb);
}
EXPORT_SYMBOL(cs_sensor_notifier_register);

/**
 *	cs_sensor_notifier_unregister - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int cs_sensor_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&sensor_notifier_list, nb);
}
EXPORT_SYMBOL(cs_sensor_notifier_unregister);

/**
 * cs_sensor_notifier_call_chain - notify clients
 *
 */
int cs_sensor_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&sensor_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(cs_sensor_notifier_call_chain);

static int __init cs_notifier_init(void)
{
	pr_info("%s end\n", __func__);
	return 0;
}

static void __exit cs_notifier_exit(void)
{
	pr_info("%s end\n", __func__);
	return;
}

module_init(cs_notifier_init);
module_exit(cs_notifier_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Coosea driver Team");
MODULE_DESCRIPTION("CS notifier driver");
