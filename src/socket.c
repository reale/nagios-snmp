/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     socket.c --- socket-related stuff
 *
 ******************************************************************************
 ******************************************************************************/



#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "nagiostrapd.h"


/* buffer size when reading from socket */
#define SCK_BUF_SIZE 1024



unsigned int socket_create(int port_number, int reuse_addr, int local_only)
{
	int sock = 0;
	struct sockaddr_in address;


	/* create a new socket */
	sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock < 0)
		log_critical(errno, "cannot create socket");

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof reuse_addr) < 0)
		log_critical(errno, "cannot set socket options");

	/* fill-in address information */
	address.sin_family = AF_INET;
	address.sin_port = htons(port_number);

	/* we accept connections from... */
	if (local_only)
		inet_pton(AF_INET, "127.0.0.1", &address.sin_addr.s_addr);
	else
		address.sin_addr.s_addr = htonl(INADDR_ANY);

	/* bind to socket */
	if (bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
		log_critical(errno, "cannot bind()");
	
	return (unsigned int) sock;
}


unsigned int socket_create_unix(char *path)
{
	int sock = 0;
	struct sockaddr_un address;


	/* create a new socket */
	sock = socket(AF_UNIX, SOCK_STREAM, 0);

	if (sock < 0)
		log_critical(errno, "cannot create socket");

	/* fill-in address information */
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, path);

	/* unlink the file */
	unlink(address.sun_path);

	/* bind to socket */
	if (bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
		log_critical(errno, "cannot bind()");
	
	return (unsigned int) sock;
}


void socket_set_nonblocking(int sock)
{
	long arg;


	if (!!strcmp(config_get_option_value(":socket_set_nonblocking"), "true"))
		return;

	if((arg = fcntl(sock, F_GETFL, NULL)) < 0)
		log_critical(errno, "cannot fcntl(..., F_GETFL) on fd %d", sock);

	arg |= O_NONBLOCK; 

	if(fcntl(sock, F_SETFL, arg) < 0)
		log_critical(errno, "cannot set non blocking flag on fd %d", sock);
}


char *socket_read_pdu(int fd)
{
	/* input buffer */
	char in_buf[SCK_BUF_SIZE];

	/* dynamically allocated buffer */
	char *buffer;

	/* return code */
	ssize_t retcode;

	size_t alloclen, bufferlen;


	DEBUG("called on socket descriptor %d", fd);
	
	buffer = xmalloc(SCK_BUF_SIZE);
	alloclen = SCK_BUF_SIZE;
	bufferlen = 0;

	while ((retcode = recv(fd, in_buf, SCK_BUF_SIZE, 0)) > 0) {
		int i, cr_found;
		/* accept only first line */
		for (i = 0, cr_found = 0; in_buf[i] != '\0' && i < retcode; i++) {
			if (in_buf[i] == '\n' || in_buf[i] == '\r') {
				in_buf[i] = '\0';
				cr_found = 1;
				break;
			}
		}
		if (bufferlen+retcode > alloclen) {
			alloclen = bufferlen + retcode + 2*alloclen;
			buffer = xrealloc(buffer, alloclen);
		}
		memcpy(buffer+bufferlen, in_buf, retcode);
		bufferlen += retcode;

		if (cr_found) break;
	}

	if (retcode < 0) {
		/* if error ... */
		log_error(errno, "recv() error detected, possibly lost pdu...");

		return NULL;
	} else {
		/* zero-terminate buffer (make sure!) */
		buffer[bufferlen] = '\0';

		/* maybe the buffer was already zero-terminated in the cycle above */
		bufferlen = strlen(buffer);

		/* if pdu successfully received... */
		DEBUG("raw pdu received (len: %d): %s", bufferlen, buffer);
		
		return buffer;
	}
}
