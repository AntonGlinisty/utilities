#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void write_error() {
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

char* separate_operators(const char* input, size_t* capacity) {
    size_t len = strlen(input);

    if (len > (SIZE_MAX - 1) / 3) {
        return NULL;
    }

    *capacity = 3 * len + 1;
    char* result = malloc(*capacity);
    if (result == NULL) {
        return NULL;
    }

    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '>' || input[i] == '<') {
            result[out++] = ' ';
            result[out++] = input[i];

            if (input[i] == '>' && i < len - 1 && input[i + 1] == '>') {
                result[out++] = input[++i];
            }

            result[out++] = ' ';
        } else {
            result[out++] = input[i];
        }
    }

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

enum ProcessInputResult {
    SKIP_PROCESSING,
    STOP_PROCESSING,
    CONTINUE_PROCESSING
};

enum ProcessInputResult process_exit(char*** argv, size_t* argc) {    
    if (*argc != 1) {
        write_error();
        free_argv(argv, argc);
        return SKIP_PROCESSING;
    }
    exit(0);
}

enum ProcessInputResult process_cd(char*** argv, size_t* argc) {
    if (*argc != 2 || chdir((*argv)[1]) == -1) {
        write_error();
    }
    free_argv(argv, argc);
    return SKIP_PROCESSING;
}

enum ProcessInputResult process_path(char*** path, size_t* path_size, char*** argv, size_t* argc) {
    if (*argc == 1) {
        free_path(path, path_size);
        free_argv(argv, argc);
        return SKIP_PROCESSING;
    }

    size_t new_path_size = *argc - 1;
    char** new_path = calloc(new_path_size, sizeof(char*));
    if (new_path == NULL) {
        return STOP_PROCESSING;
    }

    size_t index = 0;
    for (; index < new_path_size; index++) {
        *(new_path + index) = strdup((*argv)[index + 1]);
        if (*(new_path + index) == NULL) {
            free_path(&new_path,  &new_path_size);
            return STOP_PROCESSING;
        }
    }

    free_path(path, path_size);
    free_argv(argv, argc);

    *path_size = new_path_size;
    *path = new_path;

    return SKIP_PROCESSING;
}

enum ProcessInputResult process_input(char*** path, size_t* path_size, char*** argv, size_t* argc) {
    if (strcmp((*argv)[0], "exit") == 0) {
        return process_exit(argv, argc);
    }

    if (strcmp((*argv)[0], "cd") == 0) {
        return process_cd(argv, argc);
    }

    if (strcmp((*argv)[0], "path") == 0) {
        return process_path(path, path_size, argv, argc);
    }

    return CONTINUE_PROCESSING;
}

bool is_redirection(const char* arg) {
    return strcmp(arg, ">") == 0 || strcmp(arg, ">>") == 0 || strcmp(arg, "<") == 0;
}

bool validate_input(char*** argv, size_t* argc) {
    if (*argc == 0 || is_redirection((*argv)[0])) {
        write_error();
        free_argv(argv, argc);
        return false;
    }

    for (size_t i = 1; i < *argc; i++) {
        if (!is_redirection((*argv)[i])) {
            continue;
        }

        if (i + 1 == *argc || is_redirection((*argv)[i + 1])) {
            write_error();
            free_argv(argv, argc);
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
            write_error();
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

    write_error();
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

void process_child(char** argv, size_t argc, char* command_full_path) {
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

bool process_parent(pid_t pid, char***argv, size_t* argc, char** command_full_path) {
    free_argv(argv, argc);
    free_command_full_path(command_full_path);

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

bool process_commands(FILE* stream) {
    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen = 0;

    size_t path_size = 0;
    char** path = initialize_path(&path_size);
    if (path == NULL) {
        return false;
    }

    char* command_full_path = NULL;

    size_t argc = 0;
    char** argv = NULL;

    bool has_errors = true;
    while (true) {
        if (stream == stdin && !write_data(STDOUT_FILENO, "shell> ", strlen("shell> "))) { break; }
        
        if ((linelen = getline(&line, &linecap, stream)) == -1) {
            if (feof(stream)) {
                has_errors = false;
            }
            break;
        }
        line[linelen - 1] == '\n' ? line[linelen - 1] = '\0' : true;

        char* new_line = separate_operators(line, &linecap);
        if (new_line == NULL) { break; }
        
        free(line);
        line = new_line;

        bool is_empty_line = false;
        argv = parse_input(line, &argc, &is_empty_line);
        if (is_empty_line) { continue; }
        if (argv == NULL) { break; }

        enum ProcessInputResult result = process_input(&path, &path_size, &argv, &argc);
        if (result == STOP_PROCESSING) { break; }
        if (result == SKIP_PROCESSING) { continue; }

        if (!validate_input(&argv, &argc)) { continue; }

        if ((command_full_path = get_command_full_path(path, path_size, &argv, &argc)) == NULL) { continue; }
        
        pid_t pid = fork();
        if (pid == -1) { break; }
        if (pid == 0) { process_child(argv, argc, command_full_path); }
        
        if (!process_parent(pid, &argv, &argc, &command_full_path)) { break; }
    }

    free(line);
    free_path(&path, &path_size);
    free(argv);
    free(command_full_path);

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
