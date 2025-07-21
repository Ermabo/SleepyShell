#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
