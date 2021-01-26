/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     trap.c --- trap & protocol parsing
 *
 ******************************************************************************
 ******************************************************************************/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


#define TRAP_SERIALIZE_MODE_OLD  0x1
#define TRAP_SERIALIZE_MODE_NEW  0x2


struct mib_object_t {
	char *oid;
	char *value;
	char *quoted_value;
	struct mib_object_t *next;
};


struct trap_t {
	time_t seconds;
	suseconds_t useconds;
	char *hostname;
	char *ipaddress;
	char *oid;
	struct mib_object_t *object;
};


typedef enum {
	PROTO_CMD_NONE,
	PROTO_CMD_TRAP,
	PROTO_CMD_ALIVE,
	PROTO_CMD_DIAGNOSTICS,
	PROTO_CMD_INVALID
} proto_cmd_t;


struct pdu_t {
	proto_cmd_t command;
	struct trap_t *trap;
};


/* tokenizer string */
static char *trap_tokenizer;

/* trap serialized mode (new/old) */
static int trap_serialize_mode = 0;

/* number of trap parsed and mutex thereof */
static long trap_parsed = 0;
static pthread_mutex_t trap_counters_mutex = PTHREAD_MUTEX_INITIALIZER;




/*
 *     Private methods
 *
 ******************************************************************************/


/*
 * extract ip address from string
 */

static char *extract_ipaddress(char *string)
{
	/*
	 * string is of the form:
	 *
	 *   UDP: [192.168.3.96]:44084->[127.0.0.1]
	 */

	int found;
	char c;
	char *start, *address;
	size_t len = 0;

	if (is_empty(string)) {
		DEBUG("called on empty string");
		return NULL;
	}

	DEBUG("called on string %s", string);

	found = 0;
	while ((c = *string++)) {
		if (c == '[') {
			found = 1;
			break;
		}
		if (c == '\0') {
			found = 0;
			break;
		}
	}

	if (found == 0) {
		DEBUG("invalid format");
		return NULL;
	}

	start = string;

	DEBUG("start of ip address: %s", start);

	while ((c = *string++)) {
		if (c == ']') {
			found = 1;
			break;
		}
		if (c == '\0') {
			found = 0;
			break;
		}

		len++;
	}

	if (found == 0) {
		DEBUG("invalid format");
		return NULL;
	}

	address = xmalloc(len + 1);
	memcpy(address, start, len);
	address[len] = '\0';

	DEBUG("ip address found: %s (len: %d)", address, len);

	return address;
}


static struct pdu_t *alloc_pdu(void)
{
	struct pdu_t *pdu;

	DEBUG("called");

	pdu = xmalloc(sizeof *pdu);
	memset(pdu, 0, sizeof *pdu);

	return pdu;
}


static struct trap_t *alloc_trap(void)
{
	struct trap_t *trap;

	DEBUG("called");

	trap = xmalloc(sizeof *trap);
	memset(trap, 0, sizeof *trap);

	return trap;
}


static void free_trap(struct trap_t *trap)
{
	struct mib_object_t *object, *next;

	if (trap == NULL) {
		DEBUG("called on NULL pointer");
		return;
	}

	DEBUG("called");

	object = trap->object;

	while (object != NULL) {
		DEBUG("freeing list item");
		next = object->next;
		free(object->oid);
		free(object->value);
		free(object->quoted_value);
		free(object);
		object = next;
	}

	DEBUG("no more items to free");

	free(trap->hostname);
	free(trap->ipaddress);

	/* trap->oid must not be freed as it's a pointer to one of object's oid */

	free(trap);

	DEBUG("done");
}


static int oid_is_snmpTrapOID(const char *oid)
{
	int res;
	
	if (is_empty(oid)) {
		DEBUG("called on empty oid");
		return 0;
	}

	res = (strcmp(oid, "SNMPv2-MIB::snmpTrapOID.0") == 0 || strcmp(oid, ".1.3.6.1.6.3.1.1.4.1.0") == 0);

	DEBUG("called on oid %s, result is %d", oid, res);

	return res;
}


static int post_parse(struct trap_t *trap)
{
	struct timeval tv;
	struct mib_object_t *object;
	int return_true = 0;

	if (trap == NULL) {
		DEBUG("called on NULL trap");
		return 0;
	}

	/*
	 * lookup primary OID
	 */

	DEBUG("called");

	object = trap->object;

	while (object != NULL) {
		if (oid_is_snmpTrapOID(object->oid)) {
			trap->oid = object->value;
			return_true = 1;
			break;
		}
		object = object->next;
	}

	if (!return_true) {
		DEBUG("cannot find primary OID");
		return 0;
	}

	DEBUG("found primary OID");

	/*
	 * now set timestamp (seconds + microseconds...)
	 */

	gettimeofday(&tv, NULL);
	trap->seconds = tv.tv_sec;
	trap->useconds = tv.tv_usec;

	DEBUG("setting timestamp: %ld.%ld", (long) trap->seconds, (long) trap->useconds);

	/*
	 * finally, quote values...
	 */

	object = trap->object;

	while (object != NULL) {
		char *value_1, *value_2;

		if ((value_1 = remove_quotes(object->value)) != NULL) {
			DEBUG("dequoting (step #1): %s", value_1);
			if ((value_2 = backslash_quote(value_1)) != NULL) {
				DEBUG("quoting (step #2): %s", value_2);
				object->quoted_value = quote(value_2);
				DEBUG("quoting (step #3): %s", object->quoted_value);
				free(value_2);
			} else {
				DEBUG("cannot quote");
			}
			free(value_1);
		} else {
			DEBUG("cannot dequote");
			object->quoted_value = NULL;
		}

		object = object->next;
	}

	return 1;
}


typedef enum {
	S_STARTED, S_COMMAND_READ, S_HOSTNAME_READ, S_READING_IPADDRESS, S_IPADDRESS_READ, S_OID_READ, S_READING_VALUE, S_VALUE_READ
} parse_status_t;

#ifndef NDEBUG
static char *decode_proto_cmd(proto_cmd_t cmd)
{
	switch (cmd) {
		case PROTO_CMD_NONE:
			return strdup("PROTO_CMD_NONE");
		case PROTO_CMD_TRAP:
			return strdup("PROTO_CMD_TRAP");
		case PROTO_CMD_ALIVE:
			return strdup("PROTO_CMD_ALIVE");
		case PROTO_CMD_DIAGNOSTICS:
			return strdup("PROTO_CMD_DIAGNOSTICS");
		case PROTO_CMD_INVALID:
			return strdup("PROTO_CMD_INVALID");
		default:
			assert(0);
	}
}


static char *decode_status(parse_status_t status)
{
	switch (status) {
		case S_STARTED:
			return strdup("S_STARTED");
		case S_COMMAND_READ:
			return strdup("S_COMMAND_READ");
		case S_HOSTNAME_READ:
			return strdup("S_HOSTNAME_READ");
		case S_READING_IPADDRESS:
			return strdup("S_READING_IPADDRESS");
		case S_IPADDRESS_READ:
			return strdup("S_IPADDRESS_READ");
		case S_OID_READ:
			return strdup("S_OID_READ");
		case S_READING_VALUE:
			return strdup("S_READING_VALUE");
		case S_VALUE_READ:
			return strdup("S_VALUE_READ");
		default:
			assert(0);
	}
}
#endif


/*
 * main parser
 */

static struct pdu_t *parse(char *buffer, int is_private)
{
	struct pdu_t *pdu = NULL;
	struct trap_t *trap = NULL;
	struct mib_object_t *object = NULL, *old_object;
	char *str, *token, *saveptr = NULL;
	parse_status_t status;
	proto_cmd_t proto_cmd;
	int tokenizer_found = 0;
	char ipbuf[TMPBUFLEN];
	size_t tokenlen = 0, tokenlen2;

	DEBUG("called");

	buffer = remove_chars(buffer, "\n\r");

	if (is_empty(buffer)) {
		DEBUG("buffer is empty");
		pdu = alloc_pdu();
		pdu->command = PROTO_CMD_INVALID;
		return pdu;
	} else {
		DEBUG("buffer: %s", buffer);
	}

/*
 * EXAMPLE OF RAW TRAP
 *
 * TRAP :: localhost.localdomain :: UDP: [192.168.3.96]:47603->[127.0.0.1] :: .iso.3.6.1.2.1.1.3.0 5:19:31:34.58 :: .iso.3.6.1.6.3.1.1.4.1.0 .iso.3.6.1.4.1.20006.1.5 :: .iso.3.6.1.4.1.20006.1.1.1.2 "testhost" :: .iso.3.6.1.4.1.20006.1.1.1.4 1 :: .iso.3.6.1.4.1.20006.1.1.1.14 "CRITICAL - Host Unreachable (192.168.3.233)"
 *
 */

 	DEBUG("trap parsing BEGINS");

	for (str = buffer, status = S_STARTED, proto_cmd = PROTO_CMD_NONE; ; str = NULL) {

		token = strtok_r(str, " ", &saveptr);

		if (token == NULL) {
			DEBUG("token is NULL");
			break;
		} else {
			DEBUG("token: %s", token);
		}

#ifndef NDEBUG
		char *cmd_desc = decode_proto_cmd(proto_cmd);
		char *status_desc = decode_status(status);
		DEBUG("FOR iteration: status %s, command %s", status_desc, cmd_desc);
		free(status_desc);
		free(cmd_desc);
#endif

		if (proto_cmd != PROTO_CMD_NONE && proto_cmd != PROTO_CMD_TRAP) {
			DEBUG("proto command is of simple type (not TRAP)");
			break;
		}

		if (strcmp(token, trap_tokenizer) == 0) {
			DEBUG("tokenizer found");
			tokenizer_found = 1;
		}

		if (tokenizer_found && (status == S_STARTED || status == S_COMMAND_READ)) {
			DEBUG("tokenizer found and going-ahead condition");
			tokenizer_found = 0;
			continue;
		}

		DEBUG("main switch BEGINS");

		switch(status) {
			case S_STARTED:
				DEBUG("status S_STARTED");
				if (strcmp(token, "TRAP") == 0) {
					DEBUG("cmd TRAP");
					proto_cmd = PROTO_CMD_TRAP;
				} else if (strcmp(token, "ALIVE?") == 0) {
					DEBUG("cmd ALIVE");
					proto_cmd = PROTO_CMD_ALIVE;
				} else if (strcmp(token, "DIAGNOSTICS") == 0) {
					DEBUG("cmd DIAGNOSTICS");
					if (is_private) {
						proto_cmd = PROTO_CMD_DIAGNOSTICS;
					} else {
						DEBUG("not from private socket, invalid");
						proto_cmd = PROTO_CMD_INVALID;
					}
				} else {
					DEBUG("cmd INVALID");
					proto_cmd = PROTO_CMD_INVALID;
				}

				pdu = alloc_pdu();
				pdu->command = proto_cmd;
				status = S_COMMAND_READ;
				break;
			case S_COMMAND_READ:
				DEBUG("status S_COMMAND_READ");
				trap = alloc_trap();
				trap->hostname = xstrdup(token);
				DEBUG("found hostname: %s", trap->hostname);
				status = S_HOSTNAME_READ;
				break;
			case S_HOSTNAME_READ:
				DEBUG("status S_HOSTNAME_READ");
				tokenlen = strlen(token);
				strcpy(ipbuf, token);
				status = S_READING_IPADDRESS;
				break;
			case S_READING_IPADDRESS:
				DEBUG("status S_READING_IPADDRESS");
				if (tokenizer_found == 0) {
					tokenlen2 = strlen(token);
					ipbuf[tokenlen] = ' ';
					memcpy(ipbuf+tokenlen+1, token, tokenlen2);
					tokenlen += tokenlen2 + 1;
				} else {
					ipbuf[tokenlen] = '\0';
					trap->ipaddress = extract_ipaddress(ipbuf);
					DEBUG("found IPADDRESS: %s", trap->ipaddress);
					status = S_IPADDRESS_READ;
				}
				break;
			case S_IPADDRESS_READ:
				DEBUG("status S_IPADDRESS_READ");
				/* fall through */
			case S_VALUE_READ:
				DEBUG("status S_VALUE_READ");
				old_object = object;
				object = xmalloc(sizeof *object);
				if (trap->object == NULL) {
					trap->object = object;
				} else {
					old_object->next = object;
				}
				object->value = NULL;
				object->next = NULL;
				object->oid = xstrdup(token);
				status = S_OID_READ;
				break;
			case S_OID_READ:
				tokenlen = strlen(token);
				object->value = xmalloc(tokenlen + 1);
				memcpy(object->value, token, tokenlen);
				status = S_READING_VALUE;
				break;
			case S_READING_VALUE:
				if (tokenizer_found == 0) {
					tokenlen2 = strlen(token);
					object->value = xrealloc(object->value, tokenlen+tokenlen2+2);
					object->value[tokenlen] = ' ';
					memcpy(object->value+tokenlen+1, token, tokenlen2);
					tokenlen += tokenlen2 + 1;
				} else {
					object->value[tokenlen] = '\0';
					status = S_VALUE_READ;
				}
				break;
			default:
				assert(0);
				break;


		}

		tokenizer_found = 0;
	}

	if (proto_cmd == PROTO_CMD_TRAP) {

		if (status == S_READING_VALUE) {
			object->value[tokenlen] = '\0';
			status = S_VALUE_READ;
		}

		if (status != S_IPADDRESS_READ && status != S_VALUE_READ) {
			/* malformed trap */
			free_trap(trap);
			pdu->command = PROTO_CMD_INVALID;
			DEBUG("malformed trap!");
		} else {
			if (!post_parse(trap)) {
				/* cannot post parse trap */
				free_trap(trap);
				pdu->command = PROTO_CMD_INVALID;
				DEBUG("trap is not post-parsable!");
			} else {
				/* trap OK */
				pthread_mutex_lock(&trap_counters_mutex);
				trap_parsed++;
				pthread_mutex_unlock(&trap_counters_mutex);
				pdu->trap = trap;
				DEBUG("trap is OK!");
			}
		}

	}

	free(buffer);

	return pdu;
}



/*
 *     Public methods
 *
 ******************************************************************************/


struct pdu_t *trap_pdu_process(char *buffer, int is_private)
{
	return parse(buffer, is_private);
}


char *trap_get_hostname(struct trap_t *trap)
{
	if (trap == NULL)
		return NULL;
	
	return trap->hostname;
}


char *trap_get_sender_address(struct trap_t *trap)
{
	if (trap == NULL)
		return NULL;
	
	return trap->ipaddress;
}


char *trap_get_oid(struct trap_t *trap)
{
	if (trap == NULL)
		return NULL;
	
	return trap->oid;
}


char *trap_get_contents(struct trap_t *trap)
{
	struct mib_object_t *object;
	char *buffer = NULL;
	size_t oid_len, value_len, len, oldlen = 0;

	if (trap != NULL) {
		object = trap->object;

		while (object != NULL) {
			oid_len = strlen(object->oid);
			value_len = strlen(object->quoted_value);
			/* '+1' is for '=' */
			len = oid_len + value_len + 1;

			if (buffer == NULL) {
				buffer = xmalloc(len + 1);
			} else {
				buffer[oldlen-1] = ' ';
				buffer = xrealloc(buffer, oldlen + len + 1);
			}

			memcpy(buffer+oldlen, object->oid, oid_len);
			buffer[oldlen+oid_len] = '=';
			memcpy(buffer+oldlen+oid_len+1, object->quoted_value, value_len);

			oldlen += len + 1;
			object = object->next;

		}

		if (buffer != NULL) {
			buffer[oldlen-1] = '\0';
		}
	}

	return buffer;
}


unsigned int trap_get_timestamp(struct trap_t *trap)
{
	if (trap == NULL)
		return 0;
	else
		return (unsigned int) trap->seconds;
}


char *trap_pdu_get_response(struct pdu_t *pdu)
{
	char *response = NULL;

	if (pdu == NULL)
		return NULL;

	switch (pdu->command) {
		case PROTO_CMD_TRAP:
			response = xstrdup("ACCEPTED");
			break;
		case PROTO_CMD_ALIVE:
			response = xstrdup("YES");
			break;
		case PROTO_CMD_DIAGNOSTICS:
			response = diagnostics_prepare_write();
			break;
		case PROTO_CMD_INVALID:
			response = xstrdup("INVALID");
			break;
		default:
			break;
	}

	return response;
}


struct trap_t *trap_is_trap(struct pdu_t *pdu)
{
	if (pdu && pdu->command == PROTO_CMD_TRAP)
		return pdu->trap;
	else
		return NULL;
}


char *trap_serialize(struct trap_t *trap, int executed, const char *delimiter)
{
	struct mib_object_t *object;
	char *buffer;
	size_t buflen = 0, auxlen, delimit_len;
	int first_oid = 1;

	executed = !!executed;

	if (trap == NULL)
		return NULL;

	delimit_len = xstrlen(delimiter);
	
	buflen += 12; /* allow room for timestamp */
	buflen += 2;  /* allow room for ``executed'' flag (OK/NO) */
	buflen += xstrlen(trap->ipaddress);
	buflen += xstrlen(trap->hostname);
	buflen += xstrlen(trap->oid);

	buflen += 4 * delimit_len;
	auxlen = buflen;

	/* first, sum up total length */

	object = trap->object;

	while (object != NULL) {
		buflen += xstrlen(object->oid);
		buflen += xstrlen(object->quoted_value);
		buflen += 2 * max(delimit_len, 1);
		object = object->next;
	}

	buflen += 1;

	/* alloc enough space */

	buffer = xmalloc(buflen);

	/* now, copy */

	auxlen = sprintf(buffer, "%012u%s%s%s%s%s%s%s%s",
		(unsigned int) trap->seconds, delimiter,
		executed ? "OK" : "NO", delimiter, trap->hostname, delimiter, trap->ipaddress, delimiter, trap->oid);

	if (trap_serialize_mode == TRAP_SERIALIZE_MODE_OLD) {
		object = trap->object;

		while (object != NULL) {
			if (!is_empty(object->oid) && !is_empty(object->quoted_value)) {
				if (first_oid) {
					sprintf(buffer+auxlen, "%s%s;%s", delimiter, object->oid, object->quoted_value);
					auxlen += 1 + delimit_len + strlen(object->oid) + strlen(object->quoted_value);

					first_oid = 0;
				} else {
					sprintf(buffer+auxlen, ";%s;%s", object->oid, object->quoted_value);
					auxlen += 2 + strlen(object->oid) + strlen(object->quoted_value);
				}
			}
			object = object->next;
		}

	} else if (trap_serialize_mode == TRAP_SERIALIZE_MODE_NEW) {
		char *contents = trap_get_contents(trap);
		size_t contents_len = strlen(contents);

		sprintf(buffer+auxlen, "%s%s", delimiter, contents);
		free(contents);
		auxlen += delimit_len + contents_len;
	}

	buffer[auxlen] = '\0';

	return buffer;
}


void trap_free_pdu(struct pdu_t *pdu)
{
	if (pdu == NULL) {
		DEBUG("called with NULL pointer");
		return;
	}
	
	DEBUG("called");

	if (pdu->trap != NULL) {
		DEBUG("found associated trap");
		free_trap(pdu->trap);
	}
	
	free(pdu);

	DEBUG("done");
}



/*
 *     Class ``constructor''
 *
 ******************************************************************************/


void trap_init(void)
{
	trap_tokenizer = config_get_option_value(":trap_tokenizer");

	if (!strcmp(config_get_option_value(":trap_serialize_mode"), "old"))
		trap_serialize_mode = TRAP_SERIALIZE_MODE_OLD;
	else
		trap_serialize_mode = TRAP_SERIALIZE_MODE_NEW;
}



/*
 *     Callbacks for diagnostics
 *
 ******************************************************************************/


long trap_get_trap_parsed(void)
{
	long res;
	pthread_mutex_lock(&trap_counters_mutex);
	res = trap_parsed;
	pthread_mutex_unlock(&trap_counters_mutex);

	return res;
}

