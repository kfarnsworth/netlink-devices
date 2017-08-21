/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * uevent devices parsing uding libnl
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
 * @file	uevent_devices.c
 * @brief	Interface to linbl to get uevent device events.
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

#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/object-api.h>

#include "netlink_logs.h"
#include "uevent_devices.h"


/**
 * @brief	utility to search string for particular key
 * @param[in]	searchkey	string representing key being searched
 * @param[in]	buf		pointer to buffer search area
 * @param[out]	buflen		length of buffer search area
 * @return	pointer to the data if found, NULL if not found
 */
static char *ueventdev_searchkey(const char *searchkey, char *buf, int buflen)
{
	int bufpos = 0;
	int searchkeylen = strlen(searchkey);

	while (bufpos < buflen) {
		char *key;
		int keylen;

		key = &buf[bufpos];
		keylen = strlen(key);
		if (keylen == 0)
			break;
		 if ((strncmp(searchkey, key, searchkeylen) == 0) && key[searchkeylen] == '=')
			return &key[searchkeylen + 1];
		bufpos += keylen + 1;
	}
	return NULL;
}

/**
 * @brief	parses uevent looking for device state change events
 * @param[in]	p		pointer to start of event string
 * @param[in]	len		length of the event string
 * @param[out]	ud		pointer to uevent data structure
 * @return	1 if interface add or remove found, 0 otherwise
 */
static int ueventdev_parseuevent(unsigned char *p, int len, struct ueventdev_data *ud)
{
	char *payptr = (char *)p;
	char *end = (char *)p + len;
	int paylen;
	char *action, *devname;

	payptr += strlen(payptr)+1;	/* past header */
	if (payptr>=end)
		return 0;
	paylen = end-payptr;
	action = ueventdev_searchkey("ACTION", payptr, paylen);
	devname = ueventdev_searchkey("DEVNAME", payptr, paylen);
	if (action && devname) {
		if (!strcmp(action,"add") || !strcmp(action,"remove")) {
			NL_LOG(NLLOG_DEBUG, "uevent: %s device %s", action, devname);
			ud->action = !strcmp(action,"add") ? UEVENTDEV_ACTION_ADD : UEVENTDEV_ACTION_REMOVE;
			strncpy(ud->devname, devname, sizeof(ud->devname));
			return 1;
		}
	}
	return 0;
}

/**
 * @brief	custom callback for parsing special UEVENT not handled by netlink library
 * @param[in]	nl_msg		incoming netlink message
 * @param[in]	arg		pointer to our netlink context
 * @return	result of parse reported back to netlink library
 */
static int ueventdev_customcb(struct nl_msg *msg, void *arg)
{
	struct ueventdev_info *ul = (struct ueventdev_info *)arg;
	struct ueventdev_data uevent;
	struct nlmsghdr *hdr;

	if (LOG_DETAILS) NL_LOG(NLLOG_DEBUG, "uevent cb msg");

	if (!msg)
		return NL_STOP;
	
	if (nlmsg_get_proto(msg) == NETLINK_KOBJECT_UEVENT) {
		hdr = nlmsg_hdr(msg);
		
		if (ueventdev_parseuevent(nlmsg_data(hdr), nlmsg_datalen(hdr), &uevent)) {
			if (ul && ul->event)
				ul->event (&uevent, ul->context);
			else
				NL_LOG(NLLOG_ERROR, "could not send uevent msg");
		}
		return NL_OK;
	}
	return NL_SKIP;
}

/**
 * @brief	callback parses incoming uevent messages
 * @param[in]	nl_sock	netlink socket structure
 * @param[in]	nla		socket address for the incoming message
 * @param[in]	buf		message payload
 * @param[in]	creds		message credentials (ignored)
 * @return	result of parse reported back to netlink library
 */
static int ueventdev_recvcb(struct nl_sock *sk, struct sockaddr_nl *nla,
					unsigned char **buf, struct ucred **creds)
{
	int err=0, n;
	struct iovec iov;
	struct msghdr msg;
	struct nlmsghdr *hdr;

	msg.msg_name = (void *) nla;
	msg.msg_namelen = sizeof(struct sockaddr_nl);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	memset(nla, 0, sizeof(*nla));

	iov.iov_len = 1024;
	*buf = malloc(iov.iov_len + sizeof(struct nlmsghdr));
	if (!*buf)
		return NLE_NOMEM;
	iov.iov_base = *buf + sizeof(struct nlmsghdr); 

	do {
		n = recvmsg(nl_socket_get_fd(sk), &msg, 0);
		if (LOG_DETAILS) NL_LOG(NLLOG_DEBUG, "uevent recvmsg n=%d.", n);
		if (!n)
			break;
		if (n < 0) {
			if (errno != EAGAIN) {
				NL_LOG(NLLOG_WARN, "uevent recvmsg rtnd error %d", errno);
			}
			err = -nl_syserr2nlerr(errno);
			break;
		}
		else if (iov.iov_len < n ||
		    msg.msg_flags & MSG_TRUNC ||
		    msg.msg_flags & MSG_CTRUNC) {
			err = -NLE_MSG_TRUNC;
			NL_LOG(NLLOG_WARN, "uevent recvmsg buffer not big enough.");
			break;
		}
		/* fill in the nlmsghdr info so it can be processed by libnl */
		hdr = (struct nlmsghdr *)(*buf);
		hdr->nlmsg_len = n + sizeof(struct nlmsghdr);
		hdr->nlmsg_type = 0;
		hdr->nlmsg_flags = 0;
		hdr->nlmsg_seq = 0;
		hdr->nlmsg_pid = nla->nl_pid;
		err = hdr->nlmsg_len;
	} while (0);
	if ( (err <= 0) && *buf)
		free(*buf);
	return err;
}

/**
 * @brief	initializes the uevent mechanism
 * @param[in]	nl		netlink context
 * @return	result of uevent init
 */
static int ueventdev_init(struct ueventdev_info *ul)
{
	int err;

	ul->socket = nl_socket_alloc();
	if (!ul->socket) {
		NL_LOG(NLLOG_ERROR, "uevent: can't open uevent socket");
		return -1;
	}
	nl_join_groups(ul->socket, 1);
	if (nl_connect(ul->socket, NETLINK_KOBJECT_UEVENT)) {
		nl_socket_free(ul->socket);
		ul->socket = 0;
		NL_LOG(NLLOG_ERROR, "uevent: can't connect to UEVENT");
		return -1;
	}
	if ((err = nl_socket_set_nonblocking(ul->socket))) {
		nl_socket_free(ul->socket);
		ul->socket = 0;
		NL_LOG(NLLOG_ERROR, "uevent: can't set socket non-blocking(%d)", err);
		return - 1;
	}

	/* get msg uevents directly from callback as libnl does not
 	 * really support it */
	ul->cb = nl_cb_clone(nl_socket_get_cb(ul->socket));
	if (!ul->cb) {
		nl_socket_free(ul->socket);
		ul->socket = 0;
		NL_LOG(NLLOG_ERROR, "uevent: can't set new callback");
		return - 1;
	}
	nl_cb_overwrite_recv(ul->cb, ueventdev_recvcb);
	nl_cb_set(ul->cb, NL_CB_MSG_IN, NL_CB_CUSTOM, ueventdev_customcb, ul);
	nl_socket_set_cb(ul->socket, ul->cb);

	return 0;
}

/**
 * @brief	Poll the netlink socket and process all uevents
 * @param[in]	nl		netlink context
 * @return	always 0 as success
 */
int ueventdev_poll(struct ueventdev_info *ul)
{
	if (ul->socket) {
		int result;
		do {
			result = nl_recvmsgs_report(ul->socket, ul->cb);
		} while (result > 0);
	}
	return 0;
}

/**
 * @brief	Start a uevent session
 * @param[in]	ul			uevent info context ptr
 * @param[in]	netlink_event_cb	callback to report uevents
 * @param[in]	caller_context		callers context to pass into callback
 * @return	result of start
 */
int ueventdev_start(struct ueventdev_info *ul, 
		    void (*ueventdev_cb)(struct ueventdev_data *, void *), 
		    void *caller_context)
{
	int stat;

	memset(ul, 0, sizeof(struct ueventdev_info));

	ul->event = ueventdev_cb;
	ul->context = caller_context;

	stat = ueventdev_init(ul);
	if (stat < 0) {
		NL_LOG(NLLOG_ERROR, "Unable to initialize uevents, there will be no notification of hotplug events!");
		return stat;
	}
	NL_LOG(NLLOG_DEBUG, "netlink uevents ready");
	return 0;
}


/**
 * @brief	Remove connections to uevent interface
 * @param[in]	nl		netlink context
 * @return	result of stop
 */
int ueventdev_stop(struct ueventdev_info *ul)
{
	if (ul->socket)
		nl_socket_free(ul->socket);
	ul->socket = 0;

	ul->event = NULL;
	ul->context = NULL;

	NL_LOG(NLLOG_DEBUG, "netlink uevents stopped");
	return 0;
}

