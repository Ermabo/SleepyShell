#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

static bool snprintf_fits(int result, const size_t bufsize, char *label) {
    if (result < 0) {
        fprintf(stderr, "%s: encoding error\n", label ? label : "snprintf");
        return false;
    }

    // Ensure path fits in buffer
    if ((size_t)result >= bufsize) {
        fprintf(stderr, "%s: output was too long: needed %d bytes, buffer is %zu\n", label ? label : "snprintf", result + 1, bufsize);
        return false;
    }

    return true;
}

static char* expand_home_directory(char *arg, char *buf, const size_t bufsize)
{
    assert(bufsize > 0);
    assert(buf);

    char *home_path = getenv("HOME");

     if (!home_path) {
         fprintf(stderr, "cd: HOME variable not set\n");
         return NULL;
     }

    // Default to home if no argument or argument is just "~"
    if (!arg || ( arg[0] == '~' && arg [1] == '\0')) {
        int len = snprintf(buf, bufsize, "%s", home_path);
        return snprintf_fits(len, bufsize, "cd") ? buf : NULL;
    }

    // Expand path like "~/foo" to "$HOME/foo"
    if (arg[0] == '~') {
        int len = snprintf(buf, bufsize, "%s%s", home_path, arg + 1);
        return snprintf_fits(len, bufsize, "cd") ? buf : NULL;
    }

    int len = snprintf(buf, bufsize, "%s", arg);
    return snprintf_fits(len, bufsize, "cd") ? buf : NULL;
}

void builtin_cd(char *arg)
{
    char target_buf[PATH_MAX];
    char *target_path = expand_home_directory(arg, target_buf, sizeof(target_buf));

    if (target_path == NULL)
        return;

    if (chdir(target_buf) != 0) {
        perror("cd");
    }
}