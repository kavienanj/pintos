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

// Function declarations
static bool is_valid_mem_access(const void *);
static void handle_syscall(struct intr_frame *);
static void halt_user_program(void);
static void exit_user_program(int);
static pid_t exec_user_program(const char *);
static int wait_for_user_program(pid_t);
static bool create_user_program(const char *, unsigned);
static bool remove_user_program(const char *);
static int open_user_program(const char *);
static int get_user_program_filesize(int);
static int read_user_program(int, void *, unsigned);
static int write_user_program(int, const void *, unsigned);
static void seek_user_program(int, unsigned);
static unsigned tell_user_program(int);
static void close_user_program(int);
static struct openfile *get_open_file(int);

// Initialize the syscall system
void syscall_init(void) 
{
  intr_register_int(0x30, 3, INTR_ON, handle_syscall, "syscall");
  lock_init(&filesys_lock);
}

// Verify that the user pointer is valid
static bool is_valid_mem_access(const void *up)
{
  struct thread *t = thread_current();

  if (up == NULL)
    return false;
  if (is_kernel_vaddr(up))
    return false;
  if (pagedir_get_page(t->pagedir, up) == NULL)
    return false;
  
  return true;
}

// Handle system calls
static void handle_syscall(struct intr_frame *f UNUSED) 
{
  void *esp = f->esp;
  uint32_t *eax = &f->eax;
  int syscall_num;

  // Validate memory access for the syscall number and arguments
  if (!is_valid_mem_access((int *)esp))
    exit_user_program(-1);
  if (!is_valid_mem_access((int *)esp + 1))
    exit_user_program(-1);

  syscall_num = *((int *)esp);

  // Handle the syscall based on the syscall number
  switch (syscall_num) {
    case 0:
      halt_user_program();
      break;
    case 1:
    {
      int status = *(((int *)esp) + 1);
      exit_user_program(status);
      break;
    }
    case 2:
    {
      const char *cmd_line = *(((char **)esp) + 1);
      *eax = (uint32_t)exec_user_program(cmd_line);
      break;
    }
    case 3:
    {
      pid_t pid = *(((pid_t *)esp) + 1);
      *eax = (uint32_t)wait_for_user_program(pid);
      break;
    }
    case 4:
    {
      const char *file = *(((char **)esp) + 1);
      unsigned initial_size = *(((unsigned *)esp) + 2);
      *eax = (uint32_t)create_user_program(file, initial_size);
      break;
    }
    case 5:
    {
      const char *file = *(((char **)esp) + 1);
      *eax = (uint32_t)remove_user_program(file);
      break;
    }
    case 6:
    {
      const char *file = *(((char **)esp) + 1);
      *eax = (uint32_t)open_user_program(file);
      break;
    }
    case 7:
    {
      int fd = *(((int *)esp) + 1);
      *eax = (uint32_t)get_user_program_filesize(fd);
      break;
    }
    case 8:
    {
      int fd = *(((int *)esp) + 1);
      void *buffer = (void *)*(((int **)esp) + 2);
      unsigned size = *(((unsigned *)esp) + 3);
      *eax = (uint32_t)read_user_program(fd, buffer, size);
      break;
    }
    case 9:
    {
      int fd = *(((int *)esp) + 1);
      const void *buffer = (void *)*(((int **)esp) + 2);
      unsigned size = *(((unsigned *)esp) + 3);
      *eax = (uint32_t)write_user_program(fd, buffer, size);
      break;
    }
    case 10:
    {
      int fd = *(((int *)esp) + 1);
      unsigned position = *(((unsigned *)esp) + 2);
      seek_user_program(fd, position);
      break;
    }
    case 11:
    {
      int fd = *(((int *)esp) + 1);
      *eax = (uint32_t)tell_user_program(fd);
      break;
    }
    case 12:
    {
      int fd = *(((int *)esp) + 1);
      close_user_program(fd);
      break;
    }
    default:
      // Handle unknown syscall number
      exit_user_program(-1);
      break;
  }
}

// Halt the user program
static void halt_user_program(void)
{
  shutdown_power_off();
}

// Exit the user program with a status
static void exit_user_program(int status)
{
  struct thread *cur = thread_current();
  cur->exit_status = status;
  thread_exit();
}

// Execute a user program
static pid_t exec_user_program(const char *cmd_line)
{
  tid_t child_tid = TID_ERROR;

  if (!is_valid_mem_access(cmd_line))
    exit_user_program(-1);

  child_tid = process_execute(cmd_line);

  return child_tid;
}

// Wait for a user program to finish
static int wait_for_user_program(pid_t pid)
{
  return process_wait(pid);
}

// Create a user program file
static bool create_user_program(const char *file, unsigned initial_size)
{
  bool retval;
  if (is_valid_mem_access(file)) {
    lock_acquire(&filesys_lock);
    retval = filesys_create(file, initial_size);
    lock_release(&filesys_lock);
    return retval;
  } else {
    exit_user_program(-1);
  }

  return false;
}

// Remove a user program file
static bool remove_user_program(const char *file)
{
  bool retval;
  if (is_valid_mem_access(file)) {
    lock_acquire(&filesys_lock);
    retval = filesys_remove(file);
    lock_release(&filesys_lock);
    return retval;
  } else {
    exit_user_program(-1);
  }

  return false;
}

// Open a user program file
static int open_user_program(const char *file)
{
  if (is_valid_mem_access((void *)file)) {
    struct openfile *new = palloc_get_page(0);
    new->fd = thread_current()->next_fd;
    thread_current()->next_fd++;
    lock_acquire(&filesys_lock);
    new->file = filesys_open(file);
    lock_release(&filesys_lock);
    if (new->file == NULL)
      return -1;
    list_push_back(&thread_current()->openfiles, &new->elem);
    return new->fd;
  } else {
    exit_user_program(-1);
  }

  return -1;
}

// Get the size of a user program file
static int get_user_program_filesize(int fd)
{
  int retval;
  struct openfile *of = NULL;
  of = get_open_file(fd);
  if (of == NULL)
    return 0;
  lock_acquire(&filesys_lock);
  retval = file_length(of->file);
  lock_release(&filesys_lock);
  return retval;
}

// Read from a user program file
static int read_user_program(int fd, void *buffer, unsigned size)
{
  int bytes_read = 0;
  char *bufChar = NULL;
  struct openfile *of = NULL;
  if (!is_valid_mem_access(buffer))
    exit_user_program(-1);
  bufChar = (char *)buffer;
  if (fd == 0) {
    while (size > 0) {
      input_getc();
      size--;
      bytes_read++;
    }
    return bytes_read;
  } else {
    of = get_open_file(fd);
    if (of == NULL)
      return -1;
    lock_acquire(&filesys_lock);
    bytes_read = file_read(of->file, buffer, size);
    lock_release(&filesys_lock);
    return bytes_read;
  }
}

// Write to a user program file
static int write_user_program(int fd, const void *buffer, unsigned size)
{
  int bytes_written = 0;
  char *bufChar = NULL;
  struct openfile *of = NULL;
  if (!is_valid_mem_access(buffer))
    exit_user_program(-1);
  bufChar = (char *)buffer;
  if (fd == 1) {
    // Break up large buffers
    while (size > BUF_MAX) {
      putbuf(bufChar, BUF_MAX);
      bufChar += BUF_MAX;
      size -= BUF_MAX;
      bytes_written += BUF_MAX;
    }
    putbuf(bufChar, size);
    bytes_written += size;
    return bytes_written;
  } else {
    of = get_open_file(fd);
    if (of == NULL)
      return 0;
    lock_acquire(&filesys_lock);
    bytes_written = file_write(of->file, buffer, size);
    lock_release(&filesys_lock);
    return bytes_written;
  }
}

// Seek to a position in a user program file
static void seek_user_program(int fd, unsigned position)
{
  struct openfile *of = NULL;
  of = get_open_file(fd);
  if (of == NULL)
    return;
  lock_acquire(&filesys_lock);
  file_seek(of->file, position);
  lock_release(&filesys_lock);
}

// Get the current position in a user program file
static unsigned tell_user_program(int fd)
{
  unsigned retval;
  struct openfile *of = NULL;
  of = get_open_file(fd);
  if (of == NULL)
    return 0;
  lock_acquire(&filesys_lock);
  retval = file_tell(of->file);
  lock_release(&filesys_lock);
  return retval;
}

// Close a user program file
static void close_user_program(int fd)
{
  struct openfile *of = NULL;
  of = get_open_file(fd);
  if (of == NULL)
    return;
  lock_acquire(&filesys_lock);
  file_close(of->file);
  lock_release(&filesys_lock);
  list_remove(&of->elem);
  palloc_free_page(of);
}

// Helper function for getting a thread's opened file by its file descriptor
static struct openfile *get_open_file(int fd)
{
  struct thread *t = thread_current();
  struct list_elem *e;
  for (e = list_begin(&t->openfiles); e != list_end(&t->openfiles); e = list_next(e)) {
    struct openfile *of = list_entry(e, struct openfile, elem);
    if (of->fd == fd)
      return of;
  }
  return NULL;
}
