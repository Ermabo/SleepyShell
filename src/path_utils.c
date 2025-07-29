#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Searches the PATH environment variable for the given executable name.
 *
 * @param program_name  Name of the binary to search for (e.g., "ls", "grep").
 * @return              Malloc'd full path to the binary, or NULL if not found.
 *                      Caller is responsible for freeing the result.
 */
char *util_find_bin_in_path(const char *program_name) {
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
        const size_t full_path_size = strlen(folder_path) + 1 + strlen(program_name) + 1;
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

        sprintf(full_path, "%s/%s", folder_path, program_name);

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
