/*
* Bonusy:  1. (2 body) Neinteraktivny rezim - "shell" bude spracovavat aj prikazy v zadanych suboroch (skript).
*          2. (3 body) Program bude fungovat aj pod OS Linux (respektive pod inym OS).
*          3. (3 body) Interny prikaz stat vypise zoznam vsetkych aktualnych spojeni na ktorych prijma prikazy, pripadne aj vsetky sokety na ktorych prijma nove spojenia.
*          5. (4 body) Interny prikazy listen a close (s prislusnymi argumentami) pre otvorenie a zatvorenie soketu pre prijmanie spojeni.
*         14. (3 body) Konfigurovatelny tvar promptu, interny prikaz prompt.
*         21. (2 body) Prikazy musia byt rozoznane aj ako argumenty na prikazovom riadku v kombinacii s prepinacom "-c" (interne prikazy ako prepinace, -halt, -help), vykonaju sa jednorazovo a program sa ukonci.
*         24. (2 body) Program s prepinacom "-l" a menom suboru bude do neho zapisovat zaznamy o vykonavani prikazov (log-y).
*         30. (1 bod) Dobre komentare, resp. dokumentacia, v anglickom jazyku.
*/


#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/un.h>

#define HELP "**********************************************************************\n"\
			 "*                                                                    *\n"\
			 "*     Predmet: SPaASM                                                *\n"\
			 "*     Zadanie: 2                                                     *\n"\
			 "*     Autor: Robert Jacko                                            *\n"\
			 "*     Tema: Systemove programovanie a medziprocesova komunikacia     *\n"\
		     "*     Program: Jednoduchy interaktitvny shell                        *\n"\
			 "*                                                                    *\n"\
			 "*     Interne prikazy:                                               *\n"\
			 "*     HALT - ukonci program                                          *\n"\
			 "*     QUIT - ukonci spojenie, z ktoreho prisiel                      *\n"\
			 "*     HELP - vypise tieto informacie                                 *\n"\
			 "*     LISTEN [port] - zacne prijmat spojenia na zadanom porte        *\n"\
			 "*     CLOSE [index] - prestane prijmat spojenia zo zadaneho portu    *\n"\
			 "*     STAT - vypise porty a adresy aktualnych spojeni                *\n"\
			 "*     PROMPT [format] - zmena formatu promptu                        *\n"\
			 "*                     - parameter: 'default' == 'tum>'               *\n"\
			 "*                                  'um$' = USER@MACHINE$             *\n"\
			 "*                                  'tm#' = TIME MACHINE#             *\n"\
			 "*                                  't+' = TIME+                      *\n"\
			 "*                                                                    *\n"\
			 "**********************************************************************\n"
#define NO_PERMISSION "Nemate opravnenie na ukoncenie programu\n"\
					  "Pre zrusenie spojenia zadajte: QUIT\n"
#define CONN "\nPripojenie k serveru uspesne\n"\
		     "Prikazy sa budu vykonavat na serveri\n\n"
#define END "$#$END"


typedef struct Connection {
	int pid;
	int port;
	char path[100];
} Connection;


Connection connection[100];   // array of current connections
int conn_size;		// number of current connections
char prompt_format[5];   // default is 'tum>'  (time user@machine>)


void server_port(int port);
int script(char *file_name);
int commands_loop(char *line);


/* 
 * Returns a string of length 5
 * The first two characters represent HOURS
 * The last two characters represent MINUTES
 * Between HOURS and MINUTES is ':'
 * Format: "HH:MM"
 */
char *get_time() {
	struct timespec time_since_1970;
	clock_gettime(CLOCK_REALTIME, &time_since_1970);
	int secs_per_day = 60 * 60 * 24;
	int secs_today = time_since_1970.tv_sec % secs_per_day;
	int hour = secs_today / 60 / 60 + 2; // mins: /60  hours: /60  timezone: +2
	int min = secs_today / 60 % 60;		 // mins: /60
	char *time = (char*)malloc(6);
	time[0] = '0' + hour / 10;
	time[1] = '0' + hour % 10;
	time[2] = ':';
	time[3] = '0' + min / 10;
	time[4] = '0' + min % 10;
	time[5] = 0;
	return time;
}


/*
 * Returns a string representing the prompt
 * Time format: "HH:MM"
 * Parameter: char * prompt_format - "tum>" == "TIME USER@MACHINE> "
 *                                 - "um#" == "USER@MACHINE# "
 *                                 - "m$" == "TIME USER@MACHINE$ "
 *                                 - "tu+" == "TIME USER+"
 */
char *get_prompt(char *format) {
	// get username
	struct passwd *p;
	uid_t uid;
	p = getpwuid(uid = getuid());
	char *user = p->pw_name;
	int ulen = strlen(user);
	
	// get machine name
	char machine[100];
	gethostname(machine, 99);
	int mlen = strlen(machine);

	// get current time
	char *time = get_time();
	int tlen = strlen(time);

	// concatenation
	int i = 0;
	char *prompt = (char*)malloc(mlen + ulen + tlen + 5);  // 5 remaining characters:  ' ', '@', '>', ' ', 0
	memset(prompt, 0, mlen + ulen + tlen + 5);
	if (format[i] == 't') {
		strcpy(prompt, time);
		prompt[tlen] = ' ';
		i++;
	}
	if (format[i] == 'u') {
		strcat(prompt, p->pw_name);
		if(format[i + 1] == 'm')
			prompt[strlen(prompt)] = '@';
		i++;
	}
	if (format[i] == 'm') {
		strcat(prompt, machine);
		i++;
	}
	char end_of_prompt[] = "  ";
	end_of_prompt[0] = format[i];
	strcat(prompt, end_of_prompt);
	prompt[strlen(prompt)] = 0;
	return prompt;
}


/*
* Prints all current connections
*/
void print_stat() {
	int i;
	printf("Spojenia: ");
	for (i = 0; i < conn_size; i++) {
		printf("%d. ", i + 1);
		if (connection[i].port > 0)
			printf("Port: %d  ", connection[i].port);
		if (strlen(connection[i].path) > 0)
			printf("Adresa: %s", connection[i].path);
		printf("\n          ");
	}
	printf("\n");
}


/*
* Kills child processes (listeners)
* Parameter: int index - ID of the listener to be killed
*                      - if index == -1 -> kill all listeners
*/
void kill_listener(int index) {
	// IDs of listeners start from 1 not from 0
	index--; 
	// index out of size
	if (index >= conn_size)
		return;

	int i = 0;
	while(i < conn_size) {
		if(index < 0)
			kill(connection[i].pid, SIGTERM);
		else if (index == i) {
			kill(connection[i].pid, SIGTERM);
			break;
		}
		i++;
	}

	// move the last listener to the index of deleted listener
	connection[i].pid = connection[conn_size - 1].pid;
	connection[i].port = connection[conn_size - 1].port;
	strcpy(connection[i].path, connection[conn_size - 1].path);
	conn_size--;
}


/*
 * Replaces uppercase letters in the parameter "str" with lowercase letters
 * Characters less than 'A' or greater than 'B' remain unchanged     e.g. WoRd -> word
 * Characters between apostrophes remain unchanged     e.g. 'WoRd' -> WoRd
 * Apostrophes will be deleted
 * Parameters: char *str  - pointer to the string to be processed
			   int length - length of string "str"
 */
void to_lower_case(char *str, int length) {
	int i;
	int ignore = 0;  // flag to ignore letter change
	int count = 0;	// count of apostrophes
	for (i = 0; i < length; i++) {
		// apostrophe
		if (str[i] == '\'') {
			ignore = !ignore;  // change ignore flag
			count++;
		}
		else if (!ignore && str[i] >= 'A' && str[i] <= 'Z') {    // changing upper character to lower
			str[i - count] = str[i] - 'A' + 'a';			     //  and deleting the apostrophes
		}
		else
			str[i - count] = str[i];
	}
	str[length - count] = 0;
}


/*
 * Executes the system command
 * Parameter: char **command  - pointer to array of strings,
 *							  - command[0] is command, following strings are its arguments 
 */
void sys_command(char **command){
	pid_t pid = fork();
	if (pid < 0) {
		printf("Chyba pri vytvarani procesu\n");
		return;
	}
	else if (pid == 0) {
		if (execvp(command[0], command) < 0) {
			printf("Prikaz \"%s\" nemozno spustit\n", command[0]);
		}
		exit(0);
	}
	else {
		wait(NULL);
	}
}


/* 
 * Executes the piped system command
 * Parameters: char **command1  - pointer to array of strings,
 *							    - command1[0] is the command which output will be redirected to the command2
							    - following strings are its arguments
 *             char **command2  - pointer to array of strings
 *							    - command2[0] is the command, that gets input from command1's output 
								- following strings are its arguments
 */
void sys_command_pipe(char** command1, char** command2, int in_file, int out_file) {
	// pipefd[0] is READ
	// pipefd[1] is WRITE 
	int pipefd[2];
	pid_t p1, p2;

	pipe(pipefd);

	p1 = fork();
	if (p1 == 0) {
		// command1 writes its output to pipefd[1] 
		close(pipefd[0]);
		dup2(pipefd[1], out_file);
		close(pipefd[1]);
		if (execvp(command1[0], command1) < 0) {
			printf("Prikaz \"%s\" nemozno spustit\n", command1[0]);
			exit(0);
		}
	}
	else {
		p2 = fork();
		if (p2 == 0) {
			// command2 reads its input from pipefd[0]
			close(pipefd[1]);
			dup2(pipefd[0], in_file);
			close(pipefd[0]);
			if (execvp(command2[0], command2) < 0) {
				printf("Prikaz \"%s\" nemozno spustit\n", command2[0]);
				exit(0);
			}
		}
		else {
			// parent executing, waiting for two children 
			close(pipefd[0]);
			close(pipefd[1]);
			wait(NULL);
			wait(NULL);
		}
	}
}


/*
* Returns the index of the specified character - it must be separate command e.g. <, > or |
* Returns -1 if the specified character is not in the command 
* Parameters: char *command[]  - pointer to array of strings (commands)
*			  char ch		   - specified character
*			  int begin		   - starting index for searching
*			  int end		   - last index for searching
*/
int find_char(char* commands[], char ch, int begin, int end) {
	int i;
	for (i = begin; i < end; i++) {
		if (commands[i][0] == ch)
			return i;
	}
	return -1;
}


/*
* Assigns commands from index begin to index end to the command array
* Parameters: char *commands[]  - pointer to array of strings (commands)
*			  char *command[]   - pointer to array of  strings (here
*			  int begin		    - starting index for splitting
*			  int end		    - last index for splitting
*/
void split_indexed(char *commands[], char *command[], int begin, int end) {
	int i;
	for (i = begin; i < end; i++) {
		command[i - begin] = commands[i];
	}
	command[i - begin] = NULL;
}


/*
* Returns an array of complete commands with a trailing character '\n' or ';'
* Parameters: char *line  - one line from the console, user input
*			  int *count  - number of returned commands
*/
char** separate_commands(char *line, int *count) {
	*count = 0;
	int i;
	int begin = -1;
	char **commands = (char**)malloc(20 * sizeof(char*));
	for (i = 0; i < 20; i++) { commands[i] = NULL; }
	for (i = 0; i < strlen(line); i++) {
		if ((begin != -1) && (line[i] == ';' || line[i] == '\n')) {
			(*count)++;
			commands[*count - 1] = (char*)malloc(i - begin + 2);
			strncpy(commands[*count - 1], line + begin, i - begin + 1);
			commands[*count - 1][i - begin + 1] = 0;
			begin = -1;
		}
		else if (begin == -1 && line[i] != ' ' && line[i] != ';' && line[i] != '\n') {
			begin = i;
		}
	}
	return commands;
}


/*
* Finds words separated by ' ', ';' or '\n' and returns them as an array  
* Letters will be converted to lowercase 
* Parameters: char *command  - one complete command with a trailing character '\n' or ';'
*			  int *count  - number of returned words
*/
char** split_words(char *command, int *count) {
	*count = 0;
	int i;
	int begin = -1;
	char **words = (char**)malloc(20 * sizeof(char*));
	for (i = 0; i < 20; i++) { words[i] = NULL; }
	for (i = 0; i < strlen(command); i++) {
		// space character, end of command or end of line
		if ((begin != -1) && (command[i] == ' ' || command[i] == ';' || command[i] == '\n')) {
			(*count)++;
			words[*count - 1] = (char*)malloc(i - begin + 1);
			strncpy(words[*count - 1], command + begin, i - begin);
			words[*count - 1][i - begin] = 0;
			to_lower_case(words[*count - 1], i - begin);
			begin = -1;	// ignore the following delimeters
		}
		// redirecting characters
		if ((begin != -1) && (command[i] == '|' || command[i] == '>' || command[i] == '<')) {
			(*count)++;
			words[*count - 1] = (char*)malloc(i - begin + 1);
			strncpy(words[*count - 1], command + begin, i - begin);
			words[*count - 1][i - begin] = 0;
			begin = -1; // ignore the following delimeters
			(*count)++;
			words[*count - 1] = (char*)malloc(2);
			words[*count - 1][0] = command[i];
			words[*count - 1][1] = 0;
		}
		// if delimeters are ignored and current character is not delimeter
		else if (begin == -1 && command[i] != ' ' && command[i] != ';' && command[i] != '\n') {
			begin = i;  // stop ignoring the following delimeters
		}
	}
	return words;
}

/*
* Opens a text file and executes commands from its contents
* Parameter: char *file_name - name of text file with commands
* Returns status
*/
int script(char *file_name) {
	char str[512] = { 0 };
	int f = open(file_name, O_RDONLY);
	read(f, str, 512);
	close(f);
	return commands_loop(str);
}


/*
* Launchs specified commands
* Return new status: HALT = 1
*					 QUIT = 2
*					 else 0
* Parameters: char* commands[]  - array of specified commands
*             int count         - count of the commands
*/
int launch_commands(char* commands[], int count) {
	int in_index = find_char(commands, '<', 0, count);
	int out_index = find_char(commands, '>', 0, count);
	char in_file[100], out_file[100];
	int in_file_des = STDIN_FILENO;
	int out_file_des = STDOUT_FILENO;

	// cuts '<', '>' and filenames from commands
	int i = 0, j = 0;
	while(j < count) {
		if (j == in_index) {
			strcpy(in_file, commands[j + 1]);
			in_file[strlen(commands[j + 1])] = 0;
			j++;
		}
		else if (j == out_index) {
			strcpy(out_file, commands[j + 1]);
			out_file[strlen(commands[j + 1])] = 0;
			j++;
		}
		else
			commands[i++] = commands[j];
		j++;
	}
	commands[i] = NULL;
	count = i;
	int pipe_index = find_char(commands, '|', 0, count);
	
	// cancel connection
	if (strcmp(commands[0], "quit") == 0)
		return 2;
	// terminate the program
	else if (strcmp(commands[0], "halt") == 0)
		return 1;
	// listen on the new port
	else if (strcmp(commands[0], "listen") == 0) {
		int pid = fork();
		int port = atoi(commands[1]);
		connection[conn_size].pid = pid;
		connection[conn_size].port = port;
		connection[conn_size++].path[0] = 0;
		if (pid == 0) {
			server_port(port);
		}
		return 0;
	}
	// close listener
	else if (strcmp(commands[0], "close") == 0) {
		kill_listener(atoi(commands[1]));
		return 0;
	}
	// change prompt format
	else if (strcmp(commands[0], "prompt") == 0) {
		if (strcmp(commands[1], "default") == 0)
			strcpy(prompt_format, "tum>");
		else
			strcpy(prompt_format, commands[1]);
		return 0;
	}
	else if (strlen(commands[0]) >= 2 && commands[0][0] == '.' && commands[0][1] == '/') {
		return script(&commands[0][2]);
	}
	
	pid_t pid = fork();
	if (pid < 0) {
		printf("Chyba pri vytvarani procesu\n");
		return 0;
	}
	else if (pid == 0) {
		// input from the file
		if (in_index > -1) {
			in_file_des = open(in_file, O_RDONLY);
			dup2(in_file_des, STDIN_FILENO);
			close(in_file_des);
		}
		// output to the file
		if (out_index > -1) {
			out_file_des = creat(out_file, 0644);
			dup2(out_file_des, STDOUT_FILENO);
			close(out_file_des);
		}

		// no pipe
		if (pipe_index == -1) {
			// help
			if (strcmp(commands[0], "help") == 0)
				printf("%s", HELP);
			// stat
			else if (strcmp(commands[0], "stat") == 0)
				print_stat();
			// system command
			else
				sys_command(commands);
		}
		// pipe
		else {
			char *command1[pipe_index + 1], *command2[count - pipe_index];
			split_indexed(commands, command1, 0, pipe_index);
			split_indexed(commands, command2, pipe_index + 1, count);
			sys_command_pipe(command1, command2, STDIN_FILENO, STDOUT_FILENO);
		}

		exit(0);
	}
	else
		while (wait(NULL) != pid) {};
	return 0;
}


/*
* Separates oneliners to single commands and sends them to function launch_commands()
* Returns new status of the program from function launch_commands()
* Parameters: char *line  - one line from the console, user input
*/
int commands_loop(char *line) {
	int count, status = 0;
	char **commands = separate_commands(line, &count);

	int i;
	for (i = 0; i < count; i++) {
		int count_single_command;
		char **single_command = split_words(commands[i], &count_single_command);
		status = launch_commands(single_command, count_single_command);
	}

	for (i = 0; i < count; i++) {
		free(commands[i]);
	}
	free(commands);
	return status;
}


void server_port(int port) {
	int sockfd, newsockfd, portno;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
	int n;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		exit(1);
	bzero((char *)&serv_addr, sizeof(serv_addr));
	portno = port;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		exit(1);
	listen(sockfd, 5);
	clilen = sizeof(cli_addr);

	int pid;
	while (1) {
		newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if (newsockfd < 0)
			exit(1);
		//fork new process
		pid = fork();
		if (pid < 0) {
			exit(1);
		}
		if (pid == 0) {
			//child process
			close(sockfd);
			write(newsockfd, CONN, strlen(CONN));
			while (1) {
				char *prompt = get_prompt(prompt_format);
				write(newsockfd, prompt, strlen(prompt));
				char riadok[256];
				bzero(riadok, 256);
				n = read(newsockfd, riadok, 255);
				if (n < 0)
					exit(1);
				printf("%s", riadok);
				n = commands_loop(riadok);
				if (n == 1) {
					write(newsockfd, NO_PERMISSION, strlen(NO_PERMISSION));
				}
				if (n == 2) {
					printf("%s", prompt);
					close(newsockfd);
					exit(0);
				}
				printf("%s", prompt);
				fflush(stdout);
				free(prompt);
			}
		}
		else {
			//parent process
			close(newsockfd);
		}
	}
	exit(0);
}


void start(bool flag_log, char *file_log) {
	char line[256];
	int halt = 0;
	while (halt == 0) {
		char *prompt = get_prompt(prompt_format);
		printf("%s", prompt);
		free(prompt);
		fgets(line, 255, stdin);
		if (flag_log) {
			int f = open(file_log, O_WRONLY | O_APPEND | O_CREAT);
			write(f, line, strlen(line));
			close(f);
		}
		halt = commands_loop(line);
	}
}


void server_sock(char *path) {
	int s, ns, r;
	char line[256];
	struct sockaddr_un ad;

	memset(&ad, 0, sizeof(ad));
	ad.sun_family = AF_LOCAL;
	strcpy(ad.sun_path, path);
	s = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (s == -1)
		exit(2);

	unlink(path);
	bind(s, (struct sockaddr*)&ad, sizeof(ad));
	listen(s, 5);
	ns = accept(s, NULL, NULL);

	char *prompt = get_prompt(prompt_format);
	write(ns, CONN, strlen(CONN));
	write(ns, prompt, strlen(prompt));
	free(prompt);

	while ((r = read(ns, line, 255)) > 0) {
		line[r] = 0;
		prompt = get_prompt(prompt_format);
		printf("%s", line);
		int n = commands_loop(line);
		if (n == 1) {
			write(ns, NO_PERMISSION, strlen(NO_PERMISSION));
		}
		if (n == 2) {
			printf("%s", prompt);
			break;
		}
		printf("%s", prompt);
		fflush(stdout);
		write(ns, prompt, strlen(prompt));
		free(prompt);
	}

	close(ns);
	close(s);
	exit(0);
}


void arg_commands(int size, char *argv[]) {
	int i = 0;
	char commands[256] = "";
	while (i < size) {
		strcat(commands, argv[i]);
		commands[strlen(commands)] = ' ';
		i++;
	}
	commands[strlen(commands)] = ';';
	commands_loop(commands);
}


int main(int argc, char* argv[]) {
	int i = 1;
	int port = 0;
	char path[100], file_log[100];
	conn_size = 0;
	bool flag_log = false, flag_sck = false, flag_end = false;

	// switches
	while (i < argc) {
		if (strlen(argv[i]) == 2) {
			if (argv[i][0] == '-' && argv[i][1] == 'h') {
				printf("%s", HELP);
			}
			else if (argv[i][0] == '-' && argv[i][1] == 'p') {
				port = atoi(argv[i + 1]);
				i++;
			}
			else if (argv[i][0] == '-' && argv[i][1] == 'u') {
				flag_sck = true;
				strcpy(path, argv[i + 1]);
				i++;
			}
			else if (argv[i][0] == '-' && argv[i][1] == 'l') {
				flag_log = true;
				strcpy(file_log, argv[i + 1]);
				i++;
			}
			else if (argv[i][0] == '-' && argv[i][1] == 'c') {
				arg_commands(argc - i - 1, argv + i + 1);
				return 0;
			}
		}
		else if (strcmp(argv[i], "-help") == 0) {
			printf("%s", HELP);
			flag_end = true;
		}	
		else if (strcmp(argv[i], "-halt") == 0)
			return 0;

		i++;
	}

	if (flag_end)
		return 0;

	// listener port
	int pid = fork();
	connection[conn_size].pid = pid;
	connection[conn_size++].port = port;
	if (pid == 0) {
		if (port > 0)
			server_port(port);
		else
			exit(0);
	}
	if (port == 0) 
		connection[conn_size--].pid = 0;
	
	// listener socket
	pid = fork();
	connection[conn_size].pid = pid;
	strcpy(connection[conn_size++].path, path);
	if (pid == 0) {
		if (flag_sck)
			server_sock(path);
		else 
			exit(0);
	}
	if (!flag_sck)
		connection[conn_size--].pid = 0;

	strcpy(prompt_format, "tum>");
	prompt_format[4] = 0;

	// start
	start(flag_log, file_log);

	// kill all child processes (listeners)
	kill_listener(-1);

	return 0;
}