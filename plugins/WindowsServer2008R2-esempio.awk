#!/usr/bin/awk -f


function usage()
{
	print "Check di Esempio per Trap SNMP inviate da Windows 2008 Server"
}


BEGIN {
	host_address = ""
		host_name    = ""
		host_value   = ""
		svc_desc     = ""
		status       = ""
		output       = ""
		perfdata     = ""
		svc_id       = ""

		for (i = 1; i < ARGC; i++) {
			token = ARGV[i]
			split(token, parts, "=")
			key = parts[1]
			value = parts[2]

			if (key == "-h") {
				usage()
				exit
			}

			if (key == "SNMPv2-SMI::enterprises.311.1.13.1.9999.4.0") {
				output = "TestOutput"
			} else if (key=="SNMP-COMMUNITY-MIB::snmpTrapAddress.0") {
				host_address = value
			} else if (key=="SNMPv2-SMI::enterprises.311.1.13.1.9999.4.0") {
				status = 1
			}
		}


	printf "%s|%s|%s|%s|%s|%s|%s\n", host_address, host_name, svc_desc, status, output, perfdata, svc_id
}
