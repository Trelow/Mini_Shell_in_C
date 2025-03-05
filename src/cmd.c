// SPDX-License-Identifier: BSD-3-Clause

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>

#include "cmd.h"
#include "utils.h"

#define READ 0
#define WRITE 1

int printf(const char *format, ...);
char *strtok_r(char *str, const char *delim, char **saveptr);
int strcmp(const char *str1, const char *str2);
char *strstr(const char *s1, const char *s2);
int setenv(const char *var_name, const char *new_value, int change_flag);
void perror(const char *str);
void exit(int status);
void free(void *ptr);

/**
 * Parse environment variable and set it.
 */
void parse_environment_variable(char *command)
{
	if (command == NULL)
		return;

	char *saveptr;
	char *name = strtok_r(command, "=", &saveptr);
	char *value = strtok_r(NULL, "=", &saveptr);

	setenv(name, value, 1);
}

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	// Check if dir is null or if there are more words
	if (dir == NULL || dir->next_word != NULL)
		return false;

	// Get path from dir
	char *path = get_word(dir);

	// Change directory
	if (chdir(path) != 0) {
		perror("cd");
		free(path);
		return false;
	}

	free(path);
	return true;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void) { exit(SHELL_EXIT); }

/**
 * Duplicate file descriptor
 */
static int dup_fd(int fd, int descriptor)
{
	if (dup2(fd, descriptor) == -1) {
		perror("dup2");
		close(fd);
		return -1;
	}
	return 0;
}

/**
 *  Redirects a file descriptor to a file
 */
static int redirect(const char *filename, int descriptor, int append)
{
	int fd;
	// Redirect input from file
	if (descriptor == STDIN_FILENO) {
		fd = open(filename, O_RDONLY);

		if (fd == -1) {
			perror("open");
			return -1;
		}

		dup_fd(fd, STDIN_FILENO);
		// Redirect output to file
	} else {
		// Set flags for open
		int flags = O_WRONLY | O_CREAT;
		// Set flag for append if needed
		if (append)
			flags |= O_APPEND;
		else
			flags |= O_TRUNC;

		// Open file
		fd = open(filename, flags, 0644);

		if (fd == -1) {
			perror("open");
			return -1;
		}

		// Redirect output to file
		if (descriptor & STDOUT_FILENO)
			dup_fd(fd, STDOUT_FILENO);

		// Redirect error to file
		if (descriptor & STDERR_FILENO)
			dup_fd(fd, STDERR_FILENO);
	}
	close(fd);
	return 0;
}

/**
 * Apply redirections from a command
 */
static int apply_redirections(simple_command_t *s)
{
	int exit_status = 0, descriptors = 0;
	// Get input file
	char *in = get_word(s->in);
	// If input file is not null redirect input
	if (in != NULL) {
		descriptors = STDIN_FILENO;
		exit_status = redirect(in, descriptors, 0);
	}
	free(in);
	// Check if redirect failed
	if (exit_status == -1)
		return exit_status;

	// Check if output and error are the same file
	if (s->err != NULL && s->out != NULL && s->err == s->out) {
		// Get output file and error file
		char *out = get_word(s->out);
		char *err = get_word(s->err);

		// Redirect output and error to file
		if (out != NULL && err != NULL) {
			descriptors = STDOUT_FILENO;
			descriptors |= STDERR_FILENO;
			exit_status = redirect(out, descriptors, 0);
		}

		free(out);
		free(err);
		// Else check if output and error are different files
	} else {
		// Redirect error to file
		if (s->err != NULL) {
			// Get error file
			char *err = get_word(s->err);
			// Set flag for append if needed
			int append = s->io_flags & IO_ERR_APPEND;

			if (err != NULL) {
				descriptors = STDERR_FILENO;
				exit_status = redirect(err, descriptors, append);
			}
			free(err);
		}
		// Redirect output to file
		if (s->out != NULL) {	// Get output file
			char *out = get_word(s->out);
			// Set flag for append if needed
			int append = s->io_flags & IO_OUT_APPEND;

			if (out != NULL) {
				descriptors = STDOUT_FILENO;
				exit_status = redirect(out, descriptors, append);
			}
			free(out);
		}
	}
	return exit_status;
}

/**
 * Duplicate file descriptors for stdin, stdout and stderr.
 */
static void duplicate_file_descriptors(int *original_stdin,
									   int *original_stdout,
									   int *original_stderr)
{
	*original_stdin = dup(STDIN_FILENO);
	*original_stdout = dup(STDOUT_FILENO);
	*original_stderr = dup(STDERR_FILENO);

	if (*original_stdin == -1 || *original_stdout == -1 ||
		*original_stderr == -1) {
		perror("dup");
		close(*original_stdin);
		close(*original_stdout);
		close(*original_stderr);
	}
}

/**
 * Restore file descriptors to their original values.
 */
static void restore_file_descriptors(int original_stdin, int original_stdout,
									 int original_stderr)
{
	dup_fd(original_stdin, STDIN_FILENO);
	dup_fd(original_stdout, STDOUT_FILENO);
	dup_fd(original_stderr, STDERR_FILENO);

	close(original_stdin);
	close(original_stdout);
	close(original_stderr);
}

/**
 *  Free memory allocated for arguments and command
 */
static void free_command(char **argv, int argc, char *command)
{
	for (int i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
	free(command);
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	if (s == NULL)
		return 0;

	// Get command
	char *command = get_word(s->verb);
	// Get arguments
	int argc = 0;
	char **argv = get_argv(s, &argc);
	// Duplicate file descriptors
	int original_stdin, original_stdout, original_stderr;

	duplicate_file_descriptors(&original_stdin, &original_stdout,
							   &original_stderr);
	// Apply redirections
	if (apply_redirections(s) == -1) {
		free_command(argv, argc, command);
		return -1;
	}

	// Check if command is internal
	// Check if command is exit or quit
	if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
		free_command(argv, argc, command);
		restore_file_descriptors(original_stdin, original_stdout, original_stderr);
		return shell_exit();
	} else if (strcmp(command, "cd") == 0) {
		bool ret = shell_cd(s->params);

		free_command(argv, argc, command);
		restore_file_descriptors(original_stdin, original_stdout, original_stderr);

		return ret ? 0 : 1;
	}

	// Check if command is environment variable assignment
	if (strstr(command, "=") != NULL) {
		parse_environment_variable(command);
		free_command(argv, argc, command);
		restore_file_descriptors(original_stdin, original_stdout, original_stderr);
		return 0;
	}

	// Check if command is external
	// Create child process
	pid_t pid = fork();

	if (pid == -1) {
		free_command(argv, argc, command);
		restore_file_descriptors(original_stdin, original_stdout, original_stderr);
		perror("fork");
		return -1;
	}

	// Execute command
	if (pid == 0) {
		execvp(command, argv);
		printf("Execution failed for '%s'\n", command);
		exit(127);
	} else {
		int status;

		// Wait for child process to finish
		waitpid(pid, &status, 0);
		restore_file_descriptors(original_stdin, original_stdout, original_stderr);
		free_command(argv, argc, command);

		return WEXITSTATUS(status);
	}
	// Free memory
	free_command(argv, argc, command);
	return 0;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
							command_t *father)
{
	pid_t pid1, pid2;
	int status1, status2;

	// Create first child process
	pid1 = fork();
	if (pid1 == -1) {
		perror("fork");
		return false;
	}
	// Execute first command
	if (pid1 == 0)
		exit(parse_command(cmd1, level + 1, father));

	// Create second child process
	pid2 = fork();
	if (pid2 == -1) {
		perror("fork");
		return false;
	}
	// Execute second command
	if (pid2 == 0)
		exit(parse_command(cmd2, level + 1, father));

	// Wait for child processes to finish
	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);

	return true;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
						command_t *father)
{
	int pipefd[2];
	pid_t pid1, pid2;

	// Create pipe
	if (pipe(pipefd) == -1) {
		perror("pipe");
		return false;
	}
	// Create first child process
	pid1 = fork();
	if (pid1 == -1) {
		perror("fork");
		return false;
	}
	// Execute first command
	if (pid1 == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		// Execute cmd1
		exit(parse_command(cmd1, level + 1, father));
		// Parent process
	} else {
		// Create second child process
		pid2 = fork();
		if (pid2 == -1) {
			perror("fork");
			return false;
		}
		// Execute second command
		if (pid2 == 0) {
			close(pipefd[1]);
			dup2(pipefd[0], STDIN_FILENO);
			close(pipefd[0]);

			// Execute cmd2
			exit(parse_command(cmd2, level + 1, father));
			// Back to parent process
		} else {
			close(pipefd[0]);
			close(pipefd[1]);

			int status1, status2;

			// Wait for child processes to finish
			waitpid(pid1, &status1, 0);
			waitpid(pid2, &status2, 0);

			return WEXITSTATUS(status2) ? false : true;
		}
	}
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	int exit_status = 0;

	// Check if command is null
	if (c == NULL)
		return exit_status;

	// Check if command is simple, if so execute it
	if (c->op == OP_NONE) {
		exit_status = parse_simple(c->scmd, level, c);
		return exit_status;
	}

	// Check if command is sequential, parallel, conditional or pipe
	switch (c->op) {
	// Execute first command and then second command
	case OP_SEQUENTIAL:
		exit_status = parse_command(c->cmd1, level + 1, c);
		exit_status = parse_command(c->cmd2, level + 1, c);
		break;

	// Execute commands simultaneously
	case OP_PARALLEL:
		exit_status = run_in_parallel(c->cmd1, c->cmd2, level + 1, c) ? 0 : 1;
		break;

	// Execute second command only if first command returns non zero
	case OP_CONDITIONAL_NZERO:
		exit_status = parse_command(c->cmd1, level + 1, c);
		if (exit_status != 0)
			exit_status = parse_command(c->cmd2, level + 1, c);
		break;

	// Execute second command only if first command returns zero
	case OP_CONDITIONAL_ZERO:
		exit_status = parse_command(c->cmd1, level + 1, c);
		if (exit_status == 0)
			exit_status = parse_command(c->cmd2, level + 1, c);
		break;

	// Execute commands in a pipe
	case OP_PIPE:
		exit_status = run_on_pipe(c->cmd1, c->cmd2, level + 1, c) ? 0 : 1;
		break;

	// Default case
	default:
		return SHELL_EXIT;
	}

	return exit_status;
}
