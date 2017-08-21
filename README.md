netlink devices by <kyle@farnsworthtech.com>

The netlink interface into the kernel is useful to get status of the 
interfaces.  However, netlink can be complicated.  The libnl libraries 
can make it easier.  It is not short of complications, even when using 
an abstraction layer like libnl.

I created a set of functions to capture the interface and report changes 
as they occur.  Also, I show an interface to the uevent mechanism to get 
device plug and unplug events.

A more powerful utility called udev provides a better solution but I was 
looking for a minimum implementation without the extra baggage that comes 
with udev.


BUILDING:

You need to first build and install libnl.  You can get it here:
	http://www.infradead.org/~tgr/libnl/doc/api/index.html

Build libnl:

	cd ./libnl-3.2.25
	make libnl-config
	make libnl-build
	make libnl-install

Build netlink devices:

	cd ./netlink-devices
	make netlink-devices-build
	make netlink-devices-install


USAGE:

The nltest (main.c) is used to demonstrate the features:

	# ./nltest --help
	Usage:  ./nltest [--daemon|-d] [--loglevel|-l <level>] [--help|-h] [interface-name]

If interface-name is passed in, it will only monitor this interface.

EXAMPLE:

	# ./nltest
	Netlink Test Started.

Initially grabs all the interfaces:

	interface ADDR event status UP  (addr: 127.0.0.1)
	interface ADDR event status UP  (addr: 192.168.201.210)
	interface ADDR event status UP  (addr: ::1)
	interface ADDR event status UP  (addr: fe80::7aa5:4ff:fef1:25ac)


Ethernet interface is pulled out:

	interface ADDR event status DOWN  (addr: 192.168.201.210)
	interface ADDR event status DOWN  (addr: fe80::7aa5:4ff:fef1:25ac)
	interface LINK event status:DOWN linkaddr:78:a5:04:f1:25:ac
	interface LINK event status:DOWN linkaddr:78:a5:04:f1:25:ac

Ethernet interface plugged back in:

	interface ADDR event status UP  (addr: 192.168.201.210)
	interface ADDR event status UP  (addr: fe80::7aa5:4ff:fef1:25ac)


API

The libnl API provides away of getting the device information.  It also 
provides a caching system to hold this information and report back changes 
via a callback.

Call netlinkdev_start() to setup the interface to the netlink layer.  A 
context pointer to struct netlinkdev_info is filled in by the function. 
A callback netlink_event_cb is provided to report back device interface 
change events. The event is either a address or link event.  The information 
is in the struct netlinkdev_data.

        int netlinkdev_start(struct netlinkdev_info *nl, 
                             void (*netlink_event_cb)( 
                                    int,
                                    struct  netlinkdev_data *, 
                                    void *), 
                             void *caller_context)


Use netlinkdev_stop() to shutdown the netlink infterface.

    int netlinkdev_stop(struct netlinkdev_info *nl)

netlinkdev_poll() must be called periodically:

    int netlinkdev_poll(struct netlinkdev_info *nl)


libnl does not totally support uevents but it does allow installing a 
custom callback to handle the hotplug type uevents I was looking for.  

Starting and stopping is perform by these functions:

    int ueventdev_start(struct ueventdev_info *ul,
                     void (*ueventdev_cb)(struct ueventdev_data *, void *),
                     void *caller_context)
     
    int ueventdev_stop(struct ueventdev_info *ul)


The struct eventdev_info is used as the context.  The callback ueventdev_cb 
will be used to report device changes.

ueventdev_poll() must be called periodically:

    int ueventdev_poll(struct ueventdev_info *ul)

