#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>

// Allowed: malloc, free, write, close, fork, waitpid, signal, kill, exit, chdir, execve, dup, dup2, pipe, strcmp, strncmp

#define TYPE_END 0
#define TYPE_PIPE 1
#define TYPE_SEMI 2

// --- Global Variables ---
char **g_envp;
pid_t g_pids[2048]; // Sufficient for "hundreds of pipes"
int   g_pids_count;

// --- Error Reporting Functions ---
void ft_putstr_fd(const char *str, int fd) {
    if (!str) return;
    size_t len = 0;
    while (str[len]) len++;
    if (len > 0) {
        ssize_t written = 0;
        while (written < (ssize_t)len) {
            ssize_t res = write(fd, str + written, len - written);
            if (res == -1) {
                // Cannot call fatal_error if write to STDERR itself fails.
                // For this assignment, assume critical writes (like to STDERR for fatal) work.
                // If write to STDERR in fatal_error fails, the program is already in a bad state.
                if (fd == STDERR_FILENO && str && strcmp(str, "error: fatal\n") == 0) {
                    // Trying to write "error: fatal" and it failed. Infinite loop avoided.
                } else {
                    // Fallback for other write errors, attempt fatal.
                    // This part of ft_putstr_fd is tricky for the "error: fatal" on write failure.
                    // For this problem, the primary check for write failure will be outside this func for fatal.
                }
                return; // Exit or indicate error
            }
            written += res;
        }
    }
}


void fatal_error() {
    ft_putstr_fd("error: fatal\n", STDERR_FILENO);
    exit(1);
}

void cd_bad_args_error() {
    ft_putstr_fd("error: cd: bad arguments\n", STDERR_FILENO);
}

void cd_chdir_error(const char *path) {
    ft_putstr_fd("error: cd: cannot change directory to ", STDERR_FILENO);
    ft_putstr_fd(path, STDERR_FILENO);
    ft_putstr_fd("\n", STDERR_FILENO);
}

void execve_error(const char *cmd_path) {
    ft_putstr_fd("error: cannot execute ", STDERR_FILENO);
    ft_putstr_fd(cmd_path, STDERR_FILENO);
    ft_putstr_fd("\n", STDERR_FILENO);
    exit(1); // Child process exits
}

// --- File Descriptor Management ---
void checked_close(int fd) {
    if (close(fd) == -1) {
        fatal_error();
    }
}

// --- Built-in `cd` Command ---
int execute_cd(char **cmd_args) {
    int arg_count = 0;
    while (cmd_args[arg_count] != NULL) {
        arg_count++;
    }

    if (arg_count != 2) {
        cd_bad_args_error();
        return 1; // cd error, not fatal to shell
    }

    if (chdir(cmd_args[1]) == -1) {
        cd_chdir_error(cmd_args[1]);
        return 1; // cd error, not fatal to shell
    }
    return 0; // cd success
}

// --- Main Shell Logic ---
int main(int argc, char **argv, char **envp) {
    if (argc == 1) {
        return 0;
    }
    g_envp = envp;
    int i = 1; // Index for argv, starting after program name

    while (i < argc) {
        g_pids_count = 0;
        int current_input_fd = STDIN_FILENO;
        int sequence_continues = 1;

        while (i < argc && sequence_continues) {
            int arg_scan_idx = i;
            int cmd_argc_val = 0;
            while (arg_scan_idx < argc &&
                   strcmp(argv[arg_scan_idx], "|") != 0 &&
                   strcmp(argv[arg_scan_idx], ";") != 0) {
                cmd_argc_val++;
                arg_scan_idx++;
            }

            // Constraint: "we will never try a "|" immediately followed or preceded by nothing or "|" or ";""
            // This implies cmd_argc_val will be > 0 if argv[i] is a command.
            // If cmd_argc_val is 0 here, it means i pointed to an operator, which should not happen at start of cmd.
            if (cmd_argc_val == 0) { // Should be an invalid input based on constraints.
                i = arg_scan_idx; // Skip the operator
                if (i < argc && (strcmp(argv[i], "|") == 0 || strcmp(argv[i], ";") == 0)) i++;
                continue;
            }


            char **cmd_argv_val = (char **)malloc(sizeof(char *) * (cmd_argc_val + 1));
            if (!cmd_argv_val) fatal_error();
            for (int k = 0; k < cmd_argc_val; k++) {
                cmd_argv_val[k] = argv[i + k];
            }
            cmd_argv_val[cmd_argc_val] = NULL;
            
            int next_operator_type = TYPE_END;
            if (arg_scan_idx < argc) {
                if (strcmp(argv[arg_scan_idx], "|") == 0) {
                    next_operator_type = TYPE_PIPE;
                } else if (strcmp(argv[arg_scan_idx], ";") == 0) {
                    next_operator_type = TYPE_SEMI;
                }
            }

            if (strcmp(cmd_argv_val[0], "cd") == 0) {
                if (current_input_fd != STDIN_FILENO) { // cd cannot be piped into
                    checked_close(current_input_fd);
                    current_input_fd = STDIN_FILENO;
                }
                // cd also cannot pipe its output, so next_operator_type must not be TYPE_PIPE
                // This is guaranteed by "a cd command will never be immediately followed or preceded by a |"
                execute_cd(cmd_argv_val);
                // current_input_fd remains STDIN_FILENO for the next command after potential ';'
            } else {
                int pipe_fds[2] = {-1, -1};
                if (next_operator_type == TYPE_PIPE) {
                    if (pipe(pipe_fds) == -1) {
                        free(cmd_argv_val);
                        fatal_error();
                    }
                }

                pid_t pid = fork();
                if (pid == -1) {
                    free(cmd_argv_val);
                    if (pipe_fds[0] != -1) checked_close(pipe_fds[0]);
                    if (pipe_fds[1] != -1) checked_close(pipe_fds[1]);
                    fatal_error();
                }

                if (pid == 0) { // Child process
                    if (current_input_fd != STDIN_FILENO) {
                        if (dup2(current_input_fd, STDIN_FILENO) == -1) fatal_error();
                        checked_close(current_input_fd);
                    }
                    if (next_operator_type == TYPE_PIPE) {
                        checked_close(pipe_fds[0]);
                        if (dup2(pipe_fds[1], STDOUT_FILENO) == -1) fatal_error();
                        checked_close(pipe_fds[1]);
                    }
                    execve(cmd_argv_val[0], cmd_argv_val, g_envp);
                    execve_error(cmd_argv_val[0]);
                } else { // Parent process
                    if (current_input_fd != STDIN_FILENO) {
                        checked_close(current_input_fd);
                    }
                    if (next_operator_type == TYPE_PIPE) {
                        checked_close(pipe_fds[1]);
                        current_input_fd = pipe_fds[0];
                    } else {
                        current_input_fd = STDIN_FILENO;
                    }
                    if (g_pids_count < 2048) { // Basic bounds check
                        g_pids[g_pids_count++] = pid;
                    } else {
                        // Too many processes for g_pids array, should not happen with "hundreds"
                        // but could be an issue if it's thousands.
                        free(cmd_argv_val);
                        fatal_error(); // Or handle more gracefully
                    }
                }
            }
            free(cmd_argv_val);
            
            i = arg_scan_idx;
            if (i < argc) {
                if (strcmp(argv[i], "|") == 0) {
                    i++;
                    if (i == argc) sequence_continues = 0; // Dangling pipe, invalid per constraints
                } else if (strcmp(argv[i], ";") == 0) {
                    i++;
                    sequence_continues = 0;
                } else { // Should not happen with valid operator placement
                    sequence_continues = 0;
                }
            } else {
                sequence_continues = 0;
            }
        }

        for (int k = 0; k < g_pids_count; k++) {
            waitpid(g_pids[k], NULL, 0);
        }
        
        if (current_input_fd != STDIN_FILENO) {
            checked_close(current_input_fd);
        }
    }
    return 0;
}
