-- host
ALTER TABLE host ADD trap_command_id INT(11) NULL;
ALTER TABLE host ADD trap_command_arg TEXT;
ALTER TABLE host ADD trap_check_enabled TINYINT(4) NULL;

-- service
ALTER TABLE service ADD trap_command_id INT(11) NULL;
ALTER TABLE service ADD trap_command_arg TEXT;
ALTER TABLE service ADD trap_check_enabled TINYINT(4) NULL;

-- trap_handler
CREATE TABLE IF NOT EXISTS trap_handler (
	trap_command_id INT(11) NULL,
	trap_oid varchar(512) NULL,
	trap_use_sender_ip ENUM('0', '1') NOT NULL DEFAULT '1',
	PRIMARY KEY (trap_command_id)
) ENGINE=MyISAM;

-- command
INSERT INTO command (command_name, command_line, command_type)
VALUES
	('notifica-host-via-snmp-trap', '$USER1$/notifiche/send-host-trap $CONTACTEMAIL$ $HOSTNAME$ $HOSTSTATEID$ "$HOSTOUTPUT$"', 1),
	('notifica-servizio-via-snmp-trap', '$USER1$/notifiche/send-service-trap $CONTACTEMAIL$ $HOSTNAME$ $SERVICEDESC$ $SERVICESTATEID$ "$SERVICEOUTPUT$"', 1),
	('notifica-host-via-snmp-inform', '$USER1$/notifiche/send-host-trap -i $CONTACTEMAIL$ $HOSTNAME$ $HOSTSTATEID$ "$HOSTOUTPUT$"', 1),
	('notifica-servizio-via-snmp-inform', '$USER1$/notifiche/send-service-trap -i $CONTACTEMAIL$ $HOSTNAME$ $SERVICEDESC$ $SERVICESTATEID$ "$SERVICEOUTPUT$"', 1);
