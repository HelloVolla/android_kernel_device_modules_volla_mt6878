#==========================================================================
# source files and ccflags
obj-m := ft9391_tee.o
# ft9391_tee-y := ff_core.o
ft9391_tee-y := ff_core.o ff_spi.o ff_chip.o

ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(srctree)/drivers/gpu/drm/mediatek/mediatek_v2/
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_ARCH_MTK
ccflags-y += -DCONFIG_FINGERPRINT_FOCALTECH_SPI_SUPPORT
# include $(srctree)/drivers/tee/teei/Makefile.include




