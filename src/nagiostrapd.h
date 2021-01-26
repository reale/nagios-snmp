/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/




#ifndef _NAGIOSTRAPD_H_
#define _NAGIOSTRAPD_H_

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <pcre.h>

/* DEBUG() */
#ifndef NDEBUG
# define DEBUG(format, ...) log_debug(__FILE__, __LINE__, __func__, format, ## __VA_ARGS__)
#else
# define DEBUG(format, ...) (void)0;
#endif

/* bool_p() */
#define bool_p(X) ((X) ? ("true") : ("false"))

/* max() */
#ifndef max
# define max(X, Y) ((X) >= (Y) ? (X) : (Y))
#endif

#define PROGNAME "nagiostrapd"
#define PROGNAME_FULL "Nagios3 SNMP Trap Daemon"

#define CONFIG_FILE "/etc/nagiostrapd/nagiostrapd.ini"

#define TMPFILE_TEMPLATE "/nagiostrapdXXXXXX"
#define TMPBUFLEN_SMALL 128
#define TMPBUFLEN 1024

#define MAX_WORKERS 32

struct stack_t;
struct stack_item_t;
struct execlist_t;
struct trap_t;
struct pdu_t;
struct threadpool;

typedef enum { LOG_VERBOSITY_DEBUG, LOG_VERBOSITY_WARNING, LOG_VERBOSITY_ERROR, LOG_VERBOSITY_CRITICAL, LOG_VERBOSITY_NONE } log_verbosity_t;

	
/* channel.c */
extern void channel_init(int);
extern void channel_write(unsigned int, const char *, const char *, int, const char *, const char *, int);
extern long channel_get_writes_host(void);
extern long channel_get_writes_svc(void);
extern size_t channel_get_written_bytes_host(void);
extern size_t channel_get_written_bytes_svc(void);
extern int channel_get_errors(void);

/* command.c */
extern char *command_expand_1(const char *);
extern char *command_expand_2(const char *, const char *, const char *);
extern char *command_expand_3(const char *, const char *, const char *, char **, int);

/* config.c */
extern void config_load(char *, const char *);
extern char *config_get_option_value(const char *);
extern void config_dump(void);

/* daemon.c */
extern void daemon_init(uid_t uid, gid_t gid);
extern unsigned int daemon_get_socket(void);
extern void daemon_close_socket(void);

/* db.c */
#define DB_LOOKUP_NEXT_BY_HOST_NAME     0x1
#define DB_LOOKUP_NEXT_BY_HOST_ADDRESS  0x2
extern void db_init(int);
extern char *db_lookup_resource(const char *);
extern int db_lookup_cmd_id_by_oid(const char *);
extern struct command_t *db_lookup_command_by_cmd_id(int);
extern char *db_lookup_filename_by_cmd_id(int);
extern int db_lookup_use_sender_address_by_cmd_id(int, int *);
extern char *db_lookup_expanded_text_by_cmd_id(int);
extern struct host_t *db_lookup_host_by_cmd_id(int, struct host_t *, int);
extern struct svc_t *db_lookup_svc_by_cmd_id(int, struct svc_t *, int);
extern struct host_t *db_lookup_host_by_host_name(const char *);
extern struct host_t *db_lookup_host_by_address(const char *);
extern struct svc_t *db_lookup_svc_by_host_name(const char *, struct svc_t *, int);
extern struct svc_t *db_lookup_svc_by_host_address(const char *, struct svc_t *, int);
extern char *db_extract_host_address(struct host_t *, struct svc_t *);
extern char *db_extract_host_name(struct host_t *, struct svc_t *);
extern char *db_extract_svc_desc(struct svc_t *);
extern char *db_extract_expanded_text(struct host_t *, struct svc_t *);

/* diagnostics.c */
extern void diagnostics_init(void);
extern char *diagnostics_prepare_write(void);
extern void diagnostics_write(void);
extern size_t diagnostics_get_padding(void);
extern char *diagnostics_get_key(int);
extern int diagnostics_get_cumulative(int);
extern int diagnostics_get_integer(int);
extern void diagnostics_pool_clear(void);

/* diagnostics2.c */
extern void diagnostics2_do(void);

/* exec.c */
extern void exec_init(void);
extern int exec_trap(struct trap_t *, const char *, const char *);

/* execlist.c */
extern struct execlist_t *execlist_build(const char *, int, int, const char *, const char *, const char *);
extern struct execlist_t *execlist_getnext(struct execlist_t *, struct execlist_t *);
extern void execlist_destroy(struct execlist_t *);
extern char *execlist_extract_command(struct execlist_t *);
extern int execlist_extract_is_service(struct execlist_t *, int *);
extern struct host_t *execlist_extract_host(struct execlist_t *);
extern struct svc_t *execlist_extract_svc(struct execlist_t *);

/* interface.c */
extern void interface_print_help(void);
extern void interface_print_version(void);
extern void interface_print_welcome(int);
#ifndef NDEBUG
extern void interface_print_debug(void);
#endif

/* key.c */
extern char *key_get(void);

/* log.c */
extern void log_init(int, int);
extern void log_debug(const char *file, long line, const char *func, const char *format, ...);
extern void log_warning(int, const char *, ...);
extern void log_error(int, const char *, ...);
extern void log_critical(int, const char *, ...);
extern size_t log_get_log_error_size(void);
extern size_t log_get_log_debug_size(void);

/* monitor.c */
extern void monitor_init(void);
extern void monitor_start_main_loop(void);
extern void monitor_register_pid(pid_t, int);
extern void monitor_kill_children(void);

/* pidfile.c */
extern void pidfile_write(void);
extern pid_t pidfile_read(void);
extern void pidfile_erase(void);

/* query.c */
extern void query_init(void);
extern char *query_fetch(const char *);
#ifndef NDEBUG
extern void query_list(void);
#endif

/* regex.c */
extern pcre* regex_compile(const char *);
extern int regex_execute(const pcre *, const char *, int *, int);

/* socket.c */
extern unsigned int socket_create(int, int, int);
extern unsigned int socket_create_unix(char *);
extern void socket_set_nonblocking(int);
extern char *socket_read_pdu(int);

/* stack.c */
typedef void (*stack_data_destructor_t)(void *);
extern struct stack_t *stack_init(stack_data_destructor_t destructor);
extern void stack_free(struct stack_t *);
extern int stack_get_depth(struct stack_t *);
extern int stack_push(struct stack_t *, void *);
extern struct stack_item_t *stack_pop(struct stack_t *);
extern void *stack_get_data(struct stack_item_t *);
extern void stack_increment_executions(struct stack_t *);
extern int stack_count_executions(struct stack_t *);

/* startup.c */
extern void startup_set_completed(int);
extern int startup_is_completed(void);

/* standalone.c */
extern void standalone_init(const char *, const char *);
extern void standalone_start_main_loop(const char *, const char *);

/* threadpool.c */
extern unsigned int threadpool_get_num_of_threads(struct threadpool *);
extern unsigned int threadpool_get_length_of_tasks_queue(struct threadpool *);
extern unsigned int threadpool_get_length_of_free_tasks_queue(struct threadpool *);

/* trap.c */
extern struct pdu_t *trap_pdu_process(char *, int);
extern char *trap_get_sender_address(struct trap_t *);
extern char *trap_get_hostname(struct trap_t *);
extern char *trap_get_oid(struct trap_t *);
extern char *trap_get_contents(struct trap_t *);
extern unsigned int trap_get_timestamp(struct trap_t *);
extern void trap_init(void);
extern char *trap_serialize(struct trap_t *, int, const char *);
extern void trap_log(struct trap_t *, int);
extern char *trap_pdu_get_response(struct pdu_t *);
extern struct trap_t *trap_is_trap(struct pdu_t *);
extern long trap_get_trap_parsed(void);
extern void trap_free_pdu(struct pdu_t *);
extern size_t trap_get_trap_log_size(void);

/* traplog.c */
extern void traplog_init(void);
extern void traplog_log(struct trap_t *, int);

/* util.c */
extern void *xmalloc(size_t);
extern void *xcalloc(size_t, size_t);
extern void *xrealloc(void *, size_t);
extern char *xstrdup(const char *);
extern char *remove_chars(const char *, const char *);
extern char *remove_spaces(const char *);
extern char *extract_filename(const char *);
extern int set_permissions(const char *);
extern int is_integer(const char *);
extern int is_empty(const char *);
extern char *quote(const char *);
extern char *remove_quotes(const char *);
extern char *backslash_quote(char *);
extern size_t xstrlen(const char *);
extern size_t get_file_size(FILE *);
extern void get_file_lock(int);
extern void release_file_lock(int);
extern float get_bogomips(void);
extern pid_t gettid(void);

/* worker.c */
extern void worker_init(int, uid_t, gid_t);
extern void worker_start_main_loop(void);
extern unsigned int worker_get_num_of_threads(void);
extern unsigned int worker_get_length_of_tasks_queue(void);
extern unsigned int worker_get_length_of_free_tasks_queue(void);
extern int worker_get_id(void);

#endif /* _NAGIOSTRAPD_H_ */
