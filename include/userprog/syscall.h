#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

/* -------- project2 ---------- */
struct lock filesys_lock;   /* proventing race condition against  */
/* ---------------------------- */

#endif /* userprog/syscall.h */
