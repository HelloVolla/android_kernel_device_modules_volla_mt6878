# Makefile for the hyn touchscreen drivers.


ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/

obj-m += hynitron_cst9xx.o
hynitron_cst9xx-y += hyn_core.o hyn_i2c.o hyn_ts_ext.o hyn_fs_node.o
hynitron_cst9xx-y += hyn_tool.o
hynitron_cst9xx-y += hyn_gesture.o
hynitron_cst9xx-y += hyn_cst92xx.o

