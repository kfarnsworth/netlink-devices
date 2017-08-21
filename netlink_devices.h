/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * netlink devices monitors address and link state changes
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
 * @file	netlink_devices.h
 * @brief	Interface to netlink messages from the kernel via libnl.
 *
 */


#ifndef NETLINK_DEVICES_H_
#define NETLINK_DEVICES_H_

/**
 * @brief	netlink event identifiers
*/
enum {
	NETLINKDEV_EVENT_ADDR,		/**< interface address change event */
	NETLINKDEV_EVENT_LINK		/**< interface link status change event */
};

/**
 * @brief	network interface data
*/
struct netlinkdev_data {
	int		status;		/**< status of the interface reported as IFF_UP or IFF_DOWN */
	int		if_index;	/**< interface index */
	unsigned char	link_addr[6];	/**< interface link address */
	int		net_family;	/**< network family of the interface as AF_INET or AF_INET6 */
	int		net_len;	/**< network address length */
	char 		net_addr[128];	/**< network address */
};

/**
 * @brief	netlink context structure
*/
struct netlinkdev_info {
	struct nl_sock		*socket;	/**< cache managment netlink socket  */
	struct nl_cache_mngr	*mngr;		/**< cache manager context */
	struct nl_cache		*links;		/**< link cache */
	struct nl_cache		*addrs;		/**< address cache info */
	void 			(*event)(int, struct netlinkdev_data *, void *);	/**< installed event callback */
	void			*context;	/**< caller context reported back to caller */
};

int netlinkdev_start(struct netlinkdev_info *nl,
		     void (*netlink_event_cb)(int, struct netlinkdev_data *, void *),
		     void *caller_context);
int netlinkdev_stop(struct netlinkdev_info *nl);
int netlinkdev_poll(struct netlinkdev_info *nl);
int netlinkdev_getnet(struct netlinkdev_info *nl,
		      char *if_name, 
		      struct netlinkdev_data *nd);

#endif

