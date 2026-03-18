#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/iio/consumer.h>

#include "dlpc343x.h"
#include "ntc.h"

#define LIMIT_FEEDBACK

static struct i2c_client *g_client;
static int ls_in_en_gpio, gpio_ext5_gpio, proj_on_gpio;
#ifdef LIMIT_FEEDBACK
static int limit_feedback_gpio;
#endif

static int g_rgb_led_current_val = 0;
static int16_t g_pitch_angle_val = 0;

int g_temperature5 = -1;
EXPORT_SYMBOL_GPL(g_temperature5);
int g_temperature6 = -1;
EXPORT_SYMBOL_GPL(g_temperature6);
int g_temperature7 = -1;
EXPORT_SYMBOL_GPL(g_temperature7);

static struct iio_channel *temperature_chan5 = NULL;
static struct iio_channel *temperature_chan6 = NULL;
static struct iio_channel *temperature_chan7 = NULL;
static struct task_struct *dlpc343x_get_temp_thread;

enum orientation {
	horizontal,
	vertical,
	horizontal_0degrees,
	horizontal_180degrees
};
static int g_image_orientation = horizontal;

static struct mutex dlpc343x_mutex;
atomic_t dlp_state = ATOMIC_INIT(0);
EXPORT_SYMBOL_GPL(dlp_state);

int split_string(char *input_str, const char *delimiters, char *tokens[], int max_tokens)
{
	char *token;
	int count = 0, i;

	for (i = 0; i < max_tokens; i++) {
		tokens[i] = NULL;
	}
	token = strsep(&input_str, delimiters);

	while (token != NULL && count < max_tokens) {
		tokens[count] = token;
		count++;
		token = strsep(&input_str, delimiters);
	}

	return count;
}

static int dlpc343x_gpio_register(struct i2c_client *client)
{
	int ret = 0;
	u32 flag;
	struct device_node *dev_node = g_client->dev.of_node;

	/* request ls_in_en gpio */
	ls_in_en_gpio = of_get_named_gpio_flags(dev_node, "dlpc343x,ls_in_en-gpio", 0, &flag);
	if (!gpio_is_valid(ls_in_en_gpio)) {
		DLP_ERR("invalid ls_in_en_gpio: %d\n", ls_in_en_gpio);
		return -EBADR;
	}

	ret = gpio_request(ls_in_en_gpio, "LS_IN_EN_GPIO");
	if (ret < 0) {
		DLP_ERR("request ls_in_en_gpio failed, ret = %d\n", ret);
		gpio_free(ls_in_en_gpio);
		return -EBADR;
	}
	DLP_INFO("%s ls_in_en_gpio: %d\n", __func__, ls_in_en_gpio);

	/* request gpio_ext5 gpio */
	gpio_ext5_gpio = of_get_named_gpio_flags(dev_node, "dlpc343x,gpio_ext5-gpio", 0, &flag);
	if (!gpio_is_valid(gpio_ext5_gpio)) {
		DLP_ERR("invalid gpio_ext5_gpio: %d\n", gpio_ext5_gpio);
		return -EBADR;
	}

	ret = gpio_request(gpio_ext5_gpio, "GPIO_EXT5_GPIO");
	if (ret < 0) {
		DLP_ERR("request gpio_ext5_gpio failed, ret = %d\n", ret);
		gpio_free(gpio_ext5_gpio);
		return -EBADR;
	}
	DLP_INFO("%s gpio_ext5_gpio: %d\n", __func__, gpio_ext5_gpio);

	/* request proj_on gpio */
	proj_on_gpio = of_get_named_gpio_flags(dev_node, "dlpc343x,proj_on-gpio", 0, &flag);
	if (!gpio_is_valid(proj_on_gpio)) {
		DLP_ERR("invalid proj_on_gpio: %d\n", proj_on_gpio);
		return -EBADR;
	}

	ret = gpio_request(proj_on_gpio, "PROJ_ON_GPIO");
	if (ret < 0) {
		DLP_ERR("request proj_on_gpio failed, ret = %d\n", ret);
		gpio_free(proj_on_gpio);
		return -EBADR;
	}
	DLP_INFO("%s proj_on_gpio: %d\n", __func__, proj_on_gpio);

#ifdef LIMIT_FEEDBACK
	limit_feedback_gpio = of_get_named_gpio_flags(dev_node, "dlpc343x,limit_feedback-gpio", 0, &flag);
	if (!gpio_is_valid(limit_feedback_gpio)) {
		DLP_ERR("invalid limit_feedback_gpio: %d\n", limit_feedback_gpio);
		return -EBADR;
	}

	ret = gpio_request(limit_feedback_gpio, "LIMIT_FEEDBACK_GPIO");
	if (ret < 0) {
		DLP_ERR("request limit_feedback_gpio failed, ret = %d\n", ret);
		gpio_free(limit_feedback_gpio);
		return -EBADR;
	}
	DLP_INFO("%s limit_feedback_gpio: %d\n", __func__, limit_feedback_gpio);
#endif

	return ret;
}

#ifdef LIMIT_FEEDBACK
void set_gpio_ext5_gpio(int value)
{
	DLP_INFO("%s set gpio_ext5_gpio value=%d\n", __func__, value);
	if (value == 1) {
		gpio_set_value(gpio_ext5_gpio, 1);
	} else if (value == 0) {
		gpio_set_value(gpio_ext5_gpio, 0);
	}
}
EXPORT_SYMBOL_GPL(set_gpio_ext5_gpio);

int get_limit_feedback_state(void)
{
	int limit_feedback_gpio_state = -1;
	gpio_direction_input(limit_feedback_gpio);
	limit_feedback_gpio_state = gpio_get_value(limit_feedback_gpio);
	DLP_INFO("%s limit_feedback_gpio_state=%d\n", __func__, limit_feedback_gpio_state);
	return limit_feedback_gpio_state;
}
EXPORT_SYMBOL_GPL(get_limit_feedback_state);
#endif
/******************************************************************************/
/**************************** dlpc343x i2c driver *****************************/
/******************************************************************************/
int dlpc343x_i2c_read_reg(struct i2c_client *client, uint8_t reg_addr, uint8_t *data_buf, uint32_t len)
{
	int ret;
	struct i2c_msg msg[] = {
		[0] = {
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(uint8_t),
			.buf = &reg_addr,
		},
		[1] = {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data_buf,
		},
	};

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		DLP_ERR("i2c transfer failed\n");
		return ret;
	}

	return 0;
}

int dlpc343x_i2c_write(struct i2c_client *client, uint8_t reg_addr, uint8_t *data_buf, uint32_t len)
{
	uint8_t *data = NULL;
	int ret = -1;

	data = kmalloc(len + 1, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data[0] = reg_addr;
	memcpy(&data[1], data_buf, len);

	ret = i2c_master_send(client, data, len + 1);
	if (ret < 0) {
		DLP_ERR("i2c_master_send failed\n");
		return ret;
	}

	kfree(data);
	data = NULL;

	return 0;
}

static void dlpc343x_reg_init(void)
{
	uint8_t i, write_data_buf[128];
	uint8_t dlp_init_write_buf[][128] = {
		{0x10,8,0x00,0x00,0x00,0x00,0x56,0x03,0xE0,0x01}, // Write Image Crop (10h) 854x480
		{0x12,8,0x00,0x00,0x00,0x00,0x56,0x03,0xE0,0x01}, // Write Display Size (12h) 854x480
		{0x2E,4,0x56,0x03,0xE0,0x01}, // Write External Input Image Size (2Eh) 854x480
		{0xD7,1,0x00},      // Write DSI Port Enable (D7h) '0':Enable DSI Port
		{0xBD,2,0xA2,0x00}, // Write DSI Parameters (BDh) DSI HS Clock:162MHz
		{0x05,1,0x00},      // Write Input Source Select (05h) '0h': External Video Port
		{0x07,1,0x00}       // Write External Video Source Format Select (07h) '00h':DSI AutoDetect
	};

	mdelay(5);
	for (i = 0; i < sizeof(dlp_init_write_buf) / sizeof(dlp_init_write_buf[0]); i++) {
		memset(write_data_buf, 0x00, dlp_init_write_buf[i][1]);
		memcpy(write_data_buf, dlp_init_write_buf[i]+2, dlp_init_write_buf[i][1]);
		dlpc343x_i2c_write(g_client, dlp_init_write_buf[i][0], write_data_buf, dlp_init_write_buf[i][1]);
	}
	DLP_INFO("%s end\n", __func__);
}

static irqreturn_t dlpc343x_isr_top_half(int irq, void *dev_id)
{
	DLP_INFO("%s dlp_state=%d\n", __func__, atomic_read(&dlp_state));
	return IRQ_WAKE_THREAD;
}

static irqreturn_t dlpc343x_isr_bottom_half(int irq, void *dev_id)
{
	if (mutex_is_locked(&dlpc343x_mutex)) {
		DLP_INFO("dlpc343x_mutex is locked, ignore %s\n", __func__);
		return IRQ_HANDLED;
	}

	mutex_lock(&dlpc343x_mutex);
	if (atomic_read(&dlp_state)) {
		dlpc343x_reg_init();
	}
	mutex_unlock(&dlpc343x_mutex);

	return IRQ_HANDLED;
}

static int dlpc343x_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0, irq, irq_num;
	struct device_node *np;
	u32 flag;

	DLP_INFO("%s begin, i2c device name:%s, i2c client addr:0x%x\n", __func__, client->name, client->addr);
	g_client = client;

	ret = dlpc343x_gpio_register(g_client);
	if (ret < 0) {
		DLP_ERR("dlpc343x_gpio_register fail\n");
		return -1;
	} else {
		gpio_set_value(ls_in_en_gpio, 0);
		gpio_set_value(gpio_ext5_gpio, 0);
		gpio_set_value(proj_on_gpio, 0);
		atomic_set(&dlp_state, 0);
	}

	np = g_client->dev.of_node;
	if (!np) {
		return -ENODEV;
	}

	mutex_init(&dlpc343x_mutex);

	irq = of_get_named_gpio_flags(np, "dlpc343x,irq-gpio", 0, &flag);
	if (!gpio_is_valid(irq)) {
		DLP_ERR("Invalid int gpio: %d\n", irq);
		return -EBADR;
	}

	irq_num  = gpio_to_irq(irq);
	DLP_INFO("irq_num = %d\n", irq_num);

	ret = devm_request_threaded_irq(&g_client->dev, irq_num,
		dlpc343x_isr_top_half,
		dlpc343x_isr_bottom_half,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		"dlpc343x",
		NULL);
	if (ret < 0) {
		DLP_ERR("Failed to request IRQ %d: error %d\n", irq, ret);
		return -1;
	}
	DLP_INFO("%s end\n", __func__);
	return 0;
}

static void dlpc343x_i2c_remove(struct i2c_client *client)
{
	DLP_INFO("removing i2c device: %s\n", client->name);
	// Add your device cleanup code here
}

static void dlpc343x_i2c_shutdown(struct i2c_client *client)
{
	// Add your device initialization code here
	;
}

static struct i2c_device_id dlpc343x_i2c_id_table[] = {
	{ "dlpc343x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dlpc343x_i2c_id_table);

static struct of_device_id dlpc343x_i2c_match_table[] = {
	{ .compatible = "drv,dlpc343x-i2c", },
	{ },
};

static struct i2c_driver dlpc343x_i2c_driver = {
	.probe = dlpc343x_i2c_probe,
	.remove = dlpc343x_i2c_remove,
	.shutdown = dlpc343x_i2c_shutdown,
	.id_table = dlpc343x_i2c_id_table,
	.driver = {
		.name = "dlpc343x",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dlpc343x_i2c_match_table),
	},
};

/******************************************************************************/
/************************** dlpc343x platform driver **************************/
/******************************************************************************/
// -- /sys/devices/platform/dlpc343x/dlpc343x_reg
static ssize_t dlpc343x_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	uint8_t read_buf[256] = {0};
	char *p_buf = buf;
	int len = 0, i, j;

	if (!atomic_read(&dlp_state)) {
		len = snprintf(buf, PAGE_SIZE, "DLP is disabled. You need to enable DLP before performing this operation.\n");
		return len;
	}

	for (i = 0; i < sizeof(dlpc3430_all_reg) / sizeof(dlpc3430_all_reg[0]); i++) {
		memset(read_buf, 0x00, dlpc3430_all_reg[i][1]);
		dlpc343x_i2c_read_reg(g_client, dlpc3430_all_reg[i][0], read_buf, dlpc3430_all_reg[i][1]);
		p_buf += sprintf(p_buf, "[%02xh]", dlpc3430_all_reg[i][0]);
		for (j = 0; j < dlpc3430_all_reg[i][1]; j++) {
			p_buf += sprintf(p_buf, " 0x%02x", read_buf[j]);
		}
		p_buf += sprintf(p_buf, "\n");
	}
	len = p_buf - buf;

	return len;
}

static ssize_t dlpc343x_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	char *p_buf[128]; //256 exceeds limit
	uint8_t write_reg_addr, write_len, write_data_buf[128];
	int count = 0, i;

	count = split_string((char *)buf, " ", p_buf, sizeof(p_buf)/sizeof((p_buf)[0]));

	sscanf(p_buf[0], "%hhx", &write_reg_addr);
	sscanf(p_buf[1], "%hhu", &write_len);
	memset(write_data_buf, 0, sizeof(write_data_buf));
	for (i = 0; i < count - 2; i++) {
		sscanf(p_buf[i+2], "%hhx", &write_data_buf[i]);
	}

	dlpc343x_i2c_write(g_client, write_reg_addr, write_data_buf, write_len);

	return len;
}
static DEVICE_ATTR(dlpc343x_reg, 0664, dlpc343x_reg_show, dlpc343x_reg_store);

// -- /sys/devices/platform/dlpc343x/dlpc343x_state
static ssize_t dlpc343x_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	count = snprintf(buf, PAGE_SIZE, "DLP state : %s\n", atomic_read(&dlp_state) ? "Open" : "Close");
	return count;
}

static ssize_t dlpc343x_state_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0, buf_data = 0;

	ret = kstrtoint(buf, 10, &buf_data);
	if (ret < 0)
		return ret;

	if (buf_data == 1) {
		atomic_set(&dlp_state, 1);
		mdelay(5);
		gpio_set_value(ls_in_en_gpio, 1);
		gpio_set_value(gpio_ext5_gpio, 1);
		gpio_set_value(proj_on_gpio, 1);
		DLP_INFO("%s open dlp\n", __func__);
	} else if (buf_data == 0) {
		atomic_set(&dlp_state, 0);
		mdelay(5);
		gpio_set_value(proj_on_gpio, 0);
		gpio_set_value(gpio_ext5_gpio, 0);
		gpio_set_value(ls_in_en_gpio, 0);
		DLP_INFO("%s close dlp\n", __func__);
	}

	return len;
}
static DEVICE_ATTR(dlpc343x_state, 0664, dlpc343x_state_show, dlpc343x_state_store);

// -- /sys/devices/platform/dlpc343x/dlpc343x_pitch_angle_en
static ssize_t dlpc343x_pitch_angle_en_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	uint8_t write_data_buf[5];

	memset(write_data_buf, 0, sizeof(write_data_buf));
	dlpc343x_i2c_read_reg(g_client, 0x89, write_data_buf, 5);
	count = snprintf(buf, PAGE_SIZE, "Pitch angle en : %s\n", write_data_buf[0] ? "Open" : "Close");

	return count;
}

static ssize_t dlpc343x_pitch_angle_en_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0, buf_data = 0;
	uint8_t write_data_buf[5];

	ret = kstrtoint(buf, 10, &buf_data);
	if (ret < 0)
		return ret;

	if (!atomic_read(&dlp_state)) {
		DLP_ERR("DLP is disabled. You need to enable DLP before performing this operation.\n");
		return -1;
	}

	memset(write_data_buf, 0, sizeof(write_data_buf));
	dlpc343x_i2c_read_reg(g_client, 0x89, write_data_buf, 5);

	if (buf_data == 1) {
		write_data_buf[0] = 0x01;
	} else if (buf_data == 0) {
		write_data_buf[0] = 0x00;
	}

	dlpc343x_i2c_write(g_client, 0x88, write_data_buf, 5);

	return len;
}
static DEVICE_ATTR(dlpc343x_pitch_angle_en, 0664, dlpc343x_pitch_angle_en_show, dlpc343x_pitch_angle_en_store);

// -- /sys/devices/platform/dlpc343x/dlpc343x_pitch_angle_val
static ssize_t dlpc343x_pitch_angle_val_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	count = snprintf(buf, PAGE_SIZE, "Pitch angle val : %d\n", g_pitch_angle_val);
	return count;
}

static ssize_t dlpc343x_pitch_angle_val_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	uint8_t write_data_buf[5];
	int16_t pitch_angle_val_integer = 0;

	ret = kstrtos16(buf, 10, &pitch_angle_val_integer);
	if (ret < 0)
		return ret;

	DLP_INFO("%s pitch_angle_val_integer=%d\n", __func__, pitch_angle_val_integer);

	if (pitch_angle_val_integer > 40 || pitch_angle_val_integer < -40) {
		DLP_ERR("%s: pitch angle out of range\n", __func__);
		return -1;
	}

	if (!atomic_read(&dlp_state)) {
		DLP_ERR("DLP is disabled. You need to enable DLP before performing this operation.\n");
		return -1;
	}

	memset(write_data_buf, 0, sizeof(write_data_buf));
	dlpc343x_i2c_read_reg(g_client, 0x89, write_data_buf, 5);
	if (write_data_buf[0] == 0x00) {
		DLP_ERR("Keystone correction is disabled, so pitch angle cannot be set. Please enable keystone correction first.\n");
		return -1;
	}

	g_pitch_angle_val = pitch_angle_val_integer << 8; //all decimal parts are 0

	memset(write_data_buf, 0, sizeof(write_data_buf));
	write_data_buf[1] = (uint8_t)((g_pitch_angle_val >> 8) & 0xFF); //msbyte
	write_data_buf[0] = (uint8_t)(g_pitch_angle_val & 0xFF); //lsbyte

	dlpc343x_i2c_write(g_client, 0xBB, write_data_buf, 2);
	DLP_INFO("%s write_data_buf[0]=0x%x, write_data_buf[1]=0x%x\n", __func__, write_data_buf[0], write_data_buf[1]);

	return len;
}
static DEVICE_ATTR(dlpc343x_pitch_angle_val, 0664, dlpc343x_pitch_angle_val_show, dlpc343x_pitch_angle_val_store);

// -- /sys/devices/platform/dlpc343x/dlpc343x_rgb_led_current_val
static ssize_t dlpc343x_rgb_led_current_val_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	uint8_t read_buf[6] = {0};

	dlpc343x_i2c_read_reg(g_client, 0x55, read_buf, 6);
	g_rgb_led_current_val = (read_buf[5] << 8) | read_buf[4];

	count = snprintf(buf, PAGE_SIZE, "RGB LED current val : %d\n", g_rgb_led_current_val);
	return count;
}

static ssize_t dlpc343x_rgb_led_current_val_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	uint8_t write_data_buf[6];

	ret = kstrtoint(buf, 10, &g_rgb_led_current_val);
	if (ret < 0)
		return ret;

	DLP_INFO("%s g_rgb_led_current_val=%d\n", __func__, g_rgb_led_current_val);

	if (g_rgb_led_current_val > 811 || g_rgb_led_current_val < 0) { //1023
		DLP_ERR("%s: RGB LED current value out of range\n", __func__);
		return -1;
	}

	if (!atomic_read(&dlp_state)) {
		DLP_ERR("DLP is disabled. You need to enable DLP before performing this operation.\n");
		return -1;
	}

	memset(write_data_buf, 0, sizeof(write_data_buf));
	// Blue LED current
	write_data_buf[5] = (uint8_t)((g_rgb_led_current_val >> 8) & 0xFF); //msbyte
	write_data_buf[4] = (uint8_t)(g_rgb_led_current_val & 0xFF); //lsbyte
	// Green LED current
	write_data_buf[3] = write_data_buf[5];
	write_data_buf[2] = write_data_buf[4];
	// Red LED current
	write_data_buf[1] = write_data_buf[5];
	write_data_buf[0] = write_data_buf[4];

	dlpc343x_i2c_write(g_client, 0x54, write_data_buf, 6);

	return len;
}
static DEVICE_ATTR(dlpc343x_rgb_led_current_val, 0664, dlpc343x_rgb_led_current_val_show, dlpc343x_rgb_led_current_val_store);

// -- /sys/devices/platform/dlpc343x/dlpc343x_temp
static ssize_t dlpc343x_temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	count = snprintf(buf, PAGE_SIZE, "temp val:ADC5(%d), ADC6(%d), ADC7(%d)", g_temperature5, g_temperature6, g_temperature7);
	return count;
}
static DEVICE_ATTR(dlpc343x_temp, 0664, dlpc343x_temp_show, NULL);

// -- /sys/devices/platform/dlpc343x/dlpc343x_image_orientation
static ssize_t dlpc343x_image_orientation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	count = snprintf(buf, PAGE_SIZE, "Image Orientation : %d\n", g_image_orientation);
	return count;
}

static ssize_t dlpc343x_image_orientation_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	uint8_t i, write_data_buf[10];
	uint8_t write_buf_horizontal[][10] = {
		{0x14,1,0x06}, // Write Display Image Orientation (14h)
		{0x12,8,0x00,0x00,0x00,0x00,0x56,0x03,0xe0,0x01}, // Write Display Size (12h) : 854x480
	};
	uint8_t write_buf_vertical[][10] = {
		{0x14,1,0x07}, // Write Display Image Orientation (14h)
		{0x12,8,0x00,0x00,0x00,0x00,0xE0,0x01,0x0E,0x01}, // Write Display Size (12h) : 480x270
	};
	uint8_t write_buf_horizontal_0degrees[][10] = {
		{0x14,1,0x00}, // Write Display Image Orientation (14h)
		{0x12,8,0x00,0x00,0x00,0x00,0x56,0x03,0xe0,0x01}, // Write Display Size (12h) : 854x480
	};
	uint8_t write_buf_horizontal_180degrees[][10] = {
		{0x14,1,0x01}, // Write Display Image Orientation (14h)
		{0x12,8,0x00,0x00,0x00,0x00,0xE0,0x01,0x0E,0x01}, // Write Display Size (12h) : 480x270
	};

	ret = kstrtoint(buf, 10, &g_image_orientation);
	if (ret < 0)
		return ret;

	DLP_INFO("%s g_image_orientation=%d\n", __func__, g_image_orientation);

	if (!atomic_read(&dlp_state)) {
		DLP_ERR("DLP is disabled. You need to enable DLP before performing this operation.\n");
		return -1;
	}

	memset(write_data_buf, 0, sizeof(write_data_buf));
	if (g_image_orientation == horizontal) {
		for (i = 0; i < sizeof(write_buf_horizontal) / sizeof(write_buf_horizontal[0]); i++) {
			memset(write_data_buf, 0x00, write_buf_horizontal[i][1]);
			memcpy(write_data_buf, write_buf_horizontal[i]+2, write_buf_horizontal[i][1]);
			dlpc343x_i2c_write(g_client, write_buf_horizontal[i][0], write_data_buf, write_buf_horizontal[i][1]);
		}
	} else if (g_image_orientation == vertical) {
		for (i = 0; i < sizeof(write_buf_vertical) / sizeof(write_buf_vertical[0]); i++) {
			memset(write_data_buf, 0x00, write_buf_vertical[i][1]);
			memcpy(write_data_buf, write_buf_vertical[i]+2, write_buf_vertical[i][1]);
			dlpc343x_i2c_write(g_client, write_buf_vertical[i][0], write_data_buf, write_buf_vertical[i][1]);
		}
	} else if (g_image_orientation == horizontal_0degrees) {
		for (i = 0; i < sizeof(write_buf_horizontal_0degrees) / sizeof(write_buf_horizontal_0degrees[0]); i++) {
			memset(write_data_buf, 0x00, write_buf_horizontal_0degrees[i][1]);
			memcpy(write_data_buf, write_buf_horizontal_0degrees[i]+2, write_buf_horizontal_0degrees[i][1]);
			dlpc343x_i2c_write(g_client, write_buf_horizontal_0degrees[i][0], write_data_buf, write_buf_horizontal_0degrees[i][1]);
		}
	} else if (g_image_orientation == horizontal_180degrees) {
		for (i = 0; i < sizeof(write_buf_horizontal_180degrees) / sizeof(write_buf_horizontal_180degrees[0]); i++) {
			memset(write_data_buf, 0x00, write_buf_horizontal_180degrees[i][1]);
			memcpy(write_data_buf, write_buf_horizontal_180degrees[i]+2, write_buf_horizontal_180degrees[i][1]);
			dlpc343x_i2c_write(g_client, write_buf_horizontal_180degrees[i][0], write_data_buf, write_buf_horizontal_180degrees[i][1]);
		}
	}

	return len;
}
static DEVICE_ATTR(dlpc343x_image_orientation, 0664, dlpc343x_image_orientation_show, dlpc343x_image_orientation_store);

// -- /sys/devices/platform/dlpc343x/dlpc343x_image_freeze
static ssize_t dlpc343x_image_freeze_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	uint8_t read_buf[1] = {0};

	dlpc343x_i2c_read_reg(g_client, 0x1b, read_buf, 1);

	count = snprintf(buf, PAGE_SIZE, "Image Freeze Val : %d\n", read_buf[0]);
	return count;
}

static ssize_t dlpc343x_image_freeze_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0, buf_data = 0;
	uint8_t write_data_buf[1];

	ret = kstrtoint(buf, 10, &buf_data);
	if (ret < 0)
		return ret;

	if (!atomic_read(&dlp_state)) {
		DLP_ERR("DLP is disabled. You need to enable DLP before performing this operation.\n");
		return -1;
	}

	memset(write_data_buf, 0, sizeof(write_data_buf));
	dlpc343x_i2c_read_reg(g_client, 0x89, write_data_buf, 1);

	if (buf_data == 1) {
		write_data_buf[0] = 0x01;
	} else if (buf_data == 0) {
		write_data_buf[0] = 0x00;
	}

	dlpc343x_i2c_write(g_client, 0x1A, write_data_buf, 1);

	return len;
}
static DEVICE_ATTR(dlpc343x_image_freeze, 0664, dlpc343x_image_freeze_show, dlpc343x_image_freeze_store);

#ifdef LIMIT_FEEDBACK
// -- /sys/devices/platform/dlpc343x/dlpc343x_limit_feedback
static ssize_t dlpc343x_limit_feedback_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	int limit_feedback_state = 0;
	gpio_direction_input(limit_feedback_gpio);
	limit_feedback_state = gpio_get_value(limit_feedback_gpio);
	count = snprintf(buf, PAGE_SIZE, "%d\n", limit_feedback_state);
	return count;
}
static DEVICE_ATTR(dlpc343x_limit_feedback, 0664, dlpc343x_limit_feedback_show, NULL);
#endif //#ifdef LIMIT_FEEDBACK

static int dlpc343x_get_temp_fn(void *data)
{
	int count, temperature5, temperature6, temperature7;
	while (!kthread_should_stop()) {
		// get adc channel voltage value
		iio_read_channel_processed(temperature_chan5, &temperature5);
		iio_read_channel_processed(temperature_chan6, &temperature6);
		iio_read_channel_processed(temperature_chan7, &temperature7);
		// DLP_INFO("%s adc voltage val : adc5 %d, adc6 %d, adc7 %d\n", __func__, temperature5, temperature6, temperature7);

		// voltage value -> resistance value
		temperature5 = (temperature5 * (100000)) / (1800 - temperature5);
		temperature6 = (temperature6 * (100000)) / (1800 - temperature6);
		temperature7 = (temperature7 * (100000)) / (1800 - temperature7);
		// DLP_INFO("%s resistance val : adc5 %d, adc6 %d, adc7 %d\n", __func__, temperature5, temperature6, temperature7);

		// resistance values -> temperature values
		for (count = 0; count < thermistor_table_num; count++) {
			if (temperature5 > thermistor_table[count].r_min && temperature5 < thermistor_table[count].r_max) {
				g_temperature5 = thermistor_table[count].temperature;
			}
			if (temperature6 > thermistor_table[count].r_min && temperature6 < thermistor_table[count].r_max) {
				g_temperature6 = thermistor_table[count].temperature;
			}
			if (temperature7 > thermistor_table[count].r_min && temperature7 < thermistor_table[count].r_max) {
				g_temperature7 = thermistor_table[count].temperature;
			}
		}
		// DLP_INFO("%s temperature val : adc5 %d, adc6 %d, adc7 %d\n", __func__, g_temperature5, g_temperature6, g_temperature7);
		msleep(500);
	}
	return 0;
}

static int dlpc343x_probe(struct platform_device *pdev)
{
	int err = 0;

	DLP_INFO("%s begin\n", __func__);
	err = device_create_file(&pdev->dev, &dev_attr_dlpc343x_reg);
	if (err) {
		pr_err("%s device_create_file dlpc343x_reg fail", __func__);
		return -ENODEV;
	}

	err = device_create_file(&pdev->dev, &dev_attr_dlpc343x_state);
	if (err) {
		pr_err("%s device_create_file dlpc343x_state fail", __func__);
		return -ENODEV;
	}

	err = device_create_file(&pdev->dev, &dev_attr_dlpc343x_pitch_angle_en);
	if (err) {
		pr_err("%s device_create_file dlpc343x_pitch_angle fail", __func__);
		return -ENODEV;
	}

	err = device_create_file(&pdev->dev, &dev_attr_dlpc343x_pitch_angle_val);
	if (err) {
		pr_err("%s device_create_file dlpc343x_pitch_angle fail", __func__);
		return -ENODEV;
	}

	err = device_create_file(&pdev->dev, &dev_attr_dlpc343x_rgb_led_current_val);
	if (err) {
		pr_err("%s device_create_file dlpc343x_rgb_led_current_val fail", __func__);
		return -ENODEV;
	}

	err = device_create_file(&pdev->dev, &dev_attr_dlpc343x_temp);
	if (err) {
		pr_err("%s device_create_file dlpc343x_temp fail", __func__);
		return -ENODEV;
	}

	err = device_create_file(&pdev->dev, &dev_attr_dlpc343x_image_orientation);
	if (err) {
		pr_err("%s device_create_file dlpc343x_image_orientation fail", __func__);
		return -ENODEV;
	}

	err = device_create_file(&pdev->dev, &dev_attr_dlpc343x_image_freeze);
	if (err) {
		pr_err("%s device_create_file dev_attr_dlpc343x_image_freeze fail", __func__);
		return -ENODEV;
	}

#ifdef LIMIT_FEEDBACK
	err = device_create_file(&pdev->dev, &dev_attr_dlpc343x_limit_feedback);
	if (err) {
		pr_err("%s device_create_file dlpc343x_limit_feedback fail", __func__);
		return -ENODEV;
	}
#endif

	// get adc channel
	temperature_chan5 = devm_iio_channel_get(&pdev->dev, "pmic6363_adc5");
	if (IS_ERR(temperature_chan5)) {
		pr_err("%s fail to get auxadc iio ch5: %p\n", __func__, temperature_chan5);
	}

	temperature_chan6 = devm_iio_channel_get(&pdev->dev, "pmic6363_adc6");
	if (IS_ERR(temperature_chan6)) {
		pr_err("%s fail to get auxadc iio ch5: %p\n", __func__, temperature_chan6);
	}

	temperature_chan7 = devm_iio_channel_get(&pdev->dev, "pmic6363_adc7");
	if (IS_ERR(temperature_chan7)) {
		pr_err("%s fail to get auxadc iio ch5: %p\n", __func__, temperature_chan7);
	}

	// get temperature thread
	dlpc343x_get_temp_thread = kthread_run(dlpc343x_get_temp_fn, NULL, "dlpc343x_get_temp_thread");
	if (IS_ERR(dlpc343x_get_temp_thread)) {
		pr_err(KERN_ERR "Failed to create dlpc343x_get_temp_thread\n");
		return PTR_ERR(dlpc343x_get_temp_thread);
	}

	DLP_INFO("%s end\n", __func__);
	return 0;
}

static int dlpc343x_remove(struct platform_device *pdev)
{
	if (dlpc343x_get_temp_thread) {
		kthread_stop(dlpc343x_get_temp_thread);
	}
	return 0;
}

static const struct of_device_id dlpc343x_of_match[] = {
	{ .compatible = "drv,dlpc343x", },
	{ },
};

static struct platform_driver dlpc343x_driver = {
	.probe = dlpc343x_probe,
	.remove = dlpc343x_remove,
	.driver = {
		.name = "dlpc343x_driver",
		.of_match_table = dlpc343x_of_match,
	},
};

static int __init dlpc343x_init(void) {
	i2c_add_driver(&dlpc343x_i2c_driver);
	return platform_driver_register(&dlpc343x_driver);
}

static void __exit dlpc343x_exit(void) {
	i2c_del_driver(&dlpc343x_i2c_driver);
	platform_driver_unregister(&dlpc343x_driver);
}

module_init(dlpc343x_init);
module_exit(dlpc343x_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chenjiaxi@cooseagroup.com");
MODULE_DESCRIPTION("dlpc343x driver");