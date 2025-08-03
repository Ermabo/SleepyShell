#include "../src/tokenizer.h"
#include <assert.h>
#include <string.h>

// TODO: Add more tests, especially for edge cases

int main(void) {
    // Arrange
    char *buffer[10];
    const char *input = "echo hello world";

    // Act
    const int result = tokenize_input(input, buffer, 4);

    // Assert
    assert(result == 3);
    assert(!strcmp(buffer[0], "echo"));
    assert(!strcmp(buffer[1], "hello"));
    assert(!strcmp(buffer[2], "world"));

    // Cleanup
    free_tokens(buffer, result);
    return 0;
}
