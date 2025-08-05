#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "term.h"

#define INPUT_CAPACITY 1024
#define PROMPT "$ "
#define PROMPT_LEN 2

static bool term_raw_enabled = false;
static struct termios orig_termios;

enum { KEY_BACKSPACE_CTRL_H = 0x08, KEY_BACKSPACE_DEL = 0x7f };

typedef struct {
    unsigned char buffer[INPUT_CAPACITY];
    int length;
    int cursor_pos;
} InputState;

static bool is_visible_ascii(const unsigned char c) { return c >= 32 && c <= 126; }
static bool char_is_backspace(const unsigned char c) {
    return c == KEY_BACKSPACE_CTRL_H || c == KEY_BACKSPACE_DEL;
}

void term_enable_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(1);
    }

    atexit(term_disable_raw_mode);

    struct termios raw = orig_termios;

    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(1);
    }

    term_raw_enabled = true;
}

void term_disable_raw_mode() {
    assert(term_raw_enabled && "term_disable_raw_mode called before enable");

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        perror("tcsetattr");
        exit(1);
    }
}

static void redraw_input_line(const InputState *input) {
    write(STDOUT_FILENO, "\x1b[2K\r", 5);
    write(STDOUT_FILENO, PROMPT, PROMPT_LEN);

    write(STDOUT_FILENO, input->buffer, input->length);

    char seq[32];
    snprintf(seq, sizeof(seq), "\x1b[%dG", PROMPT_LEN + 1 + input->cursor_pos);

    write(STDOUT_FILENO, seq, strlen(seq));
}

char *term_read_input_raw() {
    InputState input = {0};

    redraw_input_line(&input);

    while (true) {
        unsigned char c;
        unsigned char seq[8] = {0};

        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;

        if (c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            break;
        }

        if (c == 'q') {
            return NULL;
        }

        if (c == '\x1b') {
            if (read(STDIN_FILENO, &seq[0], 1) != 1)
                continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1)
                continue;

            if (seq[0] == '[') {
                if (seq[1] == 'D') {
                    if (input.cursor_pos > 0) {
                        input.cursor_pos--;
                        redraw_input_line(&input);
                    }
                }
            }
        }

        if (char_is_backspace(c) && input.length > 0) {
            input.length--;
            input.cursor_pos = input.length;
            input.buffer[input.length] = '\0';
            redraw_input_line(&input);
        }

        if (is_visible_ascii(c) && input.length < INPUT_CAPACITY - 1) {
            input.buffer[input.length++] = c;
            input.cursor_pos = input.length;

            redraw_input_line(&input);
        }
    }

    input.buffer[input.length] = '\0';

    return strdup(input.buffer);
}