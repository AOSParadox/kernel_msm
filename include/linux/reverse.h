/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
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

#ifndef __LINUX_REVERSE_H__
#define __LINUX_REVERSE_H__

#include <linux/switch.h>

struct reverse_switch_platform_data {
	const char	*name;
	unsigned	gpio;
	unsigned int	key_code;
	unsigned	debounce_time;	/* ms */

	/* if NULL, switch_dev.name will be printed */
	const char *name_on;
	const char *name_off;
	/* if NULL, "0" or "1" will be printed */
	const char *state_on;
	const char *state_off;
};

extern int switch_dev_register(struct switch_dev *sdev);

enum reverse_level {
	CAMERA,
	DISPLAY_1,
	DISPLAY_2,
};

struct reverse_struct {
	int level;
	int (*enter_handler) (struct reverse_struct *h);
	int (*exit_handler) (struct reverse_struct *h);
};

#endif /* __LINUX_REVERSE_H__ */
