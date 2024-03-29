#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* ----- project 1 ------------ */
static struct list sleep_list; /* sleep list for blocked threads */
/* --------------------------- */

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* ---------- project 1 ------------ */
static int64_t next_tick_to_awake; /* the earliest awake time in sleep list */
/* --------------------------------- */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);


/* ------------------- project 1 -------------------- */
void thread_sleep(int64_t ticks); 
void thread_awake(int64_t ticks); 
int64_t get_next_tick_to_awake(void);
bool thread_priority_compare (struct list_elem *element1, struct list_elem *element2, void *aux UNUSED); 
bool preempt_by_priority(void);
bool thread_donate_priority_compare (struct list_elem *element1, struct list_elem *element2, void *aux UNUSED);
/* -------------------------------------------------- */
/* ------------------- project 2 -------------------- */
struct thread* get_child_by_tid(tid_t tid);
/* -------------------------------------------------- */

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
   /* 현재 실행 중인 코드를 스레드로 변환하여 스레딩 시스템을 초기화합니다. 
   	이것은 일반적으로 동작할 수 없으며 'loader.S'가 스택의 하단을 페이지 경계에 놓도록 주의했기 때문에 가능합니다.
	또한 실행 대기열 및 tid lock을 초기화합니다.

	이 함수를 호출한 후 다음을 사용하여 스레드를 작성하기 전에 페이지 할당자를 초기화해야 합니다.
	thread_create().
	이 함수가 끝날 때까지 thread_current()를 호출하는 것은 안전하지 않습니다. */
void
thread_init (void) {
/* main()에서 thread system을 초기화하기 위해 호출 된다. 
이 함수의 주요 목적은 핀토스의 초기 스레드를 위한 struct thread를 만드는 것이다. 
이때 pintos loader는 지금과 또 다른 pintos thread와 동일한 위치, 즉 초기 스레드의 스택을 page 가장 위에 배치한다. 
thread_init()을 하지 않고 thread_current()를 호출하면 fail이다. 그 이유는 thread의 magic 값이 잘못 되었기 때문이다. 
thread_current는 lock_acquire()와 같은 함수에서 많이 호출되므로 항상 핀토스의 초기에 thread_init()을 호출하여 초기화 해준다.*/
	ASSERT (intr_get_level () == INTR_OFF); // 인터럽트 관련 레지스터 핀을 끊는 것이 intr비활성화.
	// cpu는 레지스터에서 0,1로 동작 수행하는데 그게 뭔지가 ISA고, 이걸 가지고 

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the global thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);		/* sleep_list를 초기화 (추가) */
	list_init (&destruction_req);

	/* ------------- project 1 ---------------- */
	list_init (&sleep_list); /* sleep list init for blocked thread */
	next_tick_to_awake = INT64_MAX; /* next_tick_to_awake 초기화 */
	/* ---------------------------------------- */

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread (); // 지금 내가 어디에 위치해있는지를 가져옴.
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable (); // 인터럽트 활성화

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Alarm Clock 추가. 실행 중인 스레드를 슬립으로 만듦, 인자로 들어온 ticks까지 재움 */
// Thread를 blocked 상태로 만들고 sleep queue에 삽입하여 대기, timer_sleep()함수에 의해 호출됨. 
void thread_sleep(int64_t ticks) {
	struct thread* curr;

	enum intr_level old_level;
	old_level = intr_disable(); // 이 라인 이후의 과정 중에는 인터럽트를 받아들이지 않는다
	// 다만 나중에 다시 받아들이기 위해 old_level에 이전 인터럽트 상태를 담아둔다

	curr = thread_current(); // 현재의 thread 주소를 가져온다
	ASSERT(curr != idle_thread); // 현재의 thread가 idle thread이면 sleep 되지 않아야 함.

	// awake함수가 실행되어야 할 tick값을 update
	update_next_tick_to_awake(curr->wakeup_tick = ticks); // 현재의 thread의 wakeup_ticks에 인자로 들어온 ticks를 저장후 next_tick_to_awake를 업데이트한다
	list_push_back(&sleep_list, &(curr->elem)); // sleep_list에 현재 thread의 element를 슬립 리스트(슬립 큐)의 마지막에 삽입한 후에 스케쥴한다.

	thread_block(); // 현재 thread를 block 시킴. 다시 스케쥴 될 때 까지 블락된 상태로 대기

	/* 현재 스레드를 슬립 큐에 삽입한 후에 스케쥴 한다. 해당 과정 중에는 인터럽트를 받아들이지 않는다. */

	intr_set_level(old_level); // 다시 스케쥴하러 갈 수 있게 만들어줌 (인터럽트를 다시 받아들여서 인터럽트가 가능하도록 수정함)
	/* 여기서 현재 스레드가 idle thread 이면 sleep 되지 않도록 해야한다. idle 스레드는 운영체제가 초기화되고 ready_list가 생성될때 첫번째로 추가되는 스레드이다. 
		굳이 이 스레드를 만들어준 이유는 CPU를 실행상태로 유지하기 위함이다.
		CPU가 할일이 없으면 아예 꺼져버리고, 할 일이 생기면 다시 켜지는 방식이므로 소모되는 전력보다 무의미한 일이라도 그냥 계속 하고 있는게 더 적은 전력을 소모한다. */
}

/* 이제 재우고 sleep_list에 넣어뒀으니 sleep_list에서 꺼내서 깨울 함수가 필요. 이 함수가 thread_awake 함수. 
	sleep list의 모든 entry를 순회하면서 현재 tick이 깨워야 할 tick 보다 크다면 슬립 큐에서 제거하고 unblock해준다(깨운다). 
	작다면 next_tick_to_awake변수를 갱신하기 위해 update_next_tick_to_awake()를 호출한다. */
/* Alarm Clock 추가. wakeup_tick값이 ticks보다 작거나 같은 스레드를 깨움, 
	현재 대기중인 스레드들의 wakeup_tick변수 중 가장 작은 값을 next_tick_to_awake 전역 변수에 저장 */
void thread_awake(int64_t ticks) {
	// next_tick_to_awake = INT64_MAX;
	// struct list_elem* curr_elem = list_begin(&sleep_list);
	// struct thread* curr_thread;
	// //printf("thread_awake is called \n");

	// /* sleep list의 모든 entry를 순회하며 다음과 같은 작업을 수행한다.
	// 	현재 tick이 깨워야 할 tick보다 크거나 같다면 슬립큐에서 제거하고 unblock한다.
	// 	작다면 update_next_tick_to_awake()를 호출한다. */
	// while (curr_elem != list_end(&sleep_list)) {
	// 	curr_thread = list_entry(curr_elem, struct thread, elem);

	// 	if (curr_thread->wakeup_tick <= ticks){ // 현재시간(ticks)가 더 크면 리무브(슬립리스트에서 뻄)-언블락(깨움)
	// 		curr_elem = list_remove(curr_elem);
	// 		thread_unblock(curr_thread);
	// 	}
	// 	else {
	// 		curr_elem = list_next(curr_elem);
	// 		update_next_tick_to_awake(curr_thread->wakeup_tick);
		// }
	// }
	struct list_elem *e = list_begin(&sleep_list);
	while (e != list_end (&sleep_list)) {
		struct thread *t = list_entry(e, struct thread, elem);
		if (t -> wakeup_tick <= ticks) {
			e = list_remove(e);
			thread_unblock(t);
		}
		else 
			e = list_next(e);
	}
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */



/* Sets the current thread's priority to NEW_PRIORITY. 현재 스레드의 우선순위를 새 우선순위로 정한다. */ 
void
thread_set_priority (int new_priority) {
  thread_current ()->init_priority = new_priority;
  refresh_priority (); /* Donate 추가 */ // 우선순위 내림차순 정렬
  thread_test_preemption (); /* Priority Scheduling 추가 */
  
}

tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	struct kernel_thread_frame *kf; // P2_1 추가
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO); /* 페이지 할당 */
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	struct  thread *parent = thread_current();
	list_push_back(&parent->child_list, &t->child_elem);
	
	t->fd_table = palloc_get_multiple(PAL_ZERO, FDT_PAGES);
	if (t->fd_table == NULL)
		return TID_ERROR;
	tid = t->tid = 
	allocate_tid ();
	
	t->fd_idx = 2;
	t->fd_table[0] = 1; /* dummy value for STDIN */
	t->fd_table[1] = 2; /* dummy value for STDOUT */
	// t->fd_table[2] = 0; /* dummy value for STDERR (current Pintos version doesn't consider STDERR) */


	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;
	// // P2_1 추가
	// kf->eip = NULL;
	// kf->function = function; 	/* 스레드가 수행할 함수 */
	// kf->aux = aux;				/* 수행할 함수의 인자 */

	/* Add to run queue. */
	thread_unblock (t);
	if (preempt_by_priority()){
		thread_yield();
	}
	
	return tid;
}


/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;
	struct list_elem *e;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	
	list_push_back (&ready_list, &t->elem);
	t->status = THREAD_READY;
	
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();

	if (curr != idle_thread)
		list_push_back(&ready_list, &curr->elem);
	
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	thread_current ()->initial_priority = new_priority;

	/* --------- project1 ---------- */
	reset_priority();
	if (preempt_by_priority()){
		thread_yield();
	}
	/* ----------------------------- */
}

/* Returns the current thread's priority. 현재 스레드의 우선순위를 반환한다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Priority Scheduling 추가 */
bool thread_compare_priority (struct list_elem *l, struct list_elem *s, void *aux UNUSED) {
    return list_entry (l, struct thread, elem)->priority > list_entry (s, struct thread, elem)->priority;
}

/* Donate 추가 */
bool
thread_compare_donate_priority (const struct list_elem *l, 
				const struct list_elem *s, void *aux UNUSED)
{
	return list_entry (l, struct thread, donation_elem)->priority
		 > list_entry (s, struct thread, donation_elem)->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
/* thread_start() 에서 thread_create 을 하는 순간 idle thread 가 생성되고 동시에 idle 함수가 실행된다. 
	idle thread 는 여기서 한 번 schedule 을 받고 바로 sema_up 을 하여 thread_start() 의 마지막 sema_down 을 풀어주어 
	thread_start 가 작업을 끝내고 run_action() 이 실행될 수 있도록 해주고 idle 자신은 block 된다. 
	idle thread 는 pintos 에서 실행 가능한 thread 가 하나도 없을 때 이 wake 되어 다시 작동하는데, 
	이는 CPU 가 무조건 하나의 thread 는 실행하고 있는 상태를 만들기 위함이다. */
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
/* 파라미터를 보면, thread_func *function 은 이 kernel 이 실행할 함수를 가르키고, 
	void *aux 는 보조 파라미터로 synchronization 을 위한 세마포 등이 들어온다. 
	여기서 실행시키는 function 은 이 thread 가 종료될때가지 실행되는 main 함수라고 생각할 수 있다. 
	즉, 이 function 은 idle thread 라고 불리는 thread 를 하나 실행시키는데, 
	이 idle thread 는 하나의 c 프로그램에서 하나의 main 함수 안에서 여러 함수호출들이 이루어지는 것처럼, 
	pintos kernel 위에서 여러 thread 들이 동시에 실행될 수 있도록 하는 단 하나의 main thread 인 셈이다. 
	우리의 목적은 이 idle thread 위에 여러 thread 들이 동시에 실행되도록 만드는 것이다.  */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority; // 이 줄이 없어서 FAIL떴음
	t->magic = THREAD_MAGIC;

	/* -------- Project 1 ----------- */
	list_init (&t->donation_list);
	t->initial_priority = priority;
	t->wait_on_lock = NULL;
	/* ------------------------------ */
	
	/* -------- Project 2 ----------- */
	t->exit_status = 0;
	list_init(&t->child_list);
	sema_init(&t->fork_sema, 0);
	sema_init(&t->wait_sema, 0);
	sema_init(&t->free_sema, 0);

	t->running = NULL;
	/* ------------------------------ */
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is 
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else{
		/* ---------------- project 1 -----------------*/
		list_sort(&ready_list, &thread_priority_compare, NULL);
		/* --------------------------------------------*/
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
	}
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

/* depth 는 nested 의 최대 깊이를 지정해주기 위해 사용했다(max_depth = 8). 
	스레드의 wait_on_lock 이 NULL 이 아니라면 스레드가 lock 에 걸려있다는 소리이므로 
	그 lock 을 점유하고 있는 holder 스레드에게 priority 를 넘겨주는 방식을 깊이 8의 스레드까지 반복한다. 
	wait_on_lock == NULL 이라면 더 이상 donation 을 진해할 필요가 없으므로 break 해준다. */
void
donate_priority (void)
{ /* nest donation */
  int depth;
  struct thread *cur = thread_current ();

  for (depth = 0; depth < 8; depth++){ // testcase가 최대 7까지 있어서 8로 설정
    if (!cur->wait_on_lock) break;
      struct thread *holder = cur->wait_on_lock->holder;
      holder->priority = cur->priority;
      cur = holder;
  }
}

/* sheduling 함수는 thread_yield(), thread_block(), thread_exit() 함수 내의 거의 마지막 부분에 실행되어 
	CPU 의 소유권을 현재 실행중인 스레드에서 다음에 실행될 스레드로 넘겨주는 작업을 한다. */
static void
schedule (void) {
	/* 현재 실행중인 thread 를 thread A 라고 하고, 다음에 실행될 스레드를 thread B 라고 하겠다. 
		*cur 은 thread A, *next 는 next_thread_to_run() 이라는 함수(ready queue 에서 다음에 실행될 스레드를 골라서 return 함. 
		지금은 round-robin 방식으로 고른다.)에 의해 thread B 를 가르키게 되고, 
		*prev 는 thread A 가 CPU 의 소유권을 thread B 에게 넘겨준 후 thread A 를 가리키게 되는 포인터다. */
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();
	// struct thread *prev = NULL;

	ASSERT (intr_get_level () == INTR_OFF); // scheduling 도중에는 인터럽트가 발생하면 안 되기 때문에 INTR_OFF 상태인지 확인한다.
	ASSERT (curr->status != THREAD_RUNNING); // CPU 소유권을 넘겨주기 전에 running 스레드는 그 상태를 running 외의 다른 상태로 바꾸어주는 작업이 되어 있어야 하고 이를 확인하는 부분이다.
	ASSERT (is_thread (next)); // next_thread_to_run() 에 의해 올바른 thread 가 return 되었는지 확인한다.

	// if (cur != next)
    // prev = switch_threads (cur, next);
  	// thread_schedule_tail (prev);
	  // 그 후 새로 실행할 스레드가 정해졌다면 switch_thread (cur, next) 명령어를 실행한다. 이 함수는 <thread/switch.S> 에 assembly 언어로 작성되어 있다. 
	  // 이 함수가 사실상 핵심이기에 내용을 뜯어보기 전에 이 코드를 해석하려면 범용 레지스터와 assembly 언어에 대한 기본적인 이해가 필요할 것 같다.

	/* Mark us as running. */
	next->status = THREAD_RUNNING; // 상태 변경

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct thread. 
		   This must happen late so that thread_exit() doesn't pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is currently used bye the stack.
		   The real destruction logic will be called at the beginning of the schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

/* --------------------- project 1 ------------------------ */
/*  make thread sleep in timer_sleep() (../device/timer.c)  */
void thread_sleep(int64_t ticks){
	struct thread *curr;
	enum intr_level old_level;

	ASSERT (!intr_context ());//?
	old_level = intr_disable ();
	curr = thread_current ();

	ASSERT(curr != idle_thread)
	
	if (next_tick_to_awake>ticks){
		next_tick_to_awake = ticks;
	} 
	curr->wake_up_tick = ticks;
	list_push_back (&sleep_list, &curr->elem);	
	thread_block();
	intr_set_level (old_level);
}

/* make thread awake in timer_interrupt() (../device/timer.c) */
void thread_awake(int64_t ticks){
	next_tick_to_awake = INT64_MAX;
	enum intr_level old_level;
	struct list_elem *e;
	ASSERT (intr_context ());

	for (e = list_begin (&sleep_list); e != list_end (&sleep_list);) {
		struct thread* t = list_entry(e, struct thread, elem);
	
		if (t->wake_up_tick <= ticks) {
			e = list_remove(e);
			thread_unblock(t);
			if (preempt_by_priority()){
				intr_yield_on_return();
			}
		}
		else {
			if (t->wake_up_tick<next_tick_to_awake){
				next_tick_to_awake = t->wake_up_tick;
			}
			e = list_next(e);
		}
	}
}

/* global function to get value of next_tick_to_awake */
int64_t get_next_tick_to_awake(void) {
	return next_tick_to_awake;
}


/* compare priority between running thread and highest priority thread in ready_list 
	if running thread priority < highest priority thread in ready_list , return true */
bool preempt_by_priority(void) {
	int curr_priority;
	struct thread *max_ready_thread;
	struct list_elem *max_ready_elem;

	curr_priority = thread_get_priority();

	if (list_empty(&ready_list))return false; /* !! if ready list is empty, return false directly !!*/

	list_sort(&ready_list, &thread_priority_compare, NULL);
	max_ready_elem = list_begin(&ready_list);
	max_ready_thread = list_entry(max_ready_elem, struct thread, elem);

	if (curr_priority < max_ready_thread->priority) {
		return true;
	}
	else {
		return false;
	}
}

/* compare threads' priority of element1 and element2 by **elem** in struct thread */
bool thread_priority_compare (struct list_elem *element1, struct list_elem *element2, void *aux UNUSED) {
	struct thread *t1 = list_entry(element1, struct thread, elem);
	struct thread *t2 = list_entry(element2, struct thread, elem);

	if (t1->priority>t2->priority){
		return true;
	}else{
		return false;
	}
}

/* compare threads' priority of element1 and element2 by **donation_elem** in struct thread */
bool thread_donate_priority_compare (struct list_elem *element1, struct list_elem *element2, void *aux UNUSED) {
	struct thread *t1 = list_entry(element1, struct thread, donation_elem);
	struct thread *t2 = list_entry(element2, struct thread, donation_elem);

	if (t1->priority>t2->priority){
		return true;
	}else{
		return false;
	}
}

/* ------------------- project 1 functions end ------------------------------- */

/* --------------------- project 2 ------------------------ */
struct thread* get_child_by_tid(tid_t tid){
	struct thread *curr = thread_current();
	struct thread *child;
	struct list *child_list = &curr->child_list;
	struct list_elem *e;

	if (list_empty(child_list)) {
		return NULL;
	}

	for(e = list_begin(child_list); e != list_end(child_list); e = list_next(e)){
		child = list_entry(e, struct thread, child_elem);
		if (child->tid == tid) {
			return child;
		}
	}
	return NULL;
}

/* ------------------- project 1 functions end ------------------------------- */

