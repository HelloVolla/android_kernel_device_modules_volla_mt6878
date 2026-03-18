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

#ifndef __CS_NOTIFIER_H
#define __CS_NOTIFIER_H

#include <linux/notifier.h>

/* LCM panel EVENT */
#define CS_PANEL_EARLY_EVENT_BLANK		0x01
#define CS_PANEL_EVENT_BLANK		0x02
#define CS_PANEL_LATE_EVENT_BLANK		0x03
#define CS_PANEL_EVENT_BL_CHANGED		0x04
#define CS_PANEL_EVENT_FPS_CHANGED		0x05
#define CS_PANEL_EVENT_HBM_CHANGED		0x06

/* USB EVENT */
#define CS_USB_EARLY_EVENT_PLUG			0x07
#define CS_USB_EVENT_PLUG			0x08

/* TOUCH EVENT*/
#define CS_TOUCH_EVENT_DATA				0x09

/* SENSOR EVENT */
#define CS_SENSOR_EVENT_STATE			0x0A

/* LCM panel enum */
enum panel_event_blank_t {
	/* panel: power off */
	PANEL_BLANK_POWERDOWN,
	/* panel: power on */
	PANEL_BLANK_UNBLANK,
	/* panel: doze enable */
	PANEL_BLANK_DOZE_ENABLE,
	/* panel: doze disable */
	PANEL_BLANK_DOZE_DISABLE,
};

enum panel_event_fps_t {
	/* panel: FPS 60HZ  */
	PANEL_FPS_60HZ = 60,
	/* panel: FPS 90HZ  */
	PANEL_FPS_90HZ = 90,
	/* panel: FPS 120HZ  */
	PANEL_FPS_120HZ = 120,
};

enum panel_event_hbm_t {
	/* panel: hbm off */
	PANEL_HBM_OFF,
	/* panel: hbm on */
	PANEL_HBM_ON,
};

/* USB enum */
enum usb_event_plug_t {
	/* usb: plug out */
	USB_PLUG_OUT,
	/* usb: plug in */
	USB_PLUG_IN,
};

/* touch data enum */
enum touch_event_report_t {
	ANTIFAKE_TOUCH_UNKNOW = -1,
	ANTIFAKE_TOUCH_NEAR = 0,
	ANTIFAKE_TOUCH_FAR = 5,
};

/* sensor state enum */
enum sensor_dev_state_t {
	SENSOR_STATE_INIT = -1,
	SENSOR_STATE_DISABLE,
	SENSOR_STATE_ENABLE,
};

/* LCM panel struct */
struct panel_event_blank_data {
	enum panel_event_blank_t blank;
};

struct panel_event_bl_data {
	int bl_level;
};

struct panel_event_fps_data {
	enum panel_event_fps_t fps;
};

struct panel_event_hbm_data {
	enum panel_event_hbm_t hbm;
};

/* USB struct */
struct usb_event_plug_data {
	enum usb_event_plug_t plug;
};

struct touch_event_report_data {
	enum touch_event_report_t trigger;
};

struct sensor_dev_state {
	enum sensor_dev_state_t state;
};

/* LCM panel function declaration */
extern int cs_panel_notifier_register(struct notifier_block *nb);
extern int cs_panel_notifier_unregister(struct notifier_block *nb);
extern int cs_panel_notifier_call_chain(unsigned long event_type, void *v);

extern int cs_sub_panel_notifier_register(struct notifier_block *nb);
extern int cs_sub_panel_notifier_unregister(struct notifier_block *nb);
extern int cs_sub_panel_notifier_call_chain(unsigned long event_type, void *v);

/* USB function declaration */
extern int cs_usb_notifier_register(struct notifier_block *nb);
extern int cs_usb_notifier_unregister(struct notifier_block *nb);
extern int cs_usb_notifier_call_chain(unsigned long event_type, void *v);

/* touch function declaration */
extern int cs_touch_notifier_register(struct notifier_block *nb);
extern int cs_touch_notifier_unregister(struct notifier_block *nb);
extern int cs_touch_notifier_call_chain(unsigned long val, void *v);

/* sensor function declaration */
extern int cs_sensor_notifier_register(struct notifier_block *nb);
extern int cs_sensor_notifier_unregister(struct notifier_block *nb);
extern int cs_sensor_notifier_call_chain(unsigned long val, void *v);
#endif
