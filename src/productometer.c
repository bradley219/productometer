#include "productometer.h"


int main( int argc, char *argv[] )
{
	parse_args( argc, argv );
	copyright_print();
	if( flag_version )
		return 0;


	if( config_file != NULL )
		parse_config_file( config_file );

	if( (mypid = getpid() ) < 0 ) {
		perror("getpid");
		exit(-1);
	}

	init_signals();  // set up signal handlers
	setup_watches(); // add all watches with inotify_add_watch
	print_all_watches(); // print out all watches for debugging
	compile_all_pcre();  // compile regular expressions for fast comparisons

	init_database();
	add_watch_sets_to_table();
	get_computerID(&computerID);


	char buffer[ EVENT_BUFFER_SIZE ]; // Buffer for reading inotify file
	long loopnum = 0;
	int *socklist;

	socklist = (int*)malloc( sizeof(int) * (watch_set_count + 1) );
	for( int i=0; i < watch_set_count; i++ ) 
	{
		socklist[i] = watch_sets[i]->ind;
	}
	socklist[watch_set_count] = 0;

	int *socklistp = socklist;
	int highsock = 0;
	while( *socklistp != 0 ) // check for highest number fd
	{
		if( *socklistp > highsock )
			highsock = *socklistp;
		socklistp++;
	}

	while(1) // Main infinite loop
	{
			
		debugp( 5, "loopnum = %ld\n", loopnum );

		/* Setup select() */
		fd_set socks;

		FD_ZERO( &socks );
		socklistp = socklist;
		while( *socklistp != 0 ) // add all fd's to list
		{
			FD_SET( *socklistp, &socks );
			socklistp++;
		}

		/* Select timeout value */
		struct timeval timeout;
		timeout.tv_sec = select_timeout_sec;
		timeout.tv_usec = select_timeout_usec;

		int readysocks = select( highsock + 1, &socks, (fd_set*)0, (fd_set*)0, &timeout );

		if( readysocks != 0 ) 
		{
			/* Loop through all sets, check if the inotify fd is ready */
			for( int i=0; i < watch_set_count; i++ ) 
			{
				struct watch_set *myset = watch_sets[i];
				debugp( 4, "checking if watch_set[%d] (%s) is ready... ", i, myset->name );
				if( FD_ISSET( myset->ind, &socks ) ) 
				{
					debugp( 4, "ready\n" );

					int length;
					if( ( length = read( myset->ind, (void*)&buffer, EVENT_BUFFER_SIZE ) ) < 0 ) {
						perror( "read" );
						return -4;
					}
					
					int e = 0;
					struct inotify_event *event = (struct inotify_event*)buffer;

					/* loop through events */
					while( e++ < ( length / sizeof( struct inotify_event) ) ) 
					{
						inotify_event_handler( event, myset );
						event++;
					}
					memset( buffer, 0, EVENT_BUFFER_SIZE );
				}
				else {
					debugp( 4, "not ready\n" );
				}
			}
		}
		else if( readysocks == 0 ) // Timed out
		{
			flush_query_queue();
		}

		/* See if any dead children ready to be cleaned up */
		if( child_count > 0 )
		{
			int status;
			pid_t pid;
			if( ( pid = waitpid( -1, &status, WNOHANG | WUNTRACED | WCONTINUED ) ) < 0 )
			{
				perror( "waitpid" );
				debugp( 0, "waitpid() returned -1\n" );
				exit(-1);
			}
			else if( pid > 0 )
			{
				if( WIFEXITED(status) )
				{
					debugp( 2, "child pid %d exited with status %d\n", pid, WEXITSTATUS(status) );
				}
				child_count--;
			}
		}


		loopnum++;
	}

	return 0;
}

/** 
 * SIGNALS
 */
void init_signals(void)
{
	signal( SIGALRM, signal_handler );
	signal( SIGINT, signal_handler );
	signal( SIGHUP, signal_handler );
	signal( SIGKILL, signal_handler );
	signal( SIGQUIT, signal_handler );
	signal( SIGABRT, signal_handler );
	signal( SIGTRAP, signal_handler );
	signal( SIGTERM, signal_handler );
	return;
}
void signal_handler( int signo )
{
	debugp( 1, "Received signal %d. Cleaning up..\n", signo );
	debugp( 1, "Removing all inotify watches...\n" );
	for( int j=0; j < watch_set_count; j++ )
	{
		int watch_count = watch_sets[j]->num_watches;
		for( int i=0; i < watch_count; i++ ) 
		{
			if( signo != 6 ) 
			{
				if( get_debug_level() >= 5 )
				{
					debugp( 5, "Removing watch on `%s' (%d)\n", watch_sets[j]->watches[i].dir, watch_sets[j]->watches[i].wd );
				}
				else
				{
					debugp( 4, "." );
				}
			}
			inotify_rm_watch( watch_sets[j]->ind, watch_sets[j]->watches[i].wd );
		}
	}
	debugp( 1, "\n" );

	debugp( 1, "Flushing query queue...\n" );
	flush_query_queue();

	debugp( 1, "Waiting for %d child processes to exit...\n", child_count );

	/* See if any dead children ready to be cleaned up */
	while( child_count )
	{
		int status;
		pid_t pid;
		if( ( pid = waitpid( -1, &status, WUNTRACED | WCONTINUED ) ) < 0 )
		{
			perror( "waitpid" );
			debugp( 0, "waitpid() returned -1\n" );
			exit(-1);
		}
		else if( pid > 0 )
		{
			if( WIFEXITED(status) )
			{
				debugp( 1, "child pid %d exited with status %d\n", pid, WEXITSTATUS(status) );
			}
			child_count--;
		}
	}

	if( (mysql_connection != NULL) )  
	{
		debugp( 1, "Closing mysql_connection\n" );
		mysql_close( mysql_connection );
	}

	debugp( 1, "debugp_cleanup()\n" );
	debugp_cleanup();

	debugp( 1, "Exiting.\n" );
	exit(0);
}

/** 
 * INOTIFY / WATCH FUNCTIONS
 */
void inotify_event_handler( struct inotify_event *event, struct watch_set *set )
{
	global_event_count++;

	struct watch mywatch;

	if( get_watch( event, set, &mywatch ) ) 
	{
		debugp( 2, "event->wd     = %d\n", event->wd );
		debugp( 2, "event->mask   = 0x%02x\n", event->mask );
		debugp( 2, "event->cookie = 0x%02x\n", event->cookie );
		debugp( 2, "event->len    = %u\n", event->len );
		debugp( 2, "event->name   = %s\n", event->name );

		if( event->len && strlen( event->name ) ) {

			if( test_file_name( event->name, set ) )
			{
	
				char *msg = (char*)malloc( sizeof(char) * (strlen(event->name) + 300 ) );
				sprintf( msg, "Inotify event: `%s' (", event->name );

				uint32_t mask = 0x01;
				for( int i=0; i < 32; i++ ) {
					switch( event->mask & mask ) {
						case IN_ACCESS:
							strcat( msg, "IN_ACCESS" );
							break;
						case IN_MODIFY:
							strcat( msg, "IN_MODIFY" );
							break;
						case IN_ATTRIB:
							strcat( msg, "IN_ATTRIB" );
							break;
						case IN_CLOSE_WRITE:
							strcat( msg, "IN_CLOSE_WRITE" );
							break;
						case IN_CLOSE_NOWRITE:
							strcat( msg, "IN_CLOSE_NOWRITE" );
							break;
						case IN_OPEN:
							strcat( msg, "IN_OPEN" );
							break;
						case IN_MOVED_FROM:
							strcat( msg, "IN_MOVED_FROM" );
							break;
						case IN_MOVED_TO:
							strcat( msg, "IN_MOVED_TO" );
							break;
						case IN_CREATE:
							strcat( msg, "IN_CREATE" );
							break;
						case IN_DELETE:
							strcat( msg, "IN_DELETE" );
							break;
						case IN_DELETE_SELF:
							strcat( msg, "IN_DELETE_SELF" );
							break;
						case IN_MOVE_SELF:
							strcat( msg, "IN_MOVE_SELF" );
							break;
						default:
							break;
					}
					mask <<= 1;
				}
				strcat( msg, ")\n" );
				debugp( 1, "%s", msg );
				free(msg);

				struct timeval now;
				gettimeofday( &now, NULL );

				char *fullpath = malloc( sizeof(char) * (strlen(event->name) + strlen(mywatch.dir) + 1 + 1 ) );
				sprintf( fullpath, "%s/%s", mywatch.dir, event->name );
				debugp( 4, "Fullpath: %s\n", fullpath );

				char *escaped_name = (char*)malloc( sizeof(char) * (strlen(fullpath) * 2 + 1) );
				mysql_real_escape_string( mysql_connection, escaped_name, fullpath, strlen(fullpath) );

				char *buffer = malloc( sizeof(char) * (strlen(escaped_name)+ 1000 ) );
				debugp( 4, "Escaped name: %s\n", escaped_name );

				sprintf( buffer, "INSERT IGNORE INTO `files` (`computerID`,`file`) VALUES (%d,'%s')", computerID, escaped_name );
				debugp( 4, "About to add query: %s\n", buffer );
				if( !flag_testing )
				{
					add_to_query_queue( buffer );
				}

				sprintf( buffer, "INSERT INTO `events` (`setID`,`fileID`,`mask`,`time`,`usec`) VALUES (%d,(SELECT `fileID` FROM `files` WHERE computerID=%d AND `file`='%s'),%lu,FROM_UNIXTIME(%lu),%lu)", 
						set->setID,
						computerID,
						escaped_name,
						(unsigned long)event->mask, 
						(unsigned long)now.tv_sec, 
						(unsigned long)now.tv_usec );
				debugp( 4, "About to add query: %s\n", buffer );
				if( !flag_testing )
				{
					add_to_query_queue( buffer );
				}

				free(buffer);
				free(fullpath);
				free(escaped_name);
			}
		}

		if( (mywatch.wd > 0) || (get_debug_level() >=5 )) {
			debugp( 2, "full_path     = %s/%s\n", mywatch.dir, event->name );
		}
	}

	return;
}

int get_watch( struct inotify_event *event, struct watch_set *set, struct watch *found_watch )
{
	int found = 0;

	for( int i=0; i < set->num_watches; i++ )
	{
		if( set->watches[i].wd == event->wd )
		{
			*found_watch = set->watches[i];
			found = 1;
			break;
		}
	}

	return found;
}
int watch_exists( int wd, int watch_count, struct watch *watches )
{
	int found = 0;

	for( int i=0; i < watch_count; i++ ) 
	{
		if( watches[i].wd == wd ) {
			found = 1;
			break;
		}
	}

	return found;
}

/** 
 * MYSQL / DATABASE FUNCTIONS
 */
void deinit_database(void)
{
	mysql_close( mysql_connection );
	mysql_connection = NULL;
	return;
}
int init_database(void)
{
	if( mysql_connection == NULL )
	{
		mysql_connection = mysql_init(NULL);

		my_bool reconnect = 1;
		mysql_options( mysql_connection, MYSQL_OPT_RECONNECT, &reconnect );

		/* Use defaults where missing */
		if( database_host == NULL ) database_host = (char*)default_host;
		if( database_user == NULL ) database_user = (char*)default_user;
		if( database_password == NULL ) database_password = (char*)default_password;
		if( database_name == NULL ) database_name = (char*)default_database;
		if( computer_name == NULL ) computer_name = (char*)default_computer_name;
		if( database_port == 0 ) database_port = default_port;

		debugp( 1, "Connecting to MySQL database with:\n" );
		debugp( 1, "   host:     %s\n", database_host );
		debugp( 1, "   user:     %s\n", database_user);
		debugp( 1, "   password: %s\n", database_password);
		debugp( 1, "   database: %s\n", database_name);
		debugp( 1, "   port:     %d\n", database_port);

		if( !mysql_real_connect( 
					mysql_connection, 
					database_host, 
					database_user, 
					database_password, 
					database_name, 
					database_port, 
					NULL /*unix socket*/, 
					0    /*client flag*/ 
					) 
				) {
			debugp( 0, "%s\n", mysql_error(mysql_connection) );
			return(-1);
		}
	}
	return 0;
}
void add_watch_sets_to_table(void)
{
	for( int i=0; i < watch_set_count; i++ ) 
	{
		char *query = NULL;
		char *escaped_name = NULL;
		char *name = watch_sets[i]->name;

		escaped_name = (char*)malloc( sizeof(char) * ( strlen(name) * 2 + 1 ) );
		query = (char*)malloc( sizeof(char) * ( strlen(name) * 2 + 1000 + 1 ) );

		mysql_real_escape_string( mysql_connection, escaped_name, name, strlen(name) );
		sprintf(query, "INSERT IGNORE INTO `sets` (`set`) VALUES ('%s')", escaped_name );
		run_query(query);

		sprintf(query, "SELECT `setID` FROM `sets` WHERE `set` = '%s'", escaped_name );
		run_query(query);

		MYSQL_RES *result;
		result = mysql_store_result( mysql_connection );

		MYSQL_ROW row;

		int setID = 0;
		while( ( row = mysql_fetch_row( result ) ) != NULL )
		{
			setID = atoi( row[0] );
		}

		watch_sets[i]->setID = setID;

		free(escaped_name);
		free(query);
	}
	return;
}
void get_computerID( int *compID )
{
	char *query = NULL;
	char *escaped_name = NULL;

	escaped_name = (char*)malloc( sizeof(char) * ( strlen(computer_name) * 2 + 1 ) );
	query = (char*)malloc( sizeof(char) * ( strlen(computer_name) * 2 + 1000 + 1 ) );

	mysql_real_escape_string( mysql_connection, escaped_name, computer_name, strlen(computer_name) );
	sprintf(query, "INSERT IGNORE INTO `computers` (`computer`) VALUES ('%s')", escaped_name );
	run_query(query);

	sprintf(query, "SELECT `computerID` FROM `computers` WHERE `computer` = '%s'", escaped_name );
	run_query(query);

	MYSQL_RES *result;
	result = mysql_store_result( mysql_connection );

	MYSQL_ROW row;

	while( ( row = mysql_fetch_row( result ) ) != NULL )
	{
		*compID = atoi( row[0] );
	}

	debugp( 3, "computerID = %d\n", *compID );


	free(escaped_name);
	free(query);
	return;
}
void run_query( char *query )
{
	int retval;
	if( mysql_ping(mysql_connection) ) {
		perror( "mysql_ping" );
	}
	if( ( retval = mysql_query( mysql_connection, query ) ) ) {
		debugp( 0, "%s\n", mysql_error( mysql_connection ));
	}
	return;
}
void add_to_query_queue( char *input_query )
{
	char *query = NULL;
	query = (char*)malloc( sizeof(char) * (strlen(input_query) + 1) );
	query[0] = '\0';
	strcat(query, input_query);

	query_queue_count++;
	query_queue = (char**)realloc( query_queue, sizeof(char*) * (query_queue_count) );
	query_queue[query_queue_count-1] = query;

	debugp( 3, "Added query: `%s'\n", query );

	return;
}
void flush_query_queue(void)
{
	if( init_database() < 0 ) {
		perror( "init_database" );
	}
	if( mysql_ping(mysql_connection) ) {
		perror( "mysql_ping" );
	}

	/* If there are queries to be ran, fork off a child process */
	if( query_queue_count )
	{
		debugp( 4, "forking a child...\n" );

		pid_t pid = fork();

		if( pid == 0 )
		{
			/* Child process */

			sigset_t sset;
			sigemptyset(&sset);
			sigaddset( &sset, SIGALRM );
			sigaddset( &sset, SIGINT  );
			sigaddset( &sset, SIGHUP  );
			sigaddset( &sset, SIGKILL );
			sigaddset( &sset, SIGQUIT );
			sigaddset( &sset, SIGTRAP );
			sigaddset( &sset, SIGTERM );

			sigprocmask( SIG_BLOCK, &sset, NULL );

			if( (mypid = getpid() ) < 0 ) {
				perror("getpid");
				exit(-1);
			}
			debugp( 4, "Child process %d spawned\n", mypid );

			for( int i=0; i < query_queue_count; i++ )
			{
				debugp( 2, "Flushing query: `%s'\n", query_queue[i] );
				run_query( query_queue[i] );
				free( query_queue[i] );
			}
			free( query_queue );
			query_queue_count = 0;
			query_queue = NULL;

			exit(0);

		}
		/* Parent process */
		else
		{
			child_count++;
			free( query_queue );
			query_queue_count = 0;
			query_queue = NULL;
		}
	}
	return;
}



/** 
 * GENERAL TEXT PARSING / FILE MANAGEMENT
 */
int explode( char delim, char* string, char*** output_pointer )  /* Similar to strtok */
{
	debugp( 5, "explode( %c, %s, %p )\n", delim, string, output_pointer );
	char *s = string;
	char *part = NULL;
	int part_length = 0;
	int num_parts = 0;

	char **output = NULL;

	while( *s != '\0' )
	{
		debugp( 5, "*s = %c ", *s );
		part = (char*)realloc( part, sizeof(char) * (part_length + 1) ); /* Allocate 1 more byte of memory for the part string. 
																		  If the next character is a delimiter, this additional 
																		  byte will store the null terminator. */
		debugp( 5, "part->%p\n", part );
		if( *s == delim )
		{
			part[part_length] = '\0';
			debugp( 5, "part: %s\n", part );
			if( strlen(part) ) {
				num_parts++;
				output = (char**)realloc( output, sizeof(char*) * num_parts );
				output[num_parts-1] = part;
			}
			part = NULL;
			part_length = 0;
		}
		else  // add character to part
		{
			part_length++;
			part[part_length-1] = *s;
		}
		s++;
	}
	debugp( 5, "null found\n" );
	if( *(s-1) != delim ) // Add the last part
	{
		part_length++;
		part = (char*)realloc( part, sizeof(char) * (part_length+1) ); 
		part[part_length] = '\0';
		if( strlen(part) ) {
			num_parts++;
			output = (char**)realloc( output, sizeof(char*) * num_parts );
			output[num_parts-1] = part;
		}
		part = NULL;
	}

	*output_pointer = output;

	return num_parts;
}
int get_extension( char *input_string, char **output_extension )
{
	int ext_length = 0;
	char *s = input_string;
	char *extension = NULL;

	while( (*s++ != '.') && (*s != '\0') );

	while( *s != '\0' ) {
		debugp( 4, "testing *s = %c\n", *s );
		if( *s == '.' )  // another '.' found; reset counters
		{
			ext_length = 0;
			if( extension != NULL )
				free( extension );
			extension = NULL;
		}
		else 
		{
			ext_length++;
			extension = (char*)realloc( extension, sizeof(char) * (ext_length+1) );
			extension[ext_length-1] = *s;
			extension[ext_length] = '\0';
			debugp( 4, "ext=%s\n", extension );
		}
	
		s++;
	}

	*output_extension = extension;

	return ext_length;
}
int is_dir( const char *dname )
{
	struct stat sbuf;
	int retval = 0;
				
	debugp( 4, "Testing if %s is a directory.. ", dname );

	if( lstat(dname, &sbuf) == -1 ) {
		debugp( 0, "lstat() Failed.\n");
		retval = -1;
	}

	if( S_ISDIR( sbuf.st_mode ) ) {
		debugp( 4, "yes\n" );
		retval = 1;
	}
	else {
		debugp( 4, "no\n" );
		retval = 0;
	}

	return retval;
}
void parse_args( int argc, char *argv[] )
{

	struct option long_options[] =
	{
		{ "syslog", optional_argument, NULL, 0 },
		{ "version", optional_argument, NULL, 'V' },
		{ "testing", optional_argument, NULL, 't' },
		{ "extensions", required_argument, NULL, 'e' },
		{ "verbose", optional_argument, NULL, 'v' },
		{ "recursive", optional_argument, NULL, 'r' },
		{ "directory", required_argument, NULL, 'd' },
		{ "select-timeout-sec", required_argument, NULL, 0 },
		{ "select-timeout-usec", required_argument, NULL, 0 },
		{ 0, 0, 0, 0 }
	};
	int long_options_index;


	int c;
	while( ( c = getopt_long( argc, argv, "c:trvd:e:V", long_options, &long_options_index )) != -1 ) 
	{

		switch(c) {
			case 0: /* Long options with no short equivalent */
				if( strcmp( long_options[long_options_index].name, "select-timeout-sec" ) == 0 ) {
					select_timeout_sec = atoi( optarg );
					debugp( 4, "select timeout seconds = %d\n", select_timeout_sec );
				}
				else if( strcmp( long_options[long_options_index].name, "select-timeout-usec" ) == 0 ) {
					select_timeout_usec = atoi( optarg );
					debugp( 4, "select timeout microseconds = %d\n", select_timeout_usec );
				}
				else if( strcmp( long_options[long_options_index].name, "syslog" ) == 0 ) {
					debugp( 0, "Changing debug facility to syslog... goodbye!\n" );
					setup_debugp_syslog( "productometer" );
					change_debug_facility( DEBUGP_SYSLOG );
					debugp( 4, "changed debug facility to syslog\n" );
				}
				break;
			case 'c': // config file
				config_file = optarg;
				break;
			case 'd':
				if( optarg == NULL ) {
					debugp( 0, "No directory was specified!\n" );
				}
				else {
					in_dirs_count++;	
					in_dirs = (char**)realloc( in_dirs, sizeof(char*) * in_dirs_count );
					in_dirs[in_dirs_count-1] = optarg;

					debugp( 1, "New directory specified: `%s'\n", optarg );
					debugp( 2, "Current directory list:\n" );
					for( int i = 0; i < in_dirs_count; i++ ) 
					{
						debugp( 2, "\t%s\n", in_dirs[i] );
					}
				}

				break;
			case 'V':
				flag_version = 1;
				break;
			case 't': // Testing only... do not connect to MySQL or run queries
				flag_testing = 1;
				break;
			case 'r':
				flag_recursive++;
				debugp( 1, "Recursive directory listing enabled\n" );
				break;
			case 'v':
				change_debug_level_by(1);
				debugp( 1, "Verbosity increased to %d\n", get_debug_level() );
				break;
			default:
				break;
		}
	}

	return;
}
void parse_config_file( char *filename )
{
	debugp( 4, "parse_config_file( %s )\n", filename );
	if( filename != NULL )
	{
		FILE *fp; // file pointer
		char *config; // memory to store configuration file copy
		int size;

		if( ( fp = fopen( filename, "rb" ) ) > 0 ) {

			fseek( fp, 0, SEEK_END );
			size = ftell( fp );
			fseek( fp, 0, SEEK_SET );

			config = (char *)malloc( sizeof(char) * (size + 1) );

			if( size != fread( config, sizeof(char), size, fp ) ) 
			{
				free(config);
				size = -2;
				config = NULL;
				fclose(fp);
			}
			else {
				fclose(fp);
				config[size] = '\0'; // null terminate, start parsing

				debugp( 4, "Config file:\n%s\n", config );

				char **lines = NULL;
				int num_lines = explode( '\n', config, &lines ); // split into lines
				free( config ); /* free the memory we read the file into. The **lines
								   pointer has been allocated enough memeory to hold the config
								   file. not freeing would result in double memory usage for 
								   storing the file */

				struct watch_set *current_set = NULL; /* always will point to the current set 
														 that we are parsing */
				for( int l=0; l < num_lines; l++ )
				{
					debugp( 4, "Line: %s\n", lines[l] );

					if( lines[l][0] == '[' ) // Begin of new set
					{
						current_set = (struct watch_set*)malloc( sizeof(struct watch_set) ); /* allocate new memory for an empty 
																								struct watch_set */

						/* Reallocate the array that holds all of the struct watch_set's for the program and add this one ^ to it */
						watch_set_count++;
						watch_sets = (struct watch_set**)realloc( watch_sets, sizeof(struct watch_set*) * watch_set_count );
						watch_sets[watch_set_count-1] = current_set;

						/* Init counts to zero */
						current_set->ind = 0;
						current_set->recursive = 0;
						current_set->num_directories = 0;
						current_set->num_watches = 0;
						current_set->num_file_include_patterns = 0;
						current_set->num_file_exclude_patterns = 0;
						current_set->num_dir_include_patterns = 0;
						current_set->num_dir_exclude_patterns = 0;

						/* Init pointers to NULL */
						current_set->watches = NULL;
						current_set->directories = NULL;
						current_set->file_include_patterns = NULL;
						current_set->file_exclude_patterns = NULL;
						current_set->dir_include_patterns = NULL;
						current_set->dir_exclude_patterns = NULL;

						/* Name of the set is also on this line, so parse it */
						current_set->name = lines[l] + 1;

						char *c = current_set->name;
						int found_delim = 0;
						while( *c++ != '\0' ) 
						{
							if( *c == ']' ) {
								*c = '\0';
								found_delim = 1;
								break;
							}
						}
						if( !found_delim ) {
							debugp( 0, "Error, did not find matching delimiter `]' in config file\n" );
							exit(-1);
						}
						debugp( 4, "\tNew set: %s\n", current_set->name );
					}
					/* For the rest of the lines, determine what to do based on what the first part of the line looks like */
					else if( lines[l][0] == '#' ) // comment line
					{
						debugp( 4, "\tComment: %s\n", lines[l] + 1 );
						free( lines[l] );
					}
					/* Database host */
					else if( ( strncmp( lines[l], "database_host", strlen( "database_host" ) ) ) == 0 ) 
					{
						parse_line( &lines[l], "database_host", l );
						database_host = lines[l];
						debugp( 4, "\tdatabase_host = %s\n", database_host );
					}
					/* Database username */
					else if( ( strncmp( lines[l], "database_user", strlen( "database_user" ) ) ) == 0 ) 
					{
						parse_line( &lines[l], "database_user", l );
						database_user = lines[l];
						debugp( 4, "\tdatabase_user = %s\n", database_user );
					}
					/* Database password */
					else if( ( strncmp( lines[l], "database_password", strlen( "database_password" ) ) ) == 0 ) 
					{
						parse_line( &lines[l], "database_password", l );
						database_password = lines[l];
						debugp( 4, "\tdatabase_password = %s\n", database_password );
					}
					/* Database name */
					else if( ( strncmp( lines[l], "database_name", strlen( "database_name" ) ) ) == 0 ) 
					{
						parse_line( &lines[l], "database_name", l );
						database_name= lines[l];
						debugp( 4, "\tdatabase_name = %s\n", database_name );
					}
					/* Database port */
					else if( ( strncmp( lines[l], "database_port", strlen( "database_port" ) ) ) == 0 ) 
					{
						parse_line( &lines[l], "database_port", l );
						database_port = atoi( lines[l] );
						debugp( 4, "\tdatabase_port = %d\n", database_port );
					}
					/* Computer name */
					else if( ( strncmp( lines[l], "computer_name", strlen( "computer_name" ) ) ) == 0 ) 
					{
						parse_line( &lines[l], "computer_name", l );
						computer_name = lines[l];
						debugp( 4, "\tcomputer_name = %s\n", computer_name );
					}
					/* Directories are to be recursed */
					else if( ( strncmp( lines[l], "recursive", strlen( "recursive" ) ) ) == 0 ) 
					{
						if( current_set != NULL ) // Make sure we are in a set
						{
							debugp( 4, "\tRecursive\n" );
							current_set->recursive = 1;
						}
						else {
							debugp( 0, "Error: line (%d) is not within a set: %s\n", l, lines[l] );
							exit(-1);
						}
						free( lines[l] );
					}
					/* Line is a directories descriptor */
					else if( ( strncmp( lines[l], "directory", strlen( "directory" ) ) ) == 0 ) 
					{
						if( current_set != NULL ) // Make sure we are in a set
						{
							parse_line( &lines[l], "directory", l );

							current_set->num_directories++;
							current_set->directories = (char**)realloc( 
									current_set->directories,
									sizeof(char*) * current_set->num_directories );
							current_set->directories[current_set->num_directories-1] = lines[l];

						}
						else {
							debugp( 0, "Error: line (%d) is not within a set: %s\n", l, lines[l] );
							exit(-1);
						}
					}
					/* Line is a file_include_pattern descriptor */
					else if( ( strncmp( lines[l], "file_include_pattern", strlen( "file_include_pattern" ) ) ) == 0 ) 
					{
						if( current_set != NULL ) // Make sure we are in a set
						{
							parse_line( &lines[l], "file_include_pattern", l );
							current_set->num_file_include_patterns++;
							current_set->file_include_patterns = (char**)realloc( 
									current_set->file_include_patterns,
									sizeof(char*) * current_set->num_file_include_patterns );
							current_set->file_include_patterns[current_set->num_file_include_patterns-1] = lines[l];
						}
						else {
							debugp( 0, "Error: line (%d) is not within a set: %s\n", l, lines[l] );
							exit(-1);
						}
					}
					/* Line is a file_exclude_pattern descriptor */
					else if( ( strncmp( lines[l], "file_exclude_pattern", strlen( "file_exclude_pattern" ) ) ) == 0 ) 
					{
						if( current_set != NULL ) // Make sure we are in a set
						{
							parse_line( &lines[l], "file_exclude_pattern", l );
							current_set->num_file_exclude_patterns++;
							current_set->file_exclude_patterns = (char**)realloc( 
									current_set->file_exclude_patterns,
									sizeof(char*) * current_set->num_file_exclude_patterns );
							current_set->file_exclude_patterns[current_set->num_file_exclude_patterns-1] = lines[l];
						}
						else {
							debugp( 0, "Error: line (%d) is not within a set: %s\n", l, lines[l] );
							exit(-1);
						}
					}
					/* Line is a dir_include_pattern descriptor */
					else if( ( strncmp( lines[l], "dir_include_pattern", strlen( "dir_include_pattern" ) ) ) == 0 ) 
					{
						if( current_set != NULL ) // Make sure we are in a set
						{
							parse_line( &lines[l], "dir_include_pattern", l );
							current_set->num_dir_include_patterns++;
							current_set->dir_include_patterns = (char**)realloc( 
									current_set->dir_include_patterns,
									sizeof(char*) * current_set->num_dir_include_patterns );
							current_set->dir_include_patterns[current_set->num_dir_include_patterns-1] = lines[l];
						}
						else {
							debugp( 0, "Error: line (%d) is not within a set: %s\n", l, lines[l] );
							exit(-1);
						}
					}
					/* Line is a dir_exclude_pattern descriptor */
					else if( ( strncmp( lines[l], "dir_exclude_pattern", strlen( "dir_exclude_pattern" ) ) ) == 0 ) 
					{
						if( current_set != NULL ) // Make sure we are in a set
						{
							parse_line( &lines[l], "dir_exclude_pattern", l );
							current_set->num_dir_exclude_patterns++;
							current_set->dir_exclude_patterns = (char**)realloc( 
									current_set->dir_exclude_patterns,
									sizeof(char*) * current_set->num_dir_exclude_patterns );
							current_set->dir_exclude_patterns[current_set->num_dir_exclude_patterns-1] = lines[l];
						}
						else {
							debugp( 0, "Error: line (%d) is not within a set: %s\n", l, lines[l] );
							exit(-1);
						}
					}
				}

				for( int s=0; s < watch_set_count; s++ )
				{
					debugp( 4, "Set number %d\n", s );
					debugp( 4, "   recursive->%d\n", watch_sets[s]->recursive );
					debugp( 4, "   name->%s\n", watch_sets[s]->name );
					debugp( 4, "   num_directories->%d\n", watch_sets[s]->num_directories );
					for( int fi=0; fi < watch_sets[s]->num_directories; fi++ )
					{
						debugp( 4, "      directories[%d]->%s\n", fi, watch_sets[s]->directories[fi] );
					}
					debugp( 4, "   num_file_include_patterns->%d\n", watch_sets[s]->num_file_include_patterns );
					for( int fi=0; fi < watch_sets[s]->num_file_include_patterns; fi++ )
					{
						debugp( 4, "      file_include_patterns[%d]->%s\n", fi, watch_sets[s]->file_include_patterns[fi] );
					}
					debugp( 4, "   num_file_exclude_patterns->%d\n", watch_sets[s]->num_file_exclude_patterns );
					for( int fi=0; fi < watch_sets[s]->num_file_exclude_patterns; fi++ )
					{
						debugp( 4, "      file_exclude_patterns[%d]->%s\n", fi, watch_sets[s]->file_exclude_patterns[fi] );
					}
					debugp( 4, "   num_dir_include_patterns->%d\n", watch_sets[s]->num_dir_include_patterns );
					for( int fi=0; fi < watch_sets[s]->num_dir_include_patterns; fi++ )
					{
						debugp( 4, "      dir_include_patterns[%d]->%s\n", fi, watch_sets[s]->dir_include_patterns[fi] );
					}
					debugp( 4, "   num_dir_exclude_patterns->%d\n", watch_sets[s]->num_dir_exclude_patterns );
					for( int fi=0; fi < watch_sets[s]->num_dir_exclude_patterns; fi++ )
					{
						debugp( 4, "      dir_exclude_patterns[%d]->%s\n", fi, watch_sets[s]->dir_exclude_patterns[fi] );
					}

				}
				debugp( 4, "Config file parsed!\n" );
			}
		}
	}
	if( !watch_set_count ) 
	{
		debugp( 0, "No watch sets were defined in config file\n" );
		exit(-1);
	}
	return;
}
void setup_watches(void)
{

	/* Loop through all watch sets */
	for( int w=0; w < watch_set_count; w++ ) 
	{
		debugp( 4, "**** Watch Set #%d ****\n", w );

		/* Pointer to the watch set */
		struct watch_set *myset = watch_sets[w];

		/**
		 * Set up a new inotify fd
		 */
		if( ( myset->ind = inotify_init() ) < 0 ) {
			perror( "inotify_init" );
			exit(-2);
		}
		debugp( 3, "myset->ind=%d\n", myset->ind );


		/* Add directories */
		myset->num_watches = 0;
		for( int i = 0; i < myset->num_directories; i++ ) 
		{
			if( myset->recursive == 1 ) {
				recursive_add_watch2( 
						myset->ind,
						myset->directories[i],
						myset->num_dir_include_patterns,
						myset->num_dir_exclude_patterns,
						myset->dir_include_patterns,
						myset->dir_exclude_patterns,
						&myset->num_watches,
						&myset->watches
						);
			}
			//else {
			//	myset->num_watches += add_watch( ind, in_dirs[i] );
			//}
		}
	
		if( myset->num_watches == 0 ) {
			debugp( 4, "myset->num_watches == 0; closing inotify fd\n" );
			close( myset->ind );	
		}
	}

	free( global_md5_list );
	return;
}
int test_dir_name( 
		char *dir, 
		int include_pattern_count,
		int exclude_pattern_count,
		char** include_patterns,
		char** exclude_patterns
		)
{
	int retval = 0; // 0 -> no good; !0 -> good
	if( include_pattern_count == 0 ) {
		retval = 1;
	}
	else {
		retval = 0; /* Assume the dir is no good until proven good by an include pattern */
		for( int i=0; i < include_pattern_count; i++ )
		{
			char *pattern = include_patterns[i];
			debugp( 4, "   Testing `%s' against include pattern `%s'... ", dir, pattern );
			char errmsg[1000];
			int erroffset;
			pcre *cpattern = pcre_compile( pattern, 0, (const char**)&errmsg, &erroffset, NULL );

			if( pcre_exec( cpattern, NULL, dir, strlen(dir), 0, 0, NULL, 0 ) == 0 )
			{
				debugp( 4, "success!\n" );
				retval = 1;
				break;
			}
			else
			{
				debugp( 4, "failed\n" );
			}
		}
	}

	if( retval && exclude_pattern_count )  /* if the dir is still good and there 
													  are exclude patterns to check against */
	{
		for( int i=0; i < exclude_pattern_count; i++ )
		{
			char *pattern = exclude_patterns[i];
			debugp( 4, "   Testing `%s' against exclude pattern `%s'... ", dir, pattern );
			char errmsg[1000];
			int erroffset;
			pcre *cpattern = pcre_compile( pattern, 0, (const char**)&errmsg, &erroffset, NULL );

			if( pcre_exec( cpattern, NULL, dir, strlen(dir), 0, 0, NULL, 0 ) == 0 )
			{
				debugp( 4, "success!\n" );
				retval = 0;
				break;
			}
			else
			{
				debugp( 4, "failed\n" );
			}
		}
	}
	debugp( 4, "   Dir `%s' %s\n", dir, (retval) ? "passes!" : "fails!" );
	return retval;
}

int recursive_add_watch2( 
		int inotify_fd, 
		char *dir, 
		int include_pattern_count,
		int exclude_pattern_count,
		char** include_patterns,
		char** exclude_patterns,
		int *watch_count,
		struct watch **watches
		)
{
	debugp( 4, "inotify_fd = %d\n", inotify_fd );
	int watches_added = 0;
	int error_happened = 0;

	DIR *dip;
	struct dirent *dit;
	if( ( dip = opendir( dir ) ) == NULL ) 
	{
		perror("opendir");
		exit(-1);
	}

	if( test_dir_name( 
		dir, 
		include_pattern_count,
		exclude_pattern_count,
		include_patterns,
		exclude_patterns
		)  
			&&
			(add_watch2( inotify_fd, dir, watches, watch_count ) >= 0)
	  )
	{
		watches_added++;
		while( (( dit = readdir(dip) ) != NULL) && (!error_happened) )
		{
			if( strcmp( dit->d_name, "." ) && strcmp( dit->d_name, ".." ) ) { // Make sure we don't test `.' or `..'

				/* Allocate memory for the full path name */
				char *fullpath = NULL;
				fullpath = (char*)malloc( sizeof(char) * (strlen(dir) + strlen(dit->d_name) + 2 ) );
				sprintf( fullpath, "%s/%s", dir, dit->d_name );

				if( is_dir( fullpath ) == 0 ) // not a directory, free the memory
				{
					free( fullpath );
				}
				else { // is a di=ectory... 
					if( test_dir_name( 
						dir, 
						include_pattern_count,
						exclude_pattern_count,
						include_patterns,
						exclude_patterns
						) 
							&&
							(add_watch2( inotify_fd, dir, watches, watch_count ) >= 0)
					  )
					{
						watches_added++;
						debugp( 4, "watches_added=%d\n", watches_added );
						watches_added += recursive_add_watch2( 
												inotify_fd,
												fullpath, 
												include_pattern_count,
												exclude_pattern_count,
												include_patterns,
												exclude_patterns,
												watch_count,
												watches
												);
					}
					else {
						error_happened = 1;
					}
				}
				//debugp( 4, "\n" );
			}
		}
	}
	else 
	{
		error_happened = 1;
	}
	if( closedir(dip) < 0 )
	{
		perror( "closedir" );
		exit(-1);
	}
	if( error_happened ) {
		debugp( 3, "An error occurred which inturrupted recursive_add_watch()\n" );
	}
	return watches_added;
}
int add_watch2( int inotify_fd, char *dir, struct watch **watches, int *num_watches )
{
	debugp( 4, "add_watch( %d, %s, %p, %p );\n", inotify_fd, dir, watches, num_watches );
	int wd = 0;  // inotify watch descriptor
	
	if( (*num_watches < MAX_WATCHES_PER_DEVICE)
			&&
			(check_against_md5_list( dir ) == 0) ) // check that this dir hasn't been added under another set
	{
		if( ( wd = inotify_add_watch( inotify_fd, dir, 
						IN_MODIFY |
						IN_CREATE /*|
						IN_CLOSE_WRITE |
						IN_ACCESS |
						IN_ATTRIB |
						IN_CLOSE_NOWRITE |
						IN_OPEN |
						IN_MOVED_FROM |
						IN_MOVED_TO |
						IN_DELETE |
						IN_DELETE_SELF |
						IN_MOVE_SELF */
						) ) <= 0 ) 
		{
			perror( "inotify_add_watch" );
			debugp( 0, "   inotify_add_watch( %d, %s ); failed\n", inotify_fd, dir );
			exit(-1);
		}
		else
		{
			debugp( 2, "   inotify_add_watch( %d, %s ) successful\n", inotify_fd, dir );
			if( (watch_exists( wd, *num_watches, *watches ) == 0) ) // watch doesn't exist; add it to local list
			{
				debugp( 4, "watch does not yet exist\n" );

				*num_watches = *num_watches + 1;
				debugp( 4, "*num_watches now %d\n", *num_watches );

				debugp( 4, "*watches => %p\n", *watches );
				*watches = (struct watch*)realloc( *watches, sizeof(struct watch) * ( *num_watches ) );
				debugp( 4, "*watches => %p\n", *watches );
				debugp( 4, "realloc didn't crash\n" );

				int idx = *num_watches-1;
				struct watch *wat = *watches;
				wat += idx;

				wat->wd = wd;
				wat->dir = dir;

				debugp( 3, "   Added watch on %s (%d)\n", wat->dir, wat->wd );

				add_to_global_md5_list( wat->dir );

			}
			else {
				debugp( 3, "   Did not add watch on %s (%d already existed)\n", dir, wd );
			}
		}
	}
	else 
	{
		if( !(*num_watches < MAX_WATCHES_PER_DEVICE) ) 
		{
			debugp( 0, "Error: Max number of watches (%d) reached\n", *num_watches );
			wd = -1;
		}
		else
		{
			debugp( 2, "Warning: `%s' already is a member of another set. Not adding...\n", dir );
			wd = 0;
		}
	}

	return wd;
}

void copyright_print(void)
{
	debugp( 0, "\n" );
	debugp( 0, "productometer version 0.1, copyright (c) 2011 Bradley J. Snyder\n" );
	//debugp( 0, "   built on " BUILD_TIME "\n" );
	debugp( 0, "   the latest in productometry technology\n" );
	debugp( 0, "\n" );
}

void parse_line( char** line, const char *option_name, int line_num )
{
	char *c = *line;
	char quote_delim = 0;
	c += strlen( option_name ) - 1;
	while( *c++ != '\0' ) 
	{
		while( (*c == ' ') && (*c != '\0') ) /* eat up spaces */
			c++;
		if( *c != '=' ) {
			debugp( 0, "Error: malformed line expected '=' (%d) in config file: %s\n", line_num, *line );
			exit(-1);
		}
		c++;
		while( (*c == ' ') && (*c != '\0') ) /* eat up spaces */
			c++;

		if( (*c == '"') || (*c == '\'' ) ) {
			quote_delim = *c;
			c++;
		}

		break;
	}

	char *start = c; // Keep track of where the string starts

	/* If a leading delimiter exists */
	if( quote_delim != 0 ) 
	{
		/* Trim off the trailing quote delimiter */
		int found_delim = 0;
		while( *c++ != '\0' ) 
		{
			if( *c == quote_delim ) {
				if( *(c-1) != '\\' ) {
					*c = '\0';
					found_delim = 1;
					break;
				}
			}
		}
		if( !found_delim ) {
			debugp( 0, "Error, did not find matching delimiter `\"' in line %d: %s\n", line_num, line );
			exit(-1);
		}
	}

	debugp( 4, "String `%s' parsed from line %d\n", start, line_num );

	/* set the pointer given to us forward to the start of the argument */
	*line = start;

	return;
}
int  check_against_md5_list( char *dir )
{
	int found = 0;

	/* Calculate md5 */
	MD5_CTX mdContext;
	unsigned int len = strlen(dir);
	MD5Init(&mdContext);
	MD5Update(&mdContext, (unsigned char*)dir, len);
	MD5Final(&mdContext);

	for( int i=0; i < global_md5_count; i++ )
	{
		debugp( 5, "[%d]\t%s - ", i, global_md5_list[i].dir );

		/* compare digests */
		int matched = 0;
		for( int j=0; j < 16; j++ ) 
		{
			debugp( 5, "%02x vs %02x ", 
					(unsigned int)global_md5_list[i].digest[j],
					(unsigned int)mdContext.digest[j]
					);
			if( global_md5_list[i].digest[j] != mdContext.digest[j] ) {
				debugp( 5, "no match\n" );
				break;
			}
			else {
				debugp( 5, "match!!!\n" );
				matched++;
			}
		}
		if( matched == 16 ) {
			debugp( 5, "md5 sums match. checking strings..." );
			debugp( 5, "testing: \n\t%s\n\tvs\n\t%s\n", 
					global_md5_list[i].dir,
					dir
					);
			if( strcmp( global_md5_list[i].dir, dir ) == 0 ) {
				debugp( 5, "directories match!! not adding...\n" );
				found = 1;
				break;
			}
		}
	}

	return found;
}
void add_to_global_md5_list( char *dir )
{
	debugp( 3, "   Adding %s to global md5 list\n", dir );

	global_md5_count++;
	global_md5_list = (struct md5list*)realloc( global_md5_list, sizeof(struct md5list) * global_md5_count );
	global_md5_list[global_md5_count-1].dir = dir;

	MD5_CTX mdContext;
	unsigned int len = strlen(dir);

	MD5Init(&mdContext);
	MD5Update(&mdContext, (unsigned char*)dir, len);
	MD5Final(&mdContext);

	/* copy digest */
	for( int i=0; i < 16; i++ ) 
	{
		global_md5_list[global_md5_count-1].digest[i] = mdContext.digest[i];
		debugp( 4, "%02x", (unsigned int)mdContext.digest[i] );
	}
	debugp( 4, "\n" );

	return;
}
void print_all_watches(void)
{
	for( int i=0; i < watch_set_count; i++ ) 
	{
		struct watch_set *myset = watch_sets[i];
		debugp( 4, "watch_sets[%d] (%s)\n", i, myset->name );

		for( int j=0; j < myset->num_watches; j++ )
		{
			struct watch mywatch = myset->watches[j];
			debugp( 4, "wd(%d) -> %s\n", mywatch.wd, mywatch.dir );
		}

	}
	return;
}
void compile_all_pcre(void)
{
	for( int i=0; i < watch_set_count; i++ ) 
	{
		struct watch_set *myset = watch_sets[i];

		const char *errmsg;
		int erroffset;

		myset->file_include_pcres = (pcre**)malloc( sizeof(pcre*) * myset->num_file_include_patterns );
		for( int j=0; j < myset->num_file_include_patterns; j++ ) 
		{
			if( (myset->file_include_pcres[j] = pcre_compile( 
							myset->file_include_patterns[j], 
							0, 
							&errmsg, 
							&erroffset, 
							NULL ) ) == NULL ) {
				debugp( 0, "error compiling file_include_pattern `%s' at offset %d: %s\n", myset->file_include_patterns[j], erroffset, errmsg );
				exit(-1);
			}
			else
				debugp( 4, "`%s' compiled successfully at %p\n", myset->file_include_patterns[j], myset->file_include_pcres[j] );
		}

		myset->file_exclude_pcres = (pcre**)malloc( sizeof(pcre*) * myset->num_file_exclude_patterns );
		for( int j=0; j < myset->num_file_exclude_patterns; j++ ) 
		{
			if( (myset->file_exclude_pcres[j] = pcre_compile( 
							myset->file_exclude_patterns[j], 
							0, 
							&errmsg, 
							&erroffset, 
							NULL ) ) == NULL ) {
				debugp( 0, "error compiling file_exclude_pattern `%s' at offset %d: %s\n", myset->file_exclude_patterns[j], erroffset, errmsg );
				exit(-1);
			}
			else
				debugp( 4, "`%s' compiled successfully at %p\n", myset->file_exclude_patterns[j], myset->file_exclude_pcres[j] );
		}
		
		

	}
	return;
}

int test_file_name( char *filename, struct watch_set *set )
{
	int result = 0;

	/* Loop through include patterns for the set */
	if( set->num_file_include_patterns > 0 ) 
	{
		result = 0; /* Assume not pass until proven to pass */
		for( int i=0; i < set->num_file_include_patterns; i++ )
		{
			pcre *mypcre = set->file_include_pcres[i];
			char *pattern = set->file_include_patterns[i];

			debugp( 4, "   Testing file `%s' against file include pattern `%s' compiled at %p... ", filename, pattern, mypcre );

			if( pcre_exec( mypcre, NULL, filename, strlen(filename), 0, 0, NULL, 0 ) == 0 )
			{
				debugp( 4, "match\n" );
				result = 1; // passes
				break;
			}
			else
			{
				debugp( 4, "no match\n" );
			}
		}
	}
	else /* If no include patterns, default to pass */
	{
		result = 1; 
	}

	/* Loop through exclude patterns for the set if the file passed round 1 */
	if( (result>0) && (set->num_file_exclude_patterns > 0) )
	{
		for( int i=0; i < set->num_file_exclude_patterns; i++ )
		{
			pcre *mypcre = set->file_exclude_pcres[i];
			char *pattern = set->file_exclude_patterns[i];

			debugp( 4, "   Testing file `%s' against file exclude pattern `%s' compiled at %p... ", filename, pattern, mypcre );

			if( pcre_exec( mypcre, NULL, filename, strlen(filename), 0, 0, NULL, 0 ) == 0 )
			{
				debugp( 4, "match\n" );
				result = 0; // does not pass
				break;
			}
			else
			{
				debugp( 4, "no match\n" );
			}
		}
	}
	else if( (set->num_file_exclude_patterns == 0) && (result>0) ) /* no exclude patterns, but 
																							passed the first round */
	{
		result = 1; // passes
	}

	debugp( 4, "File `%s' %s\n", filename, (result) ? "passes" : "fails" );

	return result;
}
