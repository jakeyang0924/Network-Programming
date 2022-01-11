#include<stdlib.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<sys/wait.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<errno.h>
#include<sys/select.h>
#define MAX_NUM_PIPE 1001
#define MAX_PROCESS 128
#define MAX_USER 30
#define NOSOCK -1
#define WELCOME_MSG "****************************************\n** Welcome to the information server. **\n****************************************\n"

int msock = -1; // master socket(used by server)

struct User
{
	int id;
	int sockfd;
	struct sockaddr_in addr;
	char name[21];
	char msg[1500];
	int msg_index; // points to current msg position
	int glo_fd[MAX_NUM_PIPE][2];
	int cmd_index; // used for numbered-pipe and its index
	int ps_table[MAX_PROCESS];
	int ps_index;
	int userpipe[MAX_USER][2]; // if someone pipe to me, use this as file descriptor
	char path[100]; //store PATH environment variable
	int status;
} user[MAX_USER];

//builtin commands initialization
int sh_setenv(struct User* u, char **args);
int sh_printenv(struct User* u, char **args);
int sh_exit(struct User* u, char **args);
int sh_who(struct User* u, char **args);
int sh_yell(struct User* u, char **args);
int sh_tell(struct User* u, char **args);
int sh_name(struct User* u, char **args);

//global variables
char *bi_cmd[] = {"setenv", "printenv", "exit", "who", "yell", "tell", "name"};
int (*bi_ptr[])(struct User* u, char **) = {&sh_setenv, &sh_printenv, &sh_exit, &sh_who, &sh_yell, &sh_tell, &sh_name};

//implement builtin commands
int sh_setenv(struct User* u, char **args)
{
	if(!strcmp("PATH", args[1]))
	{
		strcpy(u->path, args[2]);
	}
	free(args);

	return 1;
}

int sh_printenv(struct User* u, char **args)
{
	if(!strcmp("PATH", args[1]))
	{
		printf("%s\n", u->path);
	}
	free(args);

	return 1;
}

int sh_exit(struct User* u, char **args)
{
	free(args);

	char s[50];
	sprintf(s, "*** User '%s' left. ***\n", u->name);
	for(int i=0; i<MAX_USER; i++)
	{
		if(user[i].sockfd >= 0 && user[i].id != u->id)
		{
			strcpy(user[i].msg + user[i].msg_index, s);
			user[i].msg_index += strlen(s);
		}
	}
	
	return 0;
}

int sh_who(struct User* u, char **args)
{
	printf("<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");	
	for(int i=0; i<MAX_USER; i++)
	{
		if(user[i].sockfd >= 0)
		{    
            char s[100];
			sprintf(s, "%s:%d", inet_ntoa(user[i].addr.sin_addr), ntohs(user[i].addr.sin_port));
			printf("%d\t%s\t%s\t", user[i].id, user[i].name, s);
			if(u->id == user[i].id)
				printf("<-me\n");
			else
				printf("\n");
		}
	}
	return 1;
}

int sh_yell(struct User* u, char **args)
{
	char s[1500];
	sprintf(s, "*** %s yelled ***: %s\n", u->name, args[1]);
	for(int i=0; i<MAX_USER; i++)
	{
		if(user[i].sockfd >= 0)
		{
			strcpy(user[i].msg + user[i].msg_index, s);
			user[i].msg_index += strlen(s);
		}
	}

	return 1;
}

int sh_tell(struct User* u, char **args)
{
	char s[1500];
	sprintf(s, "*** %s told you ***: %s\n", u->name, args[2]);
	int target = atoi(args[1]);
	int i;
	for(i=0; i<MAX_USER; i++)
	{
		if(user[i].sockfd != NOSOCK && user[i].id == target)
		{
			strcpy(user[i].msg + user[i].msg_index, s);
			user[i].msg_index += strlen(s);
			break;
		}
	}
	if(i == MAX_USER)//print error msg
	{
		sprintf(s, "*** Error: user #%d does not exist yet. ***\n", target);
		strcpy(u->msg + u->msg_index, s);
		u->msg_index += strlen(s);
	}

	return 1;
}

int sh_name(struct User* u, char **args)
{
	int i;
	char s[100];
	for(i=0; i<MAX_USER; i++)
	{
		if(user[i].sockfd >= 0 && !strcmp(user[i].name, args[1]))
		{    
			sprintf(s, "*** User '%s' already exists. ***\n", args[1]);
			strcpy(u->msg + u->msg_index, s);
			u->msg_index += strlen(s);
			break;
		}
	}
	if(i == MAX_USER)
	{
		strcpy(u->name, args[1]);
		sprintf(s, "*** User from %s:%d is named '%s'. ***\n", inet_ntoa(u->addr.sin_addr), htons(u->addr.sin_port), u->name);
		for(int j=0; j<MAX_USER; j++)
		{
			if(user[j].sockfd >= 0)
			{
				strcpy(user[j].msg + user[j].msg_index, s);
				user[j].msg_index += strlen(s);
			}
		}
	}

	return 1;
}

//function for executing commands
int execute(char **args, int *fd, int mode, int old_stdin, int old_stdout, int old_stderr, struct User* u){
	//mode0:nopipe, mode1:pipe, mode2:numbered-pipe, mode3:numbered-pipe-error
	int pid, status;
	
	while ((pid = fork()) < 0)
		usleep(1000);

	if(pid == 0)
	{
		//child process	
		if(mode == 3)
			dup2(fd[1], 2);
		if(mode)
		{
			dup2(fd[1], 1);
			close(fd[0]);
			close(fd[1]);
		}

		if(execvp(args[0], args) == -1)
		{
			dup2(u->sockfd, 2);
			fprintf(stderr, "Unknown command: [%s].\n", args[0]);
			exit(0);
		}
	}
	else if(pid > 0)
	{
		//parent process
		if(!mode && u->status)
			waitpid(pid, NULL, 0);

		if(mode == 1)
		{
			dup2(fd[0], 0);
			close(fd[0]);
            close(fd[1]);
		}
		else
		{
			dup2(old_stdin, 0);
			dup2(old_stdout, 1);
			dup2(old_stderr, 2);
		}
	}

	if(mode != 1)
		free(args);
	
	return 1;	
}

//get numbered-pipe's number
int get_num(char *token)
{
	char tmp[10]="\0";
	strcpy(tmp, token+1);
	int num=0;
	for(int i=strlen(tmp)-1, mul=1; i>=0; i--, mul *= 10)
		num += ((int)tmp[i] - 48)*mul;
	return num;
}

//parse the command then execute
char* preprocess(char *cmd_line, int cli_fd)
{

	//copy command line to local variable for manipulation
	char line[strlen(cmd_line)+1];
	strcpy(line, cmd_line);

	char *token;
	token = strtok(line, " \n");
	if(strcmp(token, "tell") && strcmp(token, "yell"))
	{
		while(token){
			if(!strcmp(token, ">")){
				token = strtok(NULL, " \n");
				// open(token, O_CREAT, 0664);
				if (open(token, O_CREAT, 0664) == -1)
				{
					fprintf(stderr, "cli_fd %d: open in function preprocess\n", cli_fd);
					exit(1);
				}
			}	

			token = strtok(NULL, " \n");
		}
	}

	return cmd_line;
}

int getUserPipe(struct User* u, char *tk, char* command)
{
	char s[100];
	int id = get_num(tk);
	int i;
	for(i=0; i<MAX_USER; i++)
	{
		if(user[i].sockfd >= 0 && user[i].id == id)
		{
			if(u->userpipe[id-1][0] < 0)
			{
				sprintf(s, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", id, u->id);
				strcpy(u->msg + u->msg_index, s);
				u->msg_index += strlen(s);
				return 1;
			}
			else
			{
				dup2(u->userpipe[id-1][0], 0);
				close(u->userpipe[id-1][0]);
				u->userpipe[id-1][0] = -1;
				u->userpipe[id-1][1] = -1;
				for(int j=0; j<MAX_USER; j++)
				{
					if(user[j].sockfd >= 0)
					{
						sprintf(s, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", u->name, u->id, user[i].name, id, command);
						if(user[j].id == u->id)
						{
							send(u->sockfd, s, (int)strlen(s), MSG_DONTWAIT);
							continue;
						}
						strcpy(user[j].msg + user[j].msg_index, s);
						user[j].msg_index += strlen(s);
					}
				}
			}
			break;
		}
	}
	if(i == MAX_USER)
	{
		//user doesn't exist yet
	    sprintf(s, "*** Error: user #%d does not exist yet. ***\n", id);
	    strcpy(u->msg + u->msg_index, s);
	    u->msg_index += strlen(s);
		return 1;
	}
	return 0;
}

#define BI_CNT 7
int parseNexecute(char *cmd_line, int cli_fd, int old_stdin, int old_stdout, int old_stderr, struct User* u)
{
	char command[strlen(cmd_line)+1];
	strcpy(command, cmd_line);
	command[strlen(command)-1] = '\0';

	//copy command line to local variable for manipulation
	char line[strlen(cmd_line)+1];
	strcpy(line, cmd_line);

	//set variable to store single command's arguments
	char **args = malloc(25*sizeof(char*));
	for(int i=0; i<25; i++)
		args[i] = NULL;

	/* associate stdout and stderr to client's socket */
	dup2(cli_fd, 1);
	dup2(cli_fd, 2);

	//store stdin and stdout first then listen to numbered-pipe 
	if(u->glo_fd[u->cmd_index][0] != -1)
	{
		dup2(u->glo_fd[u->cmd_index][0], 0);
		close(u->glo_fd[u->cmd_index][0]);
		close(u->glo_fd[u->cmd_index][1]);
		u->glo_fd[u->cmd_index][0] = u->glo_fd[u->cmd_index][1] = -1;
	}	

	/* redirect to devnull when error occurs */
	int devnull;
	if ((devnull = open("/dev/null", O_RDWR)) == -1)
	{
		fprintf(stderr, "cli_fd %d: open devnull in function parseNexecute\n", cli_fd);
		exit(1);
	}

	//parse the command line and execute command one by one
	int status, t=0; //token count for single command
	char *token;
	token = strtok(line, " \n");
	while(token)
	{
		if(!strcmp(token, "yell"))
		{
			args[t++] = token;
			token = strtok(NULL, "\n"); //get msg
			args[t++] = token;
			break;	
		}
		else if(!strcmp(token, "tell"))
		{
			args[t++] = token;
			token = strtok(NULL, " "); //get user id
			args[t++] = token;
			token = strtok(NULL, "\n"); //get msg
			args[t++] = token;
			break;
		}
		
		if((token[0] == '>' || token[0] == '<') && strlen(token) > 1)
		{
			int id = get_num(token);
			char s[100];
			int i;
			for(i=0; i<MAX_USER; i++)
			{
				/* check "which user are we dealing with?" */
				if(user[i].sockfd >= 0 && user[i].id == id)
				{
					if(token[0] == '>')
					{
						/* if <N is followed by >N then we need to deal with <N first */ 
						char *token2 = strtok(NULL, " \n");
						if(token2 && token2[0] == '<' && strlen(token2) > 1)
						{
							if(getUserPipe(u, token2, command))
								dup2(devnull, 0);
						}
						if(user[i].userpipe[u->id-1][1] >= 0)
						{
							sprintf(s, "*** Error: the pipe #%d->#%d already exists. ***\n", u->id, id);
							strcpy(u->msg + u->msg_index, s);
							u->msg_index += strlen(s);
							// return 1;
							dup2(devnull, 1);
						}
						else
						{
							pipe(user[i].userpipe[u->id-1]);
							dup2(user[i].userpipe[u->id-1][1], 1);
							close(user[i].userpipe[u->id-1][1]);
							for(int j=0; j<MAX_USER; j++)
							{
								if(user[j].sockfd >=0)
								{
									sprintf(s, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", u->name, u->id, command, user[i].name, id);
									strcpy(user[j].msg + user[j].msg_index, s);
									user[j].msg_index += strlen(s);
								}
							}

							u->status = 0;
						}
					}
					else
					{
						if(getUserPipe(u, token, command))
							dup2(devnull, 0);
					}
					break;
				}
			}
			if(i == MAX_USER)
			{
				//user doesn't exist yet
				sprintf(s, "*** Error: user #%d does not exist yet. ***\n", id);
				strcpy(u->msg + u->msg_index, s);
				u->msg_index += strlen(s);
				
				if(token[0] == '>')
					dup2(devnull, 1);
				else
					dup2(devnull, 0);
			}
		}
		else if(!strcmp(token, "|"))
		{
			int fd[2];
			pipe(fd);

			execute(args, fd, 1, old_stdin, old_stdout, old_stderr, u);

			t = 0;
			for(int i=0; i<25; i++)
				args[i] = NULL;

		}
		else if(!strcmp(token, ">"))
		{
			token = strtok(NULL, " \n");
			int file = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0664);
			dup2(file, 1);

			status = execute(args, NULL, 0, old_stdin, old_stdout, old_stderr, u);
			
			return status;

		}
		else if((token[0]=='|' || token[0]=='!') && (strlen(token)>1))
		{
			//get num of numbered-pipe
			int num = get_num(token);

			//calculate position to send then pipe
			int target = (u->cmd_index+num)%MAX_NUM_PIPE;		
			if(u->glo_fd[target][1] == -1)
				pipe(u->glo_fd[target]);

			if(token[0] == '!')
				execute(args, u->glo_fd[target], 3, old_stdin, old_stdout, old_stderr, u);
			else
				execute(args, u->glo_fd[target], 2, old_stdin, old_stdout, old_stderr, u);

			u->status = 0;
			return 1;

		}
		else
		{
			args[t++] = token;
		}

		token = strtok(NULL, " \n");
	}

	for(int i=0; i<BI_CNT; i++)
		if(strcmp(args[0], bi_cmd[i]) == 0){
			status = (*bi_ptr[i])(u, args);
			dup2(old_stdin, 0);
			dup2(old_stdout, 1);
			dup2(old_stderr, 2);
			return status;
		}	

	status = execute(args, NULL, 0, old_stdin, old_stdout, old_stderr, u);
	
	close(devnull);

	return status;
}

// will be execute whenever received from client
int run_npshell(struct User* u, char* cmd_line)
{
	if(cmd_line[0] == '\n')
		return 1;

	/* load current user's env variable */
	setenv("PATH", u->path, 1);

    //update index used for current command line
    u->cmd_index = (u->cmd_index + 1) % MAX_NUM_PIPE;

	int cli_fd = u->sockfd;
	int old_stdin = dup(0), old_stdout = dup(1), old_stderr = dup(2);

	int status;
	char *line;
	line = preprocess(cmd_line, cli_fd);
	status = parseNexecute(line, cli_fd, old_stdin, old_stdout, old_stderr, u);

	close(old_stdin);
	close(old_stdout);
	close(old_stderr);

	return status;
}

int TCPsocket(char *port, int qlen)
{
	struct sockaddr_in serv_addr;
	int sockfd;

	bzero((char*)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(port));

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("server can't open socket\n");
		exit(1);
	}	

	int reuse = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
	{
		perror("setsockopt\n");
		exit(1);
	}	

	if(bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("server can't bind\n");
		exit(1);
	}	

	if(listen(sockfd, qlen) < 0)
	{
		perror("can't listen\n");
		exit(1);
	}	

	return sockfd;
}

void init_fd_sets(fd_set* rfds, fd_set* wfds)
{
	FD_ZERO(rfds);
	FD_SET(msock, rfds);
	for (int i = 0; i < MAX_USER; ++i)
		if (user[i].sockfd != NOSOCK)
			FD_SET(user[i].sockfd, rfds);

	FD_ZERO(wfds);
	for (int i = 0; i < MAX_USER; ++i)
		if (user[i].sockfd != NOSOCK && user[i].msg_index > 0)
			FD_SET(user[i].sockfd, wfds);
}

void new_connection()
{
	struct sockaddr_in cli_addr;
	int clilen = sizeof(cli_addr);
	int newsockfd;

	if((newsockfd = accept(msock, (struct sockaddr *) &cli_addr, &clilen)) < 0)
	{
		perror("accept error\n");
		exit(1);
	}
		
	//find one un-used user
	for(int i=0; i<MAX_USER; i++)
		if(user[i].sockfd < 0)
		{
			user[i].id = i+1;
			user[i].sockfd = newsockfd;
			user[i].addr = cli_addr;	
			strcpy(user[i].name, "(no name)");
			user[i].cmd_index = 0;
			strcpy(user[i].path, "bin:.");
			strcpy(user[i].msg, WELCOME_MSG);
			user[i].msg_index = strlen(WELCOME_MSG);// update the starting point of next write
			for(int k=0; k<MAX_USER; k++)
				user[i].userpipe[k][0] = user[i].userpipe[k][1] = -1;
			for(int k=0; k<MAX_NUM_PIPE; k++)
				user[i].glo_fd[k][0] = user[i].glo_fd[k][1] = -1;
			for (int k = 0; k < MAX_PROCESS; k++)
				user[i].ps_table[k] = -1;
			user[i].cmd_index = 0;
			user[i].ps_index = 0;
			user[i].status = 1;
		
			/* login broadcast message to all users */
			char s[100];
			sprintf(s, "*** User '(no name)' entered from %s:%d. ***\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
			for(int j=0; j<MAX_USER; j++)
			{
				if(user[j].sockfd >= 0)
				{
					strcpy(user[j].msg + user[j].msg_index, s);
					user[j].msg_index += strlen(s); 
				}
			}

			/* command prompt */
			strcpy(user[i].msg + user[i].msg_index, "% ");
			user[i].msg_index += strlen("% ");
			
			break;
		}
}

void receive_from_user(struct User* u)
{
	// use recv() to read command, then use run_npshell() to execute command 
	char command[15001];
	int recv_cnt;
	int status; //return status from np_shell

	while ((recv_cnt = recv(u->sockfd, command, 15001, MSG_DONTWAIT)) < 0 && errno == EAGAIN || errno == EWOULDBLOCK);
	if(recv_cnt > 0)
	{
		if(command[recv_cnt-2] == '\r')
		{
			command[recv_cnt-2] = '\n';
			command[recv_cnt-1] = '\0';
		}
		else
			command[recv_cnt] = '\0';

		if((status = run_npshell(u, command)))
		{
			u->status = 1;
			strcpy(u->msg + u->msg_index, "% ");
			u->msg_index += strlen("% ");
		}
		else if(!status)
		{
			//exit, close pipe and clean relevant resources
			close(u->sockfd);
			u->sockfd = NOSOCK;
		}		
	}
}

void send_to_user(struct User* u)
{
	int send_cnt;
	u->msg[u->msg_index] = '\0';
	int len = strlen(u->msg);

	while ((send_cnt = send(u->sockfd, u->msg, len, MSG_DONTWAIT)) < 0 && errno == EAGAIN || errno == EWOULDBLOCK);
	if(send_cnt > 0)
		if(send_cnt == u->msg_index)
		{
			u->msg_index = 0;
			strcpy(u->msg, "\0");
		}
}

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);

	char* port = "echo";
	switch(argc)
	{
		case 2:
			port = argv[1];
			break;
		default:
		{
			perror("wrong input form!\n");
			exit(1);
		}
	}

	//set signal handler
	signal(SIGCHLD, SIG_IGN);

	// open socket and get sockfd for server.
	msock = TCPsocket(port, MAX_USER);

	int highfd = msock; // highest fd so far
	fd_set rfds, wfds;

	for(int i=0; i<MAX_USER; i++)
		user[i].sockfd = NOSOCK;

	while(1)
	{
		init_fd_sets(&rfds, &wfds); // initialize fd sets before select()

		// set highest fd before select()
		highfd = msock;
		for(int i=0; i<MAX_USER; i++)
			if(user[i].sockfd > highfd)
				highfd = user[i].sockfd;
		
		while (select(highfd+1, &rfds, &wfds, NULL, NULL) < 0 && errno == EINTR);
		if(FD_ISSET(msock, &rfds))
		{
			new_connection(); // return connected sockfd, return -1 when user is up to limit.
		}

		for(int i=0; i<MAX_USER; i++)
		{			
			if(user[i].sockfd != NOSOCK && FD_ISSET(user[i].sockfd, &rfds))
				receive_from_user(&user[i]);
			
			if(user[i].sockfd != NOSOCK && FD_ISSET(user[i].sockfd, &wfds))
				send_to_user(&user[i]);
		}
	}
	return 0;
}
