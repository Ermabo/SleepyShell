#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Splits input into at most capacity-1 tokens, always NULL‑terminating tokens[].
 *
 * @param input     The line to tokenize.
 * @param tokens    Output array of tokens.
 * @param capacity  Total size of tokens[] (must be ≥2).
 * @return          Number of real tokens (0..capacity-1), or -1 on error.
 */
int tokenize_input(const char *input, char *tokens[], int capacity) {
    // TODO: Better error handling, perhaps return an enum with tokenizer_errors instead
    if (capacity < 2)
        return -1;

    int max_tokens = capacity - 1;
    char token_buffer[128]; // TODO: Should probably look over buffer sizing
    int token_len = 0;
    int token_count = 0;

    int i = 0;
    char quote = 0;
    while (input[i] != '\0') {
        const char c = input[i];
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
                // No character to escape: end token and drop trailing backslash
                break;
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
                // We don’t support line‐continuation here—drop trailing '\' and finish token
                break;
            }

            if (token_len >= sizeof(token_buffer) - 1)
                goto error;

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
                goto error;

            char *token = strdup(token_buffer);
            if (!token) {
                perror("strdup");
                goto error;
            }

            tokens[token_count++] = token;
            token_len = 0;
            i++;
            continue;
        }

        if (token_len >= sizeof(token_buffer) - 1)
            goto error;

        token_buffer[token_len++] = c;
        i++;
    }

    if (quote != 0)
        goto error;

    if (token_len > 0) {
        if (token_count >= max_tokens)
            goto error;

        token_buffer[token_len] = '\0';
        char *token = strdup(token_buffer);
        if (!token) {
            perror("strdup");
            goto error;
        }

        tokens[token_count++] = token;
    }

    tokens[token_count] = NULL;
    return token_count;

error:
    for (int j = 0; j < token_count; j++) {
        free(tokens[j]);
    }
    return -1;
}

/**
 * @brief Frees shell command tokens.
 *
 * Frees each dynamically allocated token string used by the shell.
 *
 * @param tokens Array of command tokens.
 * @param count  Number of tokens.
 */
void free_tokens(char *tokens[], const int count) {
    for (int i = 0; i < count; i++) {
        free(tokens[i]);
        tokens[i] = NULL;
    }
}
