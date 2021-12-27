#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 * 각 스레드 구조는 고유한 4kB 페이지에 저장됩니다. 스레드 구조 자체는 페이지의 맨 아래(오프셋 0)에 위치
 * 페이지의 나머지 부분은 스레드의 커널 스택을 위해 예약되며 페이지 상단(오프셋 4kB)에서 아래로 확장된다. 다음은 예시입니다.*
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |          <함수 호출 스택>          |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *					      <스레드 구조>
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
	* 
	* - 이것의 결론은 두 가지이다.
		- 1. 첫째, '구조체 스레드'가 너무 커서는 안 된다. 만약 그렇다면, 커널 스택을 위한 공간이 충분하지 않을 것이다. 
		우리의 기본 '구조체 스레드'는 크기가 몇 바이트에 불과합니다. 아마도 1kB 이하에서 잘 유지되어야 할 것입니다.
		- 2. 둘째, 커널 스택이 너무 커지면 안 됩니다. 스택이 오버플로되면 스레드 상태가 손상됩니다. 
		그러므로 커널 함수는 큰 구조체나 배열을 정적이지 않은 로컬 변수로 할당해서는 안 된다. 
		대신 malloc() 또는 palloc_get_page()와 함께 동적 할당을 사용하십시오.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
 /* 이러한 문제들 중 하나의 첫 번째 증상은 아마도 실행 중인 스레드의 '구조적 스레드'의 '매직' 멤버가 
	SREAD_MAGIC으로 설정되었는지 확인하는 thread_current()의 어설션 오류일 것이다. 
	스택 오버플로가 일반적으로 이 값을 변경하여 어설션을 트리거합니다. */

/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
/* 'elem'멤버는 두 가지 목적을 가지고 있다. 실행 대기열의 요소(thread.c)일 수도 있고, 
	세마포어 대기 목록(synch.c)의 요소일 수도 있다. 이 두 가지 방법은 서로 배타적이기 때문에만 사용할 수 있다: 
	준비 상태의 스레드만 실행 대기열에 있는 반면 차단된 상태의 스레드만 세마포어 대기 목록에 있다. */

struct thread {
/* 스레드는 스레드의 정보를 저장하는 TCB 영역과, 여러 정보를 저장하는 kernel stack 영역으로 나누어져 있다. 
	TCB 영역은 offset 0 에서 시작하여 1KB 를 넘지 않는 작은 크기를 차지하고, 
	stack 영역은 나머지 영역을 차지하고 TCB 영역을 침범하지 않기 위해 위에서부터 시작하여 아래 방향으로 저장된다. */
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	/* 쓰레드 디스크립터 필드 추가 */
	// int64_t wakeup_tick;	/* 깨어나야 할 tick을 저장할 변수 추가 */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
	/* 이 값은 thread.c에 정의된 임의의 숫자이며, 스택 오버플로를 감지하는데 사용된다. 
	thread_current()는 실행 중인 스레드 구조체의 magic 멤버가 THREAD_MAGIC으로 설정 되었는지 확인한다. 
	스택 오버플로로 인해 이 값이 변경되어 ASSERT가 발생하는 경우가 있다. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
