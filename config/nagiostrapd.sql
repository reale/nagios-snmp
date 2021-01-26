--
--     N a g i o s   S N M P   T r a p   D a e m o n 
--
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------


--
--     SQL queries against the Nagios3 database
--
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------




-- BEGIN QUERY Q_FETCH_RESOURCES --
SELECT
	resource_line
FROM
	resources
WHERE 
	resource_line IS NOT NULL AND TRIM(resource_line) != ''
-- END QUERY --

-- BEGIN QUERY Q_FETCH_COMMANDS --
SELECT
	cmd.command_id, cmd.command_name, cmd.command_line, th.trap_use_sender_ip
	FROM command cmd 
	JOIN trap_handler th ON cmd.command_id = th.trap_command_id 
	WHERE
		cmd.command_id IS NOT NULL AND TRIM(cmd.command_id) != ''
		AND cmd.command_name IS NOT NULL AND TRIM(cmd.command_name) != ''
		AND cmd.command_line IS NOT NULL AND TRIM(cmd.command_line) != ''
		AND cmd.command_type = '3'
-- END QUERY --

-- BEGIN QUERY Q_FETCH_HOSTS --
SELECT * FROM
	(SELECT
		IFNULL(h.host_name, t.host_name) AS host_name,
		IFNULL(h.host_address, t.host_address) AS host_address,
		IFNULL(h.trap_command_id, t.trap_command_id) AS trap_command_id,
		IFNULL(h.trap_command_arg, t.trap_command_arg) AS trap_command_arg,
		IFNULL(h.trap_check_enabled, t.trap_check_enabled) AS trap_check_enabled FROM host h
		LEFT OUTER JOIN host t ON h.host_template_model_htm_id = t.host_id) t
	WHERE
		t.host_name IS NOT NULL AND TRIM(t.host_name) != ''
		AND t.host_address IS NOT NULL AND TRIM(t.host_address) != ''
		AND t.trap_command_id IS NOT NULL AND TRIM(t.trap_command_id) != ''
		AND t.trap_check_enabled = '1'
-- END QUERY --

-- BEGIN QUERY Q_FETCH_SVCS --
SELECT
	saux.service_description,
	h.host_name,
	h.host_address,
	saux.trap_command_id,
	saux.trap_command_arg
FROM (
	SELECT
		s.service_id AS service_id,
		IFNULL(s.service_description, st.service_description) AS service_description,
		IFNULL(s.trap_command_id, st.trap_command_id) AS trap_command_id,
		IFNULL(s.trap_command_arg, st.trap_command_arg) AS trap_command_arg,
		IFNULL(s.trap_check_enabled, st.trap_check_enabled) AS trap_check_enabled
	FROM
		service s
	LEFT OUTER JOIN service st ON s.service_template_model_stm_id = st.service_id) saux
	JOIN host_service_relation hs ON saux.service_id = hs.service_service_id 
	JOIN host h ON h.host_id = hs.host_host_id
WHERE
	saux.service_description IS NOT NULL AND TRIM(saux.service_description) != ''
	AND h.host_name IS NOT NULL AND TRIM(h.host_name) != ''
	AND h.host_address IS NOT NULL AND TRIM(h.host_address) != ''
	AND saux.trap_command_id IS NOT NULL AND TRIM(saux.trap_command_id) != ''
	AND saux.trap_check_enabled = '1'
-- END QUERY --

-- BEGIN QUERY Q_FETCH_TRAP_HANDLERS --
SELECT
	trap_oid, trap_command_id
FROM
	trap_handler
WHERE
	trap_oid IS NOT NULL AND TRIM(trap_oid) != ''
	AND trap_command_id IS NOT NULL AND TRIM(trap_command_id) != ''
-- END QUERY --
