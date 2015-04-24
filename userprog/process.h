#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/directory.h"

#define PNAME(proc) (proc->my_thread->name)

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void process_init(void);
struct process *get_process_from_tid(tid_t t);
struct process_file{
  int fd;
  struct file* my_file;
  struct list_elem elem;
  bool is_dir;
  struct dir *dir;
};
enum process_status{
  ALIVE, DEAD
};

struct process{
  struct thread *my_thread;
  struct file *my_file;
  tid_t my_tid;
  int last_descriptor;//The last file descriptor generated
  enum process_status status; //status of this process
  int exit_status; // Undefined value until status is dead. then it represents the exit status
  struct list my_files; //List of process_file.elem
  struct list my_children; // A List of struct process.elem
  struct list_elem elem; // Used for keeping track by parent
  tid_t parent_tid;
};
#endif /* userprog/process.h */
