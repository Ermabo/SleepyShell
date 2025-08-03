#define _POSIX_C_SOURCE 200809L
#include "builtins.h"
#include "path_utils.h"
#include "term/term.h"
#include "tokenizer.h"

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define INPUT_SIZE 101
#define MAX_ARGS 16
#define MAX_TOKEN_COUNT (MAX_ARGS + 1)

/**
 * @target_fd   The file descriptor we’ll redirect (e.g. STDOUT_FILENO).
 * @saved_fd    Backup of the original target_fd (initialized to –1).
 * @filename    NULL or a strdup’d string that the caller must free.
 * @open_flags  Flags passed to open(), e.g. O_WRONLY|O_CREAT|O_TRUNC.
 */
typedef struct {
    int target_fd;
    int saved_fd;
    char *filename;
    int open_flags;
} RedirSpec;

static bool is_stdout_redirect(const char *c) {
    return strcmp(c, ">") == 0 || strcmp(c, "1>") == 0;
}

static bool is_stdin_redirect(const char *c) { return strcmp(c, "<") == 0; }

static bool is_stdout_append(const char *c) { return strcmp(c, ">>") == 0; }

static bool is_stderr_redirect(const char *c) { return strcmp(c, "2>") == 0; }

static bool is_stderr_append(const char *c) { return strcmp(c, "2>>") == 0; }

/**
 * extract_redirection - remove I/O redirections from argv
 * @tokens:     array of malloc'd strings
 * @token_count: original count
 * @new_token_count: out: new count after removing ops+filenames
 * @redir_specs_count_out: out: number of specs (always 3)
 *
 * Returns a malloc'd RedirSpec[3] (stdout, stderr, stdin), with
 * .filename == NULL if untouched, or a strdup'd target on success.
 * Returns NULL on parse or alloc failure (tokens[] left intact).
 */
RedirSpec *extract_redirection(char *tokens[], int token_count, int *new_token_count,
                               int *redir_specs_count_out) {
    assert(new_token_count && redir_specs_count_out);
    assert(token_count < MAX_TOKEN_COUNT);

    char *kept_tokens[MAX_TOKEN_COUNT];
    char *dropped_tokens[MAX_TOKEN_COUNT];
    int dropped_tokens_index = 0;
    int write_index = 0;

    const int redir_spec_count = 3;
    *redir_specs_count_out = redir_spec_count;
    RedirSpec *redir_specs = calloc(redir_spec_count, sizeof *redir_specs);
    if (!redir_specs) {
        perror("calloc");
        return NULL;
    }

    const int default_fds[3] = {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO};
    for (int i = 0; i < redir_spec_count; i++) {
        redir_specs[i].target_fd = default_fds[i];
        redir_specs[i].saved_fd = -1;
    }

    for (int i = 0; i < token_count; i++) {
        int fd = -1;
        int open_flags = 0;

        if (is_stdout_redirect(tokens[i])) {
            fd = STDOUT_FILENO;
            open_flags = O_WRONLY | O_CREAT | O_TRUNC;
        } else if (is_stdout_append(tokens[i])) {
            fd = STDOUT_FILENO;
            open_flags = O_WRONLY | O_CREAT | O_APPEND;
        } else if (is_stderr_redirect(tokens[i])) {
            fd = STDERR_FILENO;
            open_flags = O_WRONLY | O_CREAT | O_TRUNC;
        } else if (is_stderr_append(tokens[i])) {
            fd = STDERR_FILENO;
            open_flags = O_WRONLY | O_CREAT | O_APPEND;
        } else if (is_stdin_redirect(tokens[i])) {
            fd = STDIN_FILENO;
            open_flags = O_RDONLY;
        }

        if (fd != -1) {
            if (i + 1 >= token_count) {
                fprintf(stderr, "syntax error: expected file after '%s'\n", tokens[i]);
                goto error;
            }
            for (int j = 0; j < redir_spec_count; j++) {
                if (redir_specs[j].target_fd == fd) {
                    // POSIX shell behavior: last redirection to the same FD wins
                    free(redir_specs[j].filename);
                    // Copy filename
                    redir_specs[j].filename = strdup(tokens[i + 1]);
                    if (!redir_specs[j].filename) {
                        perror("strdup");
                        goto error;
                    }
                    redir_specs[j].open_flags = open_flags;
                }
            }
            // Trash the redir token and filename, defer freeing until after we know parse succeeded
            dropped_tokens[dropped_tokens_index++] = tokens[i];
            dropped_tokens[dropped_tokens_index++] = tokens[i + 1];
            i++; // Skip the filename
            continue;
        }
        kept_tokens[write_index++] = tokens[i];
    }

    for (int i = 0; i < dropped_tokens_index; i++) {
        free(dropped_tokens[i]);
    }

    // Copy back & terminate
    for (int i = 0; i < write_index; i++)
        tokens[i] = kept_tokens[i];

    tokens[write_index] = NULL;
    *new_token_count = write_index;

    // Clear any stale pointers
    for (int i = write_index + 1; i < token_count; i++)
        tokens[i] = NULL;

    return redir_specs;

error:
    for (int j = 0; j < redir_spec_count; j++)
        free(redir_specs[j].filename);
    free(redir_specs);
    return NULL;
}

void apply_all_redirection(RedirSpec specs[], const int count) {
    for (int i = 0; i < count; i++) {
        RedirSpec *spec = &specs[i];

        if (!spec->filename)
            continue;

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

static void execute_command(const char *program_name, char *argv[16], RedirSpec specs[],
                            int redir_count) {
    char *bin_full_path = util_find_bin_in_path(program_name);
    if (bin_full_path == NULL) {
        printf("%s: command not found\n", program_name);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        apply_all_redirection(specs, redir_count);
        execv(bin_full_path, argv);
        perror("execv failed");
        exit(1);
    }

    wait(NULL);
    free(bin_full_path);
}

int main(int argc, char *argv[]) {
    // TODO: Finish rawmode implementation. This is just for test
    // TODO: If TERM env var is null we use fgets and bypass raw

    if (argc > 1 && strcmp(argv[1], "-raw") == 0) {
        term_enable_raw_mode();

        printf("Raw mode enabled. Press 'q' to quit.\n");

        while (1) {
            char *input = term_read_input_raw();
            free(input);
            /*char c;
            if (read(STDIN_FILENO, &c, 1) == -1) {
                perror("read");
                return 1;
            }

            printf("Pressed: '%c' (ASCII: %d)\n", isprint(c) ? c : '?', c);

            if (c == 'q')
                break*/
            ;
        }

        term_disable_raw_mode();
        return 0;
    }
    while (1) {
        setbuf(stdout, NULL);
        printf("$ ");

        char input[INPUT_SIZE];
        if (!fgets(input, INPUT_SIZE, stdin)) {
            if (feof(stdin)) {
                printf("\nexit\n");
                return 0;
            }
            perror("fgets");
            printf("\n");
            return 1;
        }

        input[strcspn(input, "\n")] = '\0';
        char *tokens[MAX_TOKEN_COUNT];
        int token_count = tokenize_input(input, tokens, MAX_TOKEN_COUNT);
        if (token_count <= 0) {
            continue;
        }

        const char *command = tokens[0];
        int cleaned_count = token_count;
        int redir_specs_count = 0;

        RedirSpec *specs =
            extract_redirection(tokens, token_count, &cleaned_count, &redir_specs_count);
        if (!specs) {
            fprintf(stderr, "Failed to parse redirections\n");
            free_tokens(tokens, token_count);
            continue;
        }

        token_count = cleaned_count;
        if (builtin_is_builtin(command)) {
            apply_all_redirection(specs, redir_specs_count);
            if (!strcmp(command, "exit")) {
                free_tokens(tokens, token_count);
                builtin_exit(tokens, token_count);
                break;
            }

            if (!strcmp(command, "echo")) {
                builtin_echo(tokens, token_count);
            } else if (!strcmp(command, "pwd")) {
                builtin_pwd();
            } else if (!strcmp(command, "cd")) {
                char *arg = token_count > 1 ? tokens[1] : NULL;
                builtin_cd(arg);
            } else if (!strcmp(command, "type")) {
                builtin_type(tokens, token_count);
            }
        } else {
            execute_command(command, tokens, specs, redir_specs_count);
        }

        // cleanup
        restore_all_redirection(specs, redir_specs_count);
        free_tokens(tokens, token_count);
        for (int i = 0; i < redir_specs_count; i++) {
            free(specs[i].filename);
        }
        free(specs);
    }

    return 0;
}