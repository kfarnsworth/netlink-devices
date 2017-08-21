/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Main entry for netlink testing as utility or daemon
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
 * @file	main.c
 * @brief	Main entry functions for testing netlink.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <arpa/inet.h>

#include "netlink_logs.h"
#include "netlink_devices.h"
#include "uevent_devices.h"

int running_daemon = 0;
int netlinklogs_level = NLLOG_INFO;
int netlinklogs_detailed = 0;

static int start_as_daemon = 0;
static int running = 1;
static int logsopen=0;

static struct ueventdev_info uevent_device_info;
static struct netlinkdev_info netlink_device_info;
static char interface_poll_name[80];

#define LOG_FATAL(fmt, args...)	{ \
	if (!logsopen) { NL_LOG_OPEN(NLLOG_FATAL); logsopen=1; } \
	NL_LOG(NLLOG_FATAL, fmt, ## args); \
	}

/**
 * @brief	netlink event callback
 * @param[in]	devdata		pointer to network data 
 * @param[in] 	arg		context pointer to netlink device info
 * @return	None
 */
static void netevent(int event, struct netlinkdev_data *devdata, void *arg)
{
	struct netlinkdev_info *nl = (struct netlinkdev_info *)arg;
	char addr[128];

	(void)nl;  /* possible use in future */

	if (event == NETLINKDEV_EVENT_ADDR)
	{
		if (devdata->net_len == 0) {
			strcpy(addr, "not set");
		}
		else {
			inet_ntop(devdata->net_family, devdata->net_addr, addr, sizeof(addr)); 
		}
		NL_LOG(NLLOG_INFO, "interface ADDR event status %s  (addr: %s)", 
				devdata->status & IFF_LOWER_UP && devdata->status & IFF_UP ? "UP": "DOWN", addr);
	}
	else if (event == NETLINKDEV_EVENT_LINK) {
		NL_LOG(NLLOG_INFO, "interface LINK event status:%s linkaddr:%02x:%02x:%02x:%02x:%02x:%02x", 
				devdata->status & IFF_LOWER_UP && devdata->status & IFF_UP ? "UP": "DOWN",
				devdata->link_addr[0], devdata->link_addr[1], devdata->link_addr[2],
				devdata->link_addr[3], devdata->link_addr[4], devdata->link_addr[5]);
	}
	else {
		NL_LOG(NLLOG_ERROR, "unknown event: %d", event);
	}
}

/**
 * @brief	hotplug event callback
 * @param[in]	devdata		pointer to device data
 * @param[in] 	arg		context pointer to event info
 * @return	None
 */
static void hotplugevent(struct ueventdev_data *devdata, void *arg)
{
	struct ueventdev_info *ul = (struct ueventdev_info *)arg;

	(void)ul;  /* possible use in future */

	NL_LOG(NLLOG_INFO, "hotplug event: '%s' was %s", 
		devdata->devname, devdata->action==UEVENTDEV_ACTION_ADD ? "ADDED" : "REMOVED" );
}



/**
 * @brief	initiialize test
 * @return	status of init
 */
static int init(void)
{
	int stat;

	stat = netlinkdev_start( &netlink_device_info, netevent, &netlink_device_info);
	if (!stat) {
		stat = ueventdev_start( &uevent_device_info, hotplugevent, &uevent_device_info);
	}
	return stat;
}

/**
 * @brief	deinitiialize test
 * @return	None
 */
static void deinit(void)
{
	ueventdev_stop( &uevent_device_info );
	netlinkdev_stop( &netlink_device_info );
}

/**
 * @brief	deinitiialize test
 * @return	None
 */
static void poll(void)
{
	/* poll netlink events */
	netlinkdev_poll( &netlink_device_info );
	ueventdev_poll( &uevent_device_info );
}

static void interfacestatus(char *ifc_name)
{
	int stat;
	struct netlinkdev_data ifc_data;
	stat = netlinkdev_getnet(&netlink_device_info, ifc_name, &ifc_data);
	char addr[128];

	if (stat < 0) {
		NL_LOG(NLLOG_INFO, "check status: Interface '%s' not available.", ifc_name);
		return;
	}

	if (ifc_data.net_len == 0) {
		strcpy(addr, "not set");
	}
	else {
		inet_ntop(ifc_data.net_family, ifc_data.net_addr, addr, sizeof(addr)); 
	}
	NL_LOG(NLLOG_INFO, "check status: name:'%s' index:%d state:%s linkaddr:%02x:%02x:%02x:%02x:%02x:%02x netaddr:%s", 
			ifc_name,
			ifc_data.if_index,
			ifc_data.status & IFF_LOWER_UP ? (ifc_data.status & IFF_UP ? "UP": "DOWN") : "LINK DOWN",
			ifc_data.link_addr[0], ifc_data.link_addr[1], ifc_data.link_addr[2],
			ifc_data.link_addr[3], ifc_data.link_addr[4], ifc_data.link_addr[5],
			addr);
}

/**
 * @brief	signal handler
 * @param[in]	sig		signal caught
 * @return	NONE
 * @ingroup	Main
 */
static void signal_handler(int sig)
{
	switch(sig){
		case SIGCHLD:
			/* child */
			NL_LOG(NLLOG_INFO, "Child signal catched.");
			break;		
		case SIGHUP:
			/* rehash the server */
			NL_LOG(NLLOG_INFO, "Hangup signal catched.");
			break;		
		case SIGTERM:
			/* finalize the server */
			NL_LOG(NLLOG_ALERT, "Terminate signal catched. Killing daemon.");
			running = 0;
			deinit();
			NL_LOG_CLOSE();
			exit(EXIT_SUCCESS);
			break;
	}
}

/**
 * @brief	Look to see if process is already running
 * @param[in]	name		string of the called executable name
 * @param[in]	mypid		process ID of this process
 * @return	process id of already running daemon or -1 is not running
 * @ingroup	Main
 */
static pid_t proc_find(char* name, pid_t mypid) 
{
	DIR* dir;
	struct dirent* ent;
	char buf[512];

	long  pid;
	char pname[100] = {0,};
	char state;
	FILE *fp=NULL; 

	if (!(dir = opendir("/proc"))) {
		fprintf(stderr, "can't open /proc");
		return -1;
	}

	while ((ent = readdir(dir)) != NULL) {
		long lpid = atol(ent->d_name);
		if ((lpid < 0) || (lpid == mypid))
			continue;
		snprintf(buf, sizeof(buf), "/proc/%ld/stat", lpid);
		fp = fopen(buf, "r");
		if (fp) {
			if ( (fscanf(fp, "%ld (%[^)]) %c", &pid, pname, &state)) != 3 ){
				fprintf(stderr, "fscanf failed \n");
				fclose(fp);
				closedir(dir);
				return -1; 
			}
			if (!strcmp(pname, name)) {
				fclose(fp);
				closedir(dir);
				return (pid_t)lpid;
			}
			fclose(fp);
		}
	}
	closedir(dir);
	return -1;
}

/**
 * @brief	Check if daemon is already running
 * @param[in]	name		string of the called executable name
 * @return	0 if daemon not running, 1 otherwise
 * @ingroup	Main
 */
static int is_already_running(const char *name)
{
	char *progname = basename((char *)name);
	if (proc_find(progname, getpid()) < 0)
		return 0;
	return 1;
}

/**
 * @brief	Fork a child process and then close the parent
 * @param[in]	name		string of the called executable name
 * @return	None
 * @ingroup	Main
 */
static void daemonize_me(const char *name)
{
	pid_t pid;

	if (is_already_running(name)) {
		fprintf(stderr, "Daemon already running...  Exiting.\n");
		exit(EXIT_FAILURE);
	}
	
	/* Fork off the parent process */
	pid = fork();
	/* An error occurred */
	if (pid < 0) {
		LOG_FATAL("can't fork.\n");
        	exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* On success: The child process becomes session leader */
	if (setsid() < 0) {
		LOG_FATAL("setsid error.");
		exit(EXIT_FAILURE);
	}

	/* Handle signals */
	signal(SIGCHLD, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGTERM, signal_handler);

	 /* ignore tty signals */
	signal(SIGTSTP,SIG_IGN);
	signal(SIGTTOU,SIG_IGN);
	signal(SIGTTIN,SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();

	/* An error occurred */
	if (pid < 0) {
		LOG_FATAL("can't fork again.");
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0)
		exit(EXIT_SUCCESS);

	/* Set new file permissions */
	umask(0);

	/* Change the working directory to the root directory */
	if (chdir("/") < 0)
		LOG_FATAL("warning: could not change dir to '/'.");

	/* Close all open file descriptors */
	int x;
	for (x = sysconf(_SC_OPEN_MAX); x>0; x--)
	{
		close (x);
	}
}

/**
 * @brief	Parse the options passed in to the test
 * @param[in]	argc		argument count
 * @param[in]	argv		arguments array of string pointers
 * @return	None
 */
static void parse_options(int argc, char *argv[])
{
	int c;

	while (1)
	{
		static struct option long_options[] =
		{
			{"daemon",	no_argument,		0,	'd'},
			{"loglevel",	required_argument,	0,	'l'},
			{"help",	no_argument,		0,	'h'},
			{0, 0, 0, 0}
		};
		
		int option_index = 0;	/* getopt_long stores the option index here. */

		c = getopt_long (argc, argv, "dl:h", long_options, &option_index);

		if (c == -1)	/* end of options. */
			break;

		switch (c)
		{
			case 'd':
				start_as_daemon = 1;
				break;
			case 'l':
				if ((strlen(optarg)==1) && (optarg[0] >= '0') && (optarg[0] <=  '6')) {
					const char *levelname;
					switch (optarg[0])
					{
						case '0': netlinklogs_level = NLLOG_FATAL; levelname="FATAL"; break;
						case '1': netlinklogs_level = NLLOG_ALERT; levelname="ALERT"; break;
						case '2': netlinklogs_level = NLLOG_ERROR; levelname="ERROR"; break;
						case '3': netlinklogs_level = NLLOG_WARN; levelname="WARNINGS"; break;
						case '4': netlinklogs_level = NLLOG_INFO; levelname="INFO"; break;
						case '6': netlinklogs_detailed = 1;
						case '5': netlinklogs_level = NLLOG_DEBUG; levelname="DEBUG"; break;
					}
					fprintf(stdout, "Log level set to %s\n", levelname);
					break;
				}
				fprintf(stderr, "ERROR: Invalid log level: %s\n", optarg);
			case 'h':
			case '?':
				/* getopt_long already printed an error message. */
				fprintf(stderr, "Usage:	%s [--daemon|-d] [--loglevel|-l <level>] [--help|-h] [interface-name]\n", argv[0]);
			default:
				exit(EXIT_FAILURE);
		}
	}
	interface_poll_name[0] = '\0';
	if (optind < argc) {
		strncpy(interface_poll_name, argv[optind], sizeof(interface_poll_name));
		interface_poll_name[sizeof(interface_poll_name)-1] = '\0';	
		fprintf(stdout, "Monitoring interface '%s'\n", interface_poll_name);
	}
}

/**
 * @brief	netlink test utility
 * @param[in]	argc		argument count
 * @param[in]	argv		arguments array of string pointers
 * @return	EXIT_SUCCESS	Normal exit
 * @return	EXIT_FAILURE	Failure occurred
 */
int main(int argc, char *argv[])
{

	parse_options(argc, argv);

	if (start_as_daemon) {
		fprintf(stdout, "Starting Netlink Test as daemon...\n");
		running_daemon = 1;
		daemonize_me(argv[0]);
	}

	NL_LOG_OPEN(netlinklogs_level);

	NL_LOG(NLLOG_INFO, "Netlink Test Started.");

	if (init() < 0) {
		NL_LOG(NLLOG_FATAL, "Failure during init.");
		NL_LOG_CLOSE();
		exit(EXIT_FAILURE);
	}

	while (running) {

		/* Process any netlink events */
		poll();

		if (strlen(interface_poll_name) > 0)
			interfacestatus(interface_poll_name);

		sleep(1);
	}

	NL_LOG(NLLOG_INFO, "Netlink Test Stopping.");

	deinit();

	NL_LOG_CLOSE();

	return EXIT_SUCCESS;
}

