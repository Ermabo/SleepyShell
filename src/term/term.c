#define _POSIX_C_SOURCE 200809L
#include "term.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static bool term_raw_enabled = false;
static struct termios orig_termios;
#define INPUT_CAPACITY 1024
typedef struct {
    unsigned char buffer[INPUT_CAPACITY];
    int length;
    int cursor_pos;
} InputState;

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
    write(STDOUT_FILENO, "$ ", 2);

    write(STDOUT_FILENO, input->buffer, input->length);

    char seq[32];
    snprintf(seq, sizeof(seq), "\x1b[%dG",
             3 + input->cursor_pos); // 3 = 2 prompt chars + 1-based index
    write(STDOUT_FILENO, seq, strlen(seq));
}

char *term_read_input_raw() {
    InputState input = {0};

    // \x7f backspace

    while (true) {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;

        if (c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            break;
        }

        if (c == 'q') {
            return NULL;
        }

        write(STDOUT_FILENO, &c, 1);

        if (input.length < INPUT_CAPACITY - 1) {
            input.buffer[input.length++] = c;
        }
    }

    input.buffer[input.length] = '\0';

    return strdup(input.buffer);
}