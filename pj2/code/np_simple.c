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
#define MAX_NUM_PIPE 1001
#define MAX_PROCESS 512

/* builtin commands declaration */
int sh_setenv(int cli_fd, char **args);
int sh_printenv(int cli_fd, char **args);
int sh_exit(int cli_fd, char **args);

/* global variables */
char *bi_cmd[] = {"setenv", "printenv", "exit"};
int (*bi_ptr[])(int, char **) = {&sh_setenv, &sh_printenv, &sh_exit};
int glo_fd[MAX_NUM_PIPE][2] = {0};
static int cmd_index = -1;
int ps_table[MAX_PROCESS];
int ps_index = -1;


/***** tool function *****/
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


/***** signal handling function *****/
void childHandler(int signo)
{
	int status, pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
		for (int i = 0; i < MAX_PROCESS; i++)
			if (ps_table[i] == pid)
				ps_table[i] = -1;
}


/***** builtin functions *****/
int sh_setenv(int cli_fd, char **args)
{
	if(setenv(args[1], args[2], 1) == -1)
	{
		char *s = "unable to initialize PATH\n";
		write(cli_fd, s, strlen(s));
	}
	free(args);

	return 1;
}

int sh_printenv(int cli_fd, char **args)
{
	char *env_val = getenv(args[1]);
	char s[20];
	if(env_val != NULL)
	{
		sprintf(s, "%s\n", env_val);
		write(cli_fd, s, strlen(s));
	}
	free(args);

	return 1;
}

int sh_exit(int cli_fd, char **args)
{
	free(args);
	close(cli_fd);
	return 0;
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

int new_connection(int msock, struct sockaddr_in* cli_addr)
{
	int clilen = sizeof(*cli_addr);
	int newsockfd;

	if((newsockfd = accept(msock, (struct sockaddr *) cli_addr, &clilen)) < 0)
	{
		perror("accept error\n");
		exit(1);
	}

	return newsockfd;
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
	while(token)
	{
		if(!strcmp(token, ">"))
		{
			token = strtok(NULL, " \n");
			if (open(token, O_CREAT, 0664) == -1)
			{
				fprintf(stderr, "cli_fd %d: open in function preprocess\n", cli_fd);
				exit(1);
			}
		}	

		token = strtok(NULL, " \n");
	}

	return cmd_line;
}

//function for executing commands
int execute(char **args, int *fd, int mode, int old_stdin, int old_stdout, int old_stderr, int cli_fd)
{
	//mode0:nopipe, mode1:redirect, mode2:pipe, mode3:numbered-pipe, mode4:numbered-pipe-error
	int pid, status;
	
	while ((pid = fork()) < 0)
		usleep(1000);

	if(pid == 0)
	{
		//child process	
		if(mode == 4)
			dup2(fd[1], 2);
		if(mode > 1)
		{
			dup2(fd[1], 1);
			close(fd[0]);
			close(fd[1]);
		}
		else if(mode == 0)
		{
			dup2(cli_fd, 1);
			dup2(cli_fd, 2);
		}

		if(execvp(args[0], args) == -1)
		{
			dup2(cli_fd, 2);
			fprintf(stderr, "Unknown command: [%s].\n", args[0]);
			exit(0);
		}
	}
	else if(pid > 0)
	{
		//parent process
		ps_index = (ps_index + 1) % MAX_PROCESS;
		ps_table[ps_index] = pid;

		if(mode == 2)
		{
			dup2(fd[0], 0);
			close(fd[0]);
            close(fd[1]);
		}
		else
			dup2(old_stdin, 0);
	}

	if(mode != 2)
		free(args);
	
	return 1;	
}

#define BI_CNT 3
int parseNexecute(char *cmd_line, int old_stdin, int old_stdout, int old_stderr, int cli_fd)
{
	if(!cmd_line)
		return 1;

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
		if(!strcmp(token, "|"))
		{
			int fd[2];
			pipe(fd);

			execute(args, fd, 2, old_stdin, old_stdout, old_stderr, cli_fd);

			t = 0;
			for(int i=0; i<25; i++)
				args[i] = NULL;

		}
		else if(!strcmp(token, ">"))
		{
			token = strtok(NULL, " \n");
			int file;
			if ((file = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0664)) == -1)
			{
				fprintf(stderr, "cli_fd %d: open in function parseNexecute\n", cli_fd);
				exit(1);
			}

			dup2(file, 1);
			status = execute(args, NULL, 1, old_stdin, old_stdout, old_stderr, cli_fd);
			dup2(old_stdout, 1);
			
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

			if(token[0] == '!')
				execute(args, glo_fd[target], 4, old_stdin, old_stdout, old_stderr, cli_fd);
			else
				execute(args, glo_fd[target], 3, old_stdin, old_stdout, old_stderr, cli_fd);
			
			return 2;
		}
		else
			args[t++] = token;

		token = strtok(NULL, " \n");
	}

	for(int i=0; i<BI_CNT; i++)
		if(strcmp(args[0], bi_cmd[i]) == 0)
		{
			status = (*bi_ptr[i])(cli_fd, args);
			return status;
		}	

	status = execute(args, NULL, 0, old_stdin, old_stdout, old_stderr, cli_fd);

	return status;
}

void start_npshell(int cli_fd)
{
	//initialize glo_fd and ps_table
	for (int i = 0; i < MAX_NUM_PIPE; i++)
		glo_fd[i][0] = glo_fd[i][1] = -1;
	for (int i = 0; i < MAX_PROCESS; i++)
		ps_table[i] = -1;

	//set signal handler
	signal(SIGCHLD, childHandler);

	setenv("PATH", "bin:.", 1);

	int status = -1;
	char cmd_line[15001], str[5];
	char *line;
	int old_stdin = dup(0), old_stdout = dup(1), old_stderr = dup(2);
	do
	{
		if (status == 1)
			for (int i = 0; i < MAX_PROCESS; i++)
				if (ps_table[i] != -1)
				{
					waitpid(ps_table[i], NULL, 0);
					ps_table[i] = -1;
				}

		strcpy(cmd_line, "\0");
		strcpy(str, "\0");
		sprintf(str, "%% ");
		write(cli_fd, str, strlen(str));

		line = read_cmd(cmd_line, cli_fd);
		line = preprocess(line, cli_fd);
		status = parseNexecute(line, old_stdin, old_stdout, old_stderr, cli_fd);

	}while(status);
}


/***** main function *****/
int main(int argc, char *argv[])
{
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

	int msock;
	if((msock = TCPsocket(port, 30)) < 0)
	{
		perror("get socket error\n");
		exit(1);
	}

	int newsockfd, childpid;
	struct sockaddr_in cli_addr;
	for( ; ; )
	{
		newsockfd = new_connection(msock, &cli_addr);

		while ((childpid = fork()) < 0) 
			usleep(1000);
		
		if(childpid == 0)
		{ 
			close(msock);

			start_npshell(newsockfd);	

			exit(0);
		}
		else
			close(newsockfd); /* parent process */
	}

	return 0;
}
