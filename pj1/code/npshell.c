#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#define MAX_NUM_PIPE 1001
#define MAX_PROCESS 512

//builtin commands initialization
int sh_setenv(char **args);
int sh_printenv(char **args);
int sh_exit(char **args);

//global variables
char *bi_cmd[] = {"setenv", "printenv", "exit"};
int (*bi_ptr[])(char **) = {&sh_setenv, &sh_printenv, &sh_exit};
int glo_fd[MAX_NUM_PIPE][2];
int cmd_index = -1;
int ps_table[MAX_PROCESS];
int ps_index = -1;

//implement builtin commands
int sh_setenv(char **args)
{
	if (setenv(args[1], args[2], 1) == -1)
		perror("setenv\n");
	free(args);

	return 1;
}

int sh_printenv(char **args)
{
	char *env_val = getenv(args[1]);
	if (env_val != NULL)
		printf("%s\n", env_val);
	free(args);

	return 1;
}

int sh_exit(char **args)
{
	free(args);
	return 0;
}

//function for executing commands
int execute(char **args, int *fd, int mode, int old_stdin, int old_stdout)
{
	//mode0:nopipe, mode1:pipe, mode2:numbered-pipe, mode3:numbered-pipe-error
	int pid, status;

	while ((pid = fork()) < 0)
		usleep(1000);

	if (pid == 0)
	{
		/*child process*/

		if (mode == 3)
			dup2(fd[1], 2);
		if (mode)
		{
			dup2(fd[1], 1);
			close(fd[0]);
			close(fd[1]);
		}
		if (execvp(args[0], args) == -1)
		{
			fprintf(stderr, "Unknown command: [%s].\n", args[0]);
			exit(EXIT_FAILURE);
		}
	}
	else if (pid > 0)
	{
		/*parent process*/

		ps_index = (ps_index + 1) % MAX_PROCESS;
		ps_table[ps_index] = pid;

		if (mode == 1)
		{
			dup2(fd[0], 0);
			close(fd[0]);
			close(fd[1]);
		}
		else
			dup2(old_stdin, 0);
	}

	if (mode != 1)
		free(args);

	return 1;
}

//get numbered-pipe's number
int get_num(char *token)
{
	char tmp[10] = "\0";
	strcpy(tmp, token + 1);
	int num = 0;
	for (int i = strlen(tmp) - 1, mul = 1; i >= 0; i--, mul *= 10)
		num += ((int)tmp[i] - 48) * mul;
	return num;
}

//read the command line
char *read_cmd(char *cmd_line)
{
	if (!fgets(cmd_line, 15001, stdin))
	{
		perror("fgets error");
		exit(1);
	}
	else if (cmd_line[0] == '\n')
		return NULL;

	cmd_index = (cmd_index + 1) % MAX_NUM_PIPE; //update index used for current command line

	return cmd_line;
}

//parse to check whether there exists redirection operator
char *preprocess(char *cmd_line)
{
	if (!cmd_line)
		return NULL;

	//copy command line to local variable for manipulation
	char line[strlen(cmd_line) + 1];
	strcpy(line, cmd_line);

	//create the file for redirection if it doesn't exist
	char *token;
	token = strtok(line, " \n");
	while (token)
	{
		if (!strcmp(token, ">"))
		{
			token = strtok(NULL, " \n");
			if (open(token, O_CREAT, 0664) == -1)
			{
				perror("open in function preprocess\n");
				exit(1);
			}
		}

		token = strtok(NULL, " \n");
	}

	return cmd_line;
}

#define BI_CNT 3
int parseNexecute(char *cmd_line, int old_stdin, int old_stdout)
{
	if (!cmd_line)
		return 1;

	//copy command line to local variable for manipulation
	char line[strlen(cmd_line) + 1];
	strcpy(line, cmd_line);

	//set variable to store single command's arguments
	char **args = malloc(25 * sizeof(char *));
	for (int i = 0; i < 25; i++)
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
	int status, t = 0; //token count for single command
	char *token;
	token = strtok(line, " \n");
	while (token)
	{
		if (!strcmp(token, "|"))
		{
			int fd[2];
			pipe(fd);

			execute(args, fd, 1, old_stdin, old_stdout);

			t = 0;
			for (int i = 0; i < 25; i++)
				args[i] = NULL;
		}
		else if (!strcmp(token, ">"))
		{
			token = strtok(NULL, " \n");

			int file;
			if ((file = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0664)) == -1)
			{
				perror("open in function parseNexecute\n");
				exit(1);
			}

			dup2(file, 1);
			status = execute(args, NULL, 0, old_stdin, old_stdout);
			dup2(old_stdout, 1);

			return status;
		}
		else if ((token[0] == '|' || token[0] == '!') && (strlen(token) > 1))
		{
			int num = get_num(token); //get num of numbered-pipe
			int target = (cmd_index + num) % MAX_NUM_PIPE; //calculate position to send then pipe
			if (glo_fd[target][1] == -1)
				pipe(glo_fd[target]);

			if (token[0] == '!')
				execute(args, glo_fd[target], 3, old_stdin, old_stdout);
			else
				execute(args, glo_fd[target], 2, old_stdin, old_stdout);

			return 2;
		}
		else
		{
			args[t++] = token;
		}

		token = strtok(NULL, " \n");
	}

	for (int i = 0; i < BI_CNT; i++)
		if (strcmp(args[0], bi_cmd[i]) == 0)
		{
			status = (*bi_ptr[i])(args);
			return status;
		}

	status = execute(args, NULL, 0, old_stdin, old_stdout);

	return status;
}

void childHandler(int signo)
{
	int status, pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
		for (int i = 0; i < MAX_PROCESS; i++)
			if (ps_table[i] == pid)
				ps_table[i] = -1;
}

int main(int argc, char **argv)
{
	//initialize glo_fd and ps_table
	for (int i = 0; i < MAX_NUM_PIPE; i++)
		glo_fd[i][0] = glo_fd[i][1] = -1;
	for (int i = 0; i < MAX_PROCESS; i++)
		ps_table[i] = -1;

	//set signal handler
	signal(SIGCHLD, childHandler);

	//initialize PATH
	if (setenv("PATH", "bin:.", 1) == -1)
	{
		perror("unable to initialize PATH\n");
		exit(1);
	}

	int status = -1;
	char cmd_line[15001];
	char *line;
	int old_stdin = dup(0), old_stdout = dup(1);
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
		printf("%% ");
		line = read_cmd(cmd_line);
		line = preprocess(line);
		status = parseNexecute(line, old_stdin, old_stdout);

	} while (status);

	return 0;
}
