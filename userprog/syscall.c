#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// P2_3 추가 */
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

/* System call 추가 */
#define	STDIN_FILENO	0
#define	STDOUT_FILENO	1

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* Project 2_3 System call 추가 */
void check_address(void* uaddr);
// void get_argument(void *rsp, int *arg, int count);
void halt(void);
void exit(int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);

int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
int _write (int fd UNUSED, const void *buffer, unsigned size);

void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int dup2(int oldfd, int newfd);

tid_t fork (const char *thread_name, struct intr_frame *f);
int exec (char *file_name);

static struct file *find_file_by_fd(int fd);
int add_file_to_fd_table(struct file *file);
void remove_file_from_fdt(int fd);

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
/* 유저 스택에 저장되어 있는 시스템 콜 넘버를 이용해 시스템 콜 핸들러 구현 */
/* 1. 스택 포인터가 유저 영역인지 확인 /저장된 인자 값이 포인터일 경우 유저 영역의 주소인지 확인
 * 2. 스택에서 시스템 콜 넘버 복사
 * 3. 시스템 콜 넘버에 따른 인자 복사 및 시스템 콜 호출 */
/* 0 : halt */
/* 1 : exit */
/* . . . */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	
	/* Projects 2_3 System call 추가 */
	
	switch (f->R.rax){			// rax is the system call number
		case SYS_HALT:			/* Halt the operating system. */
				halt();
				break;
		case SYS_EXIT:			/* Terminate this process. */
				exit(f->R.rdi);
				break;
		// case SYS_FORK:			/* Clone current process. */
		// 		f->R.rax = fork(f->R.rdi, f);
		// 		break;
		case SYS_EXEC:			/* Switch current process. */
				if (exec(f->R.rdi) == -1) {
					exit(-1);
				}
				break;   
		// case SYS_WAIT:			/* Wait for a child process to die. */   
		// 		f->R.rax = process_wait(f->R.rdi);
		// 		break;
		case SYS_CREATE:		/* Create a file. */
				f->R.rax = create(f->R.rdi, f->R.rsi);
				break;
		case SYS_REMOVE:		/* Delete a file. */
				f->R.rax = remove(f->R.rdi);
				break;
	// SYS_OPEN,                   /* Open a file. */
		case SYS_OPEN:		
				f->R.rax = open(f->R.rdi);
				break;
	// SYS_FILESIZE,               /* Obtain a file's size. */
		// case SYS_FILESIZE:		
		// 		f->R.rax = filesize(f->R.rdi);
		// 		break;
	// SYS_READ,                   /* Read from a file. */
		// case SYS_READ:		
		// 		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		// 		break;
	// SYS_WRITE,                  /* Write to a file. */
		case SYS_WRITE:		
				f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
				break;
	// SYS_SEEK,                   /* Change position in a file. */
	// 	case SYS_SEEK:		
	// 			seek(f->R.rdi, f->R.rsi);
	// 			break;
	// // SYS_TELL,                   /* Report current position in a file. */
	// 	case SYS_TELL:		
	// 			f->R.rax = tell(f->R.rdi);
	// 			break;
	// SYS_CLOSE,                  /* Close a file. */ 
		// case SYS_CLOSE:		
		// 		close(f->R.rdi);
		// 		break;
		default:
				exit(-1);
				break;

	}
	// printf ("system call!\n");
	// thread_exit ();
}

/* Project 2_2 User memory access 추가 ---------------------------------------------- */
/* 	1. 사용자가 잘못된 포인터 → Null Pointer이거나
	2. 커널 메모리에 대한 포인터 → Kernel VM을 가리키거나 ( = KERN_BASE보다 큰 값일 때)
	3. 그 영역들 중 하나에 부분적으로 블록을 제공한다면
	(a block partially in one of those regions) -> 가상메모리주소 블럭을 페이지테이블에 준다?
    → mapping 되지 않은 VM을 가리킨다면(할당되지 않은 vm주소에 접근하면)
	⇒ 프로세스를 종료해야 한다. `(exit(-1)`  */
void check_address(void* uaddr) {
	struct thread *cur = thread_current();
	if (uaddr == NULL || is_kernel_vaddr(uaddr) || pml4_get_page(cur->pml4, uaddr) == NULL) {
		exit(-1);
	}
}

/* Project 2_3 System call 추가 -------------------------------------------------- */
// void get_argument(void *rsp, int *arg, int count) {
// 	/* 유저 스택에 저장된 인자값들을 커널로 저장. 
// 	인자가 저장된 위치가 유저영역인지 확인(check_address)해야겠네 
// 	스택 포인터를 참조하여 count(인자의 개수)만큼 스택에 저장된 인자들(데이터)을 4byte크기로 꺼내어 arg 배열에 순차적으로 저장(복사) */
// 	check_address();
// }


/* -------------------------------- System call -------------------------------- */
/* power_off()를 사용하여 pintos 종료 */
void halt(void) { 
	power_off(); /* pintos를 종료시키는 함수 */
}

/* 실행중인 스레드 구조체를 가져옴 */
/* 프로세스 종료 메시지 출력,
	출력 양식: “프로세스이름: exit(종료상태)” */
/* 스레드 종료 */
void exit(int status) { 
	struct thread *cur = thread_current();
	printf("%s: exit(%d)\n", thread_name, status);
	thread_exit(); /* Thread를 종료시키는 함수 */
}

/* 파일 이름과 크기에 해당하는 파일 생성 */
/* 파일 생성 성공(함수 리턴값이 success)시 true 반환, 실패 시 false 반환 */
bool create (const char *file, unsigned initial_size) {
	check_address(file);
	return filesys_create(file, initial_size); /* 파일 이름과 파일 사이즈를 인자 값으로 받아 파일을 생성하는 함수 */\
	
}

/* 파일 이름에 해당하는 파일을 제거 */
/* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
bool remove (const char *file) {
	check_address(file);
	return filesys_remove(file); /* 파일 이름에 해당하는 파일을 제거하는 함수 */
	
}

/* 현재 프로세스를 cmd_lind에서 지정된 인수를 전달하여 이름이 지정된 실행 파일로 변경 */
int exec(char *file_name) {
	check_address(file_name);

	int file_size = strlen(file_name)+1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL) {
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size);

	if(process_exec(fn_copy) == -1) {
		return -1;
	}

	NOT_REACHED();
	return 0;
}

int open(const char *file) { // 성공 시 fd를 생성하고 반환, 실패 시 -1 반환
	check_address(file);
	struct file *open_file = filesys_open(file);

	if (open_file == NULL) {
		return -1;
	}

	int fd = add_file_to_fd_table(open_file); // fdt : file data table

	// fd table이 가득 찼다면
	if (fd == -1) {
		file_close(open_file);
	}
	return fd;
}

void close(int fd) {
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL) {
		return;
	}
	remove_file_from_fdt(fd);
}

/* 현재 프로세스의 fd테이블에 파일 추가 */
int add_file_to_fd_table(struct file *file) {
	struct thread *cur = thread_current();
	struct file **fdt = cur->fd_table;

	/* fd의 위치가 제한 범위를 넘지 않고, fd_table의 인덱스 위치와 일치한다면 */
	// cur->fd_idx 가 어디있지? -> thread.h의 thread 구조체 안에 fd_table과 함께 선언해준다.
	// 제한범위를 나타낼 FDCOUNT_LIMIT 등도 thread.h 파일 내에 선언(#define)해준다.
	while (cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx]) {
		cur->fd_idx++;
	}

	// fdt가 가득 찼다면
	if (cur->fd_idx >= FDCOUNT_LIMIT)
		return -1;

	fdt[cur->fd_idx] = file;
	return cur->fd_idx;
}

int write (int fd, const void *buffer, unsigned size) {
// 	if(fd == STDOUT_FILENO){
// 		putbuf(buffer, size);
// 		return size;
// 	}
// }
	check_address(buffer);

	int write_result;
	lock_acquire(&filesys_lock);
	if (fd == 1) {
		putbuf(buffer, size);		// 문자열을 화면에 출력하는 함수
		write_result = size;
	}
	else {
		if (find_file_by_fd(fd) != NULL) {
			write_result = file_write(find_file_by_fd(fd), buffer, size);
		}
		else {
			write_result = -1;
		}
	}
	lock_release(&filesys_lock);
	return write_result;
}

tid_t fork(const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
}

int read(int fd, void *buffer, unsigned size) {
	check_address(buffer);

	int read_result;
	struct thread *cur = thread_current();
	struct file *file_fd = find_file_by_fd(fd);

	if (fd == 0) {
		// read_result = i;
		*(char *)buffer = input_getc();		// 키보드로 입력 받은 문자를 반환하는 함수
		read_result = size;
	}
	else {
		if (find_file_by_fd(fd) == NULL) {
			return -1;
		}
		else {
			lock_acquire(&filesys_lock);
			read_result = file_read(find_file_by_fd(fd), buffer, size);
			lock_release(&filesys_lock);
		}
	}
	return read_result;
}

int filesize(int fd) {
	struct file *open_file = find_file_by_fd(fd);
	if (open_file == NULL) {
		return -1;
	}
	return file_length(open_file);
}

static struct file *find_file_by_fd(int fd) {
	struct thread *cur = thread_current();

	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}
	return cur->fd_table[fd];
}

void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();

	// Error - invalid fd
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return;

	cur->fd_table[fd] = NULL;
}

// seek()
// tell()