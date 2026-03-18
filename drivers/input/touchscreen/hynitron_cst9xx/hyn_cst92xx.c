#include "hyn_core.h"
#include "cst92xx_fw.h"

/* pri LAX10-192 added by xuejian 20240410 begin*/
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_sub_tp_info;
#endif
/* pri LAX10-192 added by xuejian 20240410 end*/

#define BOOT_I2C_ADDR   (0x5A)
#define MAIN_I2C_ADDR   (0x5A)
#define RW_REG_LEN   (2)

#define CST92XX_BIN_SIZE    (0x7F80)

#define HYNITRON_PROGRAM_PAGE_SIZE (128)

static struct hyn_ts_data *hyn_92xxdata = NULL;


static const struct hyn_chip_series hyn_6xx_fw[] = {
    {0xCACA9217,0xffffffff,"cst9217",(u8*)fw_bin},//if PART_NO_EN==0 use default chip
    {0xCACA9220,0xffffffff,"cst9220",(u8*)fw_bin},
    {0,0,NULL}
};

static int cst92xx_updata_judge(u8 *p_fw, u16 len);
static u32 cst92xx_read_checksum(void);
static int cst92xx_updata_tpinfo(void);
static int cst92xx_enter_boot(void);
static void cst92xx_rst(void);
static int cst92xx_set_workmode(enum work_mode mode,u8 enable);
static int cst92xx_read_chip_id(void);

static int cst92xx_init(struct hyn_ts_data* ts_data)
{
    int ret = 0;
    HYN_ENTER();
    hyn_92xxdata = ts_data;
    hyn_set_i2c_addr(hyn_92xxdata,BOOT_I2C_ADDR);
    ret = cst92xx_read_chip_id();
    if(ret == FALSE){
        HYN_INFO("cst92xx_read_chip_id failed");
        return FALSE;
    }
    ret = cst92xx_updata_tpinfo();
    if(ret == FALSE){
        HYN_INFO("cst92xx_updata_tpinfo failed");
    }

    hyn_92xxdata->fw_updata_addr = (u8*)fw_bin;
    hyn_92xxdata->fw_updata_len = CST92XX_BIN_SIZE;
    hyn_92xxdata->need_updata_fw = cst92xx_updata_judge((u8*)fw_bin,CST92XX_BIN_SIZE);
    if(hyn_92xxdata->need_updata_fw){
        HYN_INFO("need updata FW !!!");
    }
    cst92xx_rst();
    msleep(40);
    ret = cst92xx_updata_tpinfo();

/* pri LAX10-192 added by xuejian 20240410 begin*/
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
    sprintf(current_sub_tp_info.chip, "FW:0x%08x",hyn_92xxdata->hw_info.fw_ver);
    sprintf(current_sub_tp_info.id,"0x%04x",hyn_92xxdata->hw_info.fw_project_id);
    strcpy(current_sub_tp_info.vendor, "hynitron");
    sprintf(current_sub_tp_info.more, "%d*%d", 336, 480);
#endif
/* pri LAX10-192 added by xuejian 20240410 end*/

    if(ret == FALSE){
        HYN_INFO("cst92xx_updata_tpinfo failed");
    }
    HYN_INFO("cst92xx_init done !!!");
    return TRUE;
}


static int  cst92xx_enter_boot(void)
{
    int ok = FALSE;
    uint8_t i2c_buf[4] = {0};

    for (uint8_t t = 10;; t += 2)
    {
        if (t >= 30){
            return FALSE;
        }

        cst92xx_rst();
        mdelay(t);

        ok = hyn_wr_reg(hyn_92xxdata, 0xA001AA, 3, i2c_buf, 0);
        if(ok == FALSE){
            continue;
        }

        mdelay(1);

        ok = hyn_wr_reg(hyn_92xxdata, 0xA002,  2, i2c_buf, 2);
        if(ok == FALSE){
            continue;
        }
        HYN_INFO("[HYN][nadal-87]i2c_buf[0]:0x%x i2c_buf[1]:0x%x\n",i2c_buf[0],i2c_buf[1]);
        if ((i2c_buf[0] == 0x55) && (i2c_buf[1] == 0xB0)) {
            break;
        }
    }

    ok = hyn_wr_reg(hyn_92xxdata, 0xA00100, 3, i2c_buf, 0);
    HYN_INFO("[HYN][nadal-94]i2c_buf[0]:0x%x i2c_buf[1]:0x%x i2c_buf[2]:0x%x i2c_buf[3]:0x%x\n",
                        i2c_buf[0],i2c_buf[1],i2c_buf[2],i2c_buf[3]);
    if(ok == FALSE){
        return FALSE;
    }

    return TRUE;
}

static int erase_all_mem(void)
{
    int ok = FALSE;
    u8 i2c_buf[8];

	//erase_all_mem
    ok = hyn_wr_reg(hyn_92xxdata, 0xA0140000, 4, i2c_buf, 0);
    if (ok == FALSE){
        return FALSE;
    }
    ok = hyn_wr_reg(hyn_92xxdata, 0xA00C807F, 4, i2c_buf, 0);
    if (ok == FALSE){
        return FALSE;
    }
    ok = hyn_wr_reg(hyn_92xxdata, 0xA004EC, 3, i2c_buf, 0);
    if (ok == FALSE){
        return FALSE;
    }
        
    mdelay(300);
    for (uint16_t t = 0;; t += 10) {
        if (t >= 1000) {
           return FALSE;
        }

        mdelay(10);

        ok = hyn_wr_reg(hyn_92xxdata, 0xA005, 2, i2c_buf, 1);
        if (ok == FALSE) {
            continue;
        }

        if (i2c_buf[0] == 0x88) {
            break;
        }
    }

    return TRUE;
}




static int write_mem_page(uint16_t addr, uint8_t *buf, uint16_t len)
{
    int ok = FALSE;
    uint8_t i2c_buf[1024+2] = {0};

    i2c_buf[0] = 0xA0;
    i2c_buf[1] = 0x0C;
    i2c_buf[2] = len;
    i2c_buf[3] = len >> 8;
    //ok = hyn_i2c_write_r16(HYN_BOOT_I2C_ADDR, 0xA00C, i2c_buf, 2);
    ok = hyn_write_data(hyn_92xxdata, i2c_buf,RW_REG_LEN, 4);
    if(ok == FALSE){
         return FALSE;
    }


    i2c_buf[0] = 0xA0;
    i2c_buf[1] = 0x14;
    i2c_buf[2] = addr;
    i2c_buf[3] = addr >> 8;
    ok = hyn_write_data(hyn_92xxdata, i2c_buf,RW_REG_LEN, 4);
    if(ok == FALSE) {
        return FALSE;
    }


    i2c_buf[0] = 0xA0;
    i2c_buf[1] = 0x18;
    memcpy(i2c_buf + 2, buf, len);            
    ok = hyn_write_data(hyn_92xxdata, i2c_buf,RW_REG_LEN, len+2);
    if(ok == FALSE){
        return FALSE;
    }


    ok =  hyn_wr_reg(hyn_92xxdata,0xA004EE,3,i2c_buf,0);
    if(ok == FALSE){
        return FALSE;
    }

    for (uint16_t t = 0;; t += 10) {
        if (t >= 1000) {
            return FALSE;
        }

        mdelay(5);

        ok =  hyn_wr_reg(hyn_92xxdata,0xA005,2,i2c_buf,1);
        if(ok == FALSE){
            continue;
        }        

        if (i2c_buf[0] == 0x55) {
            break;
        }
    }

    return TRUE;
}


static int write_code(u8 *bin_addr,uint8_t retry)
{
    uint8_t data[HYNITRON_PROGRAM_PAGE_SIZE+4];//= (uint8_t *)bin_addr;
    uint16_t addr = 0;
    uint16_t remain_len = CST92XX_BIN_SIZE;
    int ret;
   
    while (remain_len > 0) {
        uint16_t cur_len = remain_len;
        if (cur_len > HYNITRON_PROGRAM_PAGE_SIZE) {
            cur_len = HYNITRON_PROGRAM_PAGE_SIZE;
        }
        
        if(0 == hyn_92xxdata->fw_file_name[0]){
            memcpy(data, bin_addr + addr, HYNITRON_PROGRAM_PAGE_SIZE);
        }else{
            ret = copy_for_updata(hyn_92xxdata,data,addr,HYNITRON_PROGRAM_PAGE_SIZE);
            if(ret == FALSE){
                HYN_ERROR("copy_for_updata error");
                return FALSE;   
            }
        }
        //HYN_INFO("write_code addr 0x%x 0x%x",addr,*data);
        if (write_mem_page(addr, data, cur_len) ==  FALSE) {
             return FALSE;
        }
        //data += cur_len;
        addr += cur_len;
        remain_len -= cur_len;
    }
    return TRUE;
}


static uint32_t cst92xx_read_checksum(void)
{
    int ok = FALSE;
    uint8_t i2c_buf[4] = {0};
    uint32_t chip_checksum = 0;
    uint8_t retry = 5;
    
    hyn_92xxdata->boot_is_pass = 0;
    ok =  hyn_wr_reg(hyn_92xxdata,0xA00300,3,i2c_buf,0);
    if (ok == FALSE) {
        return FALSE;
    }      
    mdelay(2);    
    while(retry--){
        mdelay(5);
        ok =  hyn_wr_reg(hyn_92xxdata,0xA000,2,i2c_buf,1);
        if (ok == FALSE) {
            continue;
        }
        if(i2c_buf[0]!=0) break;
    }

    mdelay(1);
     if(i2c_buf[0] == 0x01){
        hyn_92xxdata->boot_is_pass = 1;
        memset(i2c_buf,0,sizeof(i2c_buf));
        ok =  hyn_wr_reg(hyn_92xxdata,0xA008,2,i2c_buf,4);
        if (ok == FALSE) {
            return FALSE;
        }      

        chip_checksum = ((uint32_t)(i2c_buf[0])) |
            (((uint32_t)(i2c_buf[1])) << 8) |
            (((uint32_t)(i2c_buf[2])) << 16) |
            (((uint32_t)(i2c_buf[3])) << 24);
    }
    else{
        hyn_92xxdata->need_updata_fw = 1;
    }

    return chip_checksum;
}


static int cst92xx_updata_fw(u8 *bin_addr, u16 len)
{ 
    #define CHECKSUM_OFFECT  (0x7F6C)
    int retry = 0;
    int ok_copy = TRUE;
    int ok = FALSE;
    u8 i2c_buf[4];

    u32 fw_checksum=0;
    HYN_ENTER();

    if(len < CST92XX_BIN_SIZE){
        HYN_ERROR("len = %d",len);
        goto UPDATA_END;
    }
    if(len > CST92XX_BIN_SIZE) len = CST92XX_BIN_SIZE;

    if(0 != hyn_92xxdata->fw_file_name[0]){
        //node to update
        copy_for_updata(hyn_92xxdata,i2c_buf,CST92XX_BIN_SIZE-20,4);
        fw_checksum = U8TO32(i2c_buf[3],i2c_buf[2],i2c_buf[1],i2c_buf[0]);
        if(hyn_92xxdata->hw_info.ic_fw_checksum == fw_checksum){
             HYN_INFO("no update,fw_checksum is same:0x%04x",fw_checksum);
             goto UPDATA_END;
        }
    }else{
        fw_checksum = U8TO32(bin_addr[CHECKSUM_OFFECT+3],bin_addr[CHECKSUM_OFFECT+2],bin_addr[CHECKSUM_OFFECT+1],bin_addr[CHECKSUM_OFFECT+0]);
    }
    HYN_INFO("updating fw checksum:0x%04x",fw_checksum);

    hyn_irq_set(hyn_92xxdata,DISABLE);
    hyn_set_i2c_addr(hyn_92xxdata,BOOT_I2C_ADDR);
    
    HYN_INFO("updata_fw start");
    for(retry = 1; retry<5; retry++){
        hyn_92xxdata->fw_updata_process = 0;
        ok = cst92xx_enter_boot();
        if (ok == FALSE){
            continue;
        }
        hyn_92xxdata->fw_updata_process = 10;
        ok = erase_all_mem();
        if (ok == FALSE){
            continue;
        }
        hyn_92xxdata->fw_updata_process = 20;
        ok = write_code(bin_addr,retry);
        if (ok == FALSE){
            continue;
        }
        hyn_92xxdata->fw_updata_process = 30;
        hyn_92xxdata->hw_info.ic_fw_checksum = cst92xx_read_checksum();
        if(fw_checksum != hyn_92xxdata->hw_info.ic_fw_checksum){
            HYN_INFO("out data fw checksum err:0x%04x",hyn_92xxdata->hw_info.ic_fw_checksum);
            continue;
        }
        hyn_92xxdata->fw_updata_process = 40;   
        if(retry>=5){
            ok_copy = FALSE;
            break;
        }
        break;
    }

    hyn_wr_reg(hyn_92xxdata,0xA006EE,3,i2c_buf,0); //exit boot
    mdelay(2);

UPDATA_END:   
    cst92xx_rst();
    mdelay(50);

    hyn_set_i2c_addr(hyn_92xxdata,MAIN_I2C_ADDR);   

    if(ok_copy == TRUE){
        cst92xx_updata_tpinfo();
        HYN_INFO("updata_fw success");
#if IS_ENABLED(CONFIG_PRIZE_HARDWARE_INFO)
    sprintf(current_sub_tp_info.chip, "FW:0x%08x",hyn_92xxdata->hw_info.fw_ver);
#endif
    }
    else{
        HYN_INFO("updata_fw failed");
    }

    hyn_irq_set(hyn_92xxdata,ENABLE);

    return ok_copy;
}
static int16_t read_word_from_mem(uint8_t type, uint16_t addr, uint32_t *value)
{
    int16_t ret = 0;
    uint8_t i2c_buf[4] = {0};

    i2c_buf[0] = 0xA0;
    i2c_buf[1] = 0x10;
    i2c_buf[2] = type;
    ret = hyn_write_data(hyn_92xxdata,i2c_buf,2,3); 
    if (ret)
    {
        return -1;
    }

    i2c_buf[0] = 0xA0;
    i2c_buf[1] = 0x0C;
    i2c_buf[2] = addr;
    i2c_buf[3] = addr >> 8;
    ret = hyn_write_data(hyn_92xxdata,i2c_buf,2,4); 
    if (ret)
    {
        return -1;
    }

    i2c_buf[0] = 0xA0;
    i2c_buf[1] = 0x04;
    i2c_buf[2] = 0xE4;
    ret = hyn_write_data(hyn_92xxdata,i2c_buf,2,3); 
    if (ret)
    {
        return -1;
    }

    for (uint8_t t = 0;; t++)
    {
        if (t >= 100)
        {
            return -1;
        }
        ret =hyn_wr_reg(hyn_92xxdata,0xA004,2,i2c_buf,1);
        if (ret)
        {
            continue;
        }
        if (i2c_buf[0] == 0x00)
        {
            break;
        }
    }
    ret =hyn_wr_reg(hyn_92xxdata,0xA018,2,i2c_buf,4);
    if (ret)
    {
        return -1;
    }
    *value = ((uint32_t)(i2c_buf[0])) |
             (((uint32_t)(i2c_buf[1])) << 8) |
             (((uint32_t)(i2c_buf[2])) << 16) |
             (((uint32_t)(i2c_buf[3])) << 24);

    return 0;
}
static int cst92xx_read_chip_id(void)
{
    int16_t ret = 0;
    uint8_t retry = 3;
    uint32_t partno_chip_type,module_id;

    ret = cst92xx_enter_boot();
    if (ret == FALSE)
    {
        HYN_ERROR("enter_bootloader error");
        return -1;
    }
    for (; retry > 0; retry--)
    {
        // partno
        ret = read_word_from_mem(1, 0x077C, &partno_chip_type);
        if (ret)
        {
            continue;
        }
        // module id
        ret = read_word_from_mem(0, 0x7FC0, &module_id);
        if (ret)
        {
            continue;
        }
        if ((partno_chip_type >> 16) == 0xCACA)
        {
            partno_chip_type &= 0xffff;
            break;
        }
    }
    cst92xx_rst();
    msleep(40);
    HYN_INFO("partno_chip_type: 0x%04x", partno_chip_type);
    HYN_INFO("module_id: 0x%04x", module_id);
    if ((partno_chip_type != 0x9217) && (partno_chip_type != 0x9220))
    {
        HYN_ERROR("partno_chip_type error 0x%04x", partno_chip_type);
        //return -1;
    }
    return 0;
}

static int cst92xx_updata_tpinfo(void)
{
    u8 buf[30];
    struct tp_info *ic = &hyn_92xxdata->hw_info;
    int ret = 0;

    cst92xx_set_workmode(0xff,DISABLE);
    ret = hyn_wr_reg(hyn_92xxdata,0xD101,2,buf,0);
    if(ret == FALSE){
        return FALSE;
    }
    mdelay(5);

    //firmware_project_id   firmware_ic_type
    ret = hyn_wr_reg(hyn_92xxdata,0xD1F4,2,buf,28);
    if(ret == FALSE){
        return FALSE;
    }
    ic->fw_project_id = ((uint16_t)buf[17] <<8) + buf[16];
    ic->fw_chip_type = ((uint16_t)buf[19] <<8) + buf[18];

    //firmware_version
    ic->fw_ver = (buf[23]<<24)|(buf[22]<<16)|(buf[21]<<8)|buf[20];

    //tx_num   rx_num   key_num
    ic->fw_sensor_txnum = ((uint16_t)buf[1]<<8) + buf[0];
    ic->fw_sensor_rxnum = buf[2];
    ic->fw_key_num = buf[3];

    ic->fw_res_y = (buf[7]<<8)|buf[6];
    ic->fw_res_x = (buf[5]<<8)|buf[4];

    HYN_INFO("IC_info fw_project_id:%04x ictype:%04x fw_ver:%x checksum:%#x",ic->fw_project_id,ic->fw_chip_type,ic->fw_ver,ic->ic_fw_checksum);

/*pri LAX10-651 modify by ocean 20240615 start*/
    cst92xx_rst();
/*pri LAX10-651 modify by ocean 20240615 end*/

    cst92xx_set_workmode(NOMAL_MODE,ENABLE);

    return TRUE;
}

static int cst92xx_updata_judge(u8 *p_fw, u16 len)
{
    u32 f_checksum,f_fw_ver,f_ictype,f_fw_project_id;
    u8 *p_data = p_fw + len - 28;   //7F64
    struct tp_info *ic = &hyn_92xxdata->hw_info;
    int ret;

    ret = cst92xx_enter_boot();
    if (ret == FALSE){
        HYN_INFO("cst92xx_enter_boot fail,need update");
        return 1; 
    }
    hyn_92xxdata->hw_info.ic_fw_checksum = cst92xx_read_checksum();
    if(hyn_92xxdata->boot_is_pass == 0){
        HYN_INFO("boot_is_pass %d,need force update",hyn_92xxdata->boot_is_pass);
        return 1; //need updata
    }
    
    f_fw_project_id = U8TO16(p_data[1],p_data[0]);
    f_ictype = U8TO16(p_data[3],p_data[2]);

    f_fw_ver = U8TO16(p_data[7],p_data[6]);
    f_fw_ver = (f_fw_ver<<16)|U8TO16(p_data[5],p_data[4]);

    f_checksum = U8TO16(p_data[11],p_data[10]);
    f_checksum = (f_checksum << 16)|U8TO16(p_data[9],p_data[8]);

/* pri LAX10-530 added by ocean 20240510 end*/
    HYN_INFO("old fw_project_id:0x%04x ictype:0x%04x fw_ver:0x%x checksum:0x%x",f_fw_project_id,f_ictype,f_fw_ver,f_checksum);
    if(f_ictype != ic->fw_chip_type){
        HYN_ERROR("not update,please confirm: ic_type 0x%04x,ic_project_id 0x%04x",ic->fw_chip_type,ic->fw_project_id);
        return 0; //not updata
    }
        return 1; //need updata
/* pri LAX10-530 added by ocean 20240510 end*/
}

//------------------------------------------------------------------------------//

static int cst92xx_set_workmode(enum work_mode mode,u8 enable)
{
    int ok = FALSE;
    uint8_t i2c_buf[4] = {0};
    uint8_t i = 0;

    HYN_ENTER();
/* pri KMXGK-7 added by ocean 20240803 begin*/
        hyn_92xxdata->work_mode = mode;
        hyn_esdcheck_switch(hyn_92xxdata,DISABLE);
        for(i=0;i<3;i++)
        {
            ok = hyn_wr_reg(hyn_92xxdata,0xD11E,2,i2c_buf,0);
            if (ok == FALSE) {
                msleep(1);
                continue;
            }
            msleep(1);
            ok = hyn_wr_reg(hyn_92xxdata,0x0002,2,i2c_buf,2);
            if (ok == FALSE) {
                msleep(1);
                continue;
            }
            if(i2c_buf[1] == 0x1E){
                break;
            }
        }
        HYN_INFO("mode = %d\n",mode);
        switch(mode){
            case NOMAL_MODE:
                hyn_irq_set(hyn_92xxdata,ENABLE);
                hyn_esdcheck_switch(hyn_92xxdata,ENABLE);
                ok = hyn_wr_reg(hyn_92xxdata,0xD109,2,i2c_buf,0);
                if (ok == FALSE) {
                    return FALSE;
                }
                break;
            case GESTURE_MODE:
                ok = hyn_wr_reg(hyn_92xxdata,0xD104,2,i2c_buf,0);
                if (ok == FALSE) {
                    return FALSE;
                }
                break;
            case LP_MODE:
                ok = hyn_wr_reg(hyn_92xxdata,0xD107,2,i2c_buf,0);
                if (ok == FALSE) {
                    return FALSE;
                }
                break;
            case DIFF_MODE:
                ok = hyn_wr_reg(hyn_92xxdata,0xD10D,2,i2c_buf,0);
                if (ok == FALSE) {
                    return FALSE;
                }
                break;

            case RAWDATA_MODE:
                ok = hyn_wr_reg(hyn_92xxdata,0xD10A,2,i2c_buf,0);
                if (ok == FALSE) {
                    return FALSE;
                }
                break;
            case FAC_TEST_MODE:
                hyn_irq_set(hyn_92xxdata,DISABLE);
                hyn_wr_reg(hyn_92xxdata,0xD114,2,i2c_buf,0);
          
                break;
            case DEEPSLEEP:
                hyn_irq_set(hyn_92xxdata,DISABLE);
                hyn_wr_reg(hyn_92xxdata,0xD105,2,i2c_buf,0);
                break;
            default :
                //hyn_esdcheck_switch(hyn_92xxdata,ENABLE);
                hyn_92xxdata->work_mode = NOMAL_MODE;
                break;
        }
/* pri KMXGK-7 added by ocean 20240803 end*/
    return TRUE;
}

static void cst92xx_rst(void)
{
    HYN_ENTER();
    gpio_set_value(hyn_92xxdata->plat_data.reset_gpio,0);
    msleep(5);
    gpio_set_value(hyn_92xxdata->plat_data.reset_gpio,1);
}



static int cst92xx_supend(void)
{
    HYN_ENTER();
    cst92xx_set_workmode(DEEPSLEEP,0);
    return 0;
}

static int cst92xx_resum(void)
{
    cst92xx_rst();
    msleep(50);
    cst92xx_set_workmode(NOMAL_MODE,0);
    return 0;
}


static int cst92xx_report(void)
{
    int ok = FALSE;
    uint8_t i2c_buf[MAX_POINTS_REPORT*5+5] = {0};
    uint8_t finger_num = 0;
    uint8_t key_state=0,key_id = 0;

    hyn_92xxdata->rp_buf.report_need = REPORT_NONE;

    ok = hyn_wr_reg(hyn_92xxdata,0xD000,2,i2c_buf,sizeof(i2c_buf));
    if (ok == FALSE){
        return FALSE;
    }   
        
    ok = hyn_wr_reg(hyn_92xxdata,0xD000AB,3,i2c_buf,0);
    if (ok == FALSE){
        return FALSE;
    }   

/* pri KMXGK-7 added by ocean 20240803 begin*/
    if (i2c_buf[0] == 0xAB) {
        HYN_INFO("fail buf[6]=0x%02x",i2c_buf[0]);
        return FALSE;
    }
/* pri KMXGK-7 added by ocean 20240803 end*/

    if (i2c_buf[6] != 0xAB) {
        HYN_INFO("fail buf[6]=0x%02x",i2c_buf[6]);
        return FALSE;
    }

    finger_num = i2c_buf[5] & 0x7F;
    
    if (finger_num > MAX_POINTS_REPORT) {
        HYN_INFO("fail finger_num=%d",finger_num);
        return TRUE;
    }
    hyn_92xxdata->rp_buf.rep_num = finger_num;

   
    if ((i2c_buf[5] & 0x80) == 0x80) { // button
        uint8_t *data = i2c_buf + finger_num * 5;
        if (finger_num > 0) {
            data += 2;
        }
        key_state = data[0];//0x83:按键有触摸  0x80:按键抬起
        key_id = data[1]; // data[1]; :0x17  0x27  0x37

        if(key_state&0x80){            
            hyn_92xxdata->rp_buf.report_need |= REPORT_KEY;
            if((key_id == hyn_92xxdata->rp_buf.key_id || 0 == hyn_92xxdata->rp_buf.key_state)&& key_state == 0x83){
                hyn_92xxdata->rp_buf.key_id = key_id;
                hyn_92xxdata->rp_buf.key_state = 1;
            }
            else{
                hyn_92xxdata->rp_buf.key_state = 0;
            }
        }
    }
    else//pos
    {
        uint8_t index = 0;
        if((i2c_buf[4]&0xF0) > 0){
            if (i2c_buf[4] == 0x20) {
                hyn_92xxdata->gesture_id = 14;//double-click wakeup
            } else if (i2c_buf[4] == 0x10) {
                hyn_92xxdata->gesture_id = 15;//once-click wakeup
            }
            HYN_INFO("i2c_buf[4]=0x%x,gesture_id=0x%x",i2c_buf[4],hyn_92xxdata->gesture_id);
            hyn_92xxdata->rp_buf.report_need |= REPORT_GES;
            return TRUE;
        }
        
        hyn_92xxdata->rp_buf.report_need |= REPORT_POS;
        if (finger_num > 0) {
            uint8_t *data = i2c_buf;
           //uint8_t *data_ges = i2c_buf + finger_num * 5 + 2;
            uint8_t id = data[0] >> 4;
            uint8_t switch_ = data[0] & 0x0F;
            uint16_t x = ((uint16_t)(data[1]) << 4) | (data[3] >> 4);
            uint16_t y = ((uint16_t)(data[2]) << 4) | (data[3] & 0x0F);
            uint16_t z = (data[3] & 0x1F) + 0x03;

            HYN_INFO("finger=%d id=%d x=%d y=%d switch=%d",finger_num,id,x,y,switch_);

            if (id < MAX_POINTS_REPORT) {
                hyn_92xxdata->rp_buf.pos_info[index].pos_id = id;
                hyn_92xxdata->rp_buf.pos_info[index].event = (switch_ == 0x06 || switch_ == 0x07) ? 1 : 0;  
                hyn_92xxdata->rp_buf.pos_info[index].pos_x = x;
                hyn_92xxdata->rp_buf.pos_info[index].pos_y = y;
                hyn_92xxdata->rp_buf.pos_info[index].pres_z = z;
                index++;
            }
            

        }

        for (uint8_t i = 1; i < finger_num; i++) {
            uint8_t *data = i2c_buf+5*i+2;
            uint8_t id = data[0] >> 4;
            uint8_t switch_ = data[0] & 0x0F;
            uint16_t x = ((uint16_t)(data[1]) << 4) | (data[3] >> 4);
            uint16_t y = ((uint16_t)(data[2]) << 4) | (data[3] & 0x0F);
            uint16_t z = (data[4] & 0x7F);

            if (id < MAX_POINTS_REPORT) {
                hyn_92xxdata->rp_buf.pos_info[index].pos_id = id;
                hyn_92xxdata->rp_buf.pos_info[index].event = (switch_ == 0x06) ? 1 : 0;  
                hyn_92xxdata->rp_buf.pos_info[index].pos_x = x;
                hyn_92xxdata->rp_buf.pos_info[index].pos_y = y;
                hyn_92xxdata->rp_buf.pos_info[index].pres_z = z;
                index++;
            }
        }
    }

    return TRUE;
}
static u32 cst92xx_check_esd(void)
{
    int ret = 0;
    uint8_t retry=3;
    u8 buf[6];
    HYN_ENTER();

    while (retry--)
    {
        ret = hyn_wr_reg(hyn_92xxdata,0xD040,2,buf,0);
        ret = hyn_wr_reg(hyn_92xxdata,0xD040,2,buf,6);
        if(ret ==0 && hyn_sum16(0xA5,buf,4)==U8TO16(buf[4],buf[5])){
            ret = U8TO32(buf[0],buf[1],buf[2],buf[3]);
            break;
        }
    }
    

    return ret;
}
#if 0
static u32 cst92xx_check_esd(void)
{

    int16_t ok = FALSE;
    uint8_t i2c_buf[6], retry;
    uint8_t flag = 0;
    uint16_t checksum = 0;
    uint32_t esd_value = 0;

    if ((hyn_92xxdata->work_mode != NOMAL_MODE)&&(hyn_92xxdata->work_mode != GESTURE_MODE))
    {
        HYN_ERROR("esd_check work_mode error return");
        return 0;
    }
    retry = 4;
    flag = 0;
    while (retry--)
    {
        ok = hyn_wr_reg(hyn_92xxdata,0xD040,2,i2c_buf,6);
        if (ok == FALSE){
            msleep(1);
            continue;
        }  
        else
        {
            checksum = i2c_buf[0] + i2c_buf[1] + i2c_buf[2] + i2c_buf[3] + 0xA5;
            esd_value = (i2c_buf[0] << 24) + (i2c_buf[1] << 16) + (i2c_buf[2] << 8) + i2c_buf[3];
            flag = 1;
            if (((i2c_buf[4] << 8) + i2c_buf[5]) == checksum)
            {
                flag = 2;
                if (((esd_value - hyn_92xxdata->esd_last_value) < 1) && (esd_value > 20))
                {
                    flag = 3;
                }
                break;
            }
            else
            {
                HYN_INFO("ESD data checksum err:0x%x,0x%x", esd_value, checksum);
                continue;
            }
        }
    }
    HYN_INFO("ESD data:%d,0x%04x,0x%04x", flag, esd_value, hyn_92xxdata->esd_last_value);

    if ((flag == 0) || (flag == 3) || (flag == 1))
    {
        cst92xx_rst();
        msleep(40);
        esd_value = 0;
        hyn_92xxdata->esd_last_value = 0;
        HYN_INFO("esd_check power reset ic");
        if (hyn_92xxdata->work_mode == GESTURE_MODE)
        {
            ok = hyn_wr_reg(hyn_92xxdata,0xD104,2,i2c_buf,0);
            if (ok == FALSE){
                HYN_ERROR("enter_sleep failed");
            }  
        }
    }
    
    hyn_92xxdata->esd_last_value = esd_value;
 
    return esd_value;
}
#endif

static int cst92xx_prox_handle(u8 cmd)
{
    return TRUE;
}


static int cst92xx_get_dbg_data(u8 *buf, u16 len)
{
    int ret = -1;  
    u16 read_len = (hyn_92xxdata->hw_info.fw_sensor_txnum * hyn_92xxdata->hw_info.fw_sensor_rxnum)*2;
    u16 total_len = read_len + (hyn_92xxdata->hw_info.fw_sensor_txnum + hyn_92xxdata->hw_info.fw_sensor_rxnum)*2;
    HYN_ENTER();
    if(total_len > len){
        HYN_ERROR("buf too small");
        return -1;
    }
    switch(hyn_92xxdata->work_mode){
        case DIFF_MODE:
        case RAWDATA_MODE:
            ret = hyn_wr_reg(hyn_92xxdata,0x1000,2,buf,read_len); //mt 

            buf += read_len;
            read_len = hyn_92xxdata->hw_info.fw_sensor_rxnum*2;
            ret |= hyn_wr_reg(hyn_92xxdata,0x7000,2,buf,read_len); //rx

            buf += read_len;
            read_len = hyn_92xxdata->hw_info.fw_sensor_txnum*2;
            ret |= hyn_wr_reg(hyn_92xxdata,0x7200,2,buf,read_len); //tx

            ret |= hyn_wr_reg(hyn_92xxdata,0x000500,3,0,0); //end
            break;
        default:
            HYN_ERROR("work_mode:%d",hyn_92xxdata->work_mode);
            break;
    }
    return ret==0 ? total_len:-1;
}

// factory test threshold
#define SHORT_LOW_TH (500)

#define TRX_NUM (10 * 10)
#define TRX_KEY_NUM (20 + 8)

// add test threshold array
const uint16_t factory_test_threshold[TRX_NUM] =
    {
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800,
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800,
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800,
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800,
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800,
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800,
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800,
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800,
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800,
        800, 800, 800, 800, 800, 800, 800, 800, 800, 800};

#define HYN_CHECK_FACTORY_RATIO_MIN 50
#define HYN_CHECK_FACTORY_RATIO_MAX 150

#define HYN_CHECK_FACTORY_SPECIAL_SENSOR_ENABLE 0

#if HYN_CHECK_FACTORY_SPECIAL_SENSOR_ENABLE
uint8_t white_node[12] = {0, 1, 7, 8, 9, 17, 63, 71, 72, 73, 79, 80};
int16_t check_white_node(uint8_t i)
{
    uint8_t idx;
    for (idx = 0; idx < 12; idx++)
    {
        if ((white_node[idx] == i) && (white_node[idx] < 100))
        {
            return -1;
        }
    }
    return 0;
}
#endif

static int cst92xx_get_test_result(u8 *buf, u16 len)
{

//     uint16_t node_num = 0;
//     uint16_t time_out = 1000;
//     uint16_t i = 0;
//     uint16_t j = 0;
//     uint8_t total_sensor;
//     int16_t ret;

//     union
//     {
//         uint8_t buff_u8[TRX_NUM * 2];
//         int16_t buff_u16[TRX_NUM];
//     } open_higdrv = {.buff_u16 = 0};

//     union
//     {
//         uint8_t buff_u8[(TRX_KEY_NUM) * 2];
//         int16_t buff_u16[TRX_KEY_NUM];
//     } sns_short = {.buff_u16 = 0};

//     cst92xx_rst();
//     msleep(40);
//     ret = cst92xx_updata_tpinfo();
//     if (ret == FALSE)
//     {
//         HYN_ERROR("get_firmware_info failed");
//         return -1;
//     }

//     total_sensor = (hyn_92xxdata->hw_info.fw_sensor_txnum + hyn_92xxdata->hw_info.fw_sensor_rxnum + hyn_92xxdata->hw_info.fw_key_num);
//     if (hyn_92xxdata->hw_info.fw_key_num == 0)
//     {
//         node_num = hyn_92xxdata->hw_info.fw_sensor_txnum * hyn_92xxdata->hw_info.fw_sensor_rxnum;
//     }
//     else
//     {
//         node_num = (hyn_92xxdata->hw_info.fw_sensor_txnum - 1) * hyn_92xxdata->hw_info.fw_sensor_rxnum + hyn_92xxdata->hw_info.fw_key_num;
//     }

//     if ((hyn_92xxdata->hw_info.fw_sensor_txnum > 10) || (hyn_92xxdata->hw_info.fw_sensor_rxnum > 10))
//     {
//         HYN_ERROR("IC_firmware.tx_num/rx_num ERROR");
//         return -1;
//     }
//     // get open hight result
//     hyn_wr_reg(hyn_92xxdata,0xD110,2,open_higdrv.buff_u8,0); 
//     time_out = 400;
//     while (time_out--)
//     {
//         if(hyn_wait_irq_timeout(hyn_92xxdata,100) == 0)
//             break;
//         msleep(10);
//     }

//     ret = hyn_wr_reg(hyn_92xxdata,0x3000,2,open_higdrv.buff_u8,node_num * 2); 
//     if(ret == FALSE)
//     {
//         HYN_ERROR("GET 0x3000 data ERROR");
//     }
//     hyn_wr_reg(hyn_92xxdata,0x000005,3,open_higdrv.buff_u8,0); 
//     HYN_INFO("---open_higdrv---");
//     for (i = 0, j = 0; i < node_num; i++)
//     {
//         HYN_INFO("%d ", open_higdrv.buff_u16[i]);
//         j++;
//         if (j >= hyn_92xxdata->hw_info.fw_sensor_rxnum)
//         {
//             j -= hyn_92xxdata->hw_info.fw_sensor_rxnum;
//             HYN_INFO("");
//         }
//     }

//     // get short result
//     hyn_wr_reg(hyn_92xxdata,0xD112,2,sns_short.buff_u8,0); 
//     time_out = 400;
//     while (time_out--)
//     {
//         if(hyn_wait_irq_timeout(hyn_92xxdata,100) == 0)
//             break;
//         msleep(10);
//     }
//     ret = hyn_wr_reg(hyn_92xxdata,0x5000,2,sns_short.buff_u8,(total_sensor * 2)); 
//     if(ret == FALSE)
//     {
//         HYN_ERROR("GET 0x5000 data ERROR");
//     }
//     hyn_wr_reg(hyn_92xxdata,0x000005,3,open_higdrv.buff_u8,0); 
//     HYN_INFO("---sns_short---");
//     for (i = 0, j = 0; i < (total_sensor); i++)
//     {
//         HYN_INFO("%d ", sns_short.buff_u16[i]);
//         j++;
//         if ((j == hyn_92xxdata->hw_info.fw_sensor_rxnum) ||
//             (j == (hyn_92xxdata->hw_info.fw_sensor_rxnum + hyn_92xxdata->hw_info.fw_sensor_txnum)) ||
//             (j == total_sensor))
//         {
//             HYN_INFO("");
//         }
//     }
    
//     // check open result
//     for (i = 0; i < node_num; i++)
//     {
//         uint16_t factory_test_threshold_min;
//         uint16_t factory_test_threshold_max;
// #if HYN_CHECK_FACTORY_SPECIAL_SENSOR_ENABLE
//         if (check_white_node(i))
//         {
//             HYN_INFO("check_white_node %d continue", i);
//             continue;
//         }
// #endif
//         factory_test_threshold_min = factory_test_threshold[i] * HYN_CHECK_FACTORY_RATIO_MIN / 100;
//         factory_test_threshold_max = factory_test_threshold[i] * HYN_CHECK_FACTORY_RATIO_MAX / 100;
//         if (open_higdrv.buff_u16[i] < factory_test_threshold_min) //
//         {
//             HYN_ERROR("check open_higdrv MIN ERROR:%d-%d-min-%d", i, open_higdrv.buff_u16[i], factory_test_threshold_max);
//             ret = -1; // return -1;
//         }
//         if (open_higdrv.buff_u16[i] > factory_test_threshold_max) //
//         {
//             HYN_ERROR("check open_higdrv MAX ERROR:%d-%d-max-%d", i, open_higdrv.buff_u16[i], factory_test_threshold_max);
//             ret = -1; // return -1;
//         }
//     }

//     // check short result
//     for (i = 0; i < total_sensor; i++)
//     {
//         uint16_t resis = 0;
//         if (sns_short.buff_u16[i] == 0)
//         {
//             HYN_ERROR("check short ERROR NULL:%d-%d", i, sns_short.buff_u16[i]);
//             ret = -1; // return -1;
//         }
//         resis = 2000 / sns_short.buff_u16[i];
//         if (resis < SHORT_LOW_TH)
//         {
//             HYN_ERROR("check short ERROR:%d-%d", i, sns_short.buff_u16[i]);
//             ret = -1; // return -1;
//         }
//     }
//     cst92xx_rst();
//     msleep(40);
    // return ret;
    return 0;
}



const struct hyn_ts_fuc cst92xx_fuc = {
    .tp_rest = cst92xx_rst,
    .tp_report = cst92xx_report,
    .tp_supend = cst92xx_supend,
    .tp_resum = cst92xx_resum,
    .tp_chip_init = cst92xx_init,
    .tp_updata_fw = cst92xx_updata_fw,
    .tp_set_workmode = cst92xx_set_workmode,
    .tp_check_esd = cst92xx_check_esd,
    .tp_prox_handle = cst92xx_prox_handle,
    .tp_get_dbg_data = cst92xx_get_dbg_data,
    .tp_get_test_result = cst92xx_get_test_result
};


