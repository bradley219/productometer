#ifndef _PRODUCTOMETER_H_
#define _PRODUCTOMETER_H_
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/inotify.h>
#include <signal.h>
#include <mysql.h>
#include <debugp.h>
#include <pcre.h>
#include "md5.h"

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUFFER_SIZE (1024 * 16 * EVENT_SIZE)
#define MAX_WATCHES_PER_DEVICE 8000

/**
 * Flags
 */
int flag_recursive = 0;
int flag_testing = 0;
int flag_version = 0;

/** 
 * Misc globals
 */
int child_count = 0;
struct md5list {
	char *dir;
	unsigned char digest[16];
};
struct md5list *global_md5_list = NULL;
int global_md5_count = 0;
char *config_file = NULL;
char **in_dirs = NULL;
int in_dirs_count = 0;

/* Timeouts for select() */
time_t select_timeout_sec = 0; 
suseconds_t select_timeout_usec = 250000;

/** 
 * Directory watches 
 */
struct watch {
	int wd;
	char *dir;
};
struct watch_set {
	char *name;
	int  ind; // inotify file descriptor for this set
	int  setID;
	int  num_watches;
	struct watch *watches;
	int  recursive;
	int  num_directories;
	int  num_file_include_patterns;
	int  num_file_exclude_patterns;
	int  num_dir_include_patterns;
	int  num_dir_exclude_patterns;
	char **directories;
	char **file_include_patterns;
	char **file_exclude_patterns;
	char **dir_include_patterns;
	char **dir_exclude_patterns;
	pcre **file_include_pcres;
	pcre **file_exclude_pcres;
};
struct watch_set **watch_sets = NULL;
int watch_set_count = 0;

unsigned long global_event_count = 0;
pid_t mypid;
	
int ind; // inotify file descriptor

/**
 * Mysql/database globals
 */
char **query_queue = NULL;
int query_queue_count = 0;
MYSQL *mysql_connection = NULL; // mysql connection

const char *default_host = "192.168.0.111";
const char *default_user = "brsnyder";
const char *default_password = "";
const char *default_database = "inotify";
const char *default_computer_name = "default";
const int default_port = 3306;

char *database_host = NULL;
char *database_user = NULL;
char *database_password = NULL;
char *database_name = NULL;
int   database_port = 0;
char *computer_name = NULL;
int   computerID = 0;

/**
 * Functions
 */

/* Init Functions */
void init_signals(void);
int  init_database(void);

/* Event/misc handlers */
void signal_handler( int signo );
void inotify_event_handler( struct inotify_event *event, struct watch_set *set );

/* Inotify watch functions */
int test_file_name( char *filename, struct watch_set *set );
void setup_watches(void);
int get_watch( struct inotify_event *event, struct watch_set *set, struct watch *found_watch );
int watch_exists( int wd, int watch_count, struct watch *watches );
int add_watch2( int inotify_fd, char *dir, struct watch **watches, int *num_watches );
int recursive_add_watch2( 
		int inotify_fd, 
		char *dir, 
		int include_pattern_count,
		int exclude_pattern_count,
		char** include_patterns,
		char** exclude_patterns,
		int *watch_count,
		struct watch **watches
		);

/* String/parsing */
void parse_args( int argc, char *argv[] );
void parse_config_file( char *filename );
void parse_line( 
		char** line, 
		const char *option_name, 
		int line_num 
		);
int  test_extension( char *test_string );
int  is_dir( const char *dname );
int  explode( 
		char delim, 
		char* string, 
		char*** output_pointer 
		);
int  get_extension( char *input_string, char **output_extension );

/* Mysql/database functions */
void deinit_database(void);
void add_to_query_queue( char *input_query );
void flush_query_queue(void);
void run_query( char *query );
void add_watch_sets_to_table(void);
void get_computerID( int* compID );

/* Misc functions */
void copyright_print(void);
void add_to_global_md5_list( char *dir );
int  check_against_md5_list( char *dir );
void compile_all_pcre(void);
void print_all_watches(void);

#endif //#ifndef _PRODUCTOMETER_H_
