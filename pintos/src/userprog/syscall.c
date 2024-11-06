#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "devices/input.h"

#define CONSOLE_OUTPUT 1
#define KEYBOARD_INPUT 0
#define ERROR_STATUS -1

// System call interrupt handler.
static void syscall_handler(struct intr_frame *);

// Executes a new process.
static tid_t syscall_exec(const char *commandLineArguments);

// Exits the current process.
static void syscall_exit(int status);

// Changes the position of the file pointer.
static void syscall_seek(int fd, unsigned pos);

// Returns the position of the file pointer.
static unsigned syscall_tell(int fd);

// Creates a new file.
static bool syscall_create(const char *file, unsigned initialSize);

// Deletes a file.
static bool syscall_remove(const char *file);

// Reads data from a file.
static int syscall_read(int fd, void *buffer, unsigned size);

// Opens a file.
static int syscall_open(const char *file);

// Returns the size of a file.
static int syscall_filesize(int fd);

// Writes data to a file.
static int syscall_write(int fd, const void *buffer, unsigned size);

// Closes a file.
static void syscall_close(int fd);

// Validates a pointer.
void validate_ptr(const void *_ptr);

// Validates a buffer.
void validate_buffer(const void *buffer, unsigned size);

// Returns the file descriptor for the given file descriptor ID.
struct file_descriptor *get_file_descriptor(int fd);

// Validates a string.
void validate_string(const char *_str);


// Initializes the system call interface.
void syscall_init(void)
{
  // Initialize the file system lock.
  lock_init(&file_system_lock);

  // Register the system call interrupt handler.
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}



/* ####################  system call handler ################################################*/
// System call interrupt handler.
static void syscall_handler(struct intr_frame *f UNUSED)
{
  // Get the stack pointer.
  void *esp = f->esp;

  // Validate the stack pointer for non-null, not in kernel space, and validate address mapping.
  validate_ptr(esp);

  // Get the system call number.
  int syscall = (* ((int *)esp + 0));

  // Handle the system call based on the system call number.
  switch (syscall)
  {
    // Exit the current process.
    case SYS_EXIT:
    {
      syscall_exit(* ((int *)esp + 1));
      break;
    }

    // Execute a new process.
    case SYS_EXEC:
    {
      char *commandLineArguments = *(char **)((int *)esp + 1);
      validate_string(commandLineArguments);
      f->eax = syscall_exec(commandLineArguments);
      break;
    }

    // Wait for a child process to terminate.
    case SYS_WAIT:
    {
      f->eax = process_wait((*(int *)esp + 1));
      break;
    }

    // Open a file.
    case SYS_OPEN:
    {
      char *file = *(char **)((int *)esp + 1);
      validate_string(file);
      f->eax = syscall_open(file);
      break;
    }

    // Create a new file.
    case SYS_CREATE:
    {
      char *file = *(char **)((int *)esp + 1);
      validate_string(file);
      unsigned initialSize = *((unsigned *)((int *)esp + 2));
      f->eax = syscall_create(file, initialSize);
      break;
    }

    // Remove a file.
    case SYS_REMOVE:
    {
      char *file = *(char **)((int *)esp + 1);
      validate_string(file);
      f->eax = syscall_remove(file);
      break;
    }

    // Halt the operating system.
    case SYS_HALT:
    {
      shutdown_power_off();
      break;
    }

    // Read data from a file.
    case SYS_READ:
    {
      void *buffer = (void *)(*((int *)esp + 2));
      unsigned size = *((unsigned *)((int *)esp + 3));
      validate_buffer(buffer, size);
      f->eax = syscall_read(*((int *)esp + 1), buffer, size);
      break;
    }

    // Write data to a file.
    case SYS_WRITE:
    {
      void *buffer = (void *)(*((int *)esp + 2));
      unsigned size = *((unsigned *)((int *)esp + 3));
      validate_buffer(buffer, size);
      f->eax = syscall_write( *((int *)esp + 1), buffer, size);
      break;
    }

    // Get the size of a file.
    case SYS_FILESIZE:
    {
      f->eax = syscall_filesize(*((int *)esp + 1));
      break;
    }

    // Change the position of the file pointer.
    case SYS_SEEK:
    {
      unsigned pos = *((unsigned *)((int *)esp + 2));
      syscall_seek(*((int *)esp + 1), pos);
      break;
    }

    // Get the position of the file pointer.
    case SYS_TELL:
    {
      f->eax = syscall_tell(*((int *)esp + 1));
      break;
    }

    // Close a file.
    case SYS_CLOSE:
    {
      syscall_close(*((int *)esp + 1));
      break;
    }

    // do nothing.
    default:
    {
      break;
    }
  }
}


// Executes a new process.
static tid_t syscall_exec(const char *commandLineArguments)
{
  // Get the current thread.
  struct thread *currentThread = thread_current();

  // Declare variables for child thread and child list element.
  struct thread *childThread;
  struct list_elem *child;

  // Create a new child process.
  tid_t child_tid = process_execute(commandLineArguments);
  if (child_tid == TID_ERROR)
  {
    return child_tid;
  }

  // Check if the child process is in the current thread's child list.
  for (
      child = list_begin(&currentThread->child_list);
      child != list_end(&currentThread->child_list);
      child = list_next(child))
  {
    childThread = list_entry(child, struct thread, child);
    if (childThread->tid == child_tid)
    {
      break;
    }
  }

  // If the child process is not in the current thread's child list, return an error status.
  if (child == list_end(&currentThread->child_list))
  {
    return ERROR_STATUS;
  }

  // Wait until the child process finishes executing.
  sema_down(&childThread->init_sema);

  // If the child process failed to load, return an error status.
  if (!childThread->load_success_status)
  {
    return ERROR_STATUS;
  }

  // Return the child process ID.
  return child_tid;
}

// Exits the current process.
static void syscall_exit(int status)
{
  // Get the current thread and set its exit status.
  struct thread *thread = thread_current();
  thread->exit_status = status;

  // Exit the thread.
  thread_exit();
}

// Changes the position of the file pointer.
static void syscall_seek(int fd, unsigned pos)
{
  // Get the file descriptor for the given file descriptor ID.
  struct file_descriptor *_file_descriptor = get_file_descriptor(fd);

  // If the file descriptor is not null, acquire the file system lock, change the position of the file pointer, and release the lock.
  if (_file_descriptor != NULL)
  {
    lock_acquire((&file_system_lock));
    file_seek(_file_descriptor->_file, pos);
    lock_release(&file_system_lock);
  }
}

// Returns the position of the file pointer.
static unsigned syscall_tell(int fd)
{
  // Initialize the position to 0.
  unsigned pos = 0;

  // Get the file descriptor for the given file descriptor ID.
  struct file_descriptor *_file_descriptor = get_file_descriptor(fd);

  // If the file descriptor is null, return the position.
  if (_file_descriptor == NULL)
  {
    return pos;
  }

  // Acquire the file system lock, get the position of the file pointer, and release the lock.
  lock_acquire((&file_system_lock));
  pos = file_tell(_file_descriptor->_file);
  lock_release(&file_system_lock);

  // Return the position.
  return pos;
}


// Creates a new file.
static bool syscall_create(const char *file, unsigned initialSize)
{
  // Acquire the file system lock, create the file, and release the lock.
  lock_acquire(&file_system_lock);
  bool create_status = filesys_create(file, initialSize);
  lock_release(&file_system_lock);

  // Return the status of the file creation.
  return create_status;
}

// Deletes a file.
static bool syscall_remove(const char *file)
{
  // Acquire the file system lock, remove the file, and release the lock.
  lock_acquire(&file_system_lock);
  bool remove_status = filesys_remove(file);
  lock_release(&file_system_lock);

  // Return the status of the file removal.
  return remove_status;
}

// Reads data from a file.
static int syscall_read(int fd, void *buffer, unsigned size)
{
  // Declare variables for file descriptor, read size, and buffer index.
  struct file_descriptor *_file_descriptor;
  int readSize = 0;

  // If the file descriptor is for keyboard input, read from the input buffer.
  if (fd == KEYBOARD_INPUT)
  {
    for (unsigned i = 0; i < size; i++)
    {
      *((uint8_t *)buffer + i) = input_getc();
      readSize++;
    }
  }
  // If the file descriptor is for console output, return an error status.
  else if (fd == CONSOLE_OUTPUT)
  {
    return ERROR_STATUS;
  }
  // Otherwise, read from the file.
  else
  {
    _file_descriptor = get_file_descriptor(fd);
    if (_file_descriptor == NULL)
    {
      return ERROR_STATUS;
    }

    lock_acquire((&file_system_lock));
    readSize = file_read(_file_descriptor->_file, buffer, size);
    lock_release(&file_system_lock);
  }

  // Return the read size.
  return readSize;
}

// Opens a file.
static int syscall_open(const char *file)
{
  // Allocate memory for a new file descriptor.
  struct file_descriptor *file_descriptor_ptr = malloc(sizeof(struct file_descriptor *));

  // Declare variables for file pointer and current thread.
  struct file *filePointer;
  struct thread *currentThread;

  // Acquire the file system lock, open the file, and release the lock.
  lock_acquire((&file_system_lock));
  filePointer = filesys_open(file);
  lock_release(&file_system_lock);

  // If the file pointer is null, return an error status.
  if (filePointer == NULL)
  {
    return ERROR_STATUS;
  }

  // Get the current thread and set the file descriptor's ID and file pointer.
  currentThread = thread_current();
  file_descriptor_ptr->fd = currentThread->next_fd;
  currentThread->next_fd++; 
  file_descriptor_ptr->_file = filePointer;

  // Add the file descriptor to the current thread's open file descriptor list.
  list_push_back(&currentThread->open_fd_list, &file_descriptor_ptr->fd_elem);

  // Return the file descriptor ID.
  return file_descriptor_ptr->fd;
}

// Returns the size of a file.
static int syscall_filesize(int fd)
{
  // Get the file descriptor for the given file descriptor ID.
  struct file_descriptor *_file_descriptor = get_file_descriptor(fd);
  int fSize;

  // If the file descriptor is null, return an error status.
  if (_file_descriptor == NULL)
  {
    return ERROR_STATUS;
  }

  // Acquire the file system lock, get the file size, and release the lock.
  lock_acquire((&file_system_lock));
  fSize = file_length(_file_descriptor->_file);
  lock_release(&file_system_lock);

  // Return the file size.
  return fSize;
}

// Writes data to a file.
static int syscall_write(int fd, const void *buffer, unsigned size)
{
  // Declare variables for file descriptor, buffer, and written size.
  struct file_descriptor *_file_descriptor;
  char *_buffer = (char *)buffer;
  int written_size = 0;

  // If the file descriptor is for console output, write to the console output buffer.
  if (fd == CONSOLE_OUTPUT)
  {
    putbuf(_buffer, size);
    written_size = size;    
  }
  // If the file descriptor is for keyboard input, return an error status.
  else if (fd == KEYBOARD_INPUT)
  {
    return ERROR_STATUS;
  }
  // Otherwise, write to the file.
  else
  {
    _file_descriptor = get_file_descriptor(fd);
    if (_file_descriptor == NULL)
    {
      return ERROR_STATUS;
    }

    lock_acquire((&file_system_lock));
    written_size = file_write(_file_descriptor->_file, _buffer, size);
    lock_release(&file_system_lock);
  }

  // Return the written size.
  return written_size;
}

// Closes a file.
static void syscall_close(int fd)
{
  // Get the file descriptor for the given file descriptor ID.
  struct file_descriptor *_file_descriptor = get_file_descriptor(fd);

  // If the file descriptor is not null, close the file, remove the file descriptor from the open file descriptor list, and free the file descriptor.
  if (_file_descriptor != NULL)
  {
    lock_acquire((&file_system_lock));
    file_close(_file_descriptor->_file);
    lock_release(&file_system_lock);

    list_remove(&_file_descriptor->fd_elem);
    free(_file_descriptor);
  }
}


/* ####################  utility functions ################################################*/


// This function takes an integer file descriptor value and returns the corresponding file descriptor from the current process's list of open file descriptors.
struct file_descriptor *get_file_descriptor(int fd)
{
  struct thread *currentThread = thread_current();
  struct file_descriptor *_file_descriptor;
  struct list_elem *fd_elem;

  // Iterate through the list of open file descriptors and compare the file descriptor value with the file descriptor ID of each file descriptor in the list.
  for (
      fd_elem = list_begin(&currentThread->open_fd_list);
      fd_elem != list_end(&currentThread->open_fd_list);
      fd_elem = list_next(fd_elem))
  {
    _file_descriptor = list_entry(fd_elem, struct file_descriptor, fd_elem);
    if (_file_descriptor->fd == fd)
    {
      break;
    }
  }

  // If no match is found, return NULL.
  if (fd_elem == list_end(&currentThread->open_fd_list))
  {
    return NULL;
  }

  // Otherwise, return the file descriptor.
  return _file_descriptor;
}

// This function takes a pointer and checks if it is NULL, in kernel space, or not mapped to a page in the current thread's page directory. If any of these conditions are true, the function calls syscall_exit with an error status.
void validate_ptr(const void * ptr)
{
  struct thread *currentThread;
  currentThread = thread_current();

  if (ptr == NULL)
  {
    syscall_exit(ERROR_STATUS);
  }

  // Check if the pointer is in kernel space.
  if (is_kernel_vaddr(ptr))
  {
    syscall_exit(ERROR_STATUS);
  }

  // Check if the pointer is not mapped to a page in the current thread's page directory.
  if (pagedir_get_page(currentThread->pagedir, ptr) == NULL)
  {
    syscall_exit(ERROR_STATUS);
  }
}

// This function takes a string and validates each character in the string using the validate_ptr function. It iterates through the string until it reaches the null terminator.
void validate_string(const char *_str)
{
  validate_ptr(_str);
  while (*_str != '\0')
  {
    _str++;
    validate_ptr(_str);
  }
}

// This function takes a buffer and its size and validates each byte in the buffer using the validate_ptr function. It iterates through the buffer using a for loop.
void validate_buffer(const void *buffer, unsigned size)
{
  validate_ptr(buffer);
  for (unsigned i = 0; i < size; i++)
  {
    validate_ptr(buffer + i);
  }
}

