#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "builtins.h"


#define INPUT_SIZE   101
#define TOKEN_COUNT  16

// TODO: Define this alongside RedirSpec[] to make index usage readable
typedef enum {
    REDIR_STDOUT,
    REDIR_STDERR,
    REDIR_STDIN,
    REDIR_SPEC_COUNT
} RedirTarget;

// TODO: REMOVE this, no longer needed if RedirSpec[] directly stores filenames
typedef struct {
    char *stdout_file;
    char *stderr_file;
    char *stdin_file;
} RedirectionFilenames;

// TODO: REMOVE this, replacing dynamic function-pointer matching with simple string comparisons
typedef struct {
    bool (*match_fn)(const char *);
    char **target_field;
} RedirRule;

typedef struct {
    int target_fd;      // STDOUT_FILENO, STDERR_FILENO, etc.
    int saved_fd;       // backup of original fd to restore later
    char *filename;     // redirection target file
    int open_flags;     // O_WRONLY, O_APPEND, etc.
    bool append_flag;   // TODO: Don't need this, can just modify the open_flags instead
} RedirSpec;

/**
 * Searches the PATH environment variable for the given command.
 *
 * @param command  Command name to search for.
 * @return         Malloc'd Full path to the binary, or NULL if not found.
 */
static char *find_bin_in_path(char *command) {
    const char *path = getenv("PATH");
    if (path == NULL)
        return NULL;

    char *local_path = strdup(path);
    if (local_path == NULL)
        return NULL;

    char *folder_path = strtok(local_path, ":");
    size_t buffer_size = 1024;
    char *full_path = malloc(buffer_size);
    if (full_path == NULL) {
        free(local_path);
        return NULL;
    }

    while (folder_path != NULL) {
        // Add 1 for '/' and 1 for '\0' terminator
        const size_t full_path_size = strlen(folder_path) + 1 + strlen(command) + 1;
        if (buffer_size < full_path_size) {
            char *temp = realloc(full_path, full_path_size);
            if (temp == NULL) {
                free(full_path);
                free(local_path);
                return NULL;
            }
            full_path = temp;
            buffer_size = full_path_size;
        }

        sprintf(full_path, "%s/%s", folder_path, command);

        if (access(full_path, X_OK) == 0) {
            free(local_path);
            return full_path;
        }

        folder_path = strtok(NULL, ":");
    }

    free(local_path);
    free(full_path);
    return NULL;
}


/**
 * Tokenizes shell input into arguments.
 *
 * @param input       User input string.
 * @param tokens      Output array for tokens.
 * @param max_tokens  Maximum number of tokens allowed.
 * @return            Number of tokens parsed, or -1 on error.
 */
static int tokenize_input(const char *input, char *tokens[], const int max_tokens) {
    // TODO: Better error handling, perhaps return an enum with tokenizer_errors instead
    char token_buffer[128]; // TODO: Should probably look over buffer sizing
    int token_len = 0;
    int token_count = 0;

    int i = 0;
    char quote = 0;
    while (input[i] != '\0') {
        const char c = input[i];

        // TODO: Check which parts of the code we can extract into functions, maybe even create a new tokenizer module
        if (c == '\'' || c == '"') {
            if (quote == 0) {
                quote = c;
            } else if (quote == c) {
                quote = 0;
            } else {
                token_buffer[token_len++] = c;
            }
            i++;
            continue;
        }

        if (quote == '"' && c == '\\') {
            const char next = input[i + 1];
            if (next == '\0') {
                token_buffer[token_len++] = c;
                i++;
                continue; // TODO: Can't we just break here?
            }

            if (next == '"' || next == '\\' || next == '$' || next == '\n') {
                token_buffer[token_len++] = next;
                i += 2;
                continue;
            }
        }

        if (quote == 0 && c == '\\') {
            const char next = input[i + 1];
            if (next == '\0') {
                i++;
                continue;
            }

            if (token_len >= sizeof(token_buffer) - 1)
                return -1;

            token_buffer[token_len++] = next;
            i += 2;
            continue;
        }

        if (quote == 0 && (c == ' ' || c == '\t')) {
            // Check for multiple spaces and skip
            if (token_len == 0) {
                i++;
                continue;
            }

            token_buffer[token_len] = '\0';
            if (token_count >= max_tokens)
                return -1;

            tokens[token_count++] = strdup(token_buffer);
            token_len = 0;
            i++;
            continue;
        }

        if (token_len >= sizeof(token_buffer) - 1)
            return -1;

        token_buffer[token_len++] = c;
        i++;
    }

    if (quote != 0)
        return -1;

    if (token_len > 0) {
        token_buffer[token_len] = '\0';
        if (token_count >= max_tokens)
            return -1;

        tokens[token_count++] = strdup(token_buffer);
    }

    tokens[token_count] = NULL;
    return token_count;
}


/**
 * @brief Frees shell command tokens.
 *
 * Frees each dynamically allocated token string used by the shell.
 *
 * @param tokens Array of command tokens.
 * @param count  Number of tokens.
 */
static void free_tokens(char *tokens[], const int count) {
    for (int i = 0; i < count; i++) {
        free(tokens[i]);
        tokens[i] = NULL;
    }
}

static bool is_stdout_redirect(const char *c) {
    return strcmp(c, ">") == 0 || strcmp(c, "1>") == 0;
}

static bool is_stderr_redirect(const char *c) {
    return strcmp(c, "2>") == 0;
}

static bool is_stdout_append(const char *c) {
    return strcmp(c, ">>") == 0;
}


/**
 * Extracts redirection info from tokens[], and updates the token array.
 *
 * @param tokens           Input/output array of string tokens.
 * @param token_count      Number of original tokens.
 * @param redir            Output struct for redirection info.
 * @param new_token_count  Optional. If not NULL, will be set to the updated token count.
 *
 * @return 0 on success, -1 on error.
 */
int extract_redirection(char *tokens[], const int token_count, RedirectionFilenames *redir, int *new_token_count) {
    char *temp_tokens[TOKEN_COUNT] = {0};
    int write_index = 0;

    RedirRule rules[] = {
        { is_stdout_redirect, &redir->stdout_file },
        { is_stderr_redirect, &redir->stderr_file }
    };
    int rule_count = sizeof(rules) / sizeof(rules[0]);

    for (int i = 0; i < token_count; i++) {
        bool matched = false;

        for (int r = 0; r < rule_count; r++) {
            if (rules[r].match_fn(tokens[i])) {
                if (i + 1 >= token_count) {
                    // TODO: 4 levels of indentation/nesting here. Needs redesign
                    fprintf(stderr, "syntax error: expected file after redirection\n");
                    return -1;
                }
                *rules[r].target_field = strdup(tokens[i + 1]);
                free(tokens[i]);
                free(tokens[i + 1]);

                i++; // Skip filename which is the next token
                matched = true;
                break;
            }
        }

        if (!matched)
            temp_tokens[write_index++] = tokens[i];
    }

    temp_tokens[write_index] = NULL;

    // Copy back to original tokens[]
    for (int i = 0; i <= write_index; i++) {
        tokens[i] = temp_tokens[i];
    }

    if (new_token_count) {
        *new_token_count = write_index;
    }

    // Clear any leftover tokens to avoid dangling pointers or stale data
    for (int i = write_index + 1; i < token_count; i++) {
        tokens[i] = NULL;
    }

    return 0;
}

void apply_all_redirection(RedirSpec specs[], const int count) {
    for (int i = 0; i < count; i++) {
        RedirSpec *spec = &specs[i];

        if (!spec->filename)
            continue;

        // TODO: Check for append flag here and do append if true

        int fd = open(spec->filename, spec->open_flags, 0644);
        if (fd == -1) {
            perror("open");
            continue;
        }

        spec->saved_fd = dup(spec->target_fd);
        if (spec->saved_fd == -1) {
            perror("dup");
            close(fd);
            continue;
        }

        if (dup2(fd, spec->target_fd) == -1) {
            perror("dup2");
            close(fd);
            continue;
        }

        close(fd);
    }
}

void restore_all_redirection(RedirSpec specs[], const int count) {
    for (int i = 0; i < count; i++) {
        RedirSpec *spec = &specs[i];

        if (spec->saved_fd == -1)
            continue;

        if (dup2(spec->saved_fd, spec->target_fd) == -1) {
            perror("dup2");
            continue;
        }

        close(spec->saved_fd);

        spec->saved_fd = -1;
    }
}


void echo(char *command_args[16], const int token_count) {
    for (int i = 1; i < token_count; i++) {
        printf("%s", command_args[i]);

        // Ensure no trailing space after last argument
        if (i < token_count - 1)
            printf(" ");
    }

    printf("\n");
}


void type(char *command_args[16], const int token_count) {
    const char *builtins[] = {"echo", "exit", "type", "pwd", "cd"};
    const char *args = token_count > 1 ? command_args[1] : NULL;

    if (args == NULL || args[0] == '\0') {
        printf("you need to provide an arg\n");
        return;
    }

    bool found = false;
    const int len = sizeof(builtins) / sizeof(builtins[0]);
    for (int i = 0; i < len; ++i) {
        if (!strcmp(builtins[i], args)) {
            printf("%s is a shell builtin\n", builtins[i]);
            found = true;
            break;
        }
    }

    if (!found) {
        char *full_path = find_bin_in_path((char *) args);
        if (full_path) {
            printf("%s is %s\n", args, full_path);
            free(full_path);
        } else {
            printf("%s: not found\n", args);
        }
    }
}

static void execute_command(const char *command, char *command_args[16], RedirSpec specs[], int redir_count) {
    char *bin_full_path = find_bin_in_path((char *)command);
    if (bin_full_path == NULL) {
        printf("%s: command not found\n", command);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        apply_all_redirection(specs, redir_count);
        execv(bin_full_path, command_args);
        perror("execv failed");
        exit(1);
    }

    wait(NULL);
    free(bin_full_path);
}

int main() {
    while (1) {
        setbuf(stdout, NULL);
        printf("$ ");

        char input[INPUT_SIZE];
        if (!fgets(input, INPUT_SIZE, stdin)) {
            printf("\n");
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        char *command_args[TOKEN_COUNT];
        int token_count = tokenize_input(input, command_args, TOKEN_COUNT);

        if (token_count <= 0)
            continue;

        const char *command = command_args[0];
        RedirectionFilenames redir = {0};
        int cleaned_count = token_count;

        if (extract_redirection(command_args, token_count, &redir, &cleaned_count) != 0) {
            free_tokens(command_args, token_count);
            continue;
        }
        token_count = cleaned_count;

        RedirSpec specs[] = {
            { STDOUT_FILENO, -1, redir.stdout_file, O_WRONLY | O_CREAT | O_TRUNC },
            { STDERR_FILENO, -1, redir.stderr_file, O_WRONLY | O_CREAT | O_TRUNC },
            { STDIN_FILENO, -1, redir.stdin_file, O_RDONLY },
        };

        apply_all_redirection(specs, sizeof(specs) / sizeof (specs[0]));

        if (!strcmp(command, "exit")) {
            free_tokens(command_args, token_count);
            exit(EXIT_SUCCESS); // TODO: Don't need to free here right since we exit program anyway?
        }

        if (!strcmp(command, "echo")) {
            echo(command_args, token_count);
            goto cleanup;
        }

        if (!strcmp(command, "pwd")) {
            // TODO: Change bufsize to something more dynamic, also check getcwd for errors
            char cwd_buffer[1024];
            char *cwd = getcwd(cwd_buffer, sizeof(cwd_buffer));
            printf("%s\n", cwd);
            goto cleanup;
        }

        if (!strcmp(command, "cd")) {
            char *args = token_count > 1 ? command_args[1] : NULL;
            builtin_cd(args);
            goto cleanup;
        }

        if (!strcmp(command, "type")) {
            type(command_args, token_count);
            goto cleanup;
        }

        execute_command(command, command_args, specs, sizeof(specs) / sizeof (specs[0]));

        cleanup:
        restore_all_redirection(specs, sizeof(specs) / sizeof (specs[0]));
        free_tokens(command_args, token_count);

        // TODO: Clean this if mess up...
        if (redir.stdout_file) free(redir.stdout_file);
        if (redir.stderr_file) free(redir.stderr_file);
        if (redir.stdin_file)  free(redir.stdin_file);
    }

    return 0;
}
