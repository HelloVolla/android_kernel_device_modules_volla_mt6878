ccflags-y += -I$(srctree)/drivers/misc/mediatek/leds/$(MTK_PLATFORM)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/video/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/imgsensor/inc/

obj-m += scr_drv.o
scr_drv-y	+= scr_rtl.o

ccflags-y += $(addprefix -DPRIZE_MB_, $(shell echo $(CONFIG_PRIZE_BOARD) | tr a-z A-Z))

