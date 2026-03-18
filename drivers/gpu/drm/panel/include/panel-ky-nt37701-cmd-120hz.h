/*****************************************************************************
*
* Filename:
* ---------
* panel-rayle-nt37701-cmd-120hz.h
*
* Project:
* --------
* X10
*
* Description:
* ------------
* Source code of X10 lcd driver
*
* Author:
* -------
* cs
*
*======================================================
* HISTORY
*
*
*===================================================
*************************************************************************/


#ifndef PANEL_RAYLE_NT37701_CMD_120HZ_HEAD_H
#define PANEL_RAYLE_NT37701_CMD_120HZ_HEAD_H


#define REGFLAG_DELAY           0xFFFC
#define REGFLAG_UDELAY          0xFFFB
#define REGFLAG_END_OF_TABLE    0xFFFD
#define REGFLAG_RESET_LOW       0xFFFE
#define REGFLAG_RESET_HIGH      0xFFFF

#define FRAME_WIDTH                 1224
#define FRAME_HEIGHT                2688

#define PHYSICAL_WIDTH              70697
#define PHYSICAL_HEIGHT             157104

#define DATA_RATE                   1100
#define HSA                         8
#define HBP                         92
#define VSA                         4
#define VBP                         4

/*Parameter setting for mode 0 Start*/
#define MODE_0_FPS                  60
#define MODE_0_VFP                  4
#define MODE_0_HFP                  100
#define MODE_0_DATA_RATE            1100
/*Parameter setting for mode 0 End*/

/*Parameter setting for mode 2 Start*/
#define MODE_1_FPS                  120
#define MODE_1_VFP                  4
#define MODE_1_HFP                  100
#define MODE_1_DATA_RATE            1100
/*Parameter setting for mode 2 End*/

#define LFR_EN                      1
/* DSC RELATED */

#define DSC_ENABLE                  1
#define DSC_VER                     17
#define DSC_SLICE_MODE              1
#define DSC_RGB_SWAP                0
#define DSC_DSC_CFG                 34
#define DSC_RCT_ON                  1
#define DSC_BIT_PER_CHANNEL         8
#define DSC_DSC_LINE_BUF_DEPTH      9
#define DSC_BP_ENABLE               1
#define DSC_BIT_PER_PIXEL           128
//define DSC_PIC_HEIGHT
//define DSC_PIC_WIDTH
#define DSC_SLICE_HEIGHT            12
#define DSC_SLICE_WIDTH             600
#define DSC_CHUNK_SIZE              600
#define DSC_XMIT_DELAY              512
#define DSC_DEC_DELAY               556
#define DSC_SCALE_VALUE             32
#define DSC_INCREMENT_INTERVAL      302
#define DSC_DECREMENT_INTERVAL      8
#define DSC_LINE_BPG_OFFSET         12
#define DSC_NFL_BPG_OFFSET          2235
#define DSC_SLICE_BPG_OFFSET        1953
#define DSC_INITIAL_OFFSET          6144
#define DSC_FINAL_OFFSET            4336
#define DSC_FLATNESS_MINQP          3
#define DSC_FLATNESS_MAXQP          12
#define DSC_RC_MODEL_SIZE           8192
#define DSC_RC_EDGE_FACTOR          6
#define DSC_RC_QUANT_INCR_LIMIT0    11
#define DSC_RC_QUANT_INCR_LIMIT1    11
#define DSC_RC_TGT_OFFSET_HI        3
#define DSC_RC_TGT_OFFSET_LO        3

enum panel_version{
	PANEL_V1 = 1,
	PANEL_V2,
	PANEL_V3,
};

unsigned int nt37701_cmd_fhd_buf_thresh[14] = {
	896, 1792, 2688, 3584, 4480,
	5376, 6272, 6720, 7168, 7616,
	7744, 7872, 8000, 8064};
unsigned int nt37701_cmd_fhd_range_min_qp[15] = {
	0, 0, 1, 1, 3,
	3, 3, 3, 3, 3,
	5, 5, 5, 7, 13};
unsigned int nt37701_cmd_fhd_range_max_qp[15] = {
	4, 4, 5, 6, 7,
	7, 7, 8, 9, 10,
	11, 12, 13, 13, 15};
int nt37701_cmd_fhd_range_bpg_ofs[15] = {
	2, 0, 0, -2, -4,
	-6, -8, -8, -8, -10,
	-10, -12, -12, -12, -12};

#endif //end of PANEL_RAYLE_NT37701_CMD_120HZ_HEAD_H
