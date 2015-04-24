#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include <stdbool.h>
#include "lib/user/syscall.h"
#include "threads/vaddr.h"
#include "process.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/inode.h"
#include "userprog/process.h"
#include "filesys/directory.h"

#define USER_VIRTUAL_ADDRESS_BASE ((void *) 0x08048000)
static void syscall_handler (struct intr_frame *);

/** Kernal side implimentation for system calls 
    Return values are written to eax
  */
static inline void check_buffer (void *buffer, unsigned length);
static inline void check_ptr(const void* ptr);
void sys_halt (void);
void sys_exit (int status);
pid_t sys_exec (const char *file);
int sys_wait (pid_t);
bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned length);
int sys_write (int fd, const void *buffer, unsigned length);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close (int fd);
bool sys_chdir(char* dir);
bool sys_mkdir(char* dir);
bool sys_readdir(int fd, char* dir);
bool sys_isdir(int fd);
int sys_inumber(int fd);

static int sys_call3(int callno , int* kstack);
static int sys_call2(int callno , int* kstack);
static int sys_call1(int callno , int* kstack);
static int sys_call0(int callno);
static struct lock crit_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&crit_lock);
}

static int add_dir(struct dir *d)
{
  struct process_file *pf = malloc(sizeof(struct process_file));
  pf->dir = d;
  pf->is_dir = true;
  pf->fd = ++thread_current()->my_process->last_descriptor;
  list_push_back(&thread_current()->my_process->my_files, &pf->elem);
  return pf->fd;
}

static struct process_file* get_process_file(int fd){
  struct process *p = thread_current() -> my_process;
  struct list_elem *e;
  for(e = list_begin(&p->my_files) ; e != list_end(&p->my_files);
      e = list_next(e)){
    struct process_file *pf = list_entry(e, struct process_file, elem);
    if(fd == pf->fd){
      return pf;
    }
  }
  return NULL;

}

static int add_file(struct file *f){
  struct process_file *pf = malloc(sizeof(struct process_file));
  pf->my_file = f;
  pf->is_dir = false;
  pf->fd = ++thread_current() -> my_process -> last_descriptor;
  list_push_back(&thread_current()-> my_process -> my_files, &pf-> elem);
  return pf->fd;
}
static struct file* get_file(int fd){

  struct process *p = thread_current() -> my_process;
  struct list_elem *e;
  for(e = list_begin(&p->my_files) ; e != list_end(&p->my_files);
      e = list_next(e)){
    struct process_file *pf = list_entry(e, struct process_file, elem);
    if(fd == pf->fd){
      return pf->my_file;
    }
  }
  return NULL;
}
static void close_file (int fd){
  struct process *p = thread_current() -> my_process;
  struct list_elem *next, *e = list_begin(&p->my_files);

  while(e != list_end( &p->my_files)){
    next = list_next(e);
    struct process_file *pf = list_entry(e, struct process_file, elem);
    if (fd == pf->fd){
      if(pf->is_dir)
        dir_close(pf->dir);
      else
      	file_close(pf->my_file);
      list_remove(&pf->elem);
      free(pf);
      return;
    }
    e = next;
  }
}

void sys_halt () {
  shutdown_power_off();
}

void close_all_files(struct list *l){
  while(list_size(l) != 0){
    struct process_file *f = list_entry(list_begin(l), 
                                struct process_file, 
                                  elem);
    /*if(f->is_dir){
      dir_close(f->dir);
    }else*/
      close_file(f->fd);
  }
}
void sys_exit (int status) {
  struct process *p = thread_current ()->my_process;

  p->exit_status = status;
  printf("%s: exit(%d)\n", p->my_thread->name, p->exit_status);
  thread_exit(); //Calls process_exit()
}

pid_t sys_exec (const char *file) {
  tid_t t = process_execute(file);
  return t;
}

int sys_wait (pid_t id) {
  return process_wait((tid_t)id); 
}

bool sys_create (const char *file , unsigned initial_size) { 
  lock_acquire(&crit_lock);
  bool ret = filesys_create(file, initial_size, false);
  lock_release(&crit_lock);
  return ret;
}


bool sys_remove (const char *file) {
  lock_acquire(&crit_lock);
  bool ret = filesys_remove(file);
  lock_release(&crit_lock);
  return ret;
}

int sys_open (const char *file) {
 
  int ret = -1;
  lock_acquire(&crit_lock);
  struct file *fi = filesys_open(file);
  if(!fi){
    goto done;
  }
  struct inode* inode = file_get_inode(fi);
  if(inode_is_dir(inode))
    ret = add_dir((struct dir*)fi);
  else
    ret = add_file(fi);
  done:
    lock_release(&crit_lock);
    return ret;
}

int sys_filesize (int fd) {
  int ret = -1;
  lock_acquire(&crit_lock);
  struct file *f = get_file(fd);
  if (!f){
    goto done;
  }
  struct process_file *pf = get_process_file(fd);
  if(pf->is_dir)
    goto done;
  ret = file_length(f);
  done :
    lock_release(&crit_lock);
    return ret;
}

int sys_read (int fd, void *buffer, unsigned length) {  
  if( fd == 0 ){
    unsigned i;
    uint8_t *tmp = (uint8_t *)buffer;
    for(i = 0; i < length; i++){
      tmp[i] = input_getc();
    }
    return length;
  }
  int ret = -1;
  lock_acquire(&crit_lock);
  struct file *f = get_file(fd);
  if (!f)
    goto done;
  struct process_file *pf = get_process_file(fd);
  if(pf->is_dir)
    goto done;
  ret = file_read(f, buffer, length);
  done:
    lock_release(&crit_lock);
    return ret;
}


int sys_write (int fd, const void *buffer, unsigned length) {
  char* data = (char*)buffer;
  if(fd == 1){
    int bytes_left = length;
    int one_write = 0x1000;
    while(bytes_left > 0){//Writes all the bytes in buffer 0x1000 at a time
      if (bytes_left < 0x1000){
        one_write = bytes_left;
      }
      putbuf(data, one_write);
      data += one_write;
      bytes_left -= one_write;
    }
    ASSERT(bytes_left == 0);
    return length;
  }
  int ret = -1;
  lock_acquire(&crit_lock);
  struct file *f = get_file(fd);
  if (!f)
    goto done;
  struct process_file *pf = get_process_file(fd);
  if(pf->is_dir)
    goto done;

  ret = file_write(f, buffer, length);
  done:
    lock_release(&crit_lock);
    return ret;
}

void sys_seek (int fd, unsigned position) {
  struct file *f = get_file(fd);
  lock_acquire(&crit_lock);
  if (!f)
    goto done;
  struct process_file *pf = get_process_file(fd);
  if(pf->is_dir)
    goto done;

  file_seek(f, position);
  done:
    lock_release(&crit_lock);
}

unsigned sys_tell (int fd) { 
  unsigned ret = -1;
  lock_acquire(&crit_lock);
  struct file *f = get_file(fd);
  if(!f)
    goto done;
  struct process_file *pf = get_process_file(fd);
  if(pf->is_dir)
    goto done;

  ret = file_tell(f);

  done:
    lock_release(&crit_lock);
    return ret;
}


void sys_close (int fd) {
  lock_acquire(&crit_lock);
  if(get_file(fd)){
    struct process_file *pf = get_process_file(fd);
    if(pf->is_dir)
      dir_close(pf->dir);
    else
      close_file(fd);
  }
  lock_release(&crit_lock);

}

bool sys_chdir(char* dir){
  return filesys_change_dir(dir);
}

bool sys_mkdir(char* dir){
  if (strlen (dir) == 0) {
    return false;
  }
  else {
    return filesys_create(dir, 0, true);
  }
}

bool sys_readdir(int fd, char* dir_name){
  struct process_file *file = get_process_file (fd);
  if (!file) {
    return false;
  } 
  if (!file->is_dir) {
    return false;
  }
  if (!dir_readdir(file->dir, dir_name))
  {
    return false;
  } 
  return true;
}
bool sys_isdir(int fd){
  struct process_file *file = get_process_file(fd);
  if (!file) {
    return false;
  }

  return file->is_dir;
}
int sys_inumber(int fd){
  struct process_file *pf = get_process_file(fd);
  if (!pf){
    return -1;
  }
  if (pf->is_dir){
    return inode_get_inumber (dir_get_inode (pf->dir));
  } else {
    return inode_get_inumber (file_get_inode (pf->my_file));
  }
}


static void* map_to_kernel(const void *ptr_user){
  
  check_ptr(ptr_user);
  void *ptr_kernel = pagedir_get_page(thread_current() -> pagedir, ptr_user);
  if(!ptr_kernel) sys_exit(-1);
  return ptr_kernel;
}

static void fetch_and_check_arg( struct intr_frame *user_frame, int* buffer, int size){
  
  int i = 0;
  int *p_arg;
  while( i < size){
    p_arg = (int*) user_frame->esp+i+1;
    check_ptr((void *)p_arg);
    buffer[i] = *p_arg;
    i++;
  }
}

static inline void check_ptr(const void *ptr){
  
  if( !is_user_vaddr(ptr) || ptr < USER_VIRTUAL_ADDRESS_BASE){
    sys_exit(-1);
  }
}

static void check_buffer (void *buffer, unsigned length){
  unsigned i = 0;
  char* temp = (char*)buffer;
  while( i < length){
    check_ptr((void*)temp);
    temp++;
    i++;
  }
}

static int get_arg_count(int callno){
  switch(callno){
    case SYS_HALT:
      return 0;
    case SYS_CLOSE:
    case SYS_TELL:
    case SYS_FILESIZE:
    case SYS_OPEN:
    case SYS_REMOVE:
    case SYS_WAIT:
    case SYS_EXEC:
    case SYS_EXIT:
    case SYS_CHDIR:
    case SYS_MKDIR:
    case SYS_ISDIR:
    case SYS_INUMBER:
      return 1;
    case SYS_SEEK:
    case SYS_CREATE:
    case SYS_READDIR:
      return 2; 
    case SYS_WRITE:
    case SYS_READ:

      return 3;
    default:
      return -1;
  }
}
//NOTE : these take esp+1
static int sys_call3(int callno , int* kstack){
  int one = kstack[0],two = kstack[1], three = kstack[2];
  check_buffer((void*)two, (unsigned)three);
  switch(callno){
    case SYS_READ:
      
      return sys_read(one, (void*)two, (unsigned)three);
    case SYS_WRITE:
      return sys_write(one, (void*)two, (unsigned)three);
    default:
      ASSERT(false);
  }
  return 0;
}
static int sys_call2(int callno , int* kstack){
  int one = kstack[0], two = kstack[1];
  switch(callno){
    case SYS_CREATE:
      return sys_create((const char*)map_to_kernel((const void*)one),
                         (unsigned)two);
    case SYS_SEEK:
      sys_seek(one, (unsigned)two);
      break;
    case SYS_READDIR:
      return sys_readdir(one, (const char*)map_to_kernel((const void*)two));
    default:
      ASSERT(false);
  }
  return 0;
}
static int sys_call1(int callno , int* kstack){
  int one = kstack[0];
  switch(callno){
    case SYS_WAIT:
      return sys_wait(one);
    case SYS_EXIT:
      sys_exit(one);
      break;
    case SYS_CLOSE:
      sys_close(one);
      break;
    case SYS_FILESIZE:
      return sys_filesize(one);
    case SYS_EXEC:
      
      return sys_exec((const char*)map_to_kernel((const void*)one));
    case SYS_OPEN:
      return sys_open((const char*)map_to_kernel((const void*)one));
    case SYS_REMOVE:
      return sys_remove((const char*)map_to_kernel((const void*)one));
    case SYS_TELL:
      return sys_tell(one);
    case SYS_CHDIR:
      return sys_chdir((const char*)map_to_kernel((const void*)one));
    case SYS_MKDIR:
      return sys_mkdir((const char*)map_to_kernel((const void*)one));
    case SYS_ISDIR:
      return sys_isdir(one);
    case SYS_INUMBER:
      return sys_inumber(one);
    default:
      ASSERT(false);
  }
  return 0;
}
static int sys_call0(int callno){
  switch(callno){
    case SYS_HALT:
      sys_halt();
      break;
    default:
      ASSERT(false);
  }
  return 0;
}
static void syscall_handler (struct intr_frame *user_frame) {
  int* esp = user_frame->esp;
  int ret = 0;
  int args[MAX_SYSCALL_ARGS];
  check_ptr((const void*) esp);
  int syscallNumber = (int) *(esp);
  int arg_count = get_arg_count(syscallNumber);
  if(arg_count == -1)
    sys_exit(-1);
  fetch_and_check_arg(user_frame, args, arg_count);
  int* arg = &args[0];
  if(arg_count == 0)
    ret = sys_call0(syscallNumber);
  else if(arg_count == 1)
    ret = sys_call1(syscallNumber, arg);
  else if(arg_count == 2)
    ret = sys_call2(syscallNumber, arg);
  else if(arg_count == 3)
    ret = sys_call3(syscallNumber, arg);
  else
    NOT_REACHED();
  
  user_frame->eax = ret;   
  
}

