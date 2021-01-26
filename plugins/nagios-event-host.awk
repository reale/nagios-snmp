#!/usr/bin/awk -f


#
# Decodifica di Trap SNMP del tipo Nagios-to-Nagios.
#
# L'OID e': NAGIOS-NOTIFY-MIB::sHostEvent
#
# Un esempio di trap di questo tipo e' il seguente:
#
#    DISMAN-EVENT-MIB::sysUpTimeInstance="4:19:36:49.61"
#    SNMPv2-MIB::snmpTrapOID.0="NAGIOS-NOTIFY-MIB::sHostEvent"
#    NAGIOS-NOTIFY-MIB::sHostname="host"
#    NAGIOS-NOTIFY-MIB::sHostStateID="down"
#    NAGIOS-NOTIFY-MIB::sHostOutput="HostOutput"
#
# Il presente script ha il compito di convertire in dati di monitoraggio le
# informazioni fornite dalla trap.
#

#
# Quando si scrive un check per il parsing delle trap, puo' presentarsi una
# delle due situazioni seguenti:
#
#    1)  Il mittente della trap coincide con l'host/servizio da monitorare.
#
#    In tal caso, il check verra' eseguito soltanto se l'indirizzo del mittente
#    della trap coincide con quello di uno o più host al quale sia stato
#    associato il plugin stesso; il check, inoltre, verra' eseguito una volta
#    per ciascun host e per ciascun servizio a cui sia stato associato.
#    Nagios3 si aspetta dall'output del check almeno il nome dell'host (ed
#    eventualmente il nome del servizio) oltre allo stato di uscita (come
#    avviene per i plugin classici).
#
#    2)  Il mittente della trap non coincide con l'host/servizio da monitorare.
#
#    Questa situazione puo' presentarsi ad esempio quando tra il target del
#    monitoraggio e Nagios3 si interpone un collettore SNMP, ossia un
#    apparato deputato alla raccolta e preaggregazione di informazioni
#    provenienti da un segmento di rete.  In tal caso l'indirizzo o nome
#    dell'host/servizio oggetto del monitoraggio sara' contenuto nel corpo del
#    messaggio stesso, secondo un formato in generale dipendente dal tipo di
#    trap.  In questa situazione la traduzione da trap a dati di monitoraggio
#    avviene in generale in due step distinti.
#
#        * In un primo step il plugin riceve in ingresso la trap ed e' tenuto a
#        fornire, nel formato piu' sotto descritto, una serie di record di
#        output per ciascun host/servizio su cui la trap fornisce informazioni.
#
#        * In un secondo step Nagios3 verifica, per ciascuno degli
#        host/servizi ottenuti al passo precedente, se essi siano effettivamente 
#        associati a quel check (e dunque a quella trap) e richiama il check stesso
#        passandogli il nome dell'host/servizio individuato allo step precedente.
#


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

	#
	# Qui avviene il parsing degli argomenti a riga di comando.
	# Questi devono essere della forma
	#
	#     argomento=valore
	#
        for (i = 1; i < ARGC; i++) {
                token = ARGV[i]
                split(token, parts, "=")
                key = parts[1]
                value = parts[2]

		#
		# Mostriamo l'help ed usciamo.
		#
		if (key == "-h") {
			usage()
			exit
		}

                if (key == "NAGIOS-NOTIFY-MIB::sHostname") {
                        host_name = value
                } else if (key == "NAGIOS-NOTIFY-MIB::sHostStateID") {
			if (value == "up")
				status = 0
			else if (value == "down")
				status = 1
			else if (value == "unreachable")
				status = 2
			else
				status = 3
                } else if (key == "NAGIOS-NOTIFY-MIB::sHostOutput") {
                        output = value
                }
        }

	#
	# L'output deve contenere una o piu' righe nel formato seguente:
	#
        #     ip_host|nome_host|nome_servizio|stato|output|dati_performance|id_servizio
	#
	# I campi non devono essere necessariamente tutti valorizzati.
	#
        printf "%s|%s|%s|%s|%s|%s|%s\n", host_address, host_name, svc_desc, status, output, perfdata, svc_id
}

