# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Synaptics TCM touchscreen driver.
#

# Each configuration option enables a list of files.

NAME=

DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

TCM_CORE=tcm/

ccflags-y += -I$(DIR)
ccflags-y += -I$(DIR)$(TCM_CORE)

ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/

synaptics_tcm2$(NAME)-objs := syna_tcm2.o
synaptics_tcm2$(NAME)-objs += syna_tcm2_cdev.o
synaptics_tcm2$(NAME)-objs += \
					$(TCM_CORE)synaptics_touchcom_core_v1.o \
					$(TCM_CORE)synaptics_touchcom_core_v2.o \
 					$(TCM_CORE)synaptics_touchcom_func_base.o \
 					$(TCM_CORE)synaptics_touchcom_func_touch.o

ifeq ($(CONFIG_TOUCHSCREEN_SYNA_TCM2_REFLASH),y)
	synaptics_tcm2$(NAME)-objs += $(TCM_CORE)synaptics_touchcom_func_reflash.o
endif

ifeq ($(CONFIG_TOUCHSCREEN_SYNA_TCM2_ROMBOOT),y)
	synaptics_tcm2$(NAME)-objs += $(TCM_CORE)synaptics_touchcom_func_romboot.o
endif

ifeq ($(CONFIG_TOUCHSCREEN_SYNA_TCM2_I2C),y)
	synaptics_tcm2$(NAME)-objs += syna_tcm2_platform_i2c.o
endif

ifeq ($(CONFIG_TOUCHSCREEN_SYNA_TCM2_SPI),y)
	synaptics_tcm2$(NAME)-objs += syna_tcm2_platform_spi.o
endif

ifeq ($(CONFIG_TOUCHSCREEN_SYNA_TCM2_SYSFS),y)
	synaptics_tcm2$(NAME)-objs += syna_tcm2_sysfs.o
endif

ifeq ($(CONFIG_TOUCHSCREEN_SYNA_TCM2_TESTING),y)
	synaptics_tcm2$(NAME)-objs += syna_tcm2_testing.o
endif

obj-$(CONFIG_TOUCHSCREEN_SYNA_TCM2) += synaptics_tcm2$(NAME).o

