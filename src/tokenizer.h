#ifndef TOKENIZER_H
#define TOKENIZER_H

int tokenize_input(const char *input, char *tokens[], int capacity);
void free_tokens(char *tokens[], int count);

#endif