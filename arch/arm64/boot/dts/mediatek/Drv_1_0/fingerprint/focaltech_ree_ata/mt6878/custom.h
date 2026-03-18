#include "../../../drv_common.h"


#if PRI_FOCALTECH_REE_ATA_CONFIG_1
#define SPI_INDEX spi1
#define FP_IRQ_PIN 13
#define FP_RESET_PIN 1

#define VFP_POWER_NAME mt6369_vfp

#define FP_CK_AS_SPI    PINMUX_GPIO60__FUNC_SPI1_CLK
#define FP_CS_AS_SPI    PINMUX_GPIO61__FUNC_SPI1_CSB
#define FP_MO_AS_SPI    PINMUX_GPIO62__FUNC_SPI1_MO
#define FP_MI_AS_SPI    PINMUX_GPIO63__FUNC_SPI1_MI


#endif