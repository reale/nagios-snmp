#!/usr/bin/awk -f


function usage()
{
	print "Decodifica Trap SNMP Nagios-to-Nagios"
}


BEGIN {
        host_address = ""
        host_name    = ""
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

                if (key == "NAGIOS-NOTIFY-MIB::sSvcHostname") {
                        host_name = value
                } else if (key == "NAGIOS-NOTIFY-MIB::sSvcDesc") {
                        svc_desc = value
                } else if (key == "NAGIOS-NOTIFY-MIB::sSvcStateID") {
			if (value == "ok")
				status = 0
			else if (value == "warning")
				status = 1
			else if (value == "critical")
				status = 2
			else
				status = 3
                } else if (key == "NAGIOS-NOTIFY-MIB::sSvcOutput") {
                        output = value
                }
        }

        printf "%s|%s|%s|%s|%s|%s|%s\n", host_address, host_name, svc_desc, status, output, perfdata, svc_id
}
