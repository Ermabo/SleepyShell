//
// Created by sleepy on 6/27/25.
//

#ifndef BUILTINS_H
#define BUILTINS_H

void builtin_cd(char *arg);
void builtin_echo(char *args[], int arg_count);
void builtin_pwd(void);
void builtin_type(char *args[], int arg_count);
void builtin_exit(char *args[], int arg_count);
#endif //BUILTINS_H
