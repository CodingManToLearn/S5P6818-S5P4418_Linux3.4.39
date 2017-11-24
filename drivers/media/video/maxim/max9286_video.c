/*
 * drivers/media/video/maxim/max9286.c --  Video Decoder driver for the max9286
 *
 * Copyright(C) 2017. Nexell Co.,
 *
 * See file CREDITS for list of people who contributed to this project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/switch.h>
#include <linux/gpio.h>
#include <linux/firmware.h>

#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <linux/videodev2_exynos_camera.h>

#include <mach/platform.h>
#include <mach/devices.h>
#include <mach/soc.h>

#include "max9286_video.h"
#include "max9286_preset.h"

#define MAX_SENSOR_NUM	4

#define ADDR_MAX9286		0x90
#define ADDR_MAX96705		0x80
#define ADDR_MAX96705_ALL	(ADDR_MAX96705 + 14)  //Broadcast address
#define ADDR_AP_SENSOR		0xBA

unsigned int g_sensor_num = 0;
unsigned char g_sensor_is_there = 0;  //Bit 0~3 for 4 cameras, 0b1= is there; 0b0 = is not there.

#define DEFAULT_SENSOR_WIDTH			1280*4//5120//3840//2560//1280
#define DEFAULT_SENSOR_HEIGHT			799//800 // 800*4
#define DEFAULT_SENSOR_CODE				(V4L2_MBUS_FMT_YUYV8_2X8)

#define FORMAT_FLAGS_COMPRESSED			0x3
#define SENSOR_JPEG_SNAPSHOT_MEMSIZE	0x410580

#define DEFAULT_PIX_FMT					V4L2_PIX_FMT_UYVY	/* YUV422 */
#define DEFAULT_MCLK					27000000	/* 24000000 */
#define POLL_TIME_MS					10
#define CAPTURE_POLL_TIME_MS    		1000

#define THINE_I2C_RETRY_CNT				1


/* maximum time for one frame at minimum fps (15fps) in normal mode */
#define NORMAL_MODE_MAX_ONE_FRAME_DELAY_MS     67

/* maximum time for one frame at minimum fps (4fps) in night mode */
#define NIGHT_MODE_MAX_ONE_FRAME_DELAY_MS     250

/* time to move lens to target position before last af mode register write */
#define LENS_MOVE_TIME_MS       100

/* level at or below which we need to use low light capture mode */
#define HIGH_LIGHT_LEVEL	0x80

#define FIRST_AF_SEARCH_COUNT   80
#define SECOND_AF_SEARCH_COUNT  80
#define AE_STABLE_SEARCH_COUNT  4

#define FIRST_SETTING_FOCUS_MODE_DELAY_MS	100
#define SECOND_SETTING_FOCUS_MODE_DELAY_MS	200

#define MAX9286_VERSION_1_1	0x11

//#define	CAMERA_POWER_CONTROL	CFG_CAMERA_POWER

enum max9286_hw_power {
	MAX9286_HW_POWER_OFF,
	MAX9286_HW_POWER_ON,
};

/* result values returned to HAL */
enum {
	AUTO_FOCUS_FAILED,
	AUTO_FOCUS_DONE,
	AUTO_FOCUS_CANCELLED,
};

enum af_operation_status {
	AF_NONE = 0,
	AF_START,
	AF_CANCEL,
};

enum max9286_oprmode {
	MAX9286_OPRMODE_VIDEO = 0,
	MAX9286_OPRMODE_IMAGE = 1,
	MAX9286_OPRMODE_MAX,
};

struct max9286_resolution {
	u8			value;
	enum max9286_oprmode	type;
	u16			width;
	u16			height;
};

/* M5MOLS default format (codes, sizes, preset values) */
static struct v4l2_mbus_framefmt default_fmt[MAX9286_OPRMODE_MAX] = {
	[MAX9286_OPRMODE_VIDEO] = {
		.width		= DEFAULT_SENSOR_WIDTH,
		.height		= DEFAULT_SENSOR_HEIGHT,
		.code		= DEFAULT_SENSOR_CODE,
		.field		= V4L2_FIELD_NONE,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
	[MAX9286_OPRMODE_IMAGE] = {
		.width		= DEFAULT_SENSOR_WIDTH,
		.height		= DEFAULT_SENSOR_HEIGHT,
		.code		= V4L2_MBUS_FMT_JPEG_1X8,
		.field		= V4L2_FIELD_NONE,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
};

#define SIZE_DEFAULT_FFMT	ARRAY_SIZE(default_fmt)
enum max9286_preview_frame_size {
	MAX9286_PREVIEW_1920,
	MAX9286_PREVIEW_MAX
};

enum max9286_capture_frame_size {
	MAX9286_CAPTURE_W2M,		//	0x19: 1920 x 1080 2M(16:9)
	MAX9286_CAPTURE_MAX
};

/* make look-up table */
static const struct max9286_resolution max9286_resolutions[] = {
	{MAX9286_PREVIEW_1920,	MAX9286_OPRMODE_VIDEO,	DEFAULT_SENSOR_WIDTH,	DEFAULT_SENSOR_HEIGHT},
	{MAX9286_CAPTURE_W2M,	MAX9286_OPRMODE_IMAGE,	DEFAULT_SENSOR_WIDTH,	DEFAULT_SENSOR_HEIGHT},
};

struct max9286_framesize {
	u32 index;
	u32 width;
	u32 height;
};

static const struct max9286_framesize max9286_preview_framesize_list[] = {
	{MAX9286_PREVIEW_1920,	DEFAULT_SENSOR_WIDTH,	DEFAULT_SENSOR_HEIGHT},
};

static const struct max9286_framesize max9286_capture_framesize_list[] = {
	{MAX9286_CAPTURE_W2M,	DEFAULT_SENSOR_WIDTH,	DEFAULT_SENSOR_HEIGHT},
};

struct max9286_version {
	u32 major;
	u32 minor;
};

struct max9286_date_info {
	u32 year;
	u32 month;
	u32 date;
};

enum max9286_runmode {
	MAX9286_RUNMODE_NOTREADY,
	MAX9286_RUNMODE_IDLE,
	MAX9286_RUNMODE_RUNNING,
	MAX9286_RUNMODE_CAPTURE,
};

struct max9286_firmware {
	u32 addr;
	u32 size;
};

struct max9286_jpeg_param {
	u32 enable;
	u32 quality;
	u32 main_size;		/* Main JPEG file size */
	u32 thumb_size;		/* Thumbnail file size */
	u32 main_offset;
	u32 thumb_offset;
	u32 postview_offset;
};

struct max9286_position {
	int x;
	int y;
};

struct gps_info_common {
	u32 direction;
	u32 dgree;
	u32 minute;
	u32 second;
};

struct max9286_gps_info {
	unsigned char gps_buf[8];
	unsigned char altitude_buf[4];
	int gps_timeStamp;
};

struct max9286_regset {
	u32 size;
	u8 *data;
};

struct max9286_regset_table {
	const u32	*reg;
	int		array_size;
};

#define MAX9286_REGSET(x, y)		\
	[(x)] = {					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}

#define MAX9286_REGSET_TABLE(y)		\
	{					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}

struct max9286_regs {
	struct max9286_regset_table ev[EV_MAX];
	struct max9286_regset_table metering[METERING_MAX];
	struct max9286_regset_table iso[ISO_MAX];
	struct max9286_regset_table effect[V4L2_IMAGE_EFFECT_MAX];
	struct max9286_regset_table white_balance[V4L2_WHITE_BALANCE_MAX];
	struct max9286_regset_table preview_size[MAX9286_PREVIEW_MAX];
	struct max9286_regset_table capture_size[MAX9286_CAPTURE_MAX];
	struct max9286_regset_table scene_mode[V4L2_SCENE_MODE_MAX];
	struct max9286_regset_table saturation[V4L2_SATURATION_MAX];
	struct max9286_regset_table contrast[V4L2_CONTRAST_MAX];
	struct max9286_regset_table sharpness[V4L2_SHARPNESS_MAX];
	struct max9286_regset_table fps[FRAME_RATE_MAX];
	struct max9286_regset_table preview_return;
	struct max9286_regset_table jpeg_quality_high;
	struct max9286_regset_table jpeg_quality_normal;
	struct max9286_regset_table jpeg_quality_low;
	struct max9286_regset_table flash_start;
	struct max9286_regset_table flash_end;
	struct max9286_regset_table af_assist_flash_start;
	struct max9286_regset_table af_assist_flash_end;
	struct max9286_regset_table af_low_light_mode_on;
	struct max9286_regset_table af_low_light_mode_off;
	struct max9286_regset_table aeawb_lockunlock[V4L2_AE_AWB_MAX];
	//struct max9286_regset_table ae_awb_lock_on;
	//struct max9286_regset_table ae_awb_lock_off;
	struct max9286_regset_table low_cap_on;
	struct max9286_regset_table low_cap_off;
	struct max9286_regset_table wdr_on;
	struct max9286_regset_table wdr_off;
	struct max9286_regset_table face_detection_on;
	struct max9286_regset_table face_detection_off;
	struct max9286_regset_table capture_start;
	struct max9286_regset_table af_macro_mode_1;
	struct max9286_regset_table af_macro_mode_2;
	struct max9286_regset_table af_macro_mode_3;
	struct max9286_regset_table af_normal_mode_1;
	struct max9286_regset_table af_normal_mode_2;
	struct max9286_regset_table af_normal_mode_3;
	struct max9286_regset_table af_return_macro_position;
	struct max9286_regset_table single_af_start;
	struct max9286_regset_table single_af_off_1;
	struct max9286_regset_table single_af_off_2;
	struct max9286_regset_table continuous_af_on;
	struct max9286_regset_table continuous_af_off;
	struct max9286_regset_table dtp_start;
	struct max9286_regset_table dtp_stop;
	struct max9286_regset_table init_reg_1;
	struct max9286_regset_table init_reg_2;
	struct max9286_regset_table init_reg_3;
	struct max9286_regset_table init_reg_4;
	struct max9286_regset_table flash_init;
	struct max9286_regset_table reset_crop;
	struct max9286_regset_table get_ae_stable_status;
	struct max9286_regset_table get_light_level;
	struct max9286_regset_table get_1st_af_search_status;
	struct max9286_regset_table get_2nd_af_search_status;
	struct max9286_regset_table get_capture_status;
	struct max9286_regset_table get_esd_status;
	struct max9286_regset_table get_iso;
	struct max9286_regset_table get_shutterspeed;
	struct max9286_regset_table get_frame_count;
};


struct max9286_state {
	struct max9286_platform_data 	*pdata;
	struct media_pad	 	pad; /* for media deivce pad */
	struct v4l2_subdev 		sd;
    struct switch_dev       switch_dev;

	struct exynos_md		*mdev; /* for media deivce entity */
	struct v4l2_pix_format		pix;
	struct v4l2_mbus_framefmt	ffmt[2]; /* for media deivce fmt */
	struct v4l2_fract		timeperframe;
	struct max9286_jpeg_param	jpeg;
	struct max9286_version		fw;
	struct max9286_version		prm;
	struct max9286_date_info	dateinfo;
	struct max9286_position	position;
	struct v4l2_streamparm		strm;
	struct v4l2_streamparm		stored_parm;
	struct max9286_gps_info	gps_info;
	struct mutex			ctrl_lock;
	struct completion		af_complete;
	enum max9286_runmode		runmode;
	enum max9286_oprmode		oprmode;
	enum af_operation_status	af_status;
	enum v4l2_mbus_pixelcode	code; /* for media deivce code */
    int					irq;
	int 				res_type;
	u8 				resolution;
	int				preview_framesize_index;
	int				capture_framesize_index;
	int				sensor_version;
	int				freq;		/* MCLK in Hz */
	int				check_dataline;
	int				check_previewdata;
	bool 				flash_on;
	bool 				torch_on;
	bool 				power_on;
	bool 				sensor_af_in_low_light_mode;
	bool 				flash_state_on_previous_capture;
	bool 				initialized;
	bool 				restore_preview_size_needed;
	int 				one_frame_delay_ms;
	const struct 			max9286_regs *regs;

    struct i2c_client *i2c_client;
    struct v4l2_ctrl_handler handler;
    struct v4l2_ctrl *ctrl_mux;
    struct v4l2_ctrl *ctrl_status;

    /* standard control */
    struct v4l2_ctrl *ctrl_brightness;
    char brightness;

    /* nexell: detect worker */
    struct delayed_work work;
};

static const struct v4l2_fmtdesc capture_fmts[] = {
	{
		.index		= 0,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_COMPRESSED,
		.description	= "JPEG + Postview",
		.pixelformat	= V4L2_PIX_FMT_JPEG,
	},
};

static inline struct max9286_state *ctrl_to_me(struct v4l2_ctrl *ctrl)
{
    return container_of(ctrl->handler, struct max9286_state, handler);
}

#if 0
static int max9286_i2c_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s8 i = 0;
	s8 ret = 0;
	u8 buf = 0;
	struct i2c_msg msg[2];

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;// | I2C_M_NO_RD_ACK;
	msg[1].len = 1;
	msg[1].buf = &buf;

	for (i=0; i<THINE_I2C_RETRY_CNT; i++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if (likely(ret == 2))
			break;
		//mdelay(POLL_TIME_MS);
		//dev_err(&client->dev, "\e[31mmax9286_i2c_write_byte failed reg:0x%02x retry:%d\e[0m\n", addr, i);
	}

	if (unlikely(ret != 2)) {
		dev_err(&client->dev, "\e[31mmax9286_i2c_read_byte failed reg:0x%02x \e[0m\n", reg);
		return -EIO;
	}

	*data = buf;
	return 0;
}

static int max9286_i2c_write_byte(struct i2c_client *client, u8 addr, u8 val)
{
	s8 i = 0;
	s8 ret = 0;
	u8 buf[2];
	u8 read_val = 0;
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;

	buf[0] = addr;
	buf[1] = val ;

	for (i=0; i<THINE_I2C_RETRY_CNT; i++) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
			break;
		//mdelay(POLL_TIME_MS);
		//dev_err(&client->dev, "\e[31mmax9286_i2c_write_byte failed reg:0x%02x write:0x%04x, retry:%d\e[0m\n", addr, val, i);
	}

	if (ret != 1) {
		max9286_i2c_read_byte(client, addr, &read_val);
		dev_err(&client->dev, "\e[31mmax9286_i2c_write_byte failed reg:0x%02x write:0x%04x, read:0x%04x, retry:%d\e[0m\n", addr, val, read_val, i);
		return -EIO;
	}

	return 0;
}
#endif
#if 0
static int AR0140AT_i2c_read_byte(struct i2c_client *client, int index,
				  u16 subaddr, u8 *data)
{
	int err;
//	unsigned char buf[2];
	struct i2c_msg msg[2];

//	cpu_to_be16s(&subaddr);

	msg[0].addr = (ADDR_AP_SENSOR + index)>>1;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = (u8 *)&subaddr;

	msg[1].addr = (ADDR_AP_SENSOR + index)>>1;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = data;//buf;

	err = i2c_transfer(client->adapter, msg, 2);
	if (unlikely(err != 2)) {
		dev_err(&client->dev,
			"%s: register read fail\n", __func__);
		return -EIO;
	}
	printk(KERN_ERR "## [%s():%s:%d\t] index:0x%02x, subaddr:0x%04x, read_val:0x%04x \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__, index, subaddr, data);

//	*data = ((buf[0] << 8) | buf[1]);

	return 0;
}

static int AR0140AT_i2c_write_byte(struct i2c_client *client, int index,
					 u16 addr, u8 data)
{
	int retry_count = 5;
	unsigned char buf[3];
	struct i2c_msg msg = {0, 0, 3, buf};
	int ret = 0;

	msg.addr = (ADDR_AP_SENSOR + index)>>1;	

	buf[0] = addr >> 8;
	buf[1] = addr;
	buf[2] = data;


	printk(KERN_ERR, "%s : W(0x%02X%02X%02X)\n",
		__func__, buf[0], buf[1], buf[2]);
	
	do {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
			break;
		msleep(POLL_TIME_MS);
		dev_err(&client->dev, "%s: I2C err %d, retry %d.\n",
			__func__, ret, retry_count);
	} while (retry_count-- > 0);
	if (ret != 1) {
		dev_err(&client->dev, "%s: I2C is not working.\n", __func__);
		return -EIO;
	}

	return 0;
}

static int AR0140AT_i2c_read_twobyte(struct i2c_client *client, int index,
				  u16 subaddr, u16 *data)
{
	int err;
	unsigned char buf[2];
	struct i2c_msg msg[2];

//	cpu_to_be16s(&subaddr);

	msg[0].addr = (ADDR_AP_SENSOR + index)>>1; // pooyi need check
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = (u8 *)&subaddr;

	msg[1].addr = (ADDR_AP_SENSOR + index)>>1;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = buf;

	err = i2c_transfer(client->adapter, msg, 2);
	if (unlikely(err != 2)) {
		dev_err(&client->dev,
			"%s: register read fail\n", __func__);
		return -EIO;
	}

	*data = ((buf[0] << 8) | buf[1]);

	return 0;
}

static int AR0140AT_i2c_write_twobyte(struct i2c_client *client, int index,
					 u16 addr, u16 w_data)
{
	int retry_count = 5;
	unsigned char buf[4];
	struct i2c_msg msg = {0, 0, 4, buf};
	int ret = 0;

	msg.addr = (ADDR_AP_SENSOR + index)>>1;
	
	buf[0] = addr >> 8;
	buf[1] = addr;
	buf[2] = w_data >> 8;
	buf[3] = w_data & 0xff;

	printk(KERN_ERR, "%s : W(0x%02X%02X%02X%02X)\n",
		__func__, buf[0], buf[1], buf[2], buf[3]);
	
	do {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
			break;
		msleep(POLL_TIME_MS);
		dev_err(&client->dev, "%s: I2C err %d, retry %d.\n",
			__func__, ret, retry_count);
	} while (retry_count-- > 0);
	if (ret != 1) {
		dev_err(&client->dev, "%s: I2C is not working.\n", __func__);
		return -EIO;
	}

	return 0;
}
#endif
#if 1

static inline int AR0140AT_read_reg(struct i2c_client *client, int index, unsigned short reg, int len, void * pdata)
{
	unsigned char u8_buf[2] = {0};
	unsigned int value = 0;
	int retry, timeout = 20;

	if ((pdata == NULL) || (len > 4))
		return 255;

	u8_buf[0] = reg >> 8;
	u8_buf[1] = reg & 0xff;

	client->addr = (ADDR_AP_SENSOR + index)>>1;
	for (retry = 0; retry < timeout; retry ++) {
		if (2 != i2c_master_send(client, u8_buf, 2)) {
			dev_dbg(&client->dev,
				"%s:write reg error: reg=%x, retry = %d.\n", __func__, reg, retry);
			msleep(10);
			continue;
		}

		if (len != i2c_master_recv(client, (char *)(&value), len)) {
			dev_dbg(&client->dev,
				"%s:read reg error: reg=%x,len=%d, retry = %d.\n", __func__, reg, len, retry);
			msleep(10);
			continue;
		}
		break;
	}

	if (retry >= timeout) {
		dev_err(&client->dev,
			"%s:read reg error: reg=%x,len=%d.\n", __func__, reg, len);
		return 255;
	}

	if (len == 1) {
		*(unsigned int *)pdata = value & 0xff;
		printk(KERN_ERR "## [%s():%s:%d\t] index:0x%02x, subaddr:0x%04x, read_val:0x%x \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__, index, reg, *(unsigned int *)pdata);
		
	} else if (len == 2) {
		*(unsigned int *)pdata = ((value & 0xff) << 8) + ((value >> 8) & 0xff);
		printk(KERN_ERR "## [%s():%s:%d\t] index:0x%02x, subaddr:0x%04x, read_val:0x%04x \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__, index, reg, *(unsigned int *)pdata);
	} else if (len == 3) {
		*(unsigned int *)pdata = ((value & 0xff) << 16) + (value & 0xff00) + ((value >> 16) & 0xff);
	} else if (len == 4){
		*(unsigned int *)pdata = ((value & 0xff) << 24) + (((value >> 8) & 0xff) << 16) + (((value >> 16) & 0xff) << 8) + ((value >> 24) & 0xff);
	}

	return 0;
}


static inline int AR0140AT_write_reg(struct i2c_client *client, int index, unsigned short reg, int len, unsigned int val)
{
	unsigned char u8_buf[6] = {0};
	unsigned int buf_len = 2;
	int retry, timeout = 20;

	u8_buf[0] = reg >> 8;
	u8_buf[1] = reg & 0xff;

	switch (len) {
		case 1:
			u8_buf[2] = val & 0xff;
			break;
		case 2:
			u8_buf[2] = (val >> 8) & 0xff;
			u8_buf[3] = val & 0xff;
			break;
		default:
			u8_buf[2] = val >> 24;
			u8_buf[3] = (val >> 16) & 0xff;
			u8_buf[4] = (val >> 8) & 0xff;
			u8_buf[5] = val & 0xff;
			break;
	}
	buf_len += len;

	client->addr = (ADDR_AP_SENSOR + index)>>1;
	for (retry = 0; retry < timeout; retry ++) {
		if (i2c_master_send(client, u8_buf, buf_len) < 0) {
			dev_dbg(&client->dev,
				"%s:write reg error: reg=0x%x, val=0x%x, retry = %d.\n", __func__, reg, val, retry);
			msleep(10);
			continue;
		}
		break;
	}

	if (retry >= timeout) {
		dev_info(&client->dev,
			"%s:write reg error: reg=0x%x, val=0x%x.\n", __func__, reg, val);
		return 255;
	}

	return 0;
}
#endif


#if 0
static void max96705_dump_registers(struct i2c_client *client, int index)
{
	unsigned char i;
	u8 read_val = 0;
	
	printk("max96705_dump_registers: I2C ADDR = 0x%x.\r\n", ADDR_MAX96705+index);
	for (i=0; i<0x20; i++){
		max9286_i2c_read_addr_byte(client, ADDR_MAX96705+index, i, &read_val);
		printk("MAX96705 Reg 0x%02x = 0x%x.\r\n", i, read_val);
	}
}

static void max9286_dump_registers(struct i2c_client *client)
{
	unsigned char i;
	u8 read_val = 0;

	for (i=0; i<0x72; i++){
		max9286_i2c_read_addr_byte(client, ADDR_MAX9286, i, &read_val);
		printk("MAX9286 Reg 0x%02x = 0x%x.\r\n", i, read_val);
	}
}
#endif
/* AR0140AT_farmeSyncCmd
	{ADDR_AP_SENSOR, 0xc88c, 0x03},
	{ADDR_AP_SENSOR, 0xc88d, 0x03},
	{ADDR_AP_SENSOR, 0xc88e, 0x01},
	{ADDR_AP_SENSOR, 0xfc00, 0x2800},
	{ADDR_AP_SENSOR, 0x0040, 0x8100},
AR0140AT_i2c_write_byte(client, index, 0xc88c, 0x03);		// CAM_MODE_SELECT
AR0140AT_i2c_write_byte(client, index, 0xc88d, 0x03);		// CAM_MODE_SYNC_TYPE
AR0140AT_i2c_write_byte(client, index, 0xc88e, 0x01);		// CAM_MODE_SYNC_TRIGGER_MODE
AR0140AT_i2c_write_twobyte(client, index, 0xfc00, 0x2800);		// REG_COMMAND_REGISTER
AR0140AT_i2c_write_twobyte(client, index, 0x0040, 0x8100);		// REG_CMD_HANDLER_PARAMS_POOL_0
*/
static int AR0140AT_farmeSyncCmd(struct i2c_client *client, int index)
{
	unsigned short reg_data = 0;



	AR0140AT_write_reg(client, index, 0xc88c, 1, 0x03);		// CAM_MODE_SELECT
	msleep(10);
//	AR0140AT_read_reg(client, index, 0xc88c, 1, &reg_data);

	AR0140AT_write_reg(client, index, 0xc88d, 1, 0x03);		// CAM_MODE_SYNC_TYPE
	msleep(10);	
//	AR0140AT_read_reg(client, index, 0xc88d, 1, &reg_data);

	AR0140AT_write_reg(client, index, 0xc88e, 1, 0x01); 	// CAM_MODE_SYNC_TRIGGER_MODE
	msleep(10);
//	AR0140AT_read_reg(client, index, 0xc88e, 1, &reg_data);

	AR0140AT_write_reg(client, index, 0xfc00, 2, 0x2800); 	// REG_COMMAND_REGISTER
	msleep(10);
//	AR0140AT_read_reg(client, index, 0xfc00, 2, &reg_data);

	AR0140AT_write_reg(client, index, 0x0040, 2, 0x8100); 	// REG_CMD_HANDLER_PARAMS_POOL_0
	msleep(30);
//	AR0140AT_read_reg(client, index, 0x0040, 2, &reg_data);
}

static int max9286_i2c_read_addr_byte(struct i2c_client *client, u8 addr, u8 reg, u8 *data)
{
	s8 i = 0;
	s8 ret = 0;
	u8 buf = 0;
	struct i2c_msg msg[2];

	msg[0].addr = addr>>1;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg;

	msg[1].addr = addr>>1;
	msg[1].flags = I2C_M_RD;// | I2C_M_NO_RD_ACK;
	msg[1].len = 1;
	msg[1].buf = &buf;

	for (i=0; i<THINE_I2C_RETRY_CNT; i++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if (likely(ret == 2))
			break;
		//mdelay(POLL_TIME_MS);
		//dev_err(&client->dev, "\e[31mmax9286_i2c_write_byte failed reg:0x%02x retry:%d\e[0m\n", addr, i);
	}

	if (unlikely(ret != 2)) {
		dev_err(&client->dev, "\e[31mmax9286_i2c_read_byte failed, addr:0x%02x, reg:0x%02x \e[0m\n", addr, reg);
		return -EIO;
	}

	printk(KERN_ERR "## [%s():%s:%d\t] addr:0x%02x, reg:0x%02x, read_val:0x%02x \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__, addr, reg, buf);

	*data = buf;
	return 0;
}

static int max9286_i2c_write_addr_block(struct i2c_client *client, u8 addr, u8 *buf, int size)
{
	s8 i = 0;
	s8 ret = 0;
	struct i2c_msg msg;

	msg.addr = addr>>1;
	msg.flags = 0;
	msg.len = size;
	msg.buf = buf;

	for (i=0; i<THINE_I2C_RETRY_CNT; i++) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
			break;
		msleep(POLL_TIME_MS);
	}

	if (ret != 1) {
		for (i=0; i<size; i++)
			dev_err(&client->dev, "\e[31mmax9286_i2c_write_block failed\e[0m buff[%d]:0x%02x, ret:%d \n", i, buf[i], ret);

		return -EIO;
	}

	return 0;
}

static int max9286_i2c_write_array(struct i2c_client *client, struct reg_val *array_data, u8 size)
{
	s8 ret = 0;
	u8 i = 0, j = 0;
	u8 buf[2] = {0};
	u8 read_val = 0;

	struct i2c_msg msg;


	for(j=0; j<size; j++) {

		if(array_data->addr == DELAY) {
			printk(KERN_ERR "## [%s():%s:%d\t] delay : %d ms \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__, array_data->reg);
			mdelay(array_data->reg);
			array_data++;
			continue;
		}
		else if(array_data->addr == READ) {
			ret = max9286_i2c_read_addr_byte(client, array_data->reg, array_data->val, &read_val);
			array_data++;
			continue;
		}
		else if(array_data->addr == END_VAL) {
			break;
		}

		msg.addr = array_data->addr>>1;
		msg.flags = 0;
		msg.len = 2;
		msg.buf = buf;

		buf[0] = array_data->reg;
		buf[1] = array_data->val;

		printk(KERN_ERR "## [%s():%s:%d\t] ADDR:0x%02x, REG:0x%02x, VAL: 0x%02x, %d \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__, array_data->addr, buf[0], buf[1], j);

		for (i=0; i<THINE_I2C_RETRY_CNT; i++) {
			ret = i2c_transfer(client->adapter, &msg, 1);
			if (likely(ret == 1))
				break;
			msleep(POLL_TIME_MS);
		}

		if (ret != 1) {
			for (i=0; i<size; i++)
				dev_err(&client->dev, "\e[31m max9286_i2c_write_array failed\e[0m buff[%d]:0x%02x, ret:%d \n", i, buf[0], buf[1]);

			return -EIO;
		}

		array_data++;

	}
	return 0;
}

static int max9286_i2c_write(struct i2c_client *client, u8 addr, u8 reg, u8 val)
{
	s8 i = 0;
	s8 ret = 0;
	struct i2c_msg msg;
	u8 buf[2] = {0};
	
	msg.addr = addr>>1;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;

	buf[0] = reg;
	buf[1] = val;

	for (i=0; i<THINE_I2C_RETRY_CNT; i++) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
			break;
		msleep(POLL_TIME_MS);
	}

	if (i >= THINE_I2C_RETRY_CNT) {
		dev_err(&client->dev, "\e[31mmax9286_i2c_write failed ret = %d\e[0m addr[0x%x], reg[0x%x] val[0x%x] \n",ret, addr, reg, val);
		return -EIO;
	}
#if 0	//test	
	else {
		printk(KERN_ERR "## [%s():%s:%d\t] addr:0x%02x, reg:0x%02x, write_val:0x%02x \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__, addr, reg, val);
	}
#endif
	return 0;
}

#define V4L2_CID_MUX        (V4L2_CTRL_CLASS_USER | 0x1001)
#define V4L2_CID_STATUS     (V4L2_CTRL_CLASS_USER | 0x1002)

static int max9286_set_mux(struct v4l2_ctrl *ctrl)
{
    return 0;
}

static int max9286_get_status(struct v4l2_ctrl *ctrl)
{
    return 0;
}

static int max9286_set_brightness(struct v4l2_ctrl *ctrl)
{
    return 0;
}

static int max9286_s_ctrl(struct v4l2_ctrl *ctrl)
{
    switch (ctrl->id) {
    case V4L2_CID_MUX:
        return max9286_set_mux(ctrl);
    case V4L2_CID_BRIGHTNESS:
        printk("%s: brightness\n", __func__);
        return max9286_set_brightness(ctrl);
    default:
        printk(KERN_ERR "%s: invalid control id 0x%x\n", __func__, ctrl->id);
        return -EINVAL;
    }
}

static int max9286_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
    switch (ctrl->id) {
    case V4L2_CID_STATUS:
        return max9286_get_status(ctrl);
    default:
        printk(KERN_ERR "%s: invalid control id 0x%x\n", __func__, ctrl->id);
        return -EINVAL;
    }
}

static const struct v4l2_ctrl_ops max9286_ctrl_ops = {
     .s_ctrl = max9286_s_ctrl,
     .g_volatile_ctrl = max9286_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config max9286_custom_ctrls[] = {
    {
        .ops  = &max9286_ctrl_ops,
        .id   = V4L2_CID_MUX,
        .type = V4L2_CTRL_TYPE_INTEGER,
        .name = "MuxControl",
        .min  = 0,
        .max  = 1,
        .def  = 1,
        .step = 1,
    },
    {
        .ops  = &max9286_ctrl_ops,
        .id   = V4L2_CID_STATUS,
        .type = V4L2_CTRL_TYPE_INTEGER,
        .name = "Status",
        .min  = 0,
        .max  = 1,
        .def  = 1,
        .step = 1,
        .flags = V4L2_CTRL_FLAG_VOLATILE,
    }
};

#define NUM_CTRLS 3
static int max9286_initialize_ctrls(struct max9286_state *me)
{
    v4l2_ctrl_handler_init(&me->handler, NUM_CTRLS);

    me->ctrl_mux = v4l2_ctrl_new_custom(&me->handler, &max9286_custom_ctrls[0], NULL);
    if (!me->ctrl_mux) {
         printk(KERN_ERR "%s: failed to v4l2_ctrl_new_custom for mux\n", __func__);
         return -ENOENT;
    }

    me->ctrl_status = v4l2_ctrl_new_custom(&me->handler, &max9286_custom_ctrls[1], NULL);
    if (!me->ctrl_status) {
         printk(KERN_ERR "%s: failed to v4l2_ctrl_new_custom for status\n", __func__);
         return -ENOENT;
    }

    me->ctrl_brightness = v4l2_ctrl_new_std(&me->handler, &max9286_ctrl_ops,
            V4L2_CID_BRIGHTNESS, -128, 127, 1, -112);
    if (!me->ctrl_brightness) {
        printk(KERN_ERR "%s: failed to v4l2_ctrl_new_std for brightness\n", __func__);
        return -ENOENT;
    }

    me->sd.ctrl_handler = &me->handler;
    if (me->handler.error) {
        printk(KERN_ERR "%s: ctrl handler error(%d)\n", __func__, me->handler.error);
        v4l2_ctrl_handler_free(&me->handler);
        return -EINVAL;
    }

    return 0;
}

static int max9286_set_from_table(struct v4l2_subdev *sd,
				const char *setting_name,
				const struct max9286_regset_table *table,
				int table_size, int index)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/* return if table is not initilized */
	if ((unsigned int)table < (unsigned int)0xc0000000)
		return 0;

	//dev_err(&client->dev, "%s: set %s index %d\n",
	//	__func__, setting_name, index);
	if ((index < 0) || (index >= table_size)) {
		dev_err(&client->dev, "%s: index(%d) out of range[0:%d] for table for %s\n",
							__func__, index, table_size, setting_name);
		return -EINVAL;
	}
	table += index;
	if (table->reg == NULL)
		return -EINVAL;

	return 0;//max9286_write_regs(sd, table->reg, table->array_size);
}

static int max9286_set_parameter(struct v4l2_subdev *sd,
				int *current_value_ptr,
				int new_value,
				const char *setting_name,
				const struct max9286_regset_table *table,
				int table_size)
{
	int err;
/*
	if (*current_value_ptr == new_value)
		return 0;
		*/

	err = max9286_set_from_table(sd, setting_name, table,
				table_size, new_value);

	if (!err)
		*current_value_ptr = new_value;
	return err;
}

static void max9286_init_parameters(struct v4l2_subdev *sd)
{
	struct max9286_state *state =
		container_of(sd, struct max9286_state, sd);
	struct sec_cam_parm *parms =
		(struct sec_cam_parm *)&state->strm.parm.raw_data;
	struct sec_cam_parm *stored_parms =
		(struct sec_cam_parm *)&state->stored_parm.parm.raw_data;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_err(&client->dev, "%s: \n", __func__);
	state->strm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parms->capture.capturemode = 0;
	parms->capture.timeperframe.numerator = 1;
	parms->capture.timeperframe.denominator = 30;
	parms->contrast = V4L2_CONTRAST_DEFAULT;
	parms->effects = V4L2_IMAGE_EFFECT_NORMAL;
	parms->brightness = V4L2_BRIGHTNESS_DEFAULT;
	parms->flash_mode = FLASH_MODE_AUTO;
	parms->focus_mode = V4L2_FOCUS_MODE_AUTO;
	parms->iso = V4L2_ISO_AUTO;
	parms->metering = V4L2_METERING_CENTER;
	parms->saturation = V4L2_SATURATION_DEFAULT;
	parms->scene_mode = V4L2_SCENE_MODE_NONE;
	parms->sharpness = V4L2_SHARPNESS_DEFAULT;
	parms->white_balance = V4L2_WHITE_BALANCE_AUTO;
	parms->aeawb_lockunlock = V4L2_AE_UNLOCK_AWB_UNLOCK;

	stored_parms->effects = V4L2_IMAGE_EFFECT_NORMAL;
	stored_parms->brightness = V4L2_BRIGHTNESS_DEFAULT;
	stored_parms->iso = V4L2_ISO_AUTO;
	stored_parms->metering = V4L2_METERING_CENTER;
	stored_parms->scene_mode = V4L2_SCENE_MODE_NONE;
	stored_parms->white_balance = V4L2_WHITE_BALANCE_AUTO;

	state->jpeg.enable = 0;
	state->jpeg.quality = 100;
	state->jpeg.main_offset = 0;
	state->jpeg.main_size = 0;
	state->jpeg.thumb_offset = 0;
	state->jpeg.thumb_size = 0;
	state->jpeg.postview_offset = 0;

	state->fw.major = 1;

	state->one_frame_delay_ms = NORMAL_MODE_MAX_ONE_FRAME_DELAY_MS;

    /* psw0523 block this */
	/* max9286_stop_auto_focus(sd); */
}

/* This function is called from the g_ctrl api
 *
 * This function should be called only after the s_fmt call,
 * which sets the required width/height value.
 *
 * It checks a list of available frame sizes and sets the
 * most appropriate frame size.
 *
 * The list is stored in an increasing order (as far as possible).
 * Hence the first entry (searching from the beginning) where both the
 * width and height is more than the required value is returned.
 * In case of no perfect match, we set the last entry (which is supposed
 * to be the largest resolution supported.)
 */
static void max9286_set_framesize(struct v4l2_subdev *sd,
				const struct max9286_framesize *frmsize,
				int frmsize_count, bool preview)
{
	struct max9286_state *state =
		container_of(sd, struct max9286_state, sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct max9286_framesize *last_frmsize =
		&frmsize[frmsize_count - 1];
	int err;

	dev_err(&client->dev, "%s: Requested Res: %dx%d\n", __func__,
		state->pix.width, state->pix.height);

	do {
		/*
		 * In case of image capture mode,
		 * if the given image resolution is not supported,
		 * return the next higher image resolution. */
		if (preview) {
			if (frmsize->width == state->pix.width &&
				frmsize->height == state->pix.height) {
				break;
			}
		} else {
			dev_err(&client->dev,
				"%s: compare frmsize %dx%d to %dx%d\n",
				__func__,
				frmsize->width, frmsize->height,
				state->pix.width, state->pix.height);
			if (frmsize->width >= state->pix.width &&
				frmsize->height >= state->pix.height) {
				dev_err(&client->dev,
					"%s: select frmsize %dx%d, index=%d\n",
					__func__,
					frmsize->width, frmsize->height,
					frmsize->index);
				break;
			}
		}

		frmsize++;
	} while (frmsize <= last_frmsize);

	if (frmsize > last_frmsize)
		frmsize = last_frmsize;

	state->pix.width = frmsize->width;
	state->pix.height = frmsize->height;
	if (preview) {
		state->preview_framesize_index = frmsize->index;
		dev_err(&client->dev, "%s: Preview Res Set: %dx%d, index %d\n",
			__func__, state->pix.width, state->pix.height,
			state->preview_framesize_index);

		printk(KERN_ERR "## [%s():%s:%d\t] Preview Res Set: %dx%d, index %d \n",
				__FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__,
				state->pix.width, state->pix.height, state->capture_framesize_index);

		err = max9286_set_from_table(sd, "set preview size",
					state->regs->preview_size,
					ARRAY_SIZE(state->regs->preview_size),
					state->preview_framesize_index);
		if (err < 0) {
			v4l_info(client, "%s: register set failed\n", __func__);
		}

	} else {
		state->capture_framesize_index = frmsize->index;
		dev_err(&client->dev, "%s: Capture Res Set: %dx%d, index %d\n",
			__func__, state->pix.width, state->pix.height,
			state->capture_framesize_index);

		printk(KERN_ERR "## [%s():%s:%d\t] Capture Res Set: %dx%d, index %d \n",
				__FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__,
				state->pix.width, state->pix.height, state->capture_framesize_index);

		err = max9286_set_from_table(sd, "set capture size",
					state->regs->capture_size,
					ARRAY_SIZE(state->regs->capture_size),
					state->capture_framesize_index);

		if (err < 0) {
			v4l_info(client, "%s: register set failed\n", __func__);
		}

	}

}

/*
 * s_config subdev ops
 * With camera device, we need to re-initialize
 * every single opening time therefor,
 * it is not necessary to be initialized on probe time.
 * except for version checking
 * NOTE: version checking is optional
 */

static int max9286_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct max9286_state *state = container_of(sd, struct max9286_state, sd);
	int ret = 0;

	dev_err(&client->dev, "%s() on:%d \n", __func__, on);
	printk(KERN_ERR "## [%s():%s:%d\t]  \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__);

    if (on) {
		max9286_init_parameters(sd);
		if(state->power_on == MAX9286_HW_POWER_OFF)
			state->power_on = MAX9286_HW_POWER_ON;
//		gpio_direction_output(CAMERA_POWER_CONTROL, 1);

		
	} else {
		state->power_on = MAX9286_HW_POWER_OFF;
		state->initialized = false;
//		gpio_direction_output(CAMERA_POWER_CONTROL, 0);
	}

	return ret;
}



static struct reg_val test_val[] =
{
	{0x80, 0x07, 0x84},	// GMSL link 0 pixel count
	{0x90, 0x01, 0xA2},
	{0x90, 0x0C, 0x99},
	{0x90, 0x12, 0xF3},
	{0x90, 0x15, 0x0B},

	{READ, 0x80, 0x07},

	{END_VAL, 0x00, 0x00},
};

static struct reg_val test_val00[] =
{
	{0x80, 0x04, 0x47},
	{0x80, 0x07, 0x84},
	{0x80, 0x04, 0x87},

	{END_VAL, 0x00, 0x00},
};

static int max9286_hardware_init(struct i2c_client *client)
{
	int retval = 0;
	int i, ser_sid;
	u8 reg, sensor_addr = 0, regTest =0;

	//Disable CSI Output
	max9286_i2c_write(client, ADDR_MAX9286, 0x15, 0x03);

	//Enable PRBS test
 	max9286_i2c_write(client, ADDR_MAX9286, 0x0E, 0x5F);
	msleep(10);

	//Enable Custom Reverse Channel & First Pulse Length
//	max9286_i2c_write(client, ADDR_MAX9286, 0x3F, 0x4F);
//	msleep(2);

	//Reverse Channel Amplitude to mid level and transition time
//	max9286_i2c_write(client, ADDR_MAX9286, 0x3B, 0x1E);
//	msleep(2);

	//Enable ADDR_MAX96705 Configuration Link
	max9286_i2c_write(client, ADDR_MAX96705, 0x04, 0x47);
	msleep(2);

	//Increase serializer reverse channel input thresholds
	//Invert VSYNC
    max9286_i2c_write(client, ADDR_MAX96705, 0x40, 0x2F);
    msleep(2);
//	max9286_i2c_read_addr_byte(client, ADDR_MAX96705, 0x40, &regTest);	// test
//    max9286_i2c_write(client, ADDR_MAX96705, 0x97, 0x5F);
//	msleep(2);

	//Reverse Channel Amplitude level
//	max9286_i2c_write(client, ADDR_MAX9286, 0x3B, 0x19);
//	msleep(5);

	//Set YUV422 8 bits mode, Double Data Rate, 4 data lane
	max9286_i2c_write(client, ADDR_MAX9286, 0x12, 0xf3); // 0x3x lane D0 0xFx lane D0-D3
//	max9286_i2c_write(client, ADDR_MAX9286, 0x12, 0x33); // 0x3x lane D0 0xFx lane D0-D3
	msleep(5);

//	max9286_i2c_write(client, ADDR_MAX9286, 0x0C, 0x91);	// ??
//	max9286_i2c_read_addr_byte(client, ADDR_MAX96705, 0x0C, &regTest);	// test
	

	//Frame Sync
	// - Automatic Mode
	// - max9286_i2c_write(client, ADDR_MAX9286, 0x01, 0x02);
	// - Manual Mode
//	max9286_i2c_write(client, ADDR_MAX9286, 0x01, 0x00);	// int
	max9286_i2c_write(client, ADDR_MAX9286, 0x01, 0x00);	// int-manual
//	max9286_i2c_write(client, ADDR_MAX9286, 0x01, 0xc0);	// external
//	max9286_i2c_write(client, ADDR_MAX9286, 0x01, 0xe2);	// int

	// external Mode
	//max9286_i2c_write(client, ADDR_MAX9286, 0x01, 0xA2);
//	max9286_i2c_read_addr_byte(client, ADDR_MAX96705, 0x1e, &regTest);	// test

	max9286_i2c_write(client, ADDR_MAX9286, 0x63, 0x00);
	max9286_i2c_write(client, ADDR_MAX9286, 0x64, 0x00);

	max9286_i2c_write(client, ADDR_MAX9286, 0x06, 0x80);
	max9286_i2c_write(client, ADDR_MAX9286, 0x07, 0xb9);
	max9286_i2c_write(client, ADDR_MAX9286, 0x08, 0x2a);
	msleep(100);

	max9286_i2c_write(client, ADDR_MAX9286, 0x0F, 0x0b); // test
//	max9286_i2c_write(client, ADDR_MAX9286, 0x1c, 0x04);
	
	max9286_i2c_read_addr_byte(client, ADDR_MAX9286, 0x0c, &regTest);	// check VSYNC polarity check

	// Detect link
	g_sensor_num = 0;
	max9286_i2c_read_addr_byte(client, ADDR_MAX9286, 0x49, &reg);
	g_sensor_is_there = ((reg >> 4) & 0xF) | (reg & 0xF);
	if (g_sensor_is_there & (0x1 << 0))
		g_sensor_num += 1;
	if (g_sensor_is_there & (0x1 << 1))
		g_sensor_num += 1;
	if (g_sensor_is_there & (0x1 << 2))
		g_sensor_num += 1;
	if (g_sensor_is_there & (0x1 << 3))
		g_sensor_num += 1;
	pr_info("max9286_mipi: sensor number = %d. g_sensor_is_there = %d\n", g_sensor_num, g_sensor_is_there);

	if (g_sensor_num == 0) {
		pr_err("%s: no camera connected.\n", __func__);
		return -1;
	}
//	max9286_i2c_read_addr_byte(client, ADDR_MAX96705, 0x1e, &regTest);	// test

	//Disable PRBS test
	max9286_i2c_write(client, ADDR_MAX9286, 0x0E, 0x50);
//	max9286_i2c_read_addr_byte(client, ADDR_MAX96705, 0x1e, &regTest);	// test

	// Set link order in MIPI CSI-2 output
	reg = 0xE4;  //Default setting
	if (g_sensor_num == 1) {
		switch (g_sensor_is_there) {
			case 0x8:
				reg = 0x27;
				break;
			case 0x4:
				reg = 0xC6;
				break;
			case 0x2:
				reg = 0xE1;
				break;
			case 0x1:
			default:
				reg = 0xE4;
				break;
		}
	} else if (g_sensor_num == 2) {
		switch (g_sensor_is_there) {
			case 0xC:
				reg = 0x4E;
				break;
			case 0xA:
				reg = 0x72;
				break;
			case 0x9:
				reg = 0x78;
				break;
			case 0x6:
				reg = 0xD2;
				break;
			case 0x5:
				reg = 0xD8;
				break;
			case 0x3:
			default:
				reg = 0xE4;
				break;
		}
	} else if (g_sensor_num == 3) {
		switch (g_sensor_is_there) {
			case 0xE:
				reg = 0x93;
				break;
			case 0xD:
				reg = 0x9C;
				break;
			case 0xB:
				reg = 0xB4;
				break;
			case 0x7:
			default:
				reg = 0xE4;
				break;
		}
	}
	max9286_i2c_write(client, ADDR_MAX9286, 0x0B, reg);
//	max9286_i2c_read_addr_byte(client, ADDR_MAX9286, 0x0b, &regTest);	// test
//	max9286_i2c_read_addr_byte(client, ADDR_MAX96705, 0x1e, &regTest);	// test

	//Enable all links
	reg = 0xE0 | g_sensor_is_there;
	max9286_i2c_write(client, ADDR_MAX9286, 0x00, reg);
	msleep(5);

//	max9286_i2c_read_addr_byte(client, ADDR_MAX96705, 0x1e, &regTest);	// test

	//Set up links	
	reg = 0;
	for (i=1; i<=MAX_SENSOR_NUM; i++) {
		if (((0x1 << (i-1)) & g_sensor_is_there) == 0)
			continue;

		//Enable Link control channel
		reg |= (0x11 << (i-1));
		max9286_i2c_write(client, ADDR_MAX9286, 0x0A, reg);
//		max9286_i2c_read_addr_byte(client, ADDR_MAX96705, 0x1e, &regTest);	// test

		//Set ADDR_MAX96705 new address for link 0
		ser_sid = (2 * i);	
		sensor_addr = (2 * i);
		max9286_i2c_write(client, ADDR_MAX96705, 0x00, ADDR_MAX96705+ser_sid);
		msleep(2);
		
		//Set ADDR_MAX96705: Double Mode, PCLK latched on Rising Edge, HS/VS encoding
		max9286_i2c_write(client, ADDR_MAX96705+ser_sid, 0x07, 0x84);
		msleep(2);

		max9286_i2c_write(client, ADDR_MAX96705+ser_sid, 0x01, ADDR_MAX9286);
		max9286_i2c_write(client, ADDR_MAX96705+ser_sid, 0x09, ADDR_AP_SENSOR + sensor_addr);
		max9286_i2c_write(client, ADDR_MAX96705+ser_sid, 0x0A, ADDR_AP_SENSOR);
		max9286_i2c_write(client, ADDR_MAX96705+ser_sid, 0x0B, ADDR_MAX96705_ALL);
		max9286_i2c_write(client, ADDR_MAX96705+ser_sid, 0x0C, (ADDR_MAX96705 + ser_sid));

		msleep(1);
	}
//	max9286_i2c_read_addr_byte(client, ADDR_MAX96705+ser_sid, 0x1e, &regTest);	// test
	max9286_i2c_write(client, ADDR_MAX9286, 0x0A, reg);
	max9286_i2c_write(client, ADDR_MAX9286, 0x0A, reg);
	printk(KERN_ERR "## [ADDR_MAX9286] write addr:0x0A, val:0x%02x\n", reg);

	//Disable Local Auto I2C ACK
	max9286_i2c_write(client, ADDR_MAX9286, 0x34, 0x36);
#if 1
	//Initialize Camera Sensor	
	if (g_sensor_is_there & (0x1 << 0))
		AR0140AT_farmeSyncCmd(client, 2);
	if (g_sensor_is_there & (0x1 << 1))
		AR0140AT_farmeSyncCmd(client, 4);
	if (g_sensor_is_there & (0x1 << 2))
		AR0140AT_farmeSyncCmd(client, 6);
	if (g_sensor_is_there & (0x1 << 3))
		AR0140AT_farmeSyncCmd(client, 8);
#endif
	//Enable Local Auto I2C ACK
//	max9286_i2c_write(client, ADDR_MAX9286, 0x34, 0x76);
	max9286_i2c_write(client, ADDR_MAX9286, 0x34, 0xf6);


	//ADDR_MAX96705: Enable Serial Links and Disable Configuration Link
	max9286_i2c_write(client, ADDR_MAX96705_ALL, 0x04, 0x87);
	msleep(100);  //Wait for more than 2 frame time

	max9286_i2c_read_addr_byte(client, 0x90, 0x31, &regTest);
	if ((regTest>>6) & 0x1){
		dev_err(&client->dev,"## Frame synchronization is locked. read_val=0x%x\n", regTest);
	}
	else {
		dev_err(&client->dev,"##\e[31m Frame synchronization is not locked. read_val=0x%x\e[0m \n", regTest);
		msleep(100);
	}

	//Enable CSI output, set virtual channel according to the link number
	max9286_i2c_write(client, ADDR_MAX9286, 0x15, 0x0B);
	msleep(10);
/*
	printk("Dump MAX9286 registers:\r\n");
	max9286_dump_registers();
*/
	return retval;
}

static int max9286_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct max9286_state *state = container_of(sd, struct max9286_state, sd);

    int ret = 0;
	u8 data[2];
	u8 read_val = 0;

	printk(KERN_ERR "## [%s():%s:%d\t]  \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__);

	if (!state->initialized) {
		state->runmode = MAX9286_RUNMODE_RUNNING;
		state->initialized = true;
#if 0
		max9286_i2c_read_addr_byte(client, 0x90, 0x00, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x01, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x0c, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x12, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x15, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x31, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x27, &read_val);
#endif
	    dev_err(&client->dev, "%s: start\n", __func__);

#if 0
#if 0
	    dev_err(&client->dev, "%s: Reverse_Channel_Setup_1 \n", __func__);
	//	max9286_i2c_write_array(client, Reverse_Channel_Setup_1, ARRAY_SIZE(Reverse_Channel_Setup_1));
	    dev_err(&client->dev, "%s: MAX9286_Initial_Setup_2 \n", __func__);
	//	max9286_i2c_write_array(client, MAX9286_Initial_Setup_2, ARRAY_SIZE(MAX9286_Initial_Setup_2));
	    dev_err(&client->dev, "%s: GMSL_Link_Setup_3 \n", __func__);
//		max9286_i2c_write_array(client, GMSL_Link_Setup_3, ARRAY_SIZE(GMSL_Link_Setup_3));
	    dev_err(&client->dev, "%s: Image_Sensor_Initialization_4_1 \n", __func__);
//		max9286_i2c_write_array(client, Image_Sensor_Initialization_4_1, ARRAY_SIZE(Image_Sensor_Initialization_4_1));
	    dev_err(&client->dev, "%s: partron_init \n", __func__);
//		max9286_i2c_write_array(client, partron_init, ARRAY_SIZE(partron_init));
	    dev_err(&client->dev, "%s: Image_Sensor_Initialization_4_2 \n", __func__);
//		max9286_i2c_write_array(client, Image_Sensor_Initialization_4_2, ARRAY_SIZE(Image_Sensor_Initialization_4_2));
	    dev_err(&client->dev, "%s: Enable_GMSL_CSI2_5 \n", __func__);
//		max9286_i2c_write_array(client, Enable_GMSL_CSI2_5, ARRAY_SIZE(Enable_GMSL_CSI2_5));
	    dev_err(&client->dev, "%s: Verification_6 \n", __func__);
//		max9286_i2c_write_array(client, Verification_6, ARRAY_SIZE(Verification_6));
#endif 
		max9286_i2c_write_array(client, test_val00, ARRAY_SIZE(test_val00));
		mdelay(10);
		max9286_i2c_read_addr_byte(client, 0x90, 0x31, &read_val);
		printk(KERN_ERR "## [%s():%s:%d\t] read_val:0x%x \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__, read_val);

		//mdelay (5000);
#else
		max9286_hardware_init(client);
//		max9286_i2c_write_array(client, test_val, ARRAY_SIZE(test_val));
		

#endif
		mdelay(50);
#if 0
		max9286_i2c_read_addr_byte(client, 0x90, 0x00, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x01, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x0c, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x12, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x15, &read_val);
		max9286_i2c_read_addr_byte(client, 0x90, 0x31, &read_val);
#endif
		max9286_i2c_read_addr_byte(client, 0x90, 0x27, &read_val);
		if ((read_val & 0x80) == 0x80){
			dev_err(&client->dev,"## All enabled input links are locked. read_val=0x%x\n", read_val);
		}
		else {
			dev_err(&client->dev,"##\e[31m One or more enabled input links are not locked. read_val=0x%x \e[0m \n", read_val);
			return -1;
		}

#if 0
		dev_err(&client->dev,"## DESER_ERR   input pin : %d. \n", nxp_soc_gpio_get_in_value(CFG_IO_SOC_GPE11_DESER_ERR));//? "High":"Low");
		dev_err(&client->dev,"## DESER_LOCK  input pin : %d. \n", nxp_soc_gpio_get_in_value(CFG_IO_SOC_GPE12_DESER_LOCK));//? "High":"Low");
		dev_err(&client->dev,"## DESER_CCBSY input pin : %d. \n", nxp_soc_gpio_get_in_value(CFG_IO_SOC_GPE16_DESER_CCBSY));//? "High":"Low");
		dev_err(&client->dev,"## DESER_FAULT input pin : %d. \n", nxp_soc_gpio_get_in_value(CFG_IO_SOC_GPE17_DESER_FAULT));//? "High":"Low");
		dev_err(&client->dev,"## DESER_LMO   input pin : %d. \n", nxp_soc_gpio_get_in_value(CFG_IO_SOC_GPE20_DESER_LMO));//? "High":"Low");
#endif

		max9286_i2c_read_addr_byte(client, 0x90, 0x31, &read_val);
		if ((read_val>>6) & 0x1){
			dev_err(&client->dev,"## Frame synchronization is locked. read_val=0x%x\n", read_val);
		}
		else {
			dev_err(&client->dev,"##\e[31m Frame synchronization is not locked. read_val=0x%x\e[0m \n", read_val);
			return -1;
		}
	}

    return ret;
}

static const struct v4l2_subdev_core_ops max9286_core_ops = {
	.s_power		= max9286_s_power,
	.init 			= max9286_init,/* initializing API */
};


static int max9286_s_mbus_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	struct max9286_state *state = container_of(sd, struct max9286_state, sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_err(&client->dev, "%s() \n", __func__);

	if (fmt->width < fmt->height) {
		int temp;
		temp  = fmt->width;
		fmt->width = fmt->height;
		fmt->height = temp;
	}

	state->pix.width = fmt->width;
	state->pix.height = fmt->height;
	//state->pix.pixelformat = fmt->fmt.pix.pixelformat;

	if (state->oprmode == MAX9286_OPRMODE_IMAGE) {
		state->oprmode = MAX9286_OPRMODE_IMAGE;
		/*
		 * In case of image capture mode,
		 * if the given image resolution is not supported,
		 * use the next higher image resolution. */
		max9286_set_framesize(sd, max9286_capture_framesize_list,
				ARRAY_SIZE(max9286_capture_framesize_list),
				false);

	} else {
		state->oprmode = MAX9286_OPRMODE_VIDEO;
		/*
		 * In case of video mode,
		 * if the given video resolution is not matching, use
		 * the default rate (currently MAX9286_PREVIEW_WVGA).
		 */
		max9286_set_framesize(sd, max9286_preview_framesize_list,
				ARRAY_SIZE(max9286_preview_framesize_list),
				true);
	}

	state->jpeg.enable = state->pix.pixelformat == V4L2_PIX_FMT_JPEG;

	return 0;
}

static int max9286_enum_framesizes(struct v4l2_subdev *sd,
				  struct v4l2_frmsizeenum *fsize)
{
	struct max9286_state *state = container_of(sd, struct max9286_state, sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_err(&client->dev, "%s() wid=%d\t height=%d\n", __func__,state->pix.width,state->pix.height);

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = state->pix.width;
	fsize->discrete.height = state->pix.height;

	return 0;
}

static int max9286_g_parm(struct v4l2_subdev *sd,
			struct v4l2_streamparm *param)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct max9286_state *state = container_of(sd, struct max9286_state, sd);

	dev_err(&client->dev, "%s() \n", __func__);

	memcpy(param, &state->strm, sizeof(param));
	return 0;
}

static int max9286_s_parm(struct v4l2_subdev *sd,
			struct v4l2_streamparm *param)
{
	int err = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct max9286_state *state = container_of(sd, struct max9286_state, sd);
	struct sec_cam_parm *new_parms = (struct sec_cam_parm *)&param->parm.raw_data;
	struct sec_cam_parm *parms = (struct sec_cam_parm *)&state->strm.parm.raw_data;

	dev_err(&client->dev, "%s() \n", __func__);

	if (param->parm.capture.timeperframe.numerator !=
		parms->capture.timeperframe.numerator ||
		param->parm.capture.timeperframe.denominator !=
		parms->capture.timeperframe.denominator) {

		int fps = 0;
		int fps_max = 30;

		if (param->parm.capture.timeperframe.numerator &&
			param->parm.capture.timeperframe.denominator)
			fps =
			    (int)(param->parm.capture.timeperframe.denominator /
				  param->parm.capture.timeperframe.numerator);
		else
			fps = 0;

		if (fps <= 0 || fps > fps_max) {
			dev_err(&client->dev,
				"%s: Framerate %d not supported,"
				" setting it to %d fps.\n",
				__func__, fps, fps_max);
			fps = fps_max;
		}

		/*
		 * Don't set the fps value, just update it in the state
		 * We will set the resolution and
		 * fps in the start operation (preview/capture) call
		 */
		parms->capture.timeperframe.numerator = 1;
		parms->capture.timeperframe.denominator = fps;
	}

	/* we return an error if one happened but don't stop trying to
	 * set all parameters passed
	 */
	err = max9286_set_parameter(sd, &parms->contrast, new_parms->contrast,
				"contrast", state->regs->contrast,
				ARRAY_SIZE(state->regs->contrast));
	err |= max9286_set_parameter(sd, &parms->effects, new_parms->effects,
				"effect", state->regs->effect,
				ARRAY_SIZE(state->regs->effect));
	err |= max9286_set_parameter(sd, &parms->brightness,
				new_parms->brightness, "brightness",
				state->regs->ev, ARRAY_SIZE(state->regs->ev));
///	err |= max9286_set_flash_mode(sd, new_parms->flash_mode);
///	err |= max9286_set_focus_mode(sd, new_parms->focus_mode);
	err |= max9286_set_parameter(sd, &parms->iso, new_parms->iso,
				"iso", state->regs->iso,
				ARRAY_SIZE(state->regs->iso));
	err |= max9286_set_parameter(sd, &parms->metering, new_parms->metering,
				"metering", state->regs->metering,
				ARRAY_SIZE(state->regs->metering));
	err |= max9286_set_parameter(sd, &parms->saturation,
				new_parms->saturation, "saturation",
				state->regs->saturation,
				ARRAY_SIZE(state->regs->saturation));
	err |= max9286_set_parameter(sd, &parms->scene_mode,
				new_parms->scene_mode, "scene_mode",
				state->regs->scene_mode,
				ARRAY_SIZE(state->regs->scene_mode));
	err |= max9286_set_parameter(sd, &parms->sharpness,
				new_parms->sharpness, "sharpness",
				state->regs->sharpness,
				ARRAY_SIZE(state->regs->sharpness));
	err |= max9286_set_parameter(sd, &parms->aeawb_lockunlock,
				new_parms->aeawb_lockunlock, "aeawb_lockunlock",
				state->regs->aeawb_lockunlock,
				ARRAY_SIZE(state->regs->aeawb_lockunlock));
	err |= max9286_set_parameter(sd, &parms->white_balance,
				new_parms->white_balance, "white balance",
				state->regs->white_balance,
				ARRAY_SIZE(state->regs->white_balance));
	err |= max9286_set_parameter(sd, &parms->fps,
				new_parms->fps, "fps",
				state->regs->fps,
				ARRAY_SIZE(state->regs->fps));

	if (parms->scene_mode == SCENE_MODE_NIGHTSHOT)
		state->one_frame_delay_ms = NIGHT_MODE_MAX_ONE_FRAME_DELAY_MS;
	else
		state->one_frame_delay_ms = NORMAL_MODE_MAX_ONE_FRAME_DELAY_MS;

	dev_dbg(&client->dev, "%s: returning %d\n", __func__, err);
	return err;
}

static int max9286_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct max9286_state *state = container_of(sd, struct max9286_state, sd);
	int ret = 0;

	dev_err(&client->dev, "%s() \n", __func__);
	printk(KERN_ERR "## [%s():%s:%d\t] enable:%d \n", __FUNCTION__, strrchr(__FILE__, '/')+1, __LINE__, enable);

	if (enable) {
		//enable_irq(state->irq);
		ret = max9286_init(sd, enable);
	} else {
		state->runmode = MAX9286_RUNMODE_NOTREADY;
		//disable_irq(state->irq);
	}
	return ret;
}

static const struct v4l2_subdev_video_ops max9286_video_ops = {
	//.g_mbus_fmt 		= max9286_g_mbus_fmt,
	.s_mbus_fmt 		= max9286_s_mbus_fmt,
	.enum_framesizes 	= max9286_enum_framesizes,
	//.enum_fmt 		= max9286_enum_fmt,
	//.try_fmt 		= max9286_try_fmt,
	.g_parm 		= max9286_g_parm,
	.s_parm 		= max9286_s_parm,
	.s_stream 		= max9286_s_stream,
};

/**
 * __find_oprmode - Lookup MAX9286 resolution type according to pixel code
 * @code: pixel code
 */
static enum max9286_oprmode __find_oprmode(enum v4l2_mbus_pixelcode code)
{
	enum max9286_oprmode type = MAX9286_OPRMODE_VIDEO;


	do {
		if (code == default_fmt[type].code)
			return type;
	} while (type++ != SIZE_DEFAULT_FFMT);

	return 0;
}

/**
 * __find_resolution - Lookup preset and type of M-5MOLS's resolution
 * @mf: pixel format to find/negotiate the resolution preset for
 * @type: M-5MOLS resolution type
 * @resolution:	M-5MOLS resolution preset register value
 *
 * Find nearest resolution matching resolution preset and adjust mf
 * to supported values.
 */
static int __find_resolution(struct v4l2_subdev *sd,
			     struct v4l2_mbus_framefmt *mf,
			     enum max9286_oprmode *type,
			     u32 *resolution)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct max9286_resolution *fsize = &max9286_resolutions[0];
	const struct max9286_resolution *match = NULL;
	enum max9286_oprmode stype = __find_oprmode(mf->code);
	int i = ARRAY_SIZE(max9286_resolutions);
	unsigned int min_err = ~0;
	int err;

	while (i--) {
		if (stype == fsize->type) {
			err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);

			if (err < min_err) {
				min_err = err;
				match = fsize;
				stype = fsize->type;
			}
		}
		fsize++;
	}
	dev_err(&client->dev, "LINE(%d): mf width: %d, mf height: %d, mf code: %d\n", __LINE__,
		mf->width, mf->height, stype);
	dev_err(&client->dev, "LINE(%d): match width: %d, match height: %d, match code: %d\n", __LINE__,
		match->width, match->height, stype);
	if (match) {
		mf->width  = match->width;
		mf->height = match->height;
		*resolution = match->value;
		*type = stype;
		return 0;
	}
	dev_err(&client->dev, "LINE(%d): mf width: %d, mf height: %d, mf code: %d\n", __LINE__,
		mf->width, mf->height, stype);

	return -EINVAL;
}

static struct v4l2_mbus_framefmt *__find_format(struct max9286_state *state,
				struct v4l2_subdev_fh *fh,
				enum v4l2_subdev_format_whence which,
				enum max9286_oprmode type)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return fh ? v4l2_subdev_get_try_format(fh, 0) : NULL;

	return &state->ffmt[type];
}

/* enum code by flite video device command */
static int max9286_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (!code || code->index >= SIZE_DEFAULT_FFMT)
		return -EINVAL;

	code->code = default_fmt[code->index].code;

	return 0;
}

/* get format by flite video device command */
static int max9286_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct max9286_state *state =
		container_of(sd, struct max9286_state, sd);
	struct v4l2_mbus_framefmt *format;

	if (fmt->pad != 0)
		return -EINVAL;

	format = __find_format(state, fh, fmt->which, state->res_type);
	if (!format)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

/* set format by flite video device command */
static int max9286_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct max9286_state *state =
		container_of(sd, struct max9286_state, sd);
	struct v4l2_mbus_framefmt *format = &fmt->format;
	struct v4l2_mbus_framefmt *sfmt;
	enum max9286_oprmode type;
	u32 resolution = 0;
	int ret;

	if (fmt->pad != 0)
		return -EINVAL;

	ret = __find_resolution(sd, format, &type, &resolution);
	if (ret < 0)
		return ret;

	sfmt = __find_format(state, fh, fmt->which, type);
	if (!sfmt)
		return 0;

	sfmt		= &default_fmt[type];
	sfmt->width	= format->width;
	sfmt->height	= format->height;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		/* for enum size of entity by flite */
		state->oprmode  		= type;
		state->ffmt[type].width 	= format->width;
		state->ffmt[type].height 	= format->height;
#ifndef CONFIG_VIDEO_MAX9286_SENSOR_JPEG
		state->ffmt[type].code 		= V4L2_MBUS_FMT_YUYV8_2X8;
#else
		state->ffmt[type].code 		= format->code;
#endif

		/* find adaptable resolution */
		state->resolution 		= resolution;
#ifndef CONFIG_VIDEO_MAX9286_SENSOR_JPEG
		state->code 			= V4L2_MBUS_FMT_YUYV8_2X8;
#else
		state->code 			= format->code;
#endif
		state->res_type 		= type;

		/* for set foramat */
		state->pix.width 		= format->width;
		state->pix.height 		= format->height;

		if (state->power_on == MAX9286_HW_POWER_ON)
			max9286_s_mbus_fmt(sd, sfmt);  /* set format */
	}

	return 0;
}


static struct v4l2_subdev_pad_ops max9286_pad_ops = {
	.enum_mbus_code	= max9286_enum_mbus_code,
	.get_fmt		= max9286_get_fmt,
	.set_fmt		= max9286_set_fmt,
};

static const struct v4l2_subdev_ops max9286_ops = {
	.core = &max9286_core_ops,
	.video = &max9286_video_ops,
	.pad	= &max9286_pad_ops,
};

static int max9286_link_setup(struct media_entity *entity,
			    const struct media_pad *local,
			    const struct media_pad *remote, u32 flags)
{
	printk("%s\n", __func__);
	return 0;
}

static const struct media_entity_operations max9286_media_ops = {
	.link_setup = max9286_link_setup,
};

/* internal ops for media controller */
static int max9286_init_formats(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_err(&client->dev, "%s: \n", __func__);
	memset(&format, 0, sizeof(format));
	format.pad = 0;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = DEFAULT_SENSOR_CODE;
	format.format.width = DEFAULT_SENSOR_WIDTH;
	format.format.height = DEFAULT_SENSOR_HEIGHT;

	return 0;
}

static int max9286_subdev_close(struct v4l2_subdev *sd,
			      struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int max9286_subdev_registered(struct v4l2_subdev *sd)
{
	return 0;
}

static void max9286_subdev_unregistered(struct v4l2_subdev *sd)
{
	return;
}

static const struct v4l2_subdev_internal_ops max9286_v4l2_internal_ops = {
	.open = max9286_init_formats,
	.close = max9286_subdev_close,
	.registered = max9286_subdev_registered,
	.unregistered = max9286_subdev_unregistered,
};

/*
 * max9286_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int max9286_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct max9286_state *state;
	int ret = 0;

	state = kzalloc(sizeof(struct max9286_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	mutex_init(&state->ctrl_lock);
	init_completion(&state->af_complete);

	state->power_on = MAX9286_HW_POWER_OFF;

	state->runmode = MAX9286_RUNMODE_NOTREADY;
	sd = &state->sd;
	strcpy(sd->name, MAX9286_DRIVER_NAME);

	v4l2_i2c_subdev_init(sd, client, &max9286_ops);
	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sd->entity, 1, &state->pad, 0);
	if (ret < 0) {
        dev_err(&client->dev, "%s: failed\n", __func__);
        return ret;
    }

	//max9286_init_formats(sd, NULL);
	//max9286_init_parameters(sd);

	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &max9286_v4l2_internal_ops;
	sd->entity.ops = &max9286_media_ops;

    ret = max9286_initialize_ctrls(state);
    if (ret < 0) {
        printk(KERN_ERR "%s: failed to initialize controls\n", __func__);
        return ret;
    }
    state->i2c_client = client;


    return 0;
}

static int max9286_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct max9286_state *state =
		container_of(sd, struct max9286_state, sd);

	v4l2_device_unregister_subdev(sd);
	mutex_destroy(&state->ctrl_lock);
	kfree(state);
	dev_dbg(&client->dev, "Unloaded camera sensor MAX9286.\n");

	return 0;
}

#ifdef CONFIG_PM
static int max9286_suspend(struct i2c_client *client, pm_message_t state)
{
	int ret = 0;

	return ret;
}

static int max9286_resume(struct i2c_client *client)
{
	int ret = 0;

	return ret;
}

#endif

static const struct i2c_device_id max9286_id[] = {
	{ MAX9286_DRIVER_NAME, 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, max9286_id);

static struct i2c_driver max9286_i2c_driver = {
	.probe = max9286_probe,
	.remove = __devexit_p(max9286_remove),
#ifdef CONFIG_PM
	.suspend = max9286_suspend,
	.resume = max9286_resume,
#endif

	.driver = {
		.name = MAX9286_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.id_table = max9286_id,
};

static int __init max9286_mod_init(void)
{
	return i2c_add_driver(&max9286_i2c_driver);
}

static void __exit max9286_mod_exit(void)
{
	i2c_del_driver(&max9286_i2c_driver);
}

module_init(max9286_mod_init);
module_exit(max9286_mod_exit);

MODULE_DESCRIPTION("MAX9286 Video driver");
MODULE_AUTHOR(" ");
MODULE_LICENSE("GPL");

