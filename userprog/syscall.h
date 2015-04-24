#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "kernel/list.h"
void syscall_init (void);
void close_all_files(struct list *l);
void sys_exit(int ext);

#define MAX_SYSCALL_ARGS 16 //Arbitrary max number of args
#endif /* userprog/syscall.h */
