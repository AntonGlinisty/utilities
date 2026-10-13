#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void write_error() {
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message)); 
}

bool write_text(int fd, const char* buffer, size_t size) {
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

void free_path(char*** path, size_t path_size) {
    if (*path == NULL) {
        return;
    }

    for (size_t i = 0; i < path_size; i++) {
        free(*(*path + i));
    }

    free(*path);
    *path = NULL;
}

ssize_t get_command_args(char*** argv, char* line) {
    size_t argv_size = 10;
    *argv = calloc(argv_size, sizeof(char*));
    if (*argv == NULL) {
        return -1;
    }

    char* arg = NULL;
    size_t argc = 0;
    char* delimeters = " \t";

    while ((arg = strsep(&line, delimeters)) != NULL) {
        if (strlen(arg) == 0) {
            continue;
        }

        (*argv)[argc++] = arg;
        if (argc == argv_size - 1) {
            argv_size *= 2;

            char** new_argv = realloc(*argv, argv_size * sizeof(char*));
            if (new_argv == NULL) {
                free(*argv);
                *argv = NULL;
                return -1;
            }
            *argv = new_argv;
        }
    }

    if (argc == 0) {
        free(*argv);
        *argv = NULL;
    } else {
        (*argv)[argc] = NULL;
    }

    return argc;
}

char* get_command_full_path(char** path, size_t path_size, char** argv) {
    char* command = argv[0];

    for (size_t i = 0; i < path_size; i++) {
        char* candidate = *(path + i);

        char* command_full_path = malloc(strlen(candidate) + strlen(command) + 2);
        if (command_full_path == NULL) {
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

    return NULL;
}

enum BuildInFunctions {
    NOT_BUILD_IN_FUNCTION,
    PROCESS_OK,
    PROCESS_HAS_ERRORS
};

enum BuildInFunctions process_exit(ssize_t argc) {    
    if (argc != 1) {
        write_error();
        return PROCESS_OK;
    }
    exit(0);
}

enum BuildInFunctions process_cd(char** argv, ssize_t argc) {
    if (argc != 2 || chdir(argv[1]) == -1) {
        write_error();
    }
    return PROCESS_OK;
}

enum BuildInFunctions process_path(char*** path, size_t* path_size, char** argv, ssize_t argc) {
    if (argc == 1) {
        free_path(path, *path_size);
        *path_size = 0;
        return PROCESS_OK;
    }

    size_t new_path_size = argc - 1;
    char** new_path = calloc(new_path_size, sizeof(char*));
    if (new_path == NULL) {
        return PROCESS_HAS_ERRORS;
    }

    size_t index = 0;
    for (; index < new_path_size; index++) {
        *(new_path + index) = strdup(argv[index + 1]);
        if (*(new_path + index) == NULL) {
            free_path(&new_path,  new_path_size);
            return PROCESS_HAS_ERRORS;
        }
    }

    free_path(path, *path_size);

    *path_size = new_path_size;
    *path = new_path;

    return PROCESS_OK;
}

enum BuildInFunctions process_build_in_functions(char* line, char*** path, size_t* path_size, char** argv, ssize_t argc) {
    if (strcmp(argv[0], "exit") == 0) {
        return process_exit(argc);
    }

    if (strcmp(argv[0], "cd") == 0) {
        return process_cd(argv, argc);
    }

    if (strcmp(argv[0], "path") == 0) {
        return process_path(path, path_size, argv, argc);
    }

    return NOT_BUILD_IN_FUNCTION;
}

bool process_commands(FILE* stream) {
    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen = 0;

    char** path = malloc(sizeof(char*));
    if (path == NULL) {
        write_error();
        return false;
    }

    path[0] = strdup("/bin");
    if (path[0] == NULL) {
        write_error();
        free(path);
        return false;
    }
    size_t path_size = 1;

    char* command_full_path = NULL;
    char** argv = NULL;

    while (true) {
        if (stream == stdin && !write_text(STDOUT_FILENO, "wish> ", strlen("wish> "))) {
            break;
        }
        
        if ((linelen = getline(&line, &linecap, stream)) == -1) {
            break;
        }
        line[linelen - 1] == '\n' ? line[linelen - 1] = '\0' : true;

        ssize_t argc = get_command_args(&argv, line);
        if (argc == -1) { break; }
        if (argc == 0) {
            free(argv);
            argv = NULL;
            continue;
        }

        enum BuildInFunctions result = process_build_in_functions(line, &path, &path_size, argv, argc);
        if (result == PROCESS_HAS_ERRORS) { break; }
        if (result == PROCESS_OK) {
            free(argv);
            argv = NULL;
            continue;
        }

        if ((command_full_path = get_command_full_path(path, path_size, argv)) == NULL) {
            write_error();
            free(argv);
            argv = NULL;
            continue;
        }
        
        pid_t pid = fork();
        if (pid == -1) { break; }
        if (pid == 0) {
            if (execv(command_full_path, argv) == -1) {
                write_error();
                exit(1);
            }
            exit(0);
        }

        free(argv);
        argv = NULL;

        free(command_full_path);
        command_full_path = NULL;
        
        bool has_errors = false;
        while (true) {
            pid_t waitpid_result = waitpid(pid, NULL, 0);
            if (waitpid_result == -1 && errno == EINTR) {
                continue;
            }
            if (waitpid_result == -1) {
                has_errors = true;
                break;
            }
            break;
        }

        if (has_errors) { break; }
    }

    write_error();

    free(line);
    free(argv);
    free_path(&path, path_size);
    free(command_full_path);

    return false;
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
