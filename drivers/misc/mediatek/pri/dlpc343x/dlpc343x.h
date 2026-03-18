#ifndef __GC9A01__H__
#define __GC9A01__H__

#define DLP_INFO(fmt, arg...) pr_info("dlp_info:(%s, %d): " fmt, __func__, __LINE__, ##arg);
#define DLP_ERR(fmt, arg...) pr_err("dlp_err:(%s, %d): " fmt, __func__, __LINE__, ##arg);

uint8_t dlpc3430_all_reg[][2] = {
	//regaddr, return parameters number
	{0x02,1}, // Read Single Buffer Mode (02h)
	{0x04,1}, // Read Idle Mode Select (04h)
	{0x06,1}, // Read Input Source Select (06h)
	{0x08,1}, // Read External Video Source Format Select (08h)
	{0x0A,2}, // Read External Video Chroma Processing Select (0Ah)
	{0x0C,6}, // Read Test Pattern Select (0Ch)
	{0x0E,1}, // Read Splash Screen Select (0Eh)
	{0x0F,13}, // Read Splash Screen Header (0Fh)
	{0x11,8}, // Read Image Crop (11h)
	{0x13,8}, // Read Display Size (13h)
	{0x15,1}, // Read Display Image Orientation (15h)
	{0x17,1}, // Read Display Image Curtain (17h)
	{0x1B,1}, // Read Image Freeze (1Bh)
	{0x23,6}, // Read Look Select (23h)
	{0x26,30}, // Read Sequence Header Attributes (26h)
	{0x28,1}, // Read Degamma/CMT Select (28h)
	{0x2A,1}, // Read CCA Select (2Ah)
	{0x2C,1}, // Read DMD Sequencer Sync Mode (2Ch)
	{0x2F,4}, // Read Input Image Size (2Fh)
	{0x38,1}, // Read Parallel Data Mask Control (38h)
	{0x3A,1}, // Read Mirrors Lock Command (3Ah)
	{0x51,1}, // Read LED Output Control Method (51h)
	{0x53,1}, // Read RGB LED Enable (53h)
	{0x55,6}, // Read RGB LED Current (55h)
	{0x57,2}, // Read CAIC LED Max Available Power (57h)
	{0x5D,6}, // Read RGB LED Max Current (5Dh)
	{0x5F,6}, // Read CAIC RGB LED Current (5Fh)
	{0x81,3}, // Read Local Area Brightness Boost Control (81h)
	{0x85,3}, // Read CAIC Image Processing Control (85h)
	{0x87,1}, // Read Color Coordinate Adjustment Control (87h)
	{0x89,5}, // Read Keystone Correction Control (89h)
	{0xB3,1}, // Read Border Color (B3h)
	{0xB7,1}, // Read Parallel Interface Sync Polarity (B7h)
	{0xBA,14}, // Read Auto Framing Information (BAh)
	{0xBC,2}, // Read Keystone Projection Pitch Angle (BCh)
	{0xBE,2}, // Read DSI HS Clock (BEh)
	{0xD0,1}, // Read Short Status (D0h)
	{0xD1,4}, // Read System Status (D1h)
	{0xD2,8}, // Read System Software Version (D2h)
	{0xD3,6}, // Read Communication Status (D3h)
	{0xD4,1}, // Read Controller Device ID (D4h)
	{0xD5,1}, // Read DMD Device ID (D5h)
	{0xD6,2}, // Read System Temperature (D6h)
	{0xD8,1}, // Read DSI Port Enable (D8h)
	{0xD9,4}, // Read Flash Build Version (D9h)
	{0xDC,11}  // Read DMD I/F Training Data (DCh)
};

#endif