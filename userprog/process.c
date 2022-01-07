#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
/* 본격적으로 command line에서 받은 arguments를 통해 실행하고자 하는 파일에 대한 process를 만드는 과정의 시작이다 */
/* 인자로 char형 포인터 file_name을 받는다고 되어 있는데 조금 명시적인 예를 들어보면 아래와 같다.
	우리는 parse_option을 통해 command line에 입력된 것에서 run부터해서 뒷부분을 parsing할 것이다. (pintos -- -q run alarm-clock)
	그렇게 되면 arg[0]에는 run이 들어가고 arg[1]에 우리가 실행하고자 하는 file name과 그에 같이 붙는 arguments가 있는 string이 있을 것이다.
	우리는 이 arg[1]을 file_name으로 process_create_initd에 넘겨주게 되는 것이고 이것을 parsing하고 user stack에 쌓아야하는 것이다. */
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0); // palloc_get_page()를 통해 page를 할당받고 해당 page에 file_name을 copy로 저장해준다.
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/* System call 추가 */
	char *save_ptr;
	strtok_r(file_name, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	/* pintos에서는 단일 스레드만 고려하기에 thread_create()로 새로운 스레드를 생성해주고 tid를 return해준다.
		여기서, thread_create() 함수의 인자들을 눈여겨 봐야한다.
		앞에 2개 : file_name을 이름으로 하고 PRI_DEFAULT를 우선순위 값으로 가지는 새로운 스레드가 생성되고 tid를 반환한다.
		뒤에 2개 : 그리고 스레드는 fn_copy를 인자로 받는 initd라는 함수를 실행시킨다. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	// process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
// tid_t
// process_fork (const char *name, struct intr_frame *if_ UNUSED) {
// 	/* Clone current thread to new thread.*/
// 	return thread_create (name,
// 			PRI_DEFAULT, __do_fork, thread_current ());
// }

/* System Call 추가 */
tid_t process_fork(const char *name, struct intr_frame *if_) {
	// 현재 프로세스를 새 프로세스로 복제
	struct thread *cur = thread_current();
	memcpy(&cur->parent_if, if_, sizeof(struct intr_frame));

	tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, cur);
	if (tid == TID_ERROR) {
		return TID_ERROR;
	}

	struct thread *child = get_child_with_pid(tid);
	sema_down(&child->fork_sema);
	if (child->exit_status == -1) {
		return TID_ERROR;
	}

	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* System call 추가 */
	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)) {
		return true; // return false ends pml4_for_each, which is undesirable - just return true to pass this kernel va
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL) {
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if (newpage == NULL) {
		printf("[fork-duplicate] failed to palloc new page\n"); // #ifdef DEBUG
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte); // *PTE is an address that points to parent_page

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		printf("Failed to map user virtual page to given physical frame\n"); // #ifdef DEBUG
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;
	parent_if = &parent->parent_if; 						/* System call 추가 */

	/* 1. Read the cpu context to local stack. */
	// memcpy(a, b, size); b에서 size만큼을 읽어서 a에 복사한다.
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0;											/* System call 추가 */

	/* 2. Duplicate PT */ // Virtual memory space(page_table)을 만드는 행위(=pml4)
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else	// vm space 여기서 복사
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	/* System call 추가 */
	// process_init ();
	// multi-oom) Failed to duplicate
	if (parent->fd_idx == FDCOUNT_LIMIT)
		goto error;

	for (int i = 0; i < FDCOUNT_LIMIT; i++) {
		struct file *file = parent->fd_table[i];
		if (file == NULL)
			continue;

		// If 'file' is already duplicated in child, don't duplicate again but share it
		bool found = false;
		if (!found) {
			struct file *new_file;
			if (file > 2)
				new_file = file_duplicate(file);
			else
				new_file = file;

			current->fd_table[i] = new_file;
			// for문부터 여기까지 코드가 file descriptor 내용을 복사한다 
		}
	}
	current->fd_idx = parent->fd_idx;

	// child loaded successfully, wake up parent in process_fork
	sema_up(&current->fork_sema);

	/* System call 추가 ^ */

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	/* System call 추가 */
	// thread_exit ();
	current->exit_status = TID_ERROR;
	sema_up(&current->fork_sema);
	exit(TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name; // f_name은 문자열이지만 void 로 넘겨 받았다. 이를 문자열로 인식하기 위해 자료형을 char *로 변환해야 한다
	char *file_name_copy[48]; // P2_1 추가
	// f_name을 사용하는 경우를 고려하여 원본 파일명을 수정하는 대신 file_name_copy라는 이름의 사본을 생성하고 memcpy()를 이용하여 file_name의 메모리를 복사하여 이를 수정한다.
	bool success;

	// memcpy(a, b, size); b에서 size만큼을 읽어서 a에 복사한다.
	memcpy(file_name_copy, file_name, strlen(file_name)+1); // P2_1 추가. 
	/* file_name은 문자열이기 때문에 sizeof이 아니라 strlen을 사용함.
		strlen(file_name) + 1만큼의 크기를 사용하는 이유는 strlen() 함수는 '\n'이 나올 때까지 1바이트 씩 읽고 '\n'이 나오면 종료하기 때문이다.
		'\n'도 포함될 수 있도록 파일 크기에 1을 더해준다. char *이기 때문에 8바이트를 늘려 한 글자를 더 읽을 수 있다. */

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled, it stores the execution information to the member. */
	struct intr_frame _if; 	/* intr_frame 구조체 : 레지스터, 스택 포인터 같은 context switching을 위한 정보를 담고 있으며, 
			if :  intr_frame					유저 프로그램을 실행할 때 필요한 정보를 포함한다 */
	_if.ds = _if.es = _if.ss = SEL_UDSEG; // stack - user data
	_if.cs = SEL_UCSEG; // stack - user code
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup (); 
	/* 새로운 실행 파일을 현재 쓰레드에 담기 전에 현재 프로세스에 담긴 컨텍스트를 지운다. 
		즉 현재 프로세스에 할당된 page directory를 지운다. */
	/* 현재 실행 중인 스레드의 page directory와 switch information을 내려주는 역할을 한다
		새로 생성되는 process를 실행하기 위해서는 CPU를 점유해야하는데, 지금은 kernel 모드로 돌아가고 있지만 
		CPU를 선점하기 전에 지금 실행 중인 스레드와 context switching하기 위한 준비를 해야 하기 때문이다. */

	// ------------------------------------------------------------
	/* We first kill the current context. P2_1 추가 */
	// 파일명을 추출해야 하지만 다른 인자들 또한 프로세스를 실행하는 데에 필요하므로 user stack에 저장한다.
	int token_count = 0;
	char *token, *last;
	char *arg_list[64]; // arg_list라는 리스트를 생성하여 각 인자의 char *를 저장한다. 프로그램 명은 arg_list[0]에 저장되며, arg_list[1]부터 다른 인자들이 저장된다.
	char *tmp_save = token;
	
	// char strtok(char str, char* delimiters); 첫 번째 매개변수 문자열을 두 번째 매개변수 구분자를 기준으로 문자열을 분할하여 각 문자열의 포인터를 반환한다. 
	token = strtok_r(file_name_copy, " ", &last); 
	arg_list[token_count] = token;

	while (token != NULL) {
		token = strtok_r(NULL, " ", &last); /* 여백 ' '을 기준으로 문자열을 분할하여, 각 인자에 sentinel '\n'을 추가하여 저장한다. */ // sentinel : 데이터의 끝을 알리는 데 사용되는 값
		token_count++; 						// ㄴ ex) cmd line이 rm -rf 인 경우 arg_list에는 [rm\0, -rf\0, \0]의 형태로 저장된다.
		arg_list[token_count] = token;		// token_count에는 파일명을 제외한 인자의 갯수(??왜 ++인데 인자의 갯수지?->while문 돌면서 filename분할한거 하나씩 셈)가 저장된다.
	}
	// -------------------------------------------------------------

	/* And then load the binary */
	success = load (file_name_copy, &_if); // _if와 file_name을 현재 프로세스에 로드한다. 로드 성공시 1반환, else 0 */
	// file을 load해주는 load 함수. 이 함수에서 parsing 작업을 추가해야한다. parshing 후 user stack에 넣는 코드 구현하면 됨
	// load를 마치면 argument_stack() 함수를 이용하여 user stack에 인자들을 저장한다.

	/* If load failed, quit. */
	palloc_free_page (file_name); 
	// file_name은 프로그램 파일 이름을 입력하기 위해 생성한 임시 변수이기 때문에 load를 끝내면 해당 메모리를 반환해야 한다.
	if (!success) // load를 실패하면 -1을 반환한다.
		return -1;

	argument_stack(token_count, arg_list, &_if); // P2_1 추가
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);
	/* Start switched process. */
	// load()가 성공적으로 된다면(실행되면) do_iret()와 NOT_REACHED()를 통해 생성된 프로세스로 context switching을 실행한다.
	do_iret (&_if);
	NOT_REACHED ();
}

// P2_1 추가 -------------------------------------------------------------
void argument_stack (int argc, char **argv, struct intr_frame *if_) { // parsing할 인자들을 담을 stack 함수
	char *arg_address[128];

	/* Insert arguments' addresses */
	for (int i = argc - 1; i >= 0; i--) { // argv[n]부터 argv[0]까지 돌아야 하니까 i>=0까지 돌아야 한다
		int argv_len = strlen(argv[i]); 
		if_ -> rsp = if_ -> rsp - (argv_len + 1); // user stack의 최상단부터 각 배열의 문자열의 크기만큼 담는다 // if_ -> rsp는 user stack에서 현재 위치를 가리키는 stack pointer (스택의 꼭대기)
		// 각 인자에서 인자의 크기를 읽고 (각 인자에는 sentinel이 포함되어 있기 때문에 1을 더한다), 그 크기만큼 rsp를 내린다.
		memcpy(if_ -> rsp, argv[i], argv_len + 1); // memcpy(a, b, size); b에서 size만큼을 읽어서 a에 복사한다. -> 현재 위치를 가리키는 스택 포인터 rsp에 인자를 복사한다.
		arg_address[i] = if_ -> rsp;
	}

	/* Insert padding for word-align조정 */
	/* word-align을 실행한다. 64비트 환경이기 때문에 8바이트 단위로 정렬한다. */
	while (if_ -> rsp % 8 != 0) {
	if_ -> rsp--; // rsp(스택 포인터 레지스터, 스택 꼭대기)를 8의 배수에 맞추기 위해 값을 내린다.
	*(uint8_t *)(if_ -> rsp) = 0;
	}

	/* Insert addresses of strings including sentinel 센티넬이 포함된 문자열의 주소를 삽입 */
	for (int i = argc; i >= 0; i--) {
		if_ -> rsp = if_ -> rsp - 8;

		if (i == argc) // ex) i가 처음에 들어왔을 때 4라면 -> argv[3]부터인데 없으니까 0 으로 채운다 ㄱ
			memset(if_ -> rsp, 0, sizeof(char **)); // 왜 0으로 채움? -> [ 0x4747ffe0 | argv[4] | 0 | char * ]
			// memset함수는 어떤 메모리의 시작점부터 연속된 범위를 어떤 값으로(바이트 단위) 모두 지정하고 싶을 때 사용하는 함수이다.
			// if 조건문에서 memset()을 사용하면 3개의 인자를 입력 받았을 때, 밑에서부터 3개의 주소를 새기고(Address->Date영역으로) 마지막에 0을 추가한다

			else // arg_address에 저장한 주소를 입력한다. memcpy()의 인자로 포인터를 입력하기 때문에 arg_address[i]를 입력한다.
					memcpy(if_ -> rsp, &arg_address[i], sizeof(char **)); // ** : pointer to pointer. char** is pointer to a char*			
	}

	/* 또는 if문 빼고
	// Pointers to the argument strings
	(*rsp) -= 8;
	**(char ***)rsp = 0;

	for (int i = argc - 1; i >= 0; i--) {
		(*rsp) -= 8;
		**(char ***)rsp = argv[i];
	}
	*/

	/* Fake return address */
	if_ -> rsp = if_ -> rsp - 8;
	memset(if_ -> rsp, 0, sizeof(void *));
	/* 인자의 주소값을 입력하고 그 밑에는 거짓 return address(함수를 호출하는 부분의 다음 수행 명령어 주소)를 입력한다. 
		return address는 프로세스가 함수를 호출하면 해당 함수는 독자적인 stack을 가지고 
		함수가 종료되면 다시 프로세스로 돌아가기 위한 코드 영역 주소를 입력하지만, 유저 프로그램을 실행하기 위한 준비 단계이므로 돌아올 표기를 할 필요가 없다. 
		그렇기 때문에 0으로만 구성된 거짓 return address를 입력한다. */

	if_ -> R.rdi = argc;
	if_ -> R.rsi = if_ -> rsp + 8; 

}

// P2_1 ^ ----------------------------------------------------------------

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * eption), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	/* System call 추가 */
	// while(1);
	// return -1;
	struct thread *child = get_child_with_pid(child_tid);

	// [Fail] Not my child
	if (child == NULL)
		return -1;

	// Parent waits until child signals (sema_up) after its execution
	sema_down(&child->wait_sema);

	int exit_status = child->exit_status;

	// Keep child page so parent can get exit_status
	list_remove(&child->child_elem);
	sema_up(&child->free_sema); // wake-up child in process_exit - proceed with thread_exit
	return exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	/* System call 추가 */
	// P2-4 CLose all opened files
	for (int i = 0; i < FDCOUNT_LIMIT; i++)
	{
		close(i);
	}

	// palloc_free_page(cur->fdTable); 
	// thread_create 에서 할당했던 fdt 공간을 해제해준다
	palloc_free_multiple(curr->fd_table, FDT_PAGES); // multi-oom

	// P2-5 Close current executable run by this process
	file_close(curr->running);

	// Wake up blocked parent
	sema_up(&curr->wait_sema);
	
	// Postpone child termination until parents receives its exit status with 'wait'
	sema_down(&curr->free_sema);

	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool // 페이지 테이블 생성
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create (); /* 페이지 디렉토리 생성 */
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ()); /* 페이지 테이블 활성화 */

	/* Open executable file. */
	file = filesys_open (file_name); /* 프로그램 파일 Open */
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Deny 추가 */
	t->running = file;
	file_deny_write(file);

	/* Read and verify executable header. */ /* ELF파일의 헤더 정보를 읽어와 저장 */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr; /* 배치 정보를 읽어와 저장. */

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					} /* 배치정보를 통해 파일을 메모리에 적재. */
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */ /* 스택 초기화 */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here. 파일 네임 파싱시키기
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file); /* Deny 추가 (주석처리) */
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */

struct thread *get_child_with_pid(int pid)
{
	struct thread *cur = thread_current();
	struct list *child_list = &cur->child_list;

#ifdef DEBUG_WAIT
	printf("\nparent children # : %d\n", list_size(child_list));
#endif

	for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, child_elem);
		if (t->tid == pid)
			return t;
	}	
	return NULL;
}