/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/media.h>
#include <media/v4l2-ioctl.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <media/adv7481.h>
#include <media/msm_ba.h>

#include "adv7481_reg.h"

#define DRIVER_NAME "adv7481"
#define I2C_RESET_DELAY		75000
#define I2C_RW_DELAY		100
#define I2C_SW_DELAY		10000
#define GPIO_HW_DELAY_LOW	100000
#define GPIO_HW_DELAY_HI	10000
#define SDP_MIN_SLEEP		5000
#define SDP_MAX_SLEEP		6000
#define SDP_NUM_TRIES		30
#define LOCK_MIN_SLEEP		5000
#define LOCK_MAX_SLEEP		6000
#define LOCK_NUM_TRIES		20


struct adv7481_state {
	/* Platform Data */
	struct adv7481_platform_data pdata;

	/* V4L2 Data */
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_dv_timings timings;
	struct v4l2_ctrl *cable_det_ctrl;

	/* media entity controls */
	struct media_pad pad;

	struct workqueue_struct *work_queues;
	struct mutex		mutex;

	struct i2c_client *client;
	struct i2c_client *i2c_csi_txa;
	struct i2c_client *i2c_csi_txb;
	struct i2c_client *i2c_hdmi;
	struct i2c_client *i2c_edid;
	struct i2c_client *i2c_cp;
	struct i2c_client *i2c_sdp;
	struct i2c_client *i2c_rep;

	/* device status and Flags */
	int powerup;
	/* routing configuration data */
	int csia_src;
	int csib_src;
	int mode;
	/* CSI configuration data */
	int tx_auto_params;
	enum adv7481_mipi_lane tx_lanes;
};

struct adv7481_hdmi_params {
	uint16_t pll_lock;
	uint16_t tmds_freq;
	uint16_t vert_lock;
	uint16_t horz_lock;
	uint16_t pix_rep;
	uint16_t color_depth;
};

struct adv7481_vid_params {
	uint16_t pix_clk;
	uint16_t act_pix;
	uint16_t act_lines;
	uint16_t tot_pix;
	uint16_t tot_lines;
	uint16_t fr_rate;
	uint16_t intrlcd;
};

const uint8_t adv7481_default_edid_data[] = {
/* Block 0 (EDID Base Block) */
0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
/* Vendor Identification */
0x45, 0x23, 0xDD, 0xDD, 0x01, 0x01, 0x01, 0x01, 0x01, 0x16,
/* EDID Structure Version and Revision */
0x01, 0x03,
/* Display Parameters */
0x80, 0x90, 0x51, 0x78, 0x0A,
/* Color characteristics */
0x0D, 0xC9, 0xA0, 0x57, 0x47, 0x98, 0x27, 0x12, 0x48, 0x4C,
/* Established Timings */
0x21, 0x08, 0x00,
/* Standard Timings */
0x81, 0x80, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* Detailed Descriptors */
0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40,
0x58, 0x2C,
0x45, 0x00, 0xA0, 0x2A, 0x53, 0x00, 0x00, 0x1E,
/* Detailed Descriptors */
0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
0x6E, 0x28, 0x55, 0x00, 0xA0, 0x2A, 0x53, 0x00,
0x00, 0x1E,
/* Monitor Descriptor */
0x00, 0x00, 0x00, 0xFD, 0x00, 0x3A, 0x3E, 0x0F,
0x46, 0x0F, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20,
0x20, 0x20,
/* Monitor Descriptor */
0x00, 0x00, 0x00, 0xFC, 0x00, 0x54, 0x56, 0x0A,
0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
0x20, 0x20,
/* Extension Flag */
0x01,
/* checksum */
0x9A,

/* Block 1 (Extension Block) */
/* Extension Header */
0x02, 0x03, 0x37,
/* Display supports */
0xF0,
/* Video Data Bock */
0x4A, 0x10, 0x04, 0x05, 0x03, 0x02, 0x07, 0x06,
0x20, 0x01, 0x3C,
/* Audio Data Block */
0x29,
0x09, 0x07, 0x07, /* LPCM, max 2 ch, 48k, 44.1k, 32k */
0x15, 0x07, 0x50, /* AC-3, max 6 ch, 48k, 44.1k, 32k, max bitrate 640 */
0x3D, 0x07, 0x50, /* DTS, max 6ch, 48,44.1,32k, max br 640 */
/* Speaker Allocation Data Block */
0x83, 0x01, 0x00, 0x00,
/* HDMI VSDB */
/* no deep color, Max_TMDS_Clock = 165 MHz */
0x76, 0x03, 0x0C, 0x00, 0x30, 0x00, 0x80, 0x21,
/* hdmi_video_present=1, 3d_present=1, 3d_multi_present=0,
 * hdmi_vic_len=0, hdmi_3d_len=0xC */
0x2F, 0x88, 0x0C, 0x20, 0x90, 0x08, 0x10, 0x18,
0x10, 0x28, 0x10, 0x78, 0x10, 0x06, 0x26,
/* VCDB */
0xE2, 0x00, 0x7B,
/* Detailed Descriptor */
0x01, 0x1D, 0x80, 0x18, 0x71, 0x1C, 0x16, 0x20,
0x58, 0x2C, 0x25, 0x00, 0xA0, 0x2A, 0x53, 0x00,
0x00, 0x9E,
/* Detailed Descriptor */
0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0, 0x2D, 0x10,
0x10, 0x3E, 0x96, 0x00, 0xA0, 0x2A, 0x53, 0x00,
0x00, 0x18,
/* Detailed Descriptor */
0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0, 0x2D, 0x10,
0x10, 0x3E, 0x96, 0x00, 0x38, 0x2A, 0x43, 0x00,
0x00, 0x18,
/* Detailed Descriptor */
0x8C, 0x0A, 0xA0, 0x14, 0x51, 0xF0, 0x16, 0x00,
0x26, 0x7C, 0x43, 0x00, 0x38, 0x2A, 0x43, 0x00,
0x00, 0x98,
/* checksum */
0x38
};

#define ADV7481_EDID_SIZE ARRAY_SIZE(adv7481_default_edid_data)

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &(container_of(ctrl->handler,
			struct adv7481_state, ctrl_hdl)->sd);
}

static inline struct adv7481_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7481_state, sd);
}

/* I2C Rd/Rw Functions */
static int adv7481_wr_byte(struct i2c_client *i2c_client, unsigned int reg,
	unsigned int value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(i2c_client, reg & 0xFF, value);
	usleep(I2C_RW_DELAY);

	return ret;
}

static int adv7481_rd_byte(struct i2c_client *i2c_client, unsigned int reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(i2c_client, reg & 0xFF);
	usleep(I2C_RW_DELAY);

	return ret;
}

int adv7481_set_edid(struct adv7481_state *state)
{
	int i;
	int ret = 0;

	/* Enable Manual Control of EDID */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x74, 0x01);
	/* Disable Auto Enable of EDID */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x7A, 0x08);
	/* Set Primary EDID Size to 256 Bytes */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x70, 0x80);

	for (i = 0; i < ADV7481_EDID_SIZE; i++) {
		ret |= adv7481_wr_byte(state->i2c_edid, i,
						adv7481_default_edid_data[i]);
	}
	/* Manually Enable EDID on Port A */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x07, 0x01);

	return ret;
}

/* Initialize adv7481 I2C Settings */
static int adv7481_dev_init(struct adv7481_state *state,
						struct i2c_client *client)
{
	int ret;

	mutex_lock(&state->mutex);

	/* Delay required following I2C reset and I2C transactions */
	/* soft reset */
	ret = adv7481_wr_byte(state->client,
					IO_REG_RST, IO_CTRL_MAIN_RST_REG_VALUE);
	usleep(I2C_SW_DELAY);

	/* power down controls */
	ret |= adv7481_wr_byte(state->client,
				IO_REG_PWR_DN2_XTAL_HIGH_ADDR, 0x76);
	ret |= adv7481_wr_byte(state->client,
				IO_REG_CP_VID_STD_ADDR, 0x4a);

	/* Configure I2C Maps and I2C Communication Settings */
	/* io_reg_f2 I2C Auto Increment */
	ret |= adv7481_wr_byte(state->client, IO_REG_I2C_CFG_ADDR,
				IO_REG_I2C_AUTOINC_EN_REG_VALUE);
	/* DPLL Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_DPLL_ADDR,
				IO_REG_DPLL_SADDR);
	/* CP Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_CP_ADDR,
				IO_REG_CP_SADDR);
	/* HDMI RX Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_HDMI_ADDR,
				IO_REG_HDMI_SADDR);
	/* EDID Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_EDID_ADDR,
				IO_REG_EDID_SADDR);
	/* HDMI RX Repeater Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_HDMI_REP_ADDR,
				IO_REG_HDMI_REP_SADDR);
	/* HDMI RX Info-frame Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_HDMI_INF_ADDR,
				IO_REG_HDMI_INF_SADDR);
	/* CBUS Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_CBUS_ADDR,
				IO_REG_CBUS_SADDR);
	/* CEC Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_CEC_ADDR,
					IO_REG_CEC_SADDR);
	/* SDP Main Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_SDP_ADDR,
				IO_REG_SDP_SADDR);
	/* CSI-TXB Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_CSI_TXB_ADDR,
				IO_REG_CSI_TXB_SADDR);
	/* CSI-TXA Map Address */
	ret |= adv7481_wr_byte(state->client, IO_REG_CSI_TXA_ADDR,
				IO_REG_CSI_TXA_SADDR);

	/* Configure i2c clients */
	state->i2c_csi_txa = i2c_new_dummy(client->adapter,
				IO_REG_CSI_TXA_SADDR >> 1);
	state->i2c_csi_txb = i2c_new_dummy(client->adapter,
				IO_REG_CSI_TXB_SADDR >> 1);
	state->i2c_cp = i2c_new_dummy(client->adapter,
				IO_REG_CP_SADDR >> 1);
	state->i2c_hdmi = i2c_new_dummy(client->adapter,
				IO_REG_HDMI_SADDR >> 1);
	state->i2c_edid = i2c_new_dummy(client->adapter,
				IO_REG_EDID_SADDR >> 1);
	state->i2c_sdp = i2c_new_dummy(client->adapter,
				IO_REG_SDP_SADDR >> 1);
	state->i2c_rep = i2c_new_dummy(client->adapter,
				IO_REG_HDMI_REP_SADDR >> 1);

	if (!state->i2c_csi_txa || !state->i2c_csi_txb || !state->i2c_cp ||
		!state->i2c_sdp || !state->i2c_hdmi || !state->i2c_edid ||
		!state->i2c_rep) {
		pr_err("Additional I2C Client Fail\n");
		ret = EFAULT;
	}
	adv7481_set_edid(state);
	mutex_unlock(&state->mutex);

	return ret;
}

/* Initialize adv7481 hardware */
static int adv7481_hw_init(struct adv7481_platform_data *pdata,
						struct adv7481_state *state)
{
	int ret;

	if (!pdata) {
		pr_err("PDATA is NULL\n");
		return -EFAULT;
	}

	mutex_lock(&state->mutex);
	if (gpio_is_valid(pdata->rstb_gpio)) {
		ret = gpio_request(pdata->rstb_gpio, "rstb_gpio");
		if (ret) {
			pr_err("Request GPIO Fail\n");
			return ret;
		}
		ret = gpio_direction_output(pdata->rstb_gpio, 0);
		usleep(GPIO_HW_DELAY_LOW);
		ret = gpio_direction_output(pdata->rstb_gpio, 1);
		usleep(GPIO_HW_DELAY_HI);
		if (ret) {
			pr_err("Set GPIO Fail\n");
			return ret;
		}
	}
	mutex_unlock(&state->mutex);

	return ret;
}

static int adv7481_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct adv7481_state *state = to_state(sd);
	int temp = 0x0;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		temp = adv7481_rd_byte(state->client, CP_REG_VID_ADJ);
		temp |= CP_CTR_VID_ADJ_EN;
		ret = adv7481_wr_byte(state->client, CP_REG_VID_ADJ, temp);
		ret |= adv7481_wr_byte(state->client,
				CP_REG_BRIGHTNESS, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		temp = adv7481_rd_byte(state->client, CP_REG_VID_ADJ);
		temp |= CP_CTR_VID_ADJ_EN;
		ret = adv7481_wr_byte(state->client, CP_REG_VID_ADJ, temp);
		ret |= adv7481_wr_byte(state->client,
				CP_REG_CONTRAST, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		temp = adv7481_rd_byte(state->client, CP_REG_VID_ADJ);
		temp |= CP_CTR_VID_ADJ_EN;
		ret = adv7481_wr_byte(state->client, CP_REG_VID_ADJ, temp);
		ret |= adv7481_wr_byte(state->client,
				CP_REG_SATURATION, ctrl->val);
		break;
	case V4L2_CID_HUE:
		temp = adv7481_rd_byte(state->client, CP_REG_VID_ADJ);
		temp |= CP_CTR_VID_ADJ_EN;
		ret = adv7481_wr_byte(state->client, CP_REG_VID_ADJ, temp);
		ret |= adv7481_wr_byte(state->client, CP_REG_HUE, ctrl->val);
		break;
	default:
		break;
	}
	return ret;
}

static int adv7481_powerup(struct adv7481_state *state, bool powerup)
{
	if (powerup)
		pr_debug("powered up\n");
	 else
		pr_debug("powered off\n");

	return 0;
}

static int adv7481_s_power(struct v4l2_subdev *sd, int on)
{
	struct adv7481_state *state = to_state(sd);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return -EBUSY;

	ret = adv7481_powerup(state, on);
	if (ret == 0)
		state->powerup = on;

	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7481_get_sd_timings(struct adv7481_state *state, int *sd_standard)
{
	int ret = 0;
	int sdp_stat, sdp_stat2;
	int timeout = 0;

	if (sd_standard == NULL)
		return -EINVAL;

	do {
		sdp_stat = adv7481_rd_byte(state->i2c_sdp, SDP_REG_STATUS1);
		usleep_range(SDP_MIN_SLEEP, SDP_MAX_SLEEP);
		timeout++;
		sdp_stat2 = adv7481_rd_byte(state->i2c_sdp, SDP_REG_STATUS1);
	} while ((sdp_stat != sdp_stat2) && (timeout < SDP_NUM_TRIES));

	if (sdp_stat != sdp_stat2) {
		pr_err("%s(%d), adv7481 SDP status unstable: 1",
							__func__, __LINE__);
		return -ETIMEDOUT;
	}

	if (!(sdp_stat & 0x01)) {
		pr_err("%s(%d), adv7481 SD Input NOT Locked: 0x%x",
				__func__, __LINE__, sdp_stat);
		return -EBUSY;
	}

	switch ((sdp_stat &= SDP_CTRL_ADRESLT) >> 4) {
	case AD_NTSM_M_J:
		*sd_standard = V4L2_STD_NTSC;
		break;
	case AD_NTSC_4_43:
		*sd_standard = V4L2_STD_NTSC_443;
		break;
	case AD_PAL_M:
		*sd_standard = V4L2_STD_PAL_M;
		break;
	case AD_PAL_60:
		*sd_standard = V4L2_STD_PAL_60;
		break;
	case AD_PAL_B_G:
		*sd_standard = V4L2_STD_PAL;
		break;
	case AD_SECAM:
		*sd_standard = V4L2_STD_SECAM;
		break;
	case AD_PAL_COMB:
		*sd_standard = V4L2_STD_PAL_Nc | V4L2_STD_PAL_N;
		break;
	case AD_SECAM_525:
		*sd_standard = V4L2_STD_SECAM;
		break;
	default:
		*sd_standard = V4L2_STD_UNKNOWN;
		break;
	}
	return ret;
}

int adv7481_set_cvbs_mode(struct adv7481_state *state)
{
	int ret;
	uint8_t val;

	/* cvbs video settings ntsc etc */
	ret = adv7481_wr_byte(state->client, 0x00, 0x30);
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x0f, 0x00);
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x00, 0x00);
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x03, 0x42);
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x04, 0x07);
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x13, 0x00);
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x17, 0x41);
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x31, 0x12);
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x52, 0xcd);
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x0e, 0xff);
	val = adv7481_rd_byte(state->client, IO_REG_CSI_PIX_EN_SEL_ADDR);
	/* Output of SD core routed to MIPI CSI 4-lane Tx */
	val |= ADV_REG_SETFIELD(0x10, IO_CTRL_CSI4_IN_SEL);
	ret |= adv7481_wr_byte(state->client, IO_REG_CSI_PIX_EN_SEL_ADDR, val);
	/* Enable autodetect */
	ret |= adv7481_wr_byte(state->i2c_sdp, 0x0e, 0x81);

	return ret;
}

int adv7481_set_hdmi_mode(struct adv7481_state *state)
{
	int ret;
	int temp;
	uint8_t val;

	/* Configure IO setting for HDMI in and
	 * YUV 422 out via TxA CSI: 4-Lane
	 */
	/* Disable chip powerdown & Enable HDMI Rx block */
	temp = adv7481_rd_byte(state->client, IO_REG_PWR_DOWN_CTRL_ADDR);
	val = ADV_REG_SETFIELD(1, IO_CTRL_RX_EN) |
				ADV_REG_SETFIELD(0, IO_CTRL_RX_PWDN) |
				ADV_REG_SETFIELD(0, IO_CTRL_XTAL_PWDN) |
				ADV_REG_SETFIELD(0, IO_CTRL_CORE_PWDN) |
				ADV_REG_SETFIELD(0, IO_CTRL_MASTER_PWDN);
	ret = adv7481_wr_byte(state->client, IO_REG_PWR_DOWN_CTRL_ADDR, val);
	/* SDR mode */
	ret |= adv7481_wr_byte(state->client, 0x11, 0x48);
	/* Set CP core to YUV out */
	ret |= adv7481_wr_byte(state->client, 0x04, 0x00);
	/* Set CP core to SDR 422 */
	ret |= adv7481_wr_byte(state->client, 0x12, 0xF2);
	/* Saturate both Luma and Chroma values to 254 */
	ret |= adv7481_wr_byte(state->client, 0x17, 0x80);
	/* Set CP core to enable AV codes */
	ret |= adv7481_wr_byte(state->client, 0x03, 0x86);
	/* Set CP core Phase Adjustment */
	ret |= adv7481_wr_byte(state->client, 0x0C, 0xE0);
	/* Power down unused Interfaces */
	ret |= adv7481_wr_byte(state->client, 0x0E, 0xFF);
	/* Enable Tx A CSI 4-Lane & data from CP core */
	val = ADV_REG_SETFIELD(1, IO_CTRL_CSI4_EN) |
		ADV_REG_SETFIELD(1, IO_CTRL_PIX_OUT_EN) |
		ADV_REG_SETFIELD(0, IO_CTRL_CSI4_IN_SEL);
	ret |= adv7481_wr_byte(state->client, IO_REG_CSI_PIX_EN_SEL_ADDR,
		val);
	/* ADI RS CP Core: */
	ret |= adv7481_wr_byte(state->i2c_cp, 0x7C, 0x00);

	/* start to configure HDMI Rx once io-map is configured */
	/* Enable HDCP 1.1 */
	ret |= adv7481_wr_byte(state->i2c_rep, 0x40, 0x83);
	/* Foreground Channel = A */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x00, 0x08);
	/* ADI Required Write */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x98, 0xFF);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x99, 0xA3);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x9A, 0x00);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x9B, 0x0A);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x9D, 0x40);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0xCB, 0x09);
	/* ADI RS */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x3D, 0x10);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x3E, 0x69);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x3F, 0x46);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x4E, 0xFE);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x4F, 0x18);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x57, 0xA3);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x58, 0x04);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x85, 0x10);
	/* Enable All Terminations */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x83, 0x00);
	/* ADI RS */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0xA3, 0x01);
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0xBE, 0x00);
	/* HPA Manual Enable */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x6C, 0x01);
	/* HPA Asserted */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0xF8, 0x01);

	/* Audio Mute Speed Set to Fastest (Smallest Step Size) */
	ret |= adv7481_wr_byte(state->i2c_hdmi, 0x0F, 0x00);

	return ret;
}

int adv7481_set_analog_mux(struct adv7481_state *state, int input)
{
	int ain_sel = 0x0;

	switch (input) {
	case ADV7481_IP_CVBS_1:
	case ADV7481_IP_CVBS_1_HDMI_SIM:
		ain_sel = 0x0;
		break;
	case ADV7481_IP_CVBS_2:
	case ADV7481_IP_CVBS_2_HDMI_SIM:
		ain_sel = 0x1;
		break;
	case ADV7481_IP_CVBS_3:
	case ADV7481_IP_CVBS_3_HDMI_SIM:
		ain_sel = 0x2;
		break;
	case ADV7481_IP_CVBS_4:
	case ADV7481_IP_CVBS_4_HDMI_SIM:
		ain_sel = 0x3;
		break;
	case ADV7481_IP_CVBS_5:
	case ADV7481_IP_CVBS_5_HDMI_SIM:
		ain_sel = 0x4;
		break;
	case ADV7481_IP_CVBS_6:
	case ADV7481_IP_CVBS_6_HDMI_SIM:
		ain_sel = 0x5;
		break;
	case ADV7481_IP_CVBS_7:
	case ADV7481_IP_CVBS_7_HDMI_SIM:
		ain_sel = 0x6;
		break;
	case ADV7481_IP_CVBS_8:
	case ADV7481_IP_CVBS_8_HDMI_SIM:
		ain_sel = 0x7;
		break;
	}
	return 0;
}

static int adv7481_set_ip_mode(struct adv7481_state *state, int input)
{
	int ret = 0;

	switch (input) {
	case ADV7481_IP_HDMI:
		ret = adv7481_set_hdmi_mode(state);
		break;
	case ADV7481_IP_CVBS_1:
	case ADV7481_IP_CVBS_2:
	case ADV7481_IP_CVBS_3:
	case ADV7481_IP_CVBS_4:
	case ADV7481_IP_CVBS_5:
	case ADV7481_IP_CVBS_6:
	case ADV7481_IP_CVBS_7:
	case ADV7481_IP_CVBS_8:
		ret = adv7481_set_cvbs_mode(state);
		ret |= adv7481_set_analog_mux(state, input);
		break;
	case ADV7481_IP_CVBS_1_HDMI_SIM:
	case ADV7481_IP_CVBS_2_HDMI_SIM:
	case ADV7481_IP_CVBS_3_HDMI_SIM:
	case ADV7481_IP_CVBS_4_HDMI_SIM:
	case ADV7481_IP_CVBS_5_HDMI_SIM:
	case ADV7481_IP_CVBS_6_HDMI_SIM:
	case ADV7481_IP_CVBS_7_HDMI_SIM:
	case ADV7481_IP_CVBS_8_HDMI_SIM:
		ret = adv7481_set_hdmi_mode(state);
		ret |= adv7481_set_cvbs_mode(state);
		ret |= adv7481_set_analog_mux(state, input);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int adv7481_set_op_src(struct adv7481_state *state,
						int output, int input)
{
	int ret = 0;
	int temp = 0;
	int val = 0;

	switch (output) {
	case ADV7481_OP_CSIA:
		switch (input) {
		case ADV7481_IP_CVBS_1:
		case ADV7481_IP_CVBS_2:
		case ADV7481_IP_CVBS_3:
		case ADV7481_IP_CVBS_4:
		case ADV7481_IP_CVBS_5:
		case ADV7481_IP_CVBS_6:
		case ADV7481_IP_CVBS_7:
		case ADV7481_IP_CVBS_8:
			val = 0x10;
			break;
		case ADV7481_IP_CVBS_1_HDMI_SIM:
		case ADV7481_IP_CVBS_2_HDMI_SIM:
		case ADV7481_IP_CVBS_3_HDMI_SIM:
		case ADV7481_IP_CVBS_4_HDMI_SIM:
		case ADV7481_IP_CVBS_5_HDMI_SIM:
		case ADV7481_IP_CVBS_6_HDMI_SIM:
		case ADV7481_IP_CVBS_7_HDMI_SIM:
		case ADV7481_IP_CVBS_8_HDMI_SIM:
		case ADV7481_IP_HDMI:
			val = 0x00;
			break;
		case ADV7481_IP_TTL:
			val = 0x1;
			break;
		default:
			ret = -EINVAL;
		}
		temp = adv7481_rd_byte(state->client, 0x00);
		temp |= val;
		adv7481_wr_byte(state->client, 0x00, temp);
		state->csia_src = input;
		break;
	case ADV7481_OP_CSIB:
		if (input != ADV7481_IP_HDMI && input != ADV7481_IP_TTL)
			state->csib_src = input;
		else
			ret = -EINVAL;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static u32 ba_inp_to_adv7481(u32 input)
{
	u32 adv_input = ADV7481_IP_HDMI;

	switch (input) {
	case BA_IP_CVBS_0:
		adv_input = ADV7481_IP_CVBS_1;
		break;
	case BA_IP_CVBS_1:
		adv_input = ADV7481_IP_CVBS_2;
		break;
	case BA_IP_CVBS_2:
		adv_input = ADV7481_IP_CVBS_3;
		break;
	case BA_IP_CVBS_3:
		adv_input = ADV7481_IP_CVBS_4;
		break;
	case BA_IP_CVBS_4:
		adv_input = ADV7481_IP_CVBS_5;
		break;
	case BA_IP_CVBS_5:
		adv_input = ADV7481_IP_CVBS_6;
		break;
	case BA_IP_HDMI_1:
		adv_input = ADV7481_IP_HDMI;
		break;
	case BA_IP_MHL_1:
		adv_input = ADV7481_IP_HDMI;
		break;
	case BA_IP_TTL:
		adv_input = ADV7481_IP_TTL;
		break;
	default:
		adv_input = ADV7481_IP_HDMI;
		break;
	}
	return adv_input;
}

static int adv7481_s_routing(struct v4l2_subdev *sd, u32 input,
				u32 output, u32 config)
{
	int adv_input = ba_inp_to_adv7481(input);
	struct adv7481_state *state = to_state(sd);
	int ret = mutex_lock_interruptible(&state->mutex);

	if (ret)
		return ret;

	ret = adv7481_set_op_src(state, output, adv_input);
	if (ret) {
		pr_err("Output SRC Routing Error: %d\n", ret);
		goto unlock_exit;
	}

	if (state->mode != adv_input) {
		ret = adv7481_set_ip_mode(state, adv_input);
		if (ret)
			pr_err("Set input mode failed: %d\n", ret);
		else
			state->mode = adv_input;
	}

unlock_exit:
	mutex_unlock(&state->mutex);

	return ret;
}

static int adv7481_get_hdmi_timings(struct adv7481_state *state,
				struct adv7481_vid_params *vid_params,
				struct adv7481_hdmi_params *hdmi_params)
{
	int ret = 0;
	int temp1 = 0;
	int temp2 = 0;
	int fieldfactor = 0;
	uint32_t count = 0;

	/* Check TMDS PLL Lock and Frequency */
	temp1 = adv7481_rd_byte(state->i2c_hdmi, HDMI_REG_HDMI_PARAM4_ADDR);
	hdmi_params->pll_lock = ADV_REG_GETFIELD(temp1,
				HDMI_REG_TMDS_PLL_LOCKED);
	if (hdmi_params->pll_lock) {
		temp1 = adv7481_rd_byte(state->i2c_hdmi,
				HDMI_REG_TMDS_FREQ_ADDR);
		temp2 = adv7481_rd_byte(state->i2c_hdmi,
				HDMI_REG_TMDS_FREQ_FRAC_ADDR);
		hdmi_params->tmds_freq = ADV_REG_GETFIELD(temp1,
				HDMI_REG_TMDS_FREQ);
		hdmi_params->tmds_freq = (hdmi_params->tmds_freq << 1)
				+ ADV_REG_GETFIELD(temp2,
				HDMI_REG_TMDS_FREQ_0);
		hdmi_params->tmds_freq += ADV_REG_GETFIELD(temp2,
				HDMI_REG_TMDS_FREQ_FRAC)/128;
	} else {
		return -EBUSY;
	}

	/* Check Timing Lock IO Map Status3:0x71[0] */
	do {
		temp1 = adv7481_rd_byte(state->client,
				IO_HDMI_LVL_RAW_STATUS_3_ADDR);

		if (ADV_REG_GETFIELD(temp1, IO_DE_REGEN_LCK_RAW))
			break;
		count++;
		usleep_range(LOCK_MIN_SLEEP, LOCK_MAX_SLEEP);
	} while (count < LOCK_NUM_TRIES);

	if (count >= LOCK_NUM_TRIES) {
		pr_err("%s(%d), adv7481 HDMI DE regeneration block NOT Locked: 0x%x",
				__func__, __LINE__, temp1);
	}

	/* Check Timing Lock HDMI Map V:0x07[7], H:0x7[5] */
	do {
		temp1 = adv7481_rd_byte(state->i2c_hdmi,
				HDMI_REG_LINE_WIDTH_1_ADDR);

		if (ADV_REG_GETFIELD(temp1, HDMI_VERT_FILTER_LOCKED) &&
			ADV_REG_GETFIELD(temp1, HDMI_DE_REGEN_FILTER_LCK)) {
			break;
		}
		count++;
		usleep_range(LOCK_MIN_SLEEP, LOCK_MAX_SLEEP);
	} while (count < LOCK_NUM_TRIES);

	if (count >= LOCK_NUM_TRIES) {
		pr_err("%s(%d), adv7481 HDMI DE filter NOT Locked: 0x%x",
				__func__, __LINE__, temp1);
	}

	/* Check HDMI Parameters */
	temp1 = adv7481_rd_byte(state->i2c_hdmi, HDMI_REG_FIELD1_HEIGHT1_ADDR);
	hdmi_params->color_depth = ADV_REG_GETFIELD(temp1,
				HDMI_REG_DEEP_COLOR_MODE);
	temp1 = adv7481_rd_byte(state->i2c_hdmi, HDMI_REG_HDMI_PARAM5_ADDR);
	hdmi_params->pix_rep = ADV_REG_GETFIELD(temp1,
				HDMI_REG_PIXEL_REPETITION);

	/* Check Interlaced and Field Factor */
	vid_params->intrlcd = ADV_REG_GETFIELD(temp1,
				HDMI_REG_DVI_HSYNC_POLARITY);
	fieldfactor = (vid_params->intrlcd == 1) ? 2 : 1;

	/* Get Active Timing Data HDMI Map  H:0x07[4:0] + 0x08[7:0] */
	temp1 = adv7481_rd_byte(state->i2c_hdmi, HDMI_REG_LINE_WIDTH_1_ADDR);
	temp2 = adv7481_rd_byte(state->i2c_hdmi, HDMI_REG_LINE_WIDTH_2_ADDR);
	vid_params->act_pix = (((ADV_REG_GETFIELD(temp1,
			HDMI_REG_LINE_WIDTH_1) << 8) & 0x1F00) |
				ADV_REG_GETFIELD(temp2,
					HDMI_REG_LINE_WIDTH_2));

	/* Get Total Timing Data HDMI Map  H:0x1E[5:0] + 0x1F[7:0] */
	temp1 = adv7481_rd_byte(state->i2c_hdmi,
				HDMI_REG_TOTAL_LINE_WIDTH_1_ADDR);
	temp2 = adv7481_rd_byte(state->i2c_hdmi,
				HDMI_REG_TOTAL_LINE_WIDTH_2_ADDR);
	vid_params->tot_pix = (((ADV_REG_GETFIELD(temp1,
			HDMI_REG_TOTAL_LINE_WIDTH_1) << 8) & 0x3F00) |
				ADV_REG_GETFIELD(temp2,
					HDMI_REG_TOTAL_LINE_WIDTH_2));

	/* Get Active Timing Data HDMI Map  V:0x09[4:0] + 0x0A[7:0] */
	temp1 = adv7481_rd_byte(state->i2c_hdmi,
				HDMI_REG_FIELD0_HEIGHT_1_ADDR);
	temp2 = adv7481_rd_byte(state->i2c_hdmi,
				HDMI_REG_FIELD0_HEIGHT_2_ADDR);
	vid_params->act_lines = (((ADV_REG_GETFIELD(temp1,
			HDMI_REG_FIELD0_HEIGHT_1) << 8) & 0x3F00) |
				ADV_REG_GETFIELD(temp2,
					HDMI_REG_FIELD0_HEIGHT_2));

	/* Get Total Timing Data HDMI Map  V:0x26[5:0] + 0x27[7:0] */
	temp1 = adv7481_rd_byte(state->i2c_hdmi,
				HDMI_REG_FIELD0_TOTAL_HEIGHT_1_ADDR);
	temp2 = adv7481_rd_byte(state->i2c_hdmi,
				HDMI_REG_FIELD0_TOTAL_HEIGHT_2_ADDR);
	vid_params->tot_lines = (((ADV_REG_GETFIELD(temp1,
			HDMI_REG_FIELD0_TOT_HEIGHT_1) << 8) & 0x3F00) |
				ADV_REG_GETFIELD(temp2,
					HDMI_REG_FIELD0_TOT_HEIGHT_2));

	switch (hdmi_params->color_depth) {
	case CD_10BIT:
		vid_params->pix_clk =  ((vid_params->pix_clk*4)/5);
		break;
	case CD_12BIT:
		vid_params->pix_clk = ((vid_params->pix_clk*2)/3);
		break;
	case CD_16BIT:
		vid_params->pix_clk = (vid_params->pix_clk/2);
		break;
	case CD_8BIT:
	default:
		vid_params->pix_clk /= 1;
		break;
	}

	if ((vid_params->tot_pix != 0) && (vid_params->tot_lines != 0)) {
		vid_params->fr_rate = vid_params->pix_clk * fieldfactor
						/ vid_params->tot_lines;
		vid_params->fr_rate /= vid_params->tot_pix;
		vid_params->fr_rate /= (hdmi_params->pix_rep + 1);
	}

	pr_debug("%s(%d), adv7481 TMDS Resolution: %d : %d @ %d\n",
			__func__, __LINE__,
			vid_params->act_lines, vid_params->act_pix,
			vid_params->fr_rate);
	return ret;
}

static int adv7481_query_dv_timings(struct v4l2_subdev *sd,
			struct v4l2_dv_timings *timings)
{
	int ret;
	struct adv7481_state *state = to_state(sd);
	struct adv7481_vid_params vid_params;
	struct adv7481_hdmi_params hdmi_params;
	struct v4l2_bt_timings *bt_timings = &timings->bt;

	if (!timings)
		return -EINVAL;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	memset(&vid_params, 0, sizeof(struct adv7481_vid_params));
	memset(&hdmi_params, 0, sizeof(struct adv7481_hdmi_params));

	switch (state->mode) {
	case ADV7481_IP_HDMI:
	case ADV7481_IP_CVBS_1_HDMI_SIM:
		adv7481_get_hdmi_timings(state, &vid_params, &hdmi_params);
		timings->type = V4L2_DV_BT_656_1120;
		bt_timings->width = vid_params.act_pix;
		bt_timings->height = vid_params.act_lines;
		bt_timings->pixelclock = vid_params.pix_clk;
		bt_timings->interlaced = vid_params.intrlcd ?
				V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;
		if (bt_timings->interlaced == V4L2_DV_INTERLACED)
			bt_timings->height /= 2;
		break;
	default:
		return -EINVAL;
	}
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7481_query_sd_std(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	int ret = 0;
	int temp = 0;
	struct adv7481_state *state = to_state(sd);
	int tStatus = 0x0;

	tStatus = adv7481_rd_byte(state->i2c_sdp, SDP_VDEC_LOCK);
	if (!(tStatus & 0x1))
		pr_err("SIGNAL NOT LOCKED\n");

	if (!std)
		return -EINVAL;

	switch (state->mode) {
	case ADV7481_IP_CVBS_1:
	case ADV7481_IP_CVBS_2:
	case ADV7481_IP_CVBS_3:
	case ADV7481_IP_CVBS_4:
	case ADV7481_IP_CVBS_5:
	case ADV7481_IP_CVBS_6:
	case ADV7481_IP_CVBS_7:
	case ADV7481_IP_CVBS_8:
	case ADV7481_IP_CVBS_1_HDMI_SIM:
	case ADV7481_IP_CVBS_2_HDMI_SIM:
	case ADV7481_IP_CVBS_3_HDMI_SIM:
	case ADV7481_IP_CVBS_4_HDMI_SIM:
	case ADV7481_IP_CVBS_5_HDMI_SIM:
	case ADV7481_IP_CVBS_6_HDMI_SIM:
	case ADV7481_IP_CVBS_7_HDMI_SIM:
	case ADV7481_IP_CVBS_8_HDMI_SIM:
		ret = adv7481_get_sd_timings(state, &temp);
		break;
	default:
		return -EINVAL;
	}

	if (!tStatus)
		*std = (v4l2_std_id) temp;
	else
		*std = V4L2_STD_UNKNOWN;

	return ret;
}

static int adv7481_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *interval)
{
	interval->interval.numerator = 1;
	interval->interval.denominator = 60;

	return 0;
}

static int adv7481_g_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	int ret;
	struct adv7481_vid_params vid_params;
	struct adv7481_hdmi_params hdmi_params;
	struct adv7481_state *state = to_state(sd);

	if (!fmt)
		return -EINVAL;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	memset(&vid_params, 0, sizeof(struct adv7481_vid_params));
	memset(&hdmi_params, 0, sizeof(struct adv7481_hdmi_params));

	switch (state->mode) {
	case ADV7481_IP_HDMI:
	case ADV7481_IP_CVBS_1_HDMI_SIM:
		adv7481_get_hdmi_timings(state, &vid_params, &hdmi_params);
		fmt->width = vid_params.act_pix;
		fmt->height = vid_params.act_lines;
		if (vid_params.intrlcd)
			fmt->height /= 2;
		break;
	default:
		return -EINVAL;
	}
	mutex_unlock(&state->mutex);
	fmt->code = V4L2_MBUS_FMT_YUYV8_2X8;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	return ret;
}

static int adv7481_csi_powerup(struct adv7481_state *state,
		enum adv7481_output output)
{
	int ret;
	struct i2c_client *csi_map;
	uint8_t val = 0;
	uint8_t csi_sel = 0;

	/* Select CSI TX to configure data */
	if (output == ADV7481_OP_CSIA) {
		csi_sel = ADV_REG_SETFIELD(1, IO_CTRL_CSI4_EN) |
			ADV_REG_SETFIELD(1, IO_CTRL_PIX_OUT_EN) |
			ADV_REG_SETFIELD(0, IO_CTRL_CSI4_IN_SEL);
		csi_map = state->i2c_csi_txa;
	} else if (output == ADV7481_OP_CSIB) {
		/* Enable 1-Lane MIPI Tx, enable pixel output and
		 * route SD through Pixel port
		 */
		csi_sel = ADV_REG_SETFIELD(1, IO_CTRL_CSI1_EN) |
			ADV_REG_SETFIELD(1, IO_CTRL_PIX_OUT_EN) |
			ADV_REG_SETFIELD(1, IO_CTRL_SD_THRU_PIX_OUT) |
			ADV_REG_SETFIELD(0, IO_CTRL_CSI4_IN_SEL);
		csi_map = state->i2c_csi_txb;
	} else if (output == ADV7481_OP_TTL) {
		/* For now use TxA */
		csi_map = state->i2c_csi_txa;
	} else {
		/* Default to TxA */
		csi_map = state->i2c_csi_txa;
	}

	/* TXA MIPI lane settings for CSI */
	/* CSI TxA: # Lane : Power Off */
	val = ADV_REG_SETFIELD(1, CSI_CTRL_TX_PWRDN) |
			ADV_REG_SETFIELD(state->tx_lanes, CSI_CTRL_NUM_LANES);
	ret = adv7481_wr_byte(csi_map, CSI_REG_TX_CFG1_ADDR, val);
	/* Enable Tx A/B CSI #-lane */
	ret |= adv7481_wr_byte(state->client,
			IO_REG_CSI_PIX_EN_SEL_ADDR, csi_sel);
	/* CSI TxA: Auto D-PHY Timing */
	val |= ADV_REG_SETFIELD(1, CSI_CTRL_AUTO_PARAMS);
	ret |= adv7481_wr_byte(csi_map, CSI_REG_TX_CFG1_ADDR, val);

	/* DPHY and CSI Tx A */
	ret |= adv7481_wr_byte(csi_map, 0xd6, 0x07);
	ret |= adv7481_wr_byte(csi_map, 0xc4, 0x0a);
	ret |= adv7481_wr_byte(csi_map, 0x71, 0x33);
	ret |= adv7481_wr_byte(csi_map, 0x72, 0x11);
	/* CSI TxA: power up DPHY */
	ret |= adv7481_wr_byte(csi_map, 0xf0, 0x00);
	/* ADI Required Write */
	ret |= adv7481_wr_byte(csi_map, 0x31, 0x82);
	ret |= adv7481_wr_byte(csi_map, 0x1e, 0x40);
	/* adi Recommended power up sequence */
	/* DPHY and CSI Tx A Power up Sequence */
	/* CSI TxA: MIPI PLL EN */
	ret |= adv7481_wr_byte(csi_map, 0xda, 0x01);
	msleep(200);
	/* CSI TxA: # MIPI Lane : Power ON */
	val = ADV_REG_SETFIELD(0, CSI_CTRL_TX_PWRDN) |
			ADV_REG_SETFIELD(1, CSI_CTRL_AUTO_PARAMS) |
			ADV_REG_SETFIELD(state->tx_lanes, CSI_CTRL_NUM_LANES);
	ret |= adv7481_wr_byte(csi_map, CSI_REG_TX_CFG1_ADDR, val);
	msleep(100);
	/* ADI Required Write */
	ret |= adv7481_wr_byte(csi_map, 0xc1, 0x2b);
	msleep(100);
	/* ADI Required Write */
	ret |= adv7481_wr_byte(csi_map, 0x31, 0x80);

	return ret;
}

static int adv7481_set_op_stream(struct adv7481_state *state, bool on)
{
	int ret;

	if (on && state->csia_src != ADV7481_IP_NONE)
		if (ADV7481_IP_HDMI == state->csia_src) {
			state->tx_lanes = ADV7481_MIPI_2LANE;
			ret = adv7481_csi_powerup(state, ADV7481_OP_CSIA);
		} else {
			state->tx_lanes = ADV7481_MIPI_1LANE;
			ret = adv7481_csi_powerup(state, ADV7481_OP_CSIA);
		}
	else if (on && state->csib_src != ADV7481_IP_NONE) {
		/* CSI Tx B is always 1 lane */
		state->tx_lanes = ADV7481_MIPI_1LANE;
		ret = adv7481_csi_powerup(state, ADV7481_OP_CSIB);
	} else {
		/* Default to 1 lane */
		state->tx_lanes = ADV7481_MIPI_1LANE;
		ret = adv7481_csi_powerup(state, ADV7481_OP_CSIA);
	}

	return ret;
}

static int adv7481_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	int ret = 0;
	struct adv7481_state *state = to_state(sd);
	int signal_status = 0;

	signal_status = adv7481_rd_byte(state->i2c_sdp, SDP_VDEC_LOCK);
	if (!(signal_status & 0x1)) {
		pr_err("SIGNAL NOT LOCKED\n");
		*status |= V4L2_IN_ST_NO_SIGNAL;
	}
	return ret;
}

static int adv7481_s_stream(struct v4l2_subdev *sd, int on)
{
	struct adv7481_state *state = to_state(sd);
	int ret;

	ret = adv7481_set_op_stream(state, on);
	return ret;
}

static const struct v4l2_subdev_video_ops adv7481_video_ops = {
	.s_routing = adv7481_s_routing,
	.g_frame_interval = adv7481_g_frame_interval,
	.g_mbus_fmt = adv7481_g_mbus_fmt,
	.querystd = adv7481_query_sd_std,
	.g_dv_timings = adv7481_query_dv_timings,
	.g_input_status = adv7481_g_input_status,
	.s_stream = adv7481_s_stream,
};

static const struct v4l2_subdev_core_ops adv7481_core_ops = {
	.s_power = adv7481_s_power,
};

static const struct v4l2_ctrl_ops adv7481_ctrl_ops = {
	.s_ctrl = adv7481_s_ctrl,
};

static const struct v4l2_subdev_ops adv7481_ops = {
	.core = &adv7481_core_ops,
	.video = &adv7481_video_ops,
};

static int adv7481_init_v4l2_controls(struct adv7481_state *state)
{
	v4l2_ctrl_handler_init(&state->ctrl_hdl, 4);

	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7481_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, -128, 127, 1, 0);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7481_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7481_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7481_ctrl_ops,
			  V4L2_CID_HUE, -127, 128, 1, 0);

	state->sd.ctrl_handler = &state->ctrl_hdl;
	if (state->ctrl_hdl.error) {
		int err = state->ctrl_hdl.error;

		v4l2_ctrl_handler_free(&state->ctrl_hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&state->ctrl_hdl);

	return 0;
}

static int adv7481_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adv7481_state *state;
	struct adv7481_platform_data *pdata = NULL;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;
	int ret;

	pr_debug("Attempting to probe...\n");
	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		pr_err("%s %s Check i2c Functionality Fail\n",
				__func__, client->name);
		ret = -EIO;
		goto err;
	}
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			 client->addr, client->adapter->name);

	/* Create 7481 State */
	state = devm_kzalloc(&client->dev,
				sizeof(struct adv7481_state), GFP_KERNEL);
	if (state == NULL) {
		ret = -ENOMEM;
		pr_err("Check Kzalloc Fail\n");
		goto err_mem;
	}
	state->client = client;
	mutex_init(&state->mutex);

	/* Get and Check Platform Data */
	pdata = (struct adv7481_platform_data *) client->dev.platform_data;
	if (!pdata) {
		ret = -ENOMEM;
		pr_err("Getting Platform data failed\n");
		goto err_mem;
	}

	/* Configure and Register V4L2 I2C Sub-device */
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &adv7481_ops);
	state->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	state->sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS;

	/* Register as Media Entity */
	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	state->sd.entity.flags |= MEDIA_ENT_T_V4L2_SUBDEV;
	ret = media_entity_init(&state->sd.entity, 1, &state->pad, 0);
	if (ret) {
		ret = -EIO;
		pr_err("Media entity init failed\n");
		goto err_media_entity;
	}

	/* Initialize HW Config */
	ret |= adv7481_hw_init(pdata, state);
	if (ret) {
		ret = -EIO;
		pr_err("HW Initialisation Failed\n");
		goto err_media_entity;
	}

	/* Register V4l2 Control Functions */
	hdl = &state->ctrl_hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	adv7481_init_v4l2_controls(state);

	/* Initials ADV7481 State Settings */
	state->tx_auto_params = ADV7481_AUTO_PARAMS;
	state->tx_lanes = ADV7481_MIPI_1LANE;

	/* Initialize SW Init Settings and I2C sub maps 7481 */
	ret |= adv7481_dev_init(state, client);
	if (ret) {
		ret = -EIO;
		pr_err("SW Initialisation Failed\n");
		goto err_media_entity;
	}

	/* Set cvbs settings */
	ret |= adv7481_set_cvbs_mode(state);

	/* BA registration */
	ret |= msm_ba_register_subdev_node(sd);
	if (ret) {
		ret = -EIO;
		pr_err("BA INIT FAILED\n");
		goto err_media_entity;
	}
	pr_debug("Probe successful!\n");

	return ret;

err_media_entity:
	media_entity_cleanup(&sd->entity);
err_mem:
	kfree(state);
err:
	if (!ret)
		ret = 1;
	return ret;
}

static int adv7481_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7481_state *state = to_state(sd);

	msm_ba_unregister_subdev_node(sd);
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);

	v4l2_ctrl_handler_free(&state->ctrl_hdl);

	i2c_unregister_device(state->i2c_csi_txa);
	i2c_unregister_device(state->i2c_csi_txb);
	i2c_unregister_device(state->i2c_hdmi);
	i2c_unregister_device(state->i2c_edid);
	i2c_unregister_device(state->i2c_cp);
	i2c_unregister_device(state->i2c_sdp);
	i2c_unregister_device(state->i2c_rep);
	mutex_destroy(&state->mutex);
	kfree(state);

	return 0;
}

static const struct i2c_device_id adv7481_id[] = {
	{ DRIVER_NAME, 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, adv7481_id);


static struct i2c_driver adv7481_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = KBUILD_MODNAME,
	},
	.probe = adv7481_probe,
	.remove = adv7481_remove,
	.id_table = adv7481_id,
};

module_i2c_driver(adv7481_driver);

MODULE_DESCRIPTION("ADI ADV7481 HDMI/MHL/SD video receiver");
