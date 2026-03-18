# Makefile for the focaltech touchscreen drivers.


ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/
ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pri/pri_common_node/

obj-m += ft8725_tp.o
ft8725_tp-y := ft8725_core.o \
		ft8725_ex_fun.o \
		ft8725_ex_mode.o \
		ft8725_gesture.o \
		ft8725_esdcheck.o \
		ft8725_point_report_check.o \
		ft8725_proximity.o


ft8725_tp-y += ft8725_flash_spi.o


ft8725_tp-y += ft8725_spi_v2.o


ft8725_tp-y += ft8725_debug.o

ft8725_tp-y += ft8725_hp.o


