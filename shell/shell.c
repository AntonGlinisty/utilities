#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void write_error(void) {
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message)); 
}

void free_path(char*** path, size_t* path_size) {
    if (*path == NULL) {
        return;
    }

    for (size_t i = 0; i < *path_size; i++) {
        free(*(*path + i));
    }

    free(*path);
    *path = NULL;
    *path_size = 0;
}

void free_argv(char*** argv, size_t* argc) {
    free(*argv);
    *argv = NULL;
    *argc = 0;
}

void free_command_full_path (char** command_full_path) {
    free(*command_full_path);
    *command_full_path = NULL;
}

bool write_data(int fd, const char* buffer, size_t size) {
    size_t progress = 0;

    while (progress < size) {
        ssize_t bytes_write = write(fd, buffer + progress, size - progress);

        if (bytes_write > 0) {
            progress += (size_t)bytes_write;
            continue;
        }

        if (bytes_write == -1 && errno == EINTR) {
            continue;
        }

        if (bytes_write == 0) {
            errno = EIO;
        }

        return false;
    }

    return true;
}

char** initialize_path(size_t* path_size) {
    char** path = malloc(sizeof(char*));
    if (path == NULL) {
        write_error();
        return NULL;
    }

    char* init_item = strdup("/bin");
    if (init_item == NULL) {
        write_error();
        free(path);
        return NULL;
    }

    path[0] = init_item;
    *path_size = 1;

    return path;
}

char* separate_operators(char** line, size_t* linecap) {
    size_t len = strlen(*line);

    if (len > (SIZE_MAX - 1) / 3) {
        return NULL;
    }

    *linecap = 3 * len + 1;
    char* result = malloc(*linecap);
    if (result == NULL) {
        return NULL;
    }

    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if ((*line)[i] == '>' || (*line)[i] == '<' || (*line)[i] == '&') {
            result[out++] = ' ';
            result[out++] = (*line)[i];

            if ((*line)[i] == '>' && i < len - 1 && (*line)[i + 1] == '>') {
                result[out++] = (*line)[++i];
            }

            result[out++] = ' ';
        } else {
            result[out++] = (*line)[i];
        }
    }

    free(*line);
    *line = NULL;

    result[out] = '\0';
    return result;
}

char** parse_input(char* line, size_t* argc, bool* is_empty_line) {
    size_t count = 10;
    char** argv = calloc(count, sizeof(char*));
    if (argv == NULL) {
        return NULL;
    }

    char* arg = NULL;
    char* delimeters = " \t";

    while ((arg = strsep(&line, delimeters)) != NULL) {
        if (strlen(arg) == 0) {
            continue;
        }

        argv[*argc] = arg;
        *argc += 1;

        if (*argc == count - 1) {
            count *= 2;

            char** new_argv = realloc(argv, count * sizeof(char*));
            if (new_argv == NULL) {
                free(argv);
                return NULL;
            }
            argv = new_argv;
        }
    }

    if (*argc == 0) {
        free(argv);
        argv = NULL;
        *is_empty_line = true;
    } else {
        argv[*argc] = NULL;
    }

    return argv;
}

bool process_cd(char*** argv, size_t* argc) {
    if (*argc != 2 || chdir((*argv)[1]) == -1) {
        write_error();
    }
    free_argv(argv, argc);
    return true;
}

bool process_path(char*** path, size_t* path_size, char*** argv, size_t* argc) {
    if (*argc == 1) {
        free_path(path, path_size);
        free_argv(argv, argc);
        return true;
    }

    size_t new_path_size = *argc - 1;
    char** new_path = calloc(new_path_size, sizeof(char*));
    if (new_path == NULL) {
        return false;
    }

    size_t index = 0;
    for (; index < new_path_size; index++) {
        *(new_path + index) = strdup((*argv)[index + 1]);
        if (*(new_path + index) == NULL) {
            free_path(&new_path,  &new_path_size);
            return false;
        }
    }

    free_path(path, path_size);
    free_argv(argv, argc);

    *path_size = new_path_size;
    *path = new_path;

    return true;
}

bool is_redirection(const char* arg) {
    return strcmp(arg, ">") == 0 || strcmp(arg, ">>") == 0 || strcmp(arg, "<") == 0;
}

bool validate_segment(char** argv, size_t start, size_t end) {
    if (start == end || is_redirection(argv[start])) {
        return false;
    }

    for (size_t i = start + 1; i < end; i++) {
        if (!is_redirection(argv[i])) {
            continue;
        }

        if (i + 1 == end || is_redirection(argv[i + 1])) {
            return false;
        }

        i++;
    }
    
    return true;
}

char* get_command_full_path(char** path, size_t path_size, char*** argv, size_t* argc) {
    char* command = *argv[0];

    for (size_t i = 0; i < path_size; i++) {
        char* candidate = *(path + i);

        char* command_full_path = malloc(strlen(candidate) + strlen(command) + 2);
        if (command_full_path == NULL) {
            free_argv(argv, argc);
            return NULL;
        }

        strcpy(command_full_path, candidate);
        strcat(command_full_path, "/");
        strcat(command_full_path, command);

        if (access(command_full_path, X_OK) == -1) {
            free(command_full_path);
            continue;
        }
        return command_full_path;
    }

    free_argv(argv, argc);
    return NULL;
}

bool process_redirections(char** argv, size_t argc) {
    size_t next_arg = 0;

    for (size_t i = 0; i < argc; i++) {
        if (!is_redirection(argv[i])) {
            argv[next_arg++] = argv[i];
            continue;
        }

        int fd = 0;
        int target = 0;

        if (strcmp(argv[i], ">") == 0) {
            fd = open(argv[i + 1], O_WRONLY | O_TRUNC | O_CREAT, 0640);
            target = STDOUT_FILENO;
        } else if (strcmp(argv[i], ">>") == 0) {
            fd = open(argv[i + 1], O_WRONLY | O_APPEND | O_CREAT, 0640);
            target = STDOUT_FILENO;
        } else if (strcmp(argv[i], "<") == 0) {
            fd = open(argv[i + 1], O_RDONLY);
            target = STDIN_FILENO;
        }

        if (fd == -1) { return false; }
        if (dup2(fd, target) == -1 || (target == STDOUT_FILENO && dup2(fd, STDERR_FILENO) == -1)) {
            close(fd);
            return false;
        }
        if (close(fd) == -1) { return false; }
        i++;
    }

    argv[next_arg] = NULL;
    return true;
}

void process_child(char** path, size_t path_size, char** argv, size_t argc) {
    char* command_full_path = get_command_full_path(path, path_size, &argv, &argc);
    if (command_full_path == NULL) {
        write_error();
        exit(1);
    }

    if (!process_redirections(argv, argc)) {
        write_error();
        exit(1);
    }

    if (execv(command_full_path, argv) == -1) {
        write_error();
        exit(1);
    }
    exit(0);
}

bool wait_child(pid_t pid) {
    while (true) {
        pid_t waitpid_result = waitpid(pid, NULL, 0);
        if (waitpid_result == -1 && errno == EINTR) {
            continue;
        }
        if (waitpid_result == -1) {
            return false;
        }
        break;
    }

    return true;
}

pid_t spawn_child(char** path, size_t path_size, char** argv, size_t command_start, size_t command_end) {
    char** command_argv = calloc(command_end - command_start + 1, sizeof(char*));
    if (command_argv == NULL) {
        return -1;
    }

    for (size_t j = 0; j < command_end - command_start; j++) {
        command_argv[j] = argv[command_start + j];
    }
    command_argv[command_end - command_start] = NULL;

    pid_t pid = fork();
    if (pid == -1) {
        free(command_argv);
        return -1;
    }
    if (pid == 0) {
        process_child(path, path_size, command_argv, command_end - command_start);
    }

    free(command_argv);
    return pid;
}

bool process_commands_execution(char*** path, size_t* path_size, char*** argv, size_t* argc) {
    pid_t* pids_storage = calloc(*argc + 1, sizeof(pid_t));
    if (pids_storage == NULL) {
        return false;
    }

    bool has_errors = false;
    bool exit_requested = false;
    size_t command_start = 0;
    size_t childs_count = 0;

    for (size_t i = 0; i <= *argc; i++) {
        if (i != *argc && strcmp((*argv)[i], "&") != 0) {
            continue;
        }

        if (command_start < i) {
            if (!validate_segment(*argv, command_start, i)) {
                write_error();
            } else {
                size_t segment_argc = i - command_start;
                char* command = (*argv)[command_start];

                if (strcmp(command, "exit") == 0) {
                    if (segment_argc != 1) {
                        write_error();
                    } else {
                        exit_requested = true;
                    }
                } else if (strcmp(command, "cd") == 0 || strcmp(command, "path") == 0) {
                    char** segment_argv = calloc(segment_argc + 1, sizeof(char*));
                    if (segment_argv == NULL) {
                        has_errors = true;
                        break;
                    }

                    for (size_t j = 0; j < segment_argc; j++) {
                        segment_argv[j] = (*argv)[command_start + j];
                    }

                    bool result = strcmp(command, "cd") == 0 ? process_cd(argv, argc) : process_path(path, path_size, argv, argc);
                    if (!result) {
                        free_argv(&segment_argv, &segment_argc);
                        has_errors = true;
                        break;
                    }
                } else {
                    pid_t pid = spawn_child(*path, *path_size, *argv, command_start, i);
                    if (pid == -1) {
                        has_errors = true;
                        break;
                    }
                    pids_storage[childs_count++] = pid;
                }
            }
        }

        command_start = i + 1;
    }

    for (size_t i = 0; i < childs_count; i++) {
        if (!wait_child(pids_storage[i])) {
            has_errors = true;
        }
    }

    free(pids_storage);
    free_argv(argv, argc);

    if (exit_requested && !has_errors) {
        exit(0);
    }
    return !has_errors;
}

bool process_commands(FILE* stream) {
    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen = 0;

    size_t path_size = 0;
    char** path = initialize_path(&path_size);
    if (path == NULL) {
        return false;
    }

    size_t argc = 0;
    char** argv = NULL;

    bool has_errors = true;
    while (true) {
        if (stream == stdin && !write_data(STDOUT_FILENO, "shell> ", strlen("shell> "))) {
            break;
        }
        
        if ((linelen = getline(&line, &linecap, stream)) == -1) {
            if (feof(stream)) {
                has_errors = false;
            }
            break;
        }
        line[linelen - 1] == '\n' ? line[linelen - 1] = '\0' : true;

        char* new_line = separate_operators(&line, &linecap);
        if (new_line == NULL) {
            break;
        }
        line = new_line;

        bool is_empty_line = false;
        argv = parse_input(line, &argc, &is_empty_line);
        if (is_empty_line) {
            continue;
        }
        if (argv == NULL) {
            break;
        }

        if (!process_commands_execution(&path, &path_size, &argv, &argc)) {
            break;
        }
    }

    free(line);
    free_path(&path, &path_size);
    free_argv(&argv, &argc);

    if (has_errors) {
        write_error();
        return false;
    }
    
    return true;
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        write_error();
        return 1;
    }

    if (argc == 1) {
        return process_commands(stdin) ? 0 : 1;
    }

    FILE* stream = fopen(argv[1], "r");
    if (stream == NULL) {
        write_error();
        return 1;
    }

    if (!process_commands(stream)) {
        fclose(stream);
        return 1;
    }

    if (fclose(stream) == -1) {
        write_error();
        return 1;
    }

    return 0;
}
