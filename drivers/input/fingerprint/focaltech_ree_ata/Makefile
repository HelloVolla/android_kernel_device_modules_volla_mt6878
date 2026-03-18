# Makefile for the focaltech fingerprint driver.

#=======================================================================
# Project configurations, should be configured based on your projects

# If your project is MTK platform, you should set it to y, otherwise set
# it to n(remarking it is also ok)
CONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK := n

# If TEE is emulation which is called REE, you should set it to y
ifneq ($(filter y m, $(CONFIG_FINGERPRINT_FOCALTECH_REE_ATA)),)
CONFIG_FINGERPRINT_FOCALTECH_TEE_REE := y
endif

#==========================================================================
# source files and ccflags
obj-$(CONFIG_FINGERPRINT_FOCALTECH_REE_ATA) := focaltech_fp.o
focaltech_fp-y := ff_core.o

#===For MTK platform
ifeq ($(CONFIG_ARCH_MEDIATEK),y)
CONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK := y
endif

ifeq ($(CONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK),y)
ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(srctree)/drivers/gpu/drm/mediatek/mediatek_v2/
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK
focaltech_fp-y += ff_spi.o ff_chip.o
endif


ifeq ($(CONFIG_FINGERPRINT_FOCALTECH_TEE_REE),y)
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_TEE_REE
endif

#===For other platforms
ifeq ($(CONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK),y)
ifeq ($(CONFIG_FINGERPRINT_FOCALTECH_TEE_REE),y)
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_TEE_REE
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_SPI_SUPPORT
focaltech_fp-y += ff_spi.o ff_chip.o
endif
endif



