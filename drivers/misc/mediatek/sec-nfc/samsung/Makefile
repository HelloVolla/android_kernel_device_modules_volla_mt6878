#
# Makefile for Samsung NRNx NFC driver
#

obj-$(CONFIG_SEC_NFC)		+= sec_nfc.o

ccflags-$(CONFIG_NFC_DEBUG) := -DDEBUG

ifneq ($(filter y m, $(CONFIG_PRIZE_HARDWARE_INFO)),)
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/hardware_info/
endif
