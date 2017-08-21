/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * netlink log utils
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
 * @file	netlink_logs.h
 * @brief	netlink log utilties.
 *
 */

#ifndef NETLINK_LOGS_H_
#define NETLINK_LOGS_H_

#include <syslog.h>

/**
 * @brief	non-zero if running as daemon
 */
extern int running_daemon;

/**
 * @brief	netlink logs level
 */
extern int netlinklogs_level;

/**
 * @brief	netlink detailed logs enable
 */
extern int netlinklogs_detailed;

/**
 * @brief	condition true if detailed debug logging enabled
 */
#define LOG_DETAILS	netlinklogs_detailed && (netlinklogs_level == NLLOG_DEBUG)
 
/**
 * @brief	macro for opening logs to syslog
 * @param[in]	_loglevel	log level being set
 */
#define NL_LOG_OPEN(_loglevel)	{ if (running_daemon) { openlog("netlink", LOG_PID, LOG_DAEMON); } NL_LOG_LEVEL(_loglevel); }

/**
 * @brief	macro for closing logs to syslog
 */
#define NL_LOG_CLOSE()	if (running_daemon) { closelog(); }

/**
 * @brief	macro for setting log level in syslog
 * @param[in]	__loglevel	log level being set
 */
#define NL_LOG_LEVEL(__loglevel)	{ netlinklogs_level=__loglevel; if (running_daemon) { setlogmask(LOG_UPTO(__loglevel)); } }

/**
 * @brief	macro for logging a log entry to syslog
 * @param[in]	_level		log level for this log entry
 * @param[in]	_fmt		format of log entry
 * @param[in]	_arg		argument list for log entry
 */
#define NL_LOG(_level, _fmt, _args...)  { if (running_daemon) { syslog(_level, _fmt, ## _args); } else \
					  { if (_level <= netlinklogs_level ) { fprintf(stdout, _fmt, ## _args); fprintf(stdout, "\n"); } } }

/**
 * @brief	log levels translated to syslog levels
 */
#define NLLOG_ALERT	LOG_ALERT
#define NLLOG_FATAL	LOG_CRIT
#define NLLOG_ERROR	LOG_ERR
#define NLLOG_WARN	LOG_WARNING
#define NLLOG_INFO	LOG_INFO
#define NLLOG_DEBUG	LOG_DEBUG

#endif

