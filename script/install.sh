#!/bin/bash


###############################################################################
###############################################################################
##
## Nagios3 SNMP Trap --- Installation Script
##

USER1=/usr/local/nagios/libexec
nagios_user=nagios


###############################################################################
##
## Preamble
##

if [ `id -u` -ne 0 ]
then
	echo 'You must be root!'
	exit 1
fi

if [ $# -gt 0 ]
then
	nagios_patch_level=$1
fi

##
## version file
##

version_file=/etc/nagiostrap.version

##
## version number of the package
##

package_version=`cat VERSION 2>/dev/null`

##
## create an empty environment
##

if [ -n "$PREFIX_DIR" ]
then
	rm -rf $PREFIX_DIR
	mkdir $PREFIX_DIR
fi

##
## create Nagios3 user
##

useradd $nagios_user 2> /dev/null


###############################################################################
##
## SNMP Tool's snmptrapd
##

##
## install ad-hoc config file
##

install -o root -g root -m 0755 -d $PREFIX_DIR/etc/snmp
install -o root -g root -m 0644 config/snmptrapd.conf $PREFIX_DIR/etc/snmp

###############################################################################
##
## Nagios3 SNMP Trap Daemon
##

##
## install nagiostrapd's conf file
##

install -o root -g root -m 0755 -d $PREFIX_DIR/etc/nagiostrapd
install -o root -g root -m 0644 config/nagiostrapd.ini $PREFIX_DIR/etc/nagiostrapd
install -o root -g root -m 0644 config/nagiostrapd.limits $PREFIX_DIR/etc/nagiostrapd
install -o root -g root -m 0644 config/nagiostrapd.sql $PREFIX_DIR/etc/nagiostrapd

##
## avoid to compile while being root
##

nonrootuser=`stat -c %U src`

##
## create version.h header for nagiostrapd
##

su $nonrootuser -c "sed 's/__PROGVERSION__/$package_version/' < src/version.h.in > src/version.h"

##
## compile nagiostrapd
##

su $nonrootuser -c "cd src && ./switch-debug-production --debug && cd .."
su $nonrootuser -c "make clean && make all"
su $nonrootuser -c "mv src/nagiostrapd src/nagiostrapd.debug"
su $nonrootuser -c "cd src && ./switch-debug-production --production && cd .."
su $nonrootuser -c "make clean && make all"
su $nonrootuser -c "strip src/nagiostrapd"

##
## create path
##

install -o root -g root -m 0755 -d $PREFIX_DIR/usr/sbin

##
## install nagiostrapd
##

install -o root -g root -m 0755 src/nagiostrapd $PREFIX_DIR/usr/sbin
install -o root -g root -m 0755 src/nagiostrapd.debug $PREFIX_DIR/usr/sbin

##
## perform cleanup of source directory
##

make clean
cd src && ./switch-debug-production --clean && cd ..

##
## install default handler
##

install -o root -g root -m 0755 traphandlers/nagiostraphandler $PREFIX_DIR/usr/sbin

##
## install logrotate's config file
##

install -o root -g root -m 0755 -d $PREFIX_DIR/etc/logrotate.d
install -o root -g root -m 0644 logrotate/nagiostrapd $PREFIX_DIR/etc/logrotate.d

##
## install monitoring scripts
##

install -o root -g root -m 0755 script/nagiostrapd-monitor $PREFIX_DIR/usr/sbin


###############################################################################
##
## Startup scripts
##

##
## install startup script
##

install -o root -g root -m 0755 -d $PREFIX_DIR/etc/init.d
install -o root -g root -m 0755 script/nagiostrapd $PREFIX_DIR/etc/init.d

##
## install default config files
##

install -o root -g root -m 0755 -d $PREFIX_DIR/etc/default
install -o root -g root -m 0644 default/* $PREFIX_DIR/etc/default


###############################################################################
##
## Notifications
##

##
## create path
##

install -o $nagios_user -g $nagios_user -m 0755 -d $PREFIX_DIR/$USER1/notifiche

##
## install notification plugins
##

install -o $nagios_user -g $nagios_user -m 0777 notifications/* $PREFIX_DIR/$USER1/notifiche


###############################################################################
##
## Plugins
##

##
## create path
##

install -o $nagios_user -g $nagios_user -m 0777 -d $PREFIX_DIR/$USER1/TrapSNMP

##
## install plugins
##

install -o $nagios_user -g $nagios_user -m 0777 plugins/* $PREFIX_DIR/$USER1/TrapSNMP


###############################################################################
##
## MIBS
##

##
## create path
##

install -o root -g root -m 0755 -d $PREFIX_DIR/usr/share/mibs/netsnmp

##
## install plugins
##

install -o root -g root -m 0644 mibs/* $PREFIX_DIR/usr/share/mibs/netsnmp


###############################################################################
##
## Final
##

##
## write down the version number
##

echo $package_version > $PREFIX_DIR/$version_file


###############################################################################
##
## End of the Script
##
