#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "threads/thread.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

char *get_name_for_file (const char *name) {
  char name_temp[strlen(name) + 1];
  memcpy(name_temp, name, strlen(name) + 1);

    
  char *next;
  char *ptr_temp;
  char *current = strtok_r(name_temp, "/", &ptr_temp);

  if (current)
    next = strtok_r (NULL, "/", &ptr_temp);

  while (next != NULL) {
    current = next;
    next = strtok_r (NULL, "/", &ptr_temp);
  }

  char *file = malloc (strlen(current) + 1);
  memcpy (file, current, strlen(current) +1);
  return file;
}

struct dir *get_dir_by_name (const char* name) {
  char name_temp[strlen(name) + 1];
  memcpy(name_temp, name, strlen(name) + 1);
  if (strcmp (name_temp, "..") == 0)
    return dir_get_parent (thread_current ()->cur_dir); 
  else if (strcmp (name_temp, ".") == 0)
    return thread_current ()->cur_dir;
  else if (strcmp (name_temp, "/") == 0)
    return dir_open_root ();

  char *next;
  char *ptr_temp;
  char *current = strtok_r(name_temp, "/", &ptr_temp);

  if (current)
    next = strtok_r (NULL, "/", &ptr_temp);
	
  struct dir *dir;

  if(!thread_current ()->cur_dir)
    dir = dir_open_root ();
  else if (name_temp[0] == '/')
    dir = dir_open_root ();
  else
    dir = dir_reopen (thread_current ()->cur_dir);

  while (next != NULL){
    if (strcmp (current, ".") != 0){
      struct inode *inode = NULL;
      if (strcmp (current, "..") == 0){
        dir = dir_get_parent(dir);
	  if(!dir)
	    return NULL;
      } else if(!dir_lookup(dir, current, &inode)){
        return NULL;
      }
	  
	  
      if (!inode_is_dir (inode))
	    inode_close (inode);
	  else{
        dir_close (dir);
        dir = dir_open (inode);
      }
    }
    current = next;
    next = strtok_r (NULL, "/", &ptr_temp);
  }
  return dir;
}

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

bool
change_directory (const char* dir_name){

  char *file = get_name_for_file (dir_name);
  struct dir *dir = get_dir_by_name (dir_name);

  if (strcmp (dir_name, "/") == 0){
    dir_close (thread_current ()->cur_dir);
    dir = dir_open_root ();
	
    if (!dir) return false;
	
    thread_current ()->cur_dir = dir;
    free (file);
    return true;
  }

  if (strcmp (file, ".") == 0){
    dir_close (thread_current ()->cur_dir);
    dir = dir_open (dir_get_inode (dir));
	
    if (!dir) return false;
	
    thread_current ()->cur_dir = dir;
    free (file);
    return true;
  }

  if (strcmp(file, "..") == 0){
    struct dir *parent = dir_get_parent (dir);
    dir_close (dir);
	
    if (!dir) return false;
	
    dir_close (thread_current ()->cur_dir);
    thread_current ()->cur_dir = parent;
    free (file);
    return true;
  }
  
  struct inode *inode = NULL;
  
  if(!dir_lookup (dir, file, &inode))
    return false;

  dir_close (dir);
  dir_close (thread_current ()->cur_dir);
  thread_current ()->cur_dir = dir_open (inode);
  free (file);
  return true;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  bool success = false;
  if (is_dir && strcmp(name, "") == 0){
    goto done;
  }  

  block_sector_t sector = 0;
  char * file = get_name_for_file (name);
  struct dir *dir = get_dir_by_name (name);
 if(dir == NULL)
    goto done;
  if(strcmp(file, "..") == 0)
    goto done;
	
  if(strcmp(file, ".") == 0)
    goto done;

    success = free_map_allocate (1, &sector);
	if(!success) goto done;
    success = inode_create (sector, initial_size, is_dir);
	if(!success) goto done;
    success = dir_add (dir, file, sector);
	if(!success) goto done;

  if (!success && sector != 0) 
    free_map_release (sector, 1);
  dir_close (dir);
  free (file);
  
done:
  return success;
}
/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (strlen(name) == 0){
    return NULL;
  }
  
  if (strcmp (name, "/") == 0){
    return dir_open_root ();
  }

  char *file = get_name_for_file (name);
  struct dir *dir = get_dir_by_name (name);
  
  struct inode *inode = NULL;

  if (dir != NULL){
    if (strcmp(file, "..") == 0){
      struct dir *dir_parent = dir_get_parent(dir);
      if(!dir_parent){
        free (file);
		return NULL;
      }
      inode = dir_get_inode(dir_parent);
    } else if (
	((strlen (file) == 0) && (inode_get_inumber(dir_get_inode(dir)) == ROOT_DIR_SECTOR))
   || (strcmp (file, ".") == 0)) {
      free (file);
      return (struct file *) dir;
    } else {
      dir_lookup (dir, file, &inode);
    }
  }
  free (file);
  dir_close (dir);
  

  if (inode == NULL)
    return NULL;
  else if (inode_is_dir(inode))
    return (struct file *) dir_open (inode);
  else
  	return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  bool success = false;
  if (strcmp (name, "/") == 0){
    goto done;
  }
  char *file = get_name_for_file (name);
  struct dir *dir = get_dir_by_name (name);
  if(dir == NULL) goto done;
  success = dir_remove (dir, file);
  dir_close (dir); 
  free (file);
  
done:
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}





