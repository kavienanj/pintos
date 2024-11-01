#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "devices/input.h"


#define BUF_MAX 200

static bool valid_mem_access (const void *);
static void syscall_handler (struct intr_frame *);
static void userprog_halt (void);
static void userprog_exit (int);
static pid_t userprog_exec (const char *);
static int userprog_wait (pid_t);
static bool userprog_create (const char *, unsigned);
static bool userprog_remove (const char *);
static int userprog_open (const char *);
static int userprog_filesize (int);
static int userprog_read (int, void *, unsigned);
static int userprog_write (int, const void *, unsigned);
static void userprog_seek (int, unsigned);
static unsigned userprog_tell (int);
static void userprog_close (int);
static struct openfile * getFile (int);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

/* Verify that the user pointer is valid */
static bool
valid_mem_access (const void *up)
{
	struct thread *t = thread_current ();

	if (up == NULL)
		return false;
  if (is_kernel_vaddr (up))
    return false;
  if (pagedir_get_page (t->pagedir, up) == NULL)
   	return false;
  
	return true;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void *esp = f->esp;
  uint32_t *eax = &f->eax;
  int syscall_num;

  if(!valid_mem_access ( ((int *) esp) ))
    userprog_exit (-1);
  if(!valid_mem_access ( ((int *) esp)+1 ))
    userprog_exit (-1);
  syscall_num = *((int *) esp);

  switch (syscall_num) {
  	case 0:
  	  userprog_halt ();
  	  break;
  	case 1:
  	{
  	  int status = *(((int *) esp) + 1);
  	  userprog_exit (status);
  	  break;
  	}
  	case 2:
  	{
  	  const char *cmd_line = *(((char **) esp) + 1);
  	  *eax = (uint32_t) userprog_exec (cmd_line);
  	  break;
  	}
  	case 3:
  	{
  	  pid_t pid = *(((pid_t *) esp) + 1);
  	  *eax = (uint32_t) userprog_wait (pid);
  	  break;
  	}
  	case 4:
  	{
  	  const char *file = *(((char **) esp) + 1);
  	  unsigned initial_size = *(((unsigned *) esp) + 2);
  	  *eax = (uint32_t) userprog_create (file, initial_size);
  	  break;
  	}
  	case 5:
  	{
  	  const char *file = *(((char **) esp) + 1);
  	  *eax = (uint32_t) userprog_remove (file);
  	  break;
  	}
  	case 6:
  	{
  	  const char *file = *(((char **) esp) + 1);
  	  *eax = (uint32_t) userprog_open (file);
  	  break;
  	}
  	case 7:
  	{
  	  int fd = *(((int *) esp) + 1);
  	  *eax = (uint32_t) userprog_filesize (fd);
  	  break;
  	}
  	case 8:
  	{
  	  int fd = *(((int *) esp) + 1);
  	  void *buffer = (void *) *(((int **) esp) + 2);
  	  unsigned size = *(((unsigned *) esp) + 3);
  	  *eax = (uint32_t) userprog_read (fd, buffer, size);
  	  break;
  	}
  	case 9:
  	{
  	  int fd = *(((int *) esp) + 1);
  	  const void *buffer = (void *) *(((int **) esp) + 2);
  	  unsigned size = *(((unsigned *) esp) + 3);
  	  *eax = (uint32_t) userprog_write (fd, buffer, size);
  	  break;
  	}
  	case 10:
  	{
  	  int fd = *(((int *) esp) + 1);
  	  unsigned position = *(((unsigned *) esp) + 2);
  	  userprog_seek (fd, position);
  	  break;
  	}
  	case 11:
  	{
  	  int fd = *(((int *) esp) + 1);
  	  *eax = (uint32_t) userprog_tell (fd);
  	  break;
  	}
  	case 12:
  	{
  	  int fd = *(((int *) esp) + 1);
  	  userprog_close (fd);
  	  break;
  	}
  }
}

static void
userprog_halt ()
{
	shutdown_power_off ();
}

static void
userprog_exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;
	thread_exit ();
}

static pid_t
userprog_exec (const char *cmd_line)
{
	//printf("System call: exec\ncmd_line: %s\n", cmd_line);
  tid_t child_tid = TID_ERROR;

  if(!valid_mem_access(cmd_line))
    userprog_exit (-1);

  child_tid = process_execute (cmd_line);

	return child_tid;
}

static int
userprog_wait (pid_t pid)
{
  return process_wait (pid);
}

static bool
userprog_create (const char *file, unsigned initial_size)
{
  bool retval;
  if(valid_mem_access(file)) {
    lock_acquire (&filesys_lock);
    retval = filesys_create (file, initial_size);
    lock_release (&filesys_lock);
    return retval;
  }
	else
    userprog_exit (-1);

  return false;
}

static bool
userprog_remove (const char *file)
{
  bool retval;
	if(valid_mem_access(file)) {
    lock_acquire (&filesys_lock);
    retval = filesys_remove (file);
    lock_release (&filesys_lock);
    return retval;
  }
  else
    userprog_exit (-1);

  return false;
}

static int
userprog_open (const char *file)
{
	if(valid_mem_access ((void *) file)) {
    struct openfile *new = palloc_get_page (0);
    new->fd = thread_current ()->next_fd;
    thread_current ()->next_fd++;
    lock_acquire (&filesys_lock);
    new->file = filesys_open(file);
    lock_release (&filesys_lock);
    if (new->file == NULL)
      return -1;
    list_push_back(&thread_current ()->openfiles, &new->elem);
    return new->fd;
  }
	else
    userprog_exit (-1);

	return -1;

}

static int
userprog_filesize (int fd)
{
  int retval;
  struct openfile *of = NULL;
	of = getFile (fd);
  if (of == NULL)
    return 0;
  lock_acquire (&filesys_lock);
  retval = file_length (of->file);
  lock_release (&filesys_lock);
  return retval;
}

static int
userprog_read (int fd, void *buffer, unsigned size)
{
  int bytes_read = 0;
  char *bufChar = NULL;
  struct openfile *of = NULL;
	if (!valid_mem_access(buffer))
    userprog_exit (-1);
  bufChar = (char *)buffer;
	if(fd == 0) {
    while(size > 0) {
      input_getc();
      size--;
      bytes_read++;
    }
    return bytes_read;
  }
  else {
    of = getFile (fd);
    if (of == NULL)
      return -1;
    lock_acquire (&filesys_lock);
    bytes_read = file_read (of->file, buffer, size);
    lock_release (&filesys_lock);
    return bytes_read;
  }
}

static int
userprog_write (int fd, const void *buffer, unsigned size)
{
  int bytes_written = 0;
  char *bufChar = NULL;
  struct openfile *of = NULL;
	if (!valid_mem_access(buffer))
		userprog_exit (-1);
  bufChar = (char *)buffer;
  if(fd == 1) {
    /* break up large buffers */
    while(size > BUF_MAX) {
      putbuf(bufChar, BUF_MAX);
      bufChar += BUF_MAX;
      size -= BUF_MAX;
      bytes_written += BUF_MAX;
    }
    putbuf(bufChar, size);
    bytes_written += size;
    return bytes_written;
  }
  else {
    of = getFile (fd);
    if (of == NULL)
      return 0;
    lock_acquire (&filesys_lock);
    bytes_written = file_write (of->file, buffer, size);
    lock_release (&filesys_lock);
    return bytes_written;
  }
}

static void
userprog_seek (int fd, unsigned position)
{
	struct openfile *of = NULL;
  of = getFile (fd);
  if (of == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_seek (of->file, position);
  lock_release (&filesys_lock);
}

static unsigned
userprog_tell (int fd)
{
  unsigned retval;
	struct openfile *of = NULL;
  of = getFile (fd);
  if (of == NULL)
    return 0;
  lock_acquire (&filesys_lock);
  retval = file_tell (of->file);
  lock_release (&filesys_lock);
  return retval;
}

static void
userprog_close (int fd)
{
	struct openfile *of = NULL;
  of = getFile (fd);
  if (of == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_close (of->file);
  lock_release (&filesys_lock);
  list_remove (&of->elem);
  palloc_free_page (of);
}

/* Helper function for getting a thread's opened
   file by its file descriptor */
static struct openfile *
getFile (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->openfiles); e != list_end (&t->openfiles);
       e = list_next (e))
    {
      struct openfile *of = list_entry (e, struct openfile, elem);
      if(of->fd == fd)
        return of;
    }
  return NULL;
}
