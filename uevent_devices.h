/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * uevent devices parsing using libnl
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2014 Farnsworth Technology, Inc.
 */

/**
 * @file	uevent_devices.h
 * @brief	Interface to linbl to get uevent device events.
 *
 */


#ifndef UEVENT_DEVICES_H_
#define UEVENT_DEVICES_H_

/**
 * @brief	represents the interface info for the hotplug event being reported
*/
struct ueventdev_data {
#define UEVENTDEV_ACTION_ADD	1
#define UEVENTDEV_ACTION_REMOVE	2
	int		action;		/**< change action that just occured for the device */
	char		devname[50];	/**< interface device name */
};

/**
 * @brief	holds uevent information
*/
struct ueventdev_info {
	struct nl_sock		*socket;	/**< uevent netlink socket */
	struct nl_cb		*cb;		/**< callback for reporting uevents */

	void 			(*event)(struct ueventdev_data *, void *);	/**< installed event callback */
	void			*context;	/**< caller context reported back to caller */
};

int ueventdev_start(struct ueventdev_info *ul,
		    void (*ueventdev_cb)(struct ueventdev_data *, void *),
		    void *caller_context);
int ueventdev_stop(struct ueventdev_info *ul);
int ueventdev_poll(struct ueventdev_info *ul);

#endif

