#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupts on or off? */
enum intr_level {
	INTR_OFF,             /* Interrupts disabled. */
	INTR_ON               /* Interrupts enabled. */
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* Interrupt stack frame. */
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi; // 근원지(source) 인덱스 레지스터. arg[0](file name)의 char이 저장되기 시작한 주소를 저장
	uint64_t rdi; // 목적지(destinaion) 인덱스 레지스터. arg의 갯수를 저장
	uint64_t rbp; // 베이스 포인터 레지스터 (스택의 시작점)
	uint64_t rdx; // 데이터 레지스터
	uint64_t rcx; // 카운터 레지스터
	uint64_t rbx; // 베이스 레지스터
	uint64_t rax; // 누산기(accumulator) 레지스터
} __attribute__((packed));

struct intr_frame {
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers. */
	struct gp_registers R;
	uint16_t es;
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds;
	uint16_t __pad3;
	uint32_t __pad4;
	/* Pushed by intrNN_stub in intr-stubs.S. */
	uint64_t vec_no; /* Interrupt vector number. */
/* Sometimes pushed by the CPU, otherwise for consistency pushed as 0 by intrNN_stub.
   The CPU puts it just under `eip', but we move it here. 
 	CPU에 의해 푸시되는 경우도 있고, 일관성을 위해 intrNN_stub에 의해 0으로 푸시되는 경우도 있습니다.
	CPU는 'eip' 바로 아래에 두지만, 우리는 그것을 여기로 옮깁니다.*/
	uint64_t error_code;
/* Pushed by the CPU.
   These are the interrupted task's saved registers. */
	uintptr_t rip; // 프로세서가 읽고 있는 현재 명령의 위치를 가리키는 명령 포인터 레지스터.
	uint16_t cs;
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t eflags;
	uintptr_t rsp; // rsp : 스택 포인터 레지스터 (스택의 꼭대기)
	uint16_t ss;
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);

#endif /* threads/interrupt.h */
