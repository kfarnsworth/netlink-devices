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
 * Copyright (C) 2014-2015 Farnsworth Technology, Inc.
 */

/**
 * @file	netlink_devices.c
 * @brief	Interface to netlink messages from the kernel via libnl.
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <sys/param.h>

#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/cache.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>
#include <netlink/msg.h>
#include <netlink/object-api.h>

#include "netlink_logs.h"
#include "netlink_devices.h"

/* Need this to access struct nl_msgtype and struct nl_object */
#include <netlink-private/cache-api.h>
#include <netlink-private/object-api.h>

/**
 * @brief	Used for callback during interface address status searches.
 */
struct netlinkdev_findaddrinfo {
	int index;
	struct rtnl_addr *myaddr;
};

/**
 * @brief	The callback pointer, takes nl_objects as args so we can spot the difference on changes.
 */
typedef void (*diff_func_t)(struct nl_cache *, struct nl_object *, struct nl_object *, int, void *);
 
 
static void netlinkdev_changecb(struct nl_cache *cache, struct nl_object *old,
				  struct nl_object *obj, int action, void *arg);

/**
 * @brief	The function formaly known as libnl change_include 
 * 		the cb is casted to type diff_func_cb 
 * 		merging objects is a no-go, as we would loose the information 
 * 		of "old" before calling the callback.
 * @param[in]	cache		netlink cache
 * @param[in]	obj		object data of netlink interface
 * @param[in]	type		netlink message type which conains the action
 * @param[in]	cb		difference compare function
 * @param[in]	data		pointer to data handled by real callback
 * @return	always returns 0
 */
static int netlinkdev_diffinclude(struct nl_cache *cache, struct nl_object *obj, 
				    struct nl_msgtype *type, diff_func_t cb, void *data)
{
	struct nl_object *old;
 
	switch( type->mt_act )
	{
		case NL_ACT_NEW:
		case NL_ACT_DEL:
			old = nl_cache_search(cache, obj);
			if( old )
			{
				nl_cache_remove(old);
				if (type->mt_act == NL_ACT_DEL)
				{
					if (cb)
						cb(cache, old, obj, NL_ACT_DEL, data);
					nl_object_put(old);
				}
			}
			if (type->mt_act == NL_ACT_NEW)
			{		
				nl_cache_add(cache, obj);
				if (old == NULL && cb)
					cb(cache, old, obj, NL_ACT_NEW, data);
				else if (old)
				{
					if (nl_object_diff(old, obj) && cb)
						cb(cache, old, obj, NL_ACT_CHANGE, data);
 
					nl_object_put(old);
				}
			}
			break;
	}
	return 0;
}


/**
 * @brief	The custom function required to call the custom change_include.
 * 		There are no changes compared to libnls nl_cache_include beside the 
 * 		replacement of cache_include with netlinkdev_diffinclude.
 * @param[in]	cache		netlink cache
 * @param[in]	obj		object data of netlink interface
 * @param[in]	change_cb	real callback function to be called
 * @param[in]	data		pointer to data handled by real callback
 * @return	nothing
 */
static int netlinkdev_nlcacheinclude(struct nl_cache *cache, struct nl_object *obj,
					 change_func_t change_cb, void *data)
{
	struct nl_cache_ops *ops = nl_cache_get_ops(cache);
	int i;
 
	if (ops->co_obj_ops != obj->ce_ops)
		return -NLE_OBJ_MISMATCH;
 
	for (i = 0; ops->co_msgtypes[i].mt_id >= 0; i++)
		if (ops->co_msgtypes[i].mt_id == obj->ce_msgtype)
			return netlinkdev_diffinclude(cache, obj, &ops->co_msgtypes[i],
							 (diff_func_t)netlinkdev_changecb, data);
 
	return -NLE_MSGTYPE_NOSUPPORT;
}

/**
 * @brief	executes the event callback for the interface address changes
 * @param[in]	obj		object data of netlink interface that had the change
 * @param[in]	arg		pointer to netlink context
 * @return	nothing
 */
static void netlinkdev_actionaddrcb(struct nl_object *obj, void *arg)
{
	struct netlinkdev_info *nl = (struct netlinkdev_info *)arg;
	struct rtnl_addr *addr = (struct rtnl_addr *)obj;
	struct netlinkdev_data nd;
	struct rtnl_link *link;

	memset(&nd, 0, sizeof(nd));

	if (addr) {
		nd.if_index = rtnl_addr_get_ifindex(addr);
	        link = rtnl_link_get(nl->links, nd.if_index);
		if (link)
			nd.status = rtnl_link_get_flags(link);
		nd.net_family = nl_addr_get_family(rtnl_addr_get_local(addr));
		nd.net_len = nl_addr_get_len(rtnl_addr_get_local(addr));
		if (nd.net_len > sizeof(nd.net_addr))
			nd.net_len = sizeof(nd.net_addr);
		memcpy(nd.net_addr, nl_addr_get_binary_addr(rtnl_addr_get_local(addr)),  nd.net_len);
	}

	if (nl && nl->event)
		nl->event (NETLINKDEV_EVENT_ADDR, &nd, nl->context);
}

/**
 * @brief	executes the event callback for the link change event
 * @param[in]	obj		object data of netlink interface that had the change
 * @param[in]	arg		pointer to netlink context
 * @return	nothing
 */
static void netlinkdev_actionlinkcb(struct nl_object *obj, void *arg)
{
	struct netlinkdev_info *nl = (struct netlinkdev_info *)arg;
	struct rtnl_link *link = (struct rtnl_link *)obj;
	struct nl_addr *linkaddr;
	struct netlinkdev_data nd;

	memset(&nd, 0, sizeof(nd));
	if (link) {
		linkaddr=rtnl_link_get_addr(link);
		if (linkaddr) {
			memset(nd.link_addr, 0, sizeof(nd.link_addr));
			memcpy(nd.link_addr, nl_addr_get_binary_addr(linkaddr), MIN(nl_addr_get_len(linkaddr),sizeof(nd.link_addr)));
		}
		nd.status = rtnl_link_get_flags(link);
		nd.if_index = rtnl_link_get_ifindex(link);
	}

	if (nl && nl->event)
		nl->event (NETLINKDEV_EVENT_LINK, &nd, nl->context);
}


/**
 * @brief	Called when link has changed from up/down to down/up 
 * @param[in]	nl		pointer to netlink context
 * @param[in]	old		old object to compare
 * @param[in]	obj		object being udpated
 * @param[in]	action		action being commited
 * @return	nothing
 */
static void netlinkdev_changelinkcb(struct netlinkdev_info *nl, struct nl_object *_old, struct nl_object *obj, int action)
{
	struct rtnl_link *link = (struct rtnl_link *)obj;
	struct rtnl_link *old = (struct rtnl_link *)_old;
	struct rtnl_addr *filter = rtnl_addr_alloc();

	if (filter) rtnl_addr_set_ifindex(filter, rtnl_link_get_ifindex(link));

	switch( action )
	{
		case NL_ACT_NEW:
			if (LOG_DETAILS) NL_LOG(NLLOG_DEBUG, "link: NEW");
			/* fall through */
		case NL_ACT_CHANGE:
			if (LOG_DETAILS) if (action == NL_ACT_CHANGE) NL_LOG(NLLOG_DEBUG, "link: CHG");

			if ( (rtnl_link_get_flags(link) & IFF_UP) != (old?(rtnl_link_get_flags(old) & IFF_UP):0) )
			{
				nl_cache_foreach_filter(nl->addrs, (struct nl_object *)filter, netlinkdev_actionaddrcb, nl);
				netlinkdev_actionlinkcb(obj, nl);
			}
			break;
		case NL_ACT_DEL:
			if (LOG_DETAILS) NL_LOG(NLLOG_DEBUG, "link: DEL");
			nl_cache_foreach_filter(nl->addrs, (struct nl_object *)filter, netlinkdev_actionaddrcb, nl);
			netlinkdev_actionlinkcb(obj, nl);
			break;
	}
	if (filter) rtnl_addr_put(filter);
}


/**
 * @brief	Called from callback when address changes
 * @param[in]	nl		netlink context
 * @param[in]	old		old object to compare
 * @param[in]	obj		object being udpated
 * @param[in]	action		action being commited
 * @return	nothing
 */
static void netlinkdev_changeaddrcb(struct netlinkdev_info *nl, struct nl_object *old, struct nl_object *obj, int action)
{
	/* if the interface associated with the address is down, we got nothing to do */
	struct rtnl_link *link = rtnl_link_get(nl->links, rtnl_addr_get_ifindex((struct rtnl_addr *)obj));

	if (!link)
		return;

	if ((rtnl_link_get_flags(link) & IFF_UP) != 0)
	{ 
		switch( action )
		{
			case NL_ACT_NEW:
				if (LOG_DETAILS) NL_LOG(NLLOG_DEBUG, "addr: NEW");
				netlinkdev_actionaddrcb(obj, nl);
				break;
			case NL_ACT_CHANGE:
				if (LOG_DETAILS) NL_LOG(NLLOG_DEBUG, "addr: CHG");
				netlinkdev_actionaddrcb(obj, nl);
				break;
			case NL_ACT_DEL:
				if (LOG_DETAILS) NL_LOG(NLLOG_DEBUG, "addr: DEL");
				netlinkdev_actionaddrcb(obj, nl);
				break;
		}
	}
	rtnl_link_put(link);
}


/**
 * @brief	This is the callback called by the nl_cache_mngr which
 * 		dispatches the call to the appropriate function depending on 
 * 		the type of obj.
 * @param[in]	cache		netlink cache
 * @param[in]	old		old object to compare
 * @param[in]	obj		object being udpated
 * @param[in]	arg		change context passed onto approriate callback
 * @return	nothing
 */
static void netlinkdev_changecb(struct nl_cache *cache, struct nl_object *old,
				  struct nl_object *obj, int action, void *arg)
{
	struct netlinkdev_info *nl = (struct netlinkdev_info *)arg;

	if( strcmp("route/link", nl_object_get_type(obj)) == 0 )
		netlinkdev_changelinkcb(nl, old, obj, action);
	else if( strcmp("route/addr", nl_object_get_type(obj)) == 0 )
		netlinkdev_changeaddrcb(nl, old, obj, action);
}

/** 
 * @brief	utlity calls change_addr_cb to claim an address as new
 * @param[in]	object		data which represents the netlink object
 * @param[out]	arg		pointer to the netlink find address info structure
 * @return	nothing
 */
static void netlinkdev_bootcache(struct nl_object *obj, void *arg)
{
	struct netlinkdev_info *nl = arg;
	netlinkdev_changeaddrcb(nl, NULL, obj, NL_ACT_NEW);
}

/**
 * @brief	initialize the operations with the netlink library
 * @param[in]	nl		netlink context
 * @return	nothing
 */
static void netlinkdev_opsinit(struct netlinkdev_info *nl)
{
	/* overwrite the co_include_event so we can have both -old and 
	 * new- objects in the change_cb */
	if (nl->links) nl_cache_get_ops(nl->links)->co_include_event = netlinkdev_nlcacheinclude;
	if (nl->addrs) nl_cache_get_ops(nl->addrs)->co_include_event = netlinkdev_nlcacheinclude;

	/* the initial set of addrs is not propagated by the 
	 * nl_cache_mngr, iterate the cache */
	if (nl->addrs) nl_cache_foreach(nl->addrs, netlinkdev_bootcache, nl);

	/* allow sharing of info */
	if (nl->addrs) nl_cache_mngt_provide(nl->addrs);
}

/**
 * @brief	find netlink object address and fill in address
 * @param[in]	object		object data which represents the netlink interface
 * @param[out]	arg		pointer to the netlink find address info structure
 * @return	nothing
 */
static void netlinkdev_getifaddr(struct nl_object *object, void *arg)
{
	struct netlinkdev_findaddrinfo *ptr_find = (struct netlinkdev_findaddrinfo *)arg;
	struct rtnl_addr *addr = (struct rtnl_addr *)object;
	int family, index;

	if (ptr_find->myaddr != NULL)
		return;
	family = rtnl_addr_get_family(addr);
	index = rtnl_addr_get_ifindex(addr);
	if ((index == ptr_find->index) && (family == AF_INET)) {
		ptr_find->myaddr = addr;
	}
}

/**
 * @brief	utility for finding a interface index within the cache
 * @param[in]	c		netlink cache being searched
 * @param[in]	index		interface index
 * @return	pointer to found netlink address structure, NULL if not found
 */
static struct rtnl_addr *netlinkdev_findaddr(struct nl_cache *c, int index)
{
	struct netlinkdev_findaddrinfo find;
	find.myaddr = NULL;
	find.index = index;
	nl_cache_foreach(c, netlinkdev_getifaddr, &find);
	return find.myaddr;
}

/**
 * @brief	Get the status of a specified network interface
 * @param[in]	nl		netlink context
 * @param[in]	if_name		name of the interface
 * @param[out]	nd		netlink data filled in by this function
 * @return	result of get network status
 */
int netlinkdev_getnet(struct netlinkdev_info *nl, char *if_name, 
		      struct netlinkdev_data *nd)
{
	struct rtnl_link *link;
	struct nl_addr *linkaddr;
	struct rtnl_addr *addr;
	int index;

	link = rtnl_link_get_by_name(nl->links, if_name);
	if (!link) 
		return -ENODEV;

	memset(nd, 0, sizeof(struct netlinkdev_data));

	index = rtnl_link_get_ifindex(link);
	nd->if_index = index;
	nd->status = rtnl_link_get_flags(link);
	NL_LOG(NLLOG_DEBUG, "netlink: ifindex:%d status:%s\n", index, nd->status & IFF_UP ? "UP": "DOWN");
	linkaddr=rtnl_link_get_addr(link);
	if (linkaddr) {
		memset(nd->link_addr, 0, sizeof(nd->link_addr));
		memcpy(nd->link_addr, nl_addr_get_binary_addr(linkaddr), MIN(nl_addr_get_len(linkaddr),sizeof(nd->link_addr)));
	 	addr = netlinkdev_findaddr(nl->addrs, index);
		if (addr) {
			nd->net_family = nl_addr_get_family(rtnl_addr_get_local(addr));
			nd->net_len = nl_addr_get_len(rtnl_addr_get_local(addr));
			if (nd->net_len > sizeof(nd->net_addr))
				nd->net_len = sizeof(nd->net_addr);
			memcpy(nd->net_addr, nl_addr_get_binary_addr(rtnl_addr_get_local(addr)),  nd->net_len);
		}
	}
	rtnl_link_put(link);

	return 0;
}

/**
 * @brief	Poll the netlink connection and process any netlink events
 * @param[in]	nl		netlink context
 * @return	always 0 as success
 */
int netlinkdev_poll(struct netlinkdev_info *nl)
{
	nl_cache_mngr_poll(nl->mngr, 1000);
	return 0;
}

/**
 * @brief	Start a connection to netlink interface
 * @param[in]	nl			netlink context
 * @param[in]	netlink_event_cb	callback to report netlink events
 * @param[in]	caller_context		callers context to pass into callback
 * @return	result of start
 */
int netlinkdev_start(struct netlinkdev_info *nl, 
		     void (*netlink_event_cb)(int, struct netlinkdev_data *, void *), 
		     void *caller_context)
{
	int stat;

	memset(nl, 0, sizeof(struct netlinkdev_info));
	nl->socket = nl_socket_alloc();
	if (!nl->socket) {
		NL_LOG(NLLOG_ERROR, "Could not open netlink socket");
		return -ENOMEM;
	}
	nl->event = netlink_event_cb;
	nl->context = caller_context;

	stat = nl_cache_mngr_alloc(nl->socket, NETLINK_ROUTE, 0, &nl->mngr);
	if (stat < 0) {
		nl_socket_free(nl->socket);
		nl->socket = 0;
		NL_LOG(NLLOG_ERROR, "Could not allocate netlink mgr");
		return stat;
	}
	stat = nl_cache_mngr_add(nl->mngr, "route/link", (change_func_t)&netlinkdev_changecb, nl, &nl->links);
	if (stat < 0) {
		NL_LOG(NLLOG_WARN, "Could not add route/link to netlink cache mgr");
	}
	stat = nl_cache_mngr_add(nl->mngr, "route/addr", (change_func_t)&netlinkdev_changecb, nl, &nl->addrs);
	if (stat < 0) {
		NL_LOG(NLLOG_WARN, "Could not add route/addr to netlink cache mgr");
	}

	netlinkdev_opsinit(nl);

	NL_LOG(NLLOG_DEBUG, "netlink caches ready");

	return 0;
}


/**
 * @brief	Remove connections to netlink interface
 * @param[in]	nl		netlink context
 * @return	result of stop
 */
int netlinkdev_stop(struct netlinkdev_info *nl)
{
	if (nl->mngr)
		nl_cache_mngr_free(nl->mngr);
	nl->mngr = 0;
	if (nl->socket)
		nl_socket_free(nl->socket);
	nl->socket = 0;
	nl->event = NULL;
	nl->context = NULL;
	NL_LOG(NLLOG_DEBUG, "netlink caches stopped");

	return 0;
}

