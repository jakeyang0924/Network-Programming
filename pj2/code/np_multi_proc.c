#include<stdlib.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<sys/wait.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/select.h>
#include<errno.h>
#include<signal.h>
#define MAX_NUM_PIPE 1001
#define MAX_PROCESS 128
#define MAX_USER 30
#define SHMKEY 8888
#define NOSOCK -1
#define WELCOME_MSG "****************************************\n** Welcome to the information server. **\n****************************************\n"

int msock;
int offset; //used to locate shared memory for different user
int devnull = -1;

struct User
{
	pid_t pid;
	int id;
	int sockfd;
	struct sockaddr_in addr;
	char name[20];
	int msg_index;
	char msg[1500];
	int userpipe[30][2];
	int status;
	int is_upipe;
};

/* builtin commands declaration */
int sh_setenv(char **args);
int sh_printenv(char **args);
int sh_exit(char **args);
int sh_who(char **args);
int sh_yell(char **args);
int sh_tell(char **args);
int sh_name(char **args);

/* global variables */
char *bi_cmd[] = {"setenv", "printenv", "exit", "who", "yell", "tell", "name"};
int (*bi_ptr[])(char **) = {&sh_setenv, &sh_printenv, &sh_exit, &sh_who, &sh_yell, &sh_tell, &sh_name};
int glo_fd[MAX_NUM_PIPE][2];
static int cmd_index = -1;
int ps_table[MAX_PROCESS];
int ps_index = 0;
int shmid;
struct User* shmuser;// start address of shared memory for user info


/***** tool functions *****/
int get_num(char *token)
{
	char tmp[10]="\0";
	strcpy(tmp, token+1);
	int num=0;
	for(int i=strlen(tmp)-1, mul=1; i>=0; i--, mul *= 10)
		num += ((int)tmp[i] - 48)*mul;
	return num;
}

void itoa(int n, char c[])
{
	int q, r, i=0;
	q = n/10;
	r = n%10;
	if(q != 0)
	{
		c[0] = q+48;
		c[1] = r+48;
		c[2] = '\0';
	}
	else
	{
		c[0] = r+48;
		c[1] = '\0';
	}
}


/***** signal handling functions *****/
/* unlink fifo and clear shared memory */
void signal_server(int sig)
{
	for(int i=1; i<=MAX_USER; i++)
		for(int j=1; j<=MAX_USER; j++)
			if(i != j)
			{
				char s1[3], s2[3], str[20];
				itoa(i, s1);
				itoa(j, s2);
				sprintf(str, "user_pipe/%s-%s", s1, s2);
				unlink(str);
			}
	shmdt(shmuser);
	shmctl(shmid, IPC_RMID, 0);
	printf("SIGINT signal! Shared memory deallocate\n");
	signal(SIGINT, SIG_DFL);
	raise(SIGINT);
}

void signal_userpipe(int sig)
{
	if(sig == SIGUSR1)
	{
		for(int i=0; i<MAX_USER; i++)
			if((shmuser+i)->sockfd >= 0 && (shmuser+i)->is_upipe && (shmuser+offset)->userpipe[i][0] == -1)
			{
				char s1[3], s2[3], str[20];
				itoa(i+1, s1);
				itoa(offset+1, s2);
				sprintf(str, "user_pipe/%s-%s", s1, s2);
				(shmuser+offset)->userpipe[i][0] = open(str, O_RDONLY|O_NONBLOCK);

				(shmuser+i)->is_upipe = 0;
			}
	}
	else if(sig == SIGUSR2)
	{
		for(int i=0; i<MAX_USER; i++)
			if ((shmuser+i)->status == -1)
			{
				if ((shmuser+offset)->userpipe[i][0] != -1)
				{
					close((shmuser+offset)->userpipe[i][0]);
					(shmuser+offset)->userpipe[i][0] = -1;
				}
				if ((shmuser+offset)->userpipe[i][1] != -1)
				{
					//close((shmuser+offset)->userpipe[i][1]);
					(shmuser+offset)->userpipe[i][1] = -1;
				}
				(shmuser+i)->status = 1;
			}
	}
}

void signal_broadcast(int sig)
{
	if(sig == SIGINT && (shmuser+offset)->msg_index > 0)
	{
		(shmuser+offset)->msg[(shmuser+offset)->msg_index] = '\0';
		printf("%s", (shmuser+offset)->msg);
		(shmuser+offset)->msg_index = 0;
		strcpy((shmuser+offset)->msg, "\0");
	}
}


/***** builtin functions *****/
int sh_setenv(char **args)
{
	if(setenv(args[1], args[2], 1) == -1)
		printf("unable to set variable %s\n", args[1]);
	free(args);

	return 1;
}

int sh_printenv(char **args)
{
	char *env_val = getenv(args[1]);
	if(env_val != NULL)
		printf("%s\n", env_val);
	free(args);

	return 1;
}

int sh_exit(char **args)
{
	struct User* u = shmuser+offset;
	
	free(args);

	u->sockfd = NOSOCK;
	u->status = -1;

	/* close user pipe that's open */
	for (int i=0; i<MAX_USER; i++)
	{
		if((shmuser+i)->sockfd >= 0)
		{
			if((shmuser+i)->sockfd >= 0)
				kill((shmuser+i)->pid, SIGUSR2);

			if (u->userpipe[i][0] != -1)
			{				
				close(u->userpipe[i][0]);
				u->userpipe[i][0] = -1;

				char s1[3], s2[3], str[20];
				itoa(i+1, s1);
				itoa(offset+1, s2);
				sprintf(str, "user_pipe/%s-%s", s1, s2);
				unlink(str);
			}
			if (u->userpipe[i][1] != -1)
			{
				close(u->userpipe[i][1]);
				u->userpipe[i][1] = -1;

				char s1[3], s2[3], str[20];
				itoa(offset+1, s1);
				itoa(i+1, s2);
				sprintf(str, "user_pipe/%s-%s", s1, s2);
				unlink(str);
			}
		}
	}

	char s[50];
	sprintf(s, "*** User '%s' left. ***\n", u->name);
	for(int i=0; i<MAX_USER; i++)
		if((shmuser+i)->sockfd >= 0)
		{
			strcpy((shmuser+i)->msg + (shmuser+i)->msg_index, s);
			(shmuser+i)->msg_index += strlen(s);
			kill((shmuser+i)->pid, SIGINT);
		}

	return 0;
}

int sh_who(char **args)
{
	printf("<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");	
	for(int i=0; i<MAX_USER; i++)
	{
		if((shmuser+i)->sockfd >= 0)
		{    
            char s[100];
			sprintf(s, "%s:%d", inet_ntoa((shmuser+i)->addr.sin_addr), htons((shmuser+i)->addr.sin_port));
			printf("%d\t%s\t%s\t", (shmuser+i)->id, (shmuser+i)->name, s);
			if((shmuser+offset)->id == (shmuser+i)->id)
				printf("<-me\n");
			else
				printf("\n");
		}
	}
	return 1;
}

int sh_yell(char **args)
{
	char s[1500];
	sprintf(s, "*** %s yelled ***: %s\n", (shmuser+offset)->name, args[1]);
	pid_t pid;
	for(int i=0; i<MAX_USER; i++)
	{
		if((shmuser+i)->sockfd >= 0)
		{
			strcpy((shmuser+i)->msg + (shmuser+i)->msg_index, s);
			(shmuser+i)->msg_index += strlen(s);
			pid = (shmuser+i)->pid;
			kill(pid, SIGINT);
		}
	}

	return 1;
}

int sh_tell(char **args)
{
	char s[1500];
	sprintf(s, "*** %s told you ***: %s\n", (shmuser+offset)->name, args[2]);
	int target = atoi(args[1]);
	int i;
	for(i=0; i<MAX_USER; i++)
	{
		if((shmuser+i)->sockfd != NOSOCK && (shmuser+i)->id == target)
		{
			strcpy((shmuser+i)->msg + (shmuser+i)->msg_index, s);
			(shmuser+i)->msg_index += strlen(s);
			kill((shmuser+i)->pid, SIGINT);
			break;
		}
	}
	if(i == MAX_USER)//print error msg
	{
		sprintf(s, "*** Error: user #%d does not exist yet. ***\n", target);
		strcpy((shmuser+offset)->msg + (shmuser+offset)->msg_index, s);
		(shmuser+offset)->msg_index += strlen(s);
		kill((shmuser+offset)->pid, SIGINT);
	}

	return 1;
}

int sh_name(char **args)
{
	struct User* u = shmuser+offset;
	int i;
	char s[100];
	for(i=0; i<MAX_USER; i++)
	{
		if((shmuser+i)->sockfd >= 0 && !strcmp((shmuser+i)->name, args[1]))
		{    
			sprintf(s, "*** User '%s' already exists. ***\n", args[1]);
			strcpy(u->msg + u->msg_index, s);
			u->msg_index += strlen(s);
			kill(u->pid, SIGINT);
			break;
		}
	}
	if(i == MAX_USER)
	{
		strcpy(u->name, args[1]);
		sprintf(s, "*** User from %s:%d is named '%s'. ***\n", inet_ntoa(u->addr.sin_addr), htons(u->addr.sin_port), u->name);
		for(int j=0; j<MAX_USER; j++)
		{
			if((shmuser+j)->sockfd >= 0)
			{
				strcpy((shmuser+j)->msg + (shmuser+j)->msg_index, s);
				(shmuser+j)->msg_index += strlen(s);
				kill((shmuser+j)->pid, SIGINT);
			}
		}
	}

	return 1;
}


/***** connection functions *****/
int TCPsocket(char *port, int qlen)
{
	struct sockaddr_in serv_addr;
	int sockfd;

	bzero((char*)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(port));

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("server can't open socket\n");
		return -1;
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

int new_connection()
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
		if((shmuser+i)->sockfd < 0)
		{
			offset = i;
			(shmuser+i)->id = i+1;
			(shmuser+i)->sockfd = newsockfd;
			(shmuser+i)->addr = cli_addr;	
			strcpy((shmuser+i)->name, "(no name)");
			strcpy((shmuser+i)->msg, WELCOME_MSG);
			(shmuser+i)->msg_index = strlen(WELCOME_MSG);// update the starting point of next write
			for(int k=0; k<MAX_USER; k++)
				(shmuser+i)->userpipe[k][0] = (shmuser+i)->userpipe[k][1] = -1;
			(shmuser+i)->status = 1;
			(shmuser+i)->is_upipe = 0;
			printf("id: %d enter\n", (shmuser+i)->id);

			char s[100];
			sprintf(s, "*** User '(no name)' entered from %s:%d. ***\n", inet_ntoa(cli_addr.sin_addr), htons(cli_addr.sin_port));
			for(int j=0; j<MAX_USER; j++)
			{
				if((shmuser+j)->sockfd >= 0)
				{
					strcpy((shmuser+j)->msg + (shmuser+j)->msg_index, s);
					(shmuser+j)->msg_index += strlen(s); 
				}
			}
			return newsockfd;
		}
	
	perror("too many users. Limit: 30\n");
	close(newsockfd);
	return -1;
}


/***** execution functions *****/
//read the command line
char* read_cmd(char *cmd_line, int cli_fd)
{
	int n, rc;
	char c;
	for(n=1; n<15001; n++)
	{
		if((rc = read(cli_fd, &c, 1)) == 1)
		{
			if(c == '\r')
			{
				n--;
				continue;
			}
			cmd_line[n-1] = c;
			if(c == '\n')
			{
				if(n == 1)
					return NULL;
				break;
			}
		}
		else if(rc == 0)
		{
			if(n == 1)
				perror("EOF");
			break;
		}
		else
		{
			perror("read error");
			return NULL;
		}
	}
	cmd_line[n] = '\0';

	//update index used for current command line
	cmd_index = (cmd_index + 1) % MAX_NUM_PIPE;

	return cmd_line;
}

//parse the command then execute
char* preprocess(char *cmd_line, int cli_fd)
{
	if(!cmd_line)
		return NULL;

	//copy command line to local variable for manipulation
	char line[strlen(cmd_line)+1];
	strcpy(line, cmd_line);

	char *token;
	token = strtok(line, " \n");
	if(strcmp(token, "tell") && strcmp(token, "yell"))
	{
		while(token)
		{
			if(!strcmp(token, ">"))
			{
				token = strtok(NULL, " \n");
				open(token, O_CREAT, 0664);
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
		if((shmuser+i)->sockfd >= 0 && (shmuser+i)->id == id)
		{
			if(u->userpipe[id-1][0] < 0)
			{
				sprintf(s, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", id, u->id);
				strcpy(u->msg + u->msg_index, s);
				u->msg_index += strlen(s);
				kill(u->pid, SIGINT);
				return 1;
			}
			else
			{
				dup2(u->userpipe[id-1][0], 0);
				close(u->userpipe[id-1][0]);
				u->userpipe[id-1][0] = -1;
				u->userpipe[id-1][1] = -1;

				char s1[3], s2[3], str[20];
				itoa(id, s1);
				itoa(offset+1, s2);
				sprintf(str, "user_pipe/%s-%s", s1, s2);
				unlink(str);

				for(int j=0; j<MAX_USER; j++)
				{
					if((shmuser+j)->sockfd >= 0)
					{
						sprintf(s, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", u->name, u->id, (shmuser+i)->name, id, command);
						strcpy((shmuser+j)->msg + (shmuser+j)->msg_index, s);
						(shmuser+j)->msg_index += strlen(s);
						kill((shmuser+j)->pid, SIGINT);
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
		kill(u->pid, SIGINT);
		return 1;
	}
	return 0;
}

//function for executing commands
int execute(char **args, int *fd, int mode, int old_stdin, int cli_fd)
{
	//mode0:nopipe, mode1:pipe, mode2:numbered-pipe, mode3:numbered-pipe-error
	int pid, status;
	
	while ((pid = fork()) < 0) 
		usleep(1000);
	if(pid == 0)
	{
		if(mode == 3)
			dup2(fd[1], 2);
		if(mode){
			dup2(fd[1], 1);
			close(fd[0]);
			close(fd[1]);
		}	
		
		if(execvp(args[0], args) == -1)
		{
			dup2(cli_fd, 2);
			fprintf(stderr, "Unknown command: [%s].\n", args[0]);
			exit(1);
		}
	}
	else if(pid > 0)
	{
		if(!mode && (shmuser+offset)->status)
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
			dup2(cli_fd, 1);
		}
	}

	if(mode != 1)
		free(args);
	
	return 1;	
}

#define BI_CNT 7
int parseNexecute(char *cmd_line, int old_stdin, int old_stdout, int old_stderr, int cli_fd)
{
	if(!cmd_line)
		return 1;

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

	//store stdin and stdout first then listen to numbered-pipe 
	if (glo_fd[cmd_index][0] != -1)
	{
		dup2(glo_fd[cmd_index][0], 0);
		close(glo_fd[cmd_index][0]);
		close(glo_fd[cmd_index][1]);
		glo_fd[cmd_index][0] = glo_fd[cmd_index][1] = -1;
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
			token = strtok(NULL, "\n");//get msg
			args[t++] = token;
			break;	
		}
		else if(!strcmp(token, "tell"))
		{
			args[t++] = token;
			token = strtok(NULL, " "); //get user id
			args[t++] = token;
			token = strtok(NULL, "\n");//get msg
			args[t++] = token;
			break;
		}

		if((token[0] == '>' || token[0] == '<') && strlen(token) > 1)
		{
			struct User *u = shmuser+offset;
			int id = get_num(token);
			char s[100];
			int i;
			for(i=0; i<MAX_USER; i++)
			{
				if((shmuser+i)->sockfd >= 0 && (shmuser+i)->id == id)
				{
					if(token[0] == '>')
					{
						char *token2 = strtok(NULL, " \n");
						if(token2 && token2[0] == '<' && strlen(token2) > 1)
						{
							if(getUserPipe(u, token2, command))
								dup2(devnull, 0);
						}

						char s1[3], s2[3], str[20];
						itoa(offset+1, s1);
						itoa(i+1, s2);
						sprintf(str, "user_pipe/%s-%s", s1, s2);
						if(mkfifo(str, 0666) == -1 && errno == EEXIST)
						{
							sprintf(s, "*** Error: the pipe #%d->#%d already exists. ***\n", u->id, id);
							strcpy(u->msg + u->msg_index, s);
							u->msg_index += strlen(s);
							kill(u->pid, SIGINT);
							
							/* redirect stdout to null */
							dup2(devnull, 1);
						}
						else
						{						
							u->is_upipe = 1;

							/* signal another process to open fifo for read */
							kill((shmuser+i)->pid, SIGUSR1);
							
							/* open fifo for write */
							if((u->userpipe[id-1][1] = open(str, O_WRONLY)) == -1)								 
							{
								dup2(old_stderr, 2);
								perror("fifo open write error\n");
								dup2(cli_fd, 2);
							}
							
							for(int j=0; j<MAX_USER; j++)
							{
								if((shmuser+j)->sockfd >=0)
								{
									sprintf(s, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", u->name, u->id, command, (shmuser+i)->name, id);
									strcpy((shmuser+j)->msg + (shmuser+j)->msg_index, s);
									(shmuser+j)->msg_index += strlen(s);
									kill((shmuser+j)->pid, SIGINT);
								}
							}

							dup2(u->userpipe[id-1][1], 1);
							close(u->userpipe[id-1][1]);

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
				kill(u->pid, SIGINT);
				
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

			execute(args, fd, 1, old_stdin, cli_fd);

			t = 0;
			for(int i=0; i<25; i++)
				args[i] = NULL;
		}
		else if(!strcmp(token, ">"))
		{
			token = strtok(NULL, " \n");
			int file = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0664);
			dup2(file, 1);

			status = execute(args, NULL, 0, old_stdin, cli_fd);
			
			return status;
		}
		else if((token[0]=='|' || token[0]=='!') && (strlen(token)>1))
		{
			//get num of numbered-pipe
			int num = get_num(token);

			//calculate position to send then pipe
			int target = (cmd_index+num)%MAX_NUM_PIPE;		
			if(glo_fd[target][1] == -1)
				pipe(glo_fd[target]);

			//int *fd = glo_fd[target];
			if(token[0] == '!')
				execute(args, glo_fd[target], 3, old_stdin, cli_fd);
			else
				execute(args, glo_fd[target], 2, old_stdin, cli_fd);

			(shmuser+offset)->status = 0;
			return 1;
		}
		else
			args[t++] = token;

		token = strtok(NULL, " \n");
	}

	for(int i=0; i<BI_CNT; i++)
		if(strcmp(args[0], bi_cmd[i]) == 0)
		{
			status = (*bi_ptr[i])(args);
			dup2(old_stdin, 0);
			return status;
		}	
	
	status = execute(args, NULL, 0, old_stdin, cli_fd);

	return status;
}

void start_npshell(int cli_fd)
{
	int old_stdin = dup(0), old_stdout = dup(1), old_stderr = dup(2);
	dup2(cli_fd, 1);
	dup2(cli_fd, 2);

	for(int i=0; i<MAX_USER; i++)
		if((shmuser+i)->sockfd >= 0)
			kill((shmuser+i)->pid, SIGINT);

	int status;
	char cmd_line[15001], str[5];
	char *line;
	do
	{
		(shmuser+offset)->status = 1;
		strcpy(cmd_line, "\0");
		strcpy(str, "\0");
		sprintf(str, "%% ");
		write(cli_fd, str, strlen(str));

		line = read_cmd(cmd_line, cli_fd);
		line = preprocess(line, cli_fd);
		status = parseNexecute(line, old_stdin, old_stdout, old_stderr, cli_fd);
	}while(status);

	close(old_stdin);
	close(old_stdout);
	close(old_stderr);
}


/***** main function *****/
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

	//signal handling
	signal(SIGINT, signal_server);

	if((msock = TCPsocket(port, MAX_USER)) < 0)
	{
		perror("get socket error\n");
		exit(1);
	}

	/*server allocates shared memory and attach*/
	if((shmid = shmget(SHMKEY, MAX_USER * sizeof(struct User), IPC_CREAT|0666)) < 0)
	{
		perror("shmget error\n");
		exit(1);
	}
	shmuser = (struct User*) shmat(shmid, NULL, 0); //attach to shared memory

	for(int i=0; i<MAX_USER; i++)
		(shmuser + i)->sockfd = NOSOCK;

	int newsockfd, childpid;
	while(1)
	{
		if((newsockfd = new_connection()) < 0)
		{
			perror("new connection error\n");
			exit(1);
		}

		while ((childpid = fork()) < 0) 
			usleep(1000);
		if(childpid == 0)
		{ 
			close(msock);
			setbuf(stdout, NULL);

			signal(SIGINT, SIG_DFL);
			signal(SIGINT, signal_broadcast);			
			signal(SIGUSR1, signal_userpipe);
			signal(SIGUSR2, signal_userpipe);
			signal(SIGCHLD, SIG_IGN);

			/* attach to shared memory */
			shmuser = (struct User*) shmat(shmid, NULL, 0);
			
			printf("store pid %d\n", getpid());
			(shmuser+offset)->pid = getpid();

			//initialize glo_fd and ps_table
			for (int i = 0; i < MAX_NUM_PIPE; i++)
				glo_fd[i][0] = glo_fd[i][1] = -1;
			for (int i = 0; i < MAX_PROCESS; i++)
				ps_table[i] = -1;

			/* initialize environment var */
			setenv("PATH", "bin:.", 1);

			/* redirect to devnull when error occurs */
			if((devnull = open("/dev/null", O_RDWR)) < 0)
			{
				perror("open /dev/null failed!/n");
				exit(1);
			}

			/* process the request */
			start_npshell(newsockfd);	

			/*detach shared memory*/
			shmdt(shmuser);

			close(devnull);
			close(newsockfd);

			exit(0);
		}
		else
			close(newsockfd); /* parent process */
	}
	return 0;
}