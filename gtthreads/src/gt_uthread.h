#ifndef __GT_UTHREAD_H
#define __GT_UTHREAD_H

#include <time.h>

/* User-level thread implementation (using alternate signal stacks) */

typedef unsigned int uthread_t;
typedef unsigned int uthread_group_t;

/* uthread states */
#define UTHREAD_INIT 0x01
#define UTHREAD_RUNNABLE 0x02
#define UTHREAD_RUNNING 0x04
#define UTHREAD_CANCELLED 0x08
#define UTHREAD_DONE 0x10

/* Credit scheduler states */
#define UTHREAD_CREDIT_UNDER 0x01
#define UTHREAD_CREDIT_OVER 0x02

#define UTHREAD_DEFAULT_CREDITS 25

/* uthread struct : has all the uthread context info */
typedef struct uthread_struct
{
	
	int uthread_state; /* UTHREAD_INIT, UTHREAD_RUNNABLE, UTHREAD_RUNNING, UTHREAD_CANCELLED, UTHREAD_DONE */
	int uthread_priority; /* uthread running priority */
    int uthread_original_credits;
	double uthread_credits; /* Current credit count (used only in credit scheduler!) */
	int cpu_id; /* cpu it is currently executing on */
	int last_cpu_id; /* last cpu it was executing on */
	
	uthread_t uthread_tid; /* thread id */
	uthread_group_t uthread_gid; /* thread group id  */
	int (*uthread_func)(void*);
	void *uthread_arg;

    clock_t init_time;
    clock_t runnable_time;
    clock_t running_time;
    clock_t done_time;
	double used_time;

	void *exit_status; /* exit status */
	int reserved1;
	int reserved2;
	int reserved3;
	
	sigjmp_buf uthread_env; /* 156 bytes : save user-level thread context*/
	stack_t uthread_stack; /* 12 bytes : user-level thread stack */
	TAILQ_ENTRY(uthread_struct) uthread_runq;
} uthread_struct_t;

typedef struct matrix
{
	// 2D array
	int *arr;

	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;

typedef struct __uthread_arg
{
	// Compute A*A = C
	matrix_t *_A, *_C;
	unsigned int reserved0;

	unsigned int tid;
	unsigned int gid;

	unsigned int credits; // Original num credits
	struct timeval created; // Creation time (real)
	struct timeval runtime; // Run time (real)
	double used_time;
	unsigned int size; // Matrix size
} uthread_arg_t;

struct __kthread_runqueue;
extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(struct __kthread_runqueue *),
                             int from_timer);
#endif
