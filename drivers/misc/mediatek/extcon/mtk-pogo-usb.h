/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

//add by wanwen,add pogo 20260130 start
enum usb_data_state {
    DATA_STATE_NONE = 0,
    DATA_STATE_TYPEC_DEVICE,
    DATA_STATE_HUB_HOST
};

enum usb_input_pwr_state {
    PWR_STATE_NONE = 0,
    PWR_STATE_TYPEC,
    PWR_STATE_POGO
};

enum bc12_state {
    BC12_STATE_DONE = 0,
    BC12_STATE_REQUEST,
    BC12_STATE_ANALYZING
};

struct port_state {
    bool typec_src;
    bool typec_snk;
    bool pogo_src;
    bool pogo_snk;
};

struct usb_state {
    enum usb_data_state data_state;
    enum usb_input_pwr_state pwr_state;
	int case_state;
    struct port_state port_states;
    bool state_changed;
};
//add by wanwen,add pogo 20260130 end
struct mtk_pogo_extcon_info {
	struct device *dev;
	struct extcon_dev *edev;
	struct usb_role_switch *role_sw;
	unsigned int c_role; /* current data role */
	struct workqueue_struct *extcon_wq;
	struct regulator *vbus;
	unsigned int vbus_vol;
	unsigned int vbus_cur;
	bool vbus_on;
	struct power_supply *usb_psy;
	struct notifier_block psy_nb;
	struct delayed_work wq_psy;
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	struct tcpc_device *tcpc_dev;
	struct notifier_block tcpc_nb;
#endif
	bool bypss_typec_sink;
	/* id gpio */
	struct gpio_desc *id_gpiod;
	int id_irq;
	struct delayed_work wq_detcable;
	int charger_state;
	bool typec_fast;
	bool pogo_fast;
//add by wanwen,add pogo 20260130 start
	int usb_sw;
	int pogo_otg;
	int usb_otg;
	int pogo_ovp_en;
	int usb_ovp_en;
	int otg_en;
	int dc_in_int;
	int usb_in_int;
	int pogo_otg_int;

	int dc_in_irq;
	int usb_in_irq;
	int pogo_otg_irq;

	struct delayed_work dc_in_irq_work;
	struct delayed_work usb_in_irq_work;
	struct delayed_work pogo_otg_irq_work;
	struct usb_state state;
	bool typec_usb_device; /* if typec attach snk and bc1.2 type is SDP/CDP */
	bool pogo_device;
	bool typec_otg_device; /* if typec attach src */
	bool pogo_otg_device;
	enum power_supply_usb_type typec_usb_type; /* typec bc12 result */
	enum power_supply_usb_type pogo_usb_type;
	enum bc12_state typec_bc12_state; /* typec bc12 state */
	enum bc12_state pogo_bc12_state;

	/* system lock */
	struct wakeup_source *state_wakelock;

	/* state machine */
	wait_queue_head_t state_machine_wq;
	struct task_struct *machine_task;
	unsigned long inputs;
	atomic_t machine_run;
	/* chg det */
	struct power_supply *bc12_psy;
	struct delayed_work bc12_dwork;
	struct delayed_work bc12_psy_notifier_work;
	struct notifier_block bc12_psy_nb;
	struct mutex bc12_lock;
//add by wanwen,add pogo 20260130 end
};

#define ATTACHED_TYPEC_SRC      0
#define ATTACHED_TYPEC_SNK      1
#define ATTACHED_POGO_SRC      2
#define BC12_CHECK_DONE      3

struct usb_role_info {
	struct mtk_pogo_extcon_info *extcon;
	struct delayed_work dwork;
	unsigned int d_role; /* desire data role */
};

enum {
	DUAL_PROP_MODE_UFP = 0,
	DUAL_PROP_MODE_DFP,
	DUAL_PROP_MODE_NONE,
};

enum {
	DUAL_PROP_PR_SRC = 0,
	DUAL_PROP_PR_SNK,
	DUAL_PROP_PR_NONE,
};
