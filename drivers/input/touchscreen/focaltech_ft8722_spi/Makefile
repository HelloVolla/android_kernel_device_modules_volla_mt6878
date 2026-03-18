# Makefile for the focaltech touchscreen drivers.


ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/
ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pri/pri_common_node/

obj-m += focaltech_ft8722_spi.o
focaltech_ft8722_spi-y := focaltech_core.o \
		focaltech_ex_fun.o \
		focaltech_ex_mode.o \
		focaltech_gesture.o \
		focaltech_esdcheck.o \
		focaltech_point_report_check.o

focaltech_ft8722_spi-y += focaltech_flash_spi.o


focaltech_ft8722_spi-y += focaltech_spi_v2.o


focaltech_ft8722_spi-y += focaltech_proximity.o


