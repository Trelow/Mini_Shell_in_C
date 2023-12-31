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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir) {
  if (dir == NULL || dir->next_word != NULL) {
    // If no arguments or more than one argument, do nothing
    return true;
  }

  // Get the directory path
  char *dir_path = get_word(dir);

  // Change the directory
  if (chdir(dir_path) != 0) {
    perror("chdir");
    free(dir_path);
    return false;
  }

  free(dir_path);
  return true;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void) {
  exit(0); // Exit with status 0 (successful termination)
}

static int redirect_output(const char *filename, int append) {
  int flags = O_WRONLY | O_CREAT;

  if (append) {
    flags |= O_APPEND;
  } else {
    flags |= O_TRUNC;
  }

  int fd = open(filename, flags, 0644);

  if (fd == -1) {
    printf("open");
    return -1;
  }

  if (dup2(fd, STDOUT_FILENO) == -1) {
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int redirect_input(const char *filename) {
  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    perror("open");
    return -1;
  }

  if (dup2(fd, STDIN_FILENO) == -1) {
    perror("dup2");
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}
static int redirect_stderr(const char *filename, int append) {
  int flags = O_WRONLY | O_CREAT;
  if (append) {
    flags |= O_APPEND;
  } else {
    flags |= O_TRUNC;
  }

  int fd = open(filename, flags, 0644);

  if (fd == -1) {
    perror("open");
    return -1;
  }

  if (dup2(fd, STDERR_FILENO) == -1) {
    perror("dup2");
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int redirect_output_stderr(const char *filename) {
  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) {
    perror("open");
    return -1;
  }

  if (dup2(fd, STDOUT_FILENO) == -1) {
    perror("dup2");
    close(fd);
    return -1;
  }

  if (dup2(fd, STDERR_FILENO) == -1) {
    perror("dup2");
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int apply_redirections(simple_command_t *s) {
  char *in = get_word(s->in);
  int exit_status = 0;
  if (in != NULL) {
    // Redirect input from file
    exit_status = redirect_input(in);
  }
  free(in);
  if (exit_status == -1) {
    return -1;
  }

  if (s->err != NULL && s->out != NULL && s->err == s->out) {
    char *out = get_word(s->out);
    char *err = get_word(s->err);

    // int append = s->io_flags & IO_OUT_APPEND;
    if (out != NULL && err != NULL) {
      // Redirect output to file
      // int append1 = s->io_flags & IO_OUT_APPEND;
      // int append2 = s->io_flags & IO_ERR_APPEND;
      int exit_status = redirect_output_stderr(out);
    }

    free(out);
    free(err);
  } else {
    if (s->err != NULL) {
      char *err = get_word(s->err);
      int append = s->io_flags & IO_ERR_APPEND;
      if (err != NULL) {
        // Redirect output to file
        exit_status = redirect_stderr(err, append);
      }
      free(err);
    }
    if (s->out != NULL) {
      char *out = get_word(s->out);
      int append = s->io_flags & IO_OUT_APPEND;
      if (out != NULL) {

        // Redirect output to file
        exit_status = redirect_output(out, append);
      }
      free(out);
    }
  }

  return exit_status;
}

static void duplicate_file_descriptors(int *original_stdin,
                                       int *original_stdout,
                                       int *original_stderr) {
  *original_stdin = dup(STDIN_FILENO);
  *original_stdout = dup(STDOUT_FILENO);
  *original_stderr = dup(STDERR_FILENO);
}

static void restore_file_descriptors(int original_stdin, int original_stdout,
                                     int original_stderr) {
  dup2(original_stdin, STDIN_FILENO);
  dup2(original_stdout, STDOUT_FILENO);
  dup2(original_stderr, STDERR_FILENO);

  close(original_stdin);
  close(original_stdout);
  close(original_stderr);
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father) {
  /* TODO: Sanity checks. */

  char *command = get_word(s->verb);
  int argc = 0;
  char **argv = get_argv(s, &argc);

  // Duplicate original file descriptors
  int original_stdin, original_stdout, original_stderr;
  duplicate_file_descriptors(&original_stdin, &original_stdout,
                             &original_stderr);
  // Apply redirections
  // int redirections_failed = false;
  if (apply_redirections(s) == -1) {
    return -1;
  }

  if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
    free(command);       // Free the dynamically allocated command string
    return shell_exit(); // Call shell_exit function
  } else if (strcmp(command, "cd") == 0) {
    free(command); // Free the dynamically allocated command string
    bool ret = shell_cd(s->params);

    // Restore original file descriptors
    restore_file_descriptors(original_stdin, original_stdout, original_stderr);

    return ret ? 0 : 1;
  }

  // if (env_vars != NULL) {
  //   EnvVar *var = env_vars;
  //   while (var != NULL) {
  //     printf("%s=%s\n", var->name, var->value);
  //     var = var->next;
  //   }
  // }
  // Check if the command is an environment variable assignment
  if (strstr(command, "=") != NULL) {
    // printf("env");
    parse_env_assignment(command);
    return 0; // Successfully processed environment variable assignment
  }

  // Expand environment variables in command
  // printf("%s", command);
  // char *expanded_command = expand_env_vars(ar);
  // printf("%s", expanded_command);

  // expand each argument
  // for (int i = 0; i < argc; i++) {
  //   printf("%s", argv[i]);
  //   argv[i] = expand_env_vars(argv[i]);
  //   printf("%s", argv[i]);
  // }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    return -1;
  }

  if (pid == 0) {
    // In child process
    int status = execvp(command, argv); // Execute the command
    perror(command); // execvp only returns if there is an error
                     // exit dtatus of comm
    exit(EXIT_FAILURE);
  } else {
    // In parent process
    int status;
    waitpid(pid, &status, 0); // Wait for the child process to finish

    // Restore original file descriptors
    restore_file_descriptors(original_stdin, original_stdout, original_stderr);
    // printf("%d", WEXITSTATUS(status));
    // printf("%d", WEXITSTATUS(status));
    return WEXITSTATUS(status); // Return the exit status of the child
  }

  free(command);
  // free(expanded_command);
  return 0; // TODO: Replace with actual exit status
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
                            command_t *father) {
  /* TODO: Execute cmd1 and cmd2 simultaneously. */

  pid_t pid1, pid2;

  // Fork first child process to run cmd1
  pid1 = fork();
  if (pid1 == -1) {
    perror("fork");
    return false;
  }
  if (pid1 == 0) {
    // In the first child process
    exit(parse_command(cmd1, level + 1, father));
  }

  // Fork second child process to run cmd2
  pid2 = fork();
  if (pid2 == -1) {
    perror("fork");
    return false;
  }
  if (pid2 == 0) {
    // In the second child process
    exit(parse_command(cmd2, level + 1, father));
  }

  // In the parent process, return immediately without waiting for children
  return true;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
                        command_t *father) {
  int pipefd[2];
  pid_t pid1, pid2;

  if (pipe(pipefd) == -1) {
    perror("pipe");
    return false;
  }

  pid1 = fork();
  if (pid1 == -1) {
    perror("fork");
    return false;
  }

  if (pid1 == 0) {
    // Child process for cmd1
    close(pipefd[0]); // Close unused read end
    dup2(pipefd[1],
         STDOUT_FILENO); // Redirect STDOUT to write end of the pipe
    close(pipefd[1]);

    // Execute cmd1
    exit(parse_command(cmd1, level + 1, father));
  } else {
    // Parent process
    pid2 = fork();
    if (pid2 == -1) {
      perror("fork");
      return false;
    }

    if (pid2 == 0) {
      // Child process for cmd2
      close(pipefd[1]);              // Close unused write end
      dup2(pipefd[0], STDIN_FILENO); // Redirect STDIN to read end of the pipe
      close(pipefd[0]);

      // Execute cmd2
      exit(parse_command(cmd2, level + 1, father));
    } else {
      // Parent process
      close(pipefd[0]);
      close(pipefd[1]);

      int status1, status2;
      waitpid(pid1, &status1, 0);
      waitpid(pid2, &status2, 0);
      // printf("%d", WEXITSTATUS(status1));
      // printf("%d", WEXITSTATUS(status2));
      int exit_status = WEXITSTATUS(status2);
      // printf("%d", exit_status);

      return WEXITSTATUS(status2) ? false : true;
    }
  }
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father) {
  /* TODO: sanity checks */
  int exit_status = 0;
  // printf("%d", c->op);
  if (c->op == OP_NONE) {
    /* TODO: Execute a simple command. */
    exit_status = parse_simple(c->scmd, level, c);

    return exit_status; /* TODO: Replace with actual exit code of command. */
  }

  switch (c->op) {
  case OP_SEQUENTIAL:
    /* TODO: Execute the commands one after the other. */
    /* Execute the commands one after the other. */
    // printf("dfdfdf");
    exit_status = parse_command(c->cmd1, level + 1, c);
    // Check if first command executed successfully
    exit_status = parse_command(c->cmd2, level + 1, c);

    // parse_command(c->cmd2, level + 1, c);

    break;

  case OP_PARALLEL:
    /* TODO: Execute the commands simultaneously. */
    exit_status = run_in_parallel(c->cmd1, c->cmd2, level + 1, c) ? 0 : 1;
    break;

  case OP_CONDITIONAL_NZERO:
    /* Execute the second command only if the first one returns non zero. */
    exit_status = parse_command(c->cmd1, level + 1, c);
    if (exit_status != 0) {
      exit_status = parse_command(c->cmd2, level + 1, c);
    }
    break;

  case OP_CONDITIONAL_ZERO:
    /* Execute the second command only if the first one returns zero. */
    exit_status = parse_command(c->cmd1, level + 1, c);
    if (exit_status == 0) {
      exit_status = parse_command(c->cmd2, level + 1, c);
    }
    break;

  case OP_PIPE:
    exit_status = run_on_pipe(c->cmd1, c->cmd2, level + 1, c) ? 0 : 1;
    // printf("%d", exit_status);
    break;

  default:
    return SHELL_EXIT;
  }

  return exit_status;
}
