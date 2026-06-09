# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/tcpc/inc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/

obj-$(CONFIG_EXTCON_MTK_USB) += extcon-mtk-usb.o
# drv add wanwen, add pogo pin, 20260324 start
obj-$(CONFIG_MTK_POGO_USB) += mtk-pogo-usb.o
# drv add wanwen, add pogo pin, 20260324 end
