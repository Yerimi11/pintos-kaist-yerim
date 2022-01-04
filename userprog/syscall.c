#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
/* ---------- Project 2 ---------- */
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "kernel/stdio.h"
/* ------------------------------- */

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* ---------- Project 2 ---------- */
void check_address(const uint64_t *uaddr);

void halt (void);			/* 구현 완료 */
void exit (int status);		/* 구현 완료 */
tid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *cmd_line);
int wait (tid_t child_tid UNUSED); /* process_wait()으로 대체 필요 */
bool create (const char *file, unsigned initial_size); 	/* 구현 완료 */
bool remove (const char *file);							/* 구현 완료 */
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
/* ------------------------------- */

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call! rax : %d\n", f->R.rax);
	// thread_exit ();

	/* ---------- Project 2 ---------- */
	switch(f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(-1);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			if (exec(f->R.rdi) == -1)
				exit(-1);
			break;
		case SYS_CREATE:
			create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			remove(f->R.rdi);
			break;
		case SYS_OPEN:
			if (open(f->R.rdi) == -1)
				exit(-1);
			break;
		case SYS_FILESIZE:
			if (filesize(f->R.rdi) == -1)
				exit(-1);
			break;
		case SYS_READ:
			if (read(f->R.rdi, f->R.rsi, f->R.rdx))
				exit(-1);
			break;
		case SYS_WRITE:
			if (write(f->R.rdi, f->R.rsi, f->R.rdx))
				exit(-1);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			break;
	}
	/* ------------------------------- */
}

/* ---------- Project 2 ---------- */
void 
check_address (const uint64_t *user_addr) {
	struct thread *curr = thread_current();
	if (user_addr = NULL || !(is_user_vaddr(user_addr)) || pml4_get_page(curr->pml4, user_addr) == NULL) {
		exit(-1);
	}
}

void 
halt (void) {
	power_off();
}

void exit(int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;
	
	thread_exit();
}

tid_t fork (const char *thread_name, struct intr_frame *f) {
	check_address(thread_name);
	return process_fork(thread_name, f);
}

int
exec(const char *cmd_line) {
	check_address(cmd_line);

	char *cmd_line_cp;
	
	cmd_line_cp = palloc_get_page(0);
	if (cmd_line_cp == NULL) {
		exit(-1);
	}
	strlcpy (cmd_line_cp, cmd_line, PGSIZE);

	if (process_exec(cmd_line_cp) == -1) {
		return -1;
	}

	NOT_REACHED();
	return 0;
}

bool 
create (const char *file, unsigned initial_size) {
	check_address(file);
	return filesys_create(file, initial_size);
}

bool 
remove (const char *file) {
	check_address(file);
	return filesys_remove(file);
}

int add_file_to_fdt(struct file *file) {
	struct thread *curr = thread_current();
	struct file **fdt = curr->fd_table;

	while (curr->fd_idx < FDCOUNT_LIMIT && fdt[curr->fd_idx]) {
		curr->fd_idx++;
	}

	if (curr->fd_idx >= FDCOUNT_LIMIT) {
		return -1;
	}

	fdt[curr->fd_idx] = file;
	return curr->fd_idx;
}

int
open (const char *file) {
	check_address(file);
	struct file *open_file = filesys_open(file);

	if (open_file == NULL) {
		return -1;
	}
	
	int fd = add_file_to_fdt(open_file);

	if (fd == -1) {
		file_close(open_file);
	}
	return fd;
}

static struct file *get_file_from_fd_table(int fd) {
	struct thread *curr = thread_current();

	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}
	return curr->fd_table[fd];
}

int
filesize (int fd) {
	struct file *open_file = get_file_from_fd_table(fd);
	
	if (open_file == NULL) {
		return -1;
	}
	return file_length(open_file);
}

int
read (int fd, void *buffer, unsigned size) {

	if (fd == 2) {
		return -1;
	}

	check_address(buffer);

	int read_result_size;
	struct thread *curr = thread_current();
	struct file *file_obj = get_file_from_fd_table(fd);


	/* fd = STDIN = 0 */
	if (fd == 0) {
		
		/* try 1 */
		int i;
		unsigned char *buf = buffer;
		for (i = 0; i < size; i++) {
			char c = input_getc();
			*buf++ = c;
			if (c == '\0')
				break;
		}
		/* ----- */

		/* try 2 */
		*(char *)buffer = input_getc();	
		/* ----- */

		read_result_size = i;
	}

	/* other file fd */
	else {
		if (file_obj == NULL) {
			return -1;
		} 
		else {
			lock_acquire(&filesys_lock);
			read_result_size = file_read(file_obj, buffer, size);
			lock_release(&filesys_lock);
		}
	}

	return read_result_size;
}

int
write (int fd, const void *buffer, unsigned size) {

	if (fd == 2) {
		return -1;
	}

	check_address(buffer);

	int write_result_size;
	struct file *file_obj = get_file_from_fd_table(fd);
	
	/* fd = STDOUT = 1 */
	if (fd == 1) {
		putbuf(buffer, size);		/* to print buffer strings on the display*/
		write_result_size = size;
	}

	/* other file fd */
	else {
		if (file_obj == NULL) {
			write_result_size = -1;
		}
		else {
			lock_acquire(&filesys_lock);
			write_result_size = file_write(file_obj, buffer, size);
			lock_release(&filesys_lock);
		}
	}

	return write_result_size;
}

void
seek (int fd, unsigned position) {
	struct file *file_obj = get_file_from_fd_table(fd);

	/* STDIN, STDOUT, STDERR = 0, 1, 2 */
	// 블로그에는 file_obj <= 2 라고 되어있는데, 식별자 fd <= 2 경우 아닌가? file_obj는 포인터인데?
	// 그런데 fd_table[0], fd_table[1], fd_table[2] 안에 Null주소 (0x0)으로 초기화하는 거라면 2보다 작으니 file_obj도 가능할듯.
	// 아... 찾아보니 이 2기 블로그들은 fd = 1 STDIN, 2 = STDOUT 으로 해놓았다. 그리고 file_obj 역시 파일식별자로 보임.
	if (fd <= 2) {
		return;
	}
	
	file_seek(file_obj, position);
}

unsigned
tell (int fd) {
	struct file *file_obj = get_file_from_fd_table(fd);

	if (file_obj <= 2) {
		return;
	}
	
	return file_tell(file_obj);	
}

void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();

	// Error - invalid fd
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return;

	cur->fd_table[fd] = NULL;
}

void
close (int fd) {
	struct file *file_obj = get_file_from_fd_table(fd);

	if (file_obj == NULL) {
		return;
	}
	
	remove_file_from_fdt(fd);
}
/* ------------------------------- */