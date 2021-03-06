#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>

#include "gt_include.h"


/**********************************************************************/
/* runqueue operations */
static inline void __add_to_runqueue(runqueue_t *runq, uthread_struct_t *u_elm);
static inline void __rem_from_runqueue(runqueue_t *runq, uthread_struct_t *u_elm);

/**********************************************************************/
/* runqueue operations */
static inline void __add_to_runqueue(runqueue_t *runq, uthread_struct_t *u_elem)
{
	unsigned int uprio, ugroup;
	uthread_head_t *uhead;

	/* Find a position in the runq based on priority and group.
	 * Update the masks. */
	uprio = u_elem->uthread_priority;
	ugroup = u_elem->uthread_gid;

	/* Insert at the tail */
	uhead = &runq->prio_array[uprio].group[ugroup];
	TAILQ_INSERT_TAIL(uhead, u_elem, uthread_runq);

	/* Update information */
	if(!IS_BIT_SET(runq->prio_array[uprio].group_mask, ugroup))
		SET_BIT(runq->prio_array[uprio].group_mask, ugroup);

	runq->uthread_tot++;

	runq->uthread_prio_tot[uprio]++;
	if(!IS_BIT_SET(runq->uthread_mask, uprio))
		SET_BIT(runq->uthread_mask, uprio);

	runq->uthread_group_tot[ugroup]++;
	if(!IS_BIT_SET(runq->uthread_group_mask[ugroup], uprio))
		SET_BIT(runq->uthread_group_mask[ugroup], uprio);

	return;
}

static inline void __rem_from_runqueue(runqueue_t *runq, uthread_struct_t *u_elem)
{
	unsigned int uprio, ugroup;
	uthread_head_t *uhead;

	/* Find a position in the runq based on priority and group.
	 * Update the masks. */
	uprio = u_elem->uthread_priority;
	ugroup = u_elem->uthread_gid;

	/* Insert at the tail */
	uhead = &runq->prio_array[uprio].group[ugroup];
	TAILQ_REMOVE(uhead, u_elem, uthread_runq);

	/* Update information */
	if(TAILQ_EMPTY(uhead))
		RESET_BIT(runq->prio_array[uprio].group_mask, ugroup);

	runq->uthread_tot--;

	if(!(--(runq->uthread_prio_tot[uprio])))
		RESET_BIT(runq->uthread_mask, uprio);

	if(!(--(runq->uthread_group_tot[ugroup])))
	{
		assert(TAILQ_EMPTY(uhead));
		RESET_BIT(runq->uthread_group_mask[ugroup], uprio);
	}

	return;
}


/**********************************************************************/
/* Exported runqueue operations */
extern void init_runqueue(runqueue_t *runq)
{
	uthread_head_t *uhead;
	int i, j;
	/* Everything else is global, so already initialized to 0(correct init value) */
	for(i=0; i<MAX_UTHREAD_PRIORITY; i++)
	{
		for(j=0; j<MAX_UTHREAD_GROUPS; j++)
		{
			uhead = &((runq)->prio_array[i].group[j]);
			TAILQ_INIT(uhead);
		}
	}
	return;
}

extern void add_to_runqueue(runqueue_t *runq, gt_spinlock_t *runq_lock, uthread_struct_t *u_elem)
{
	gt_spin_lock(runq_lock);
	runq_lock->holder = 0x02;

    if (u_elem != NULL)
	    __add_to_runqueue(runq, u_elem);

    gt_spin_unlock(runq_lock);
	return;
}

extern void rem_from_runqueue(runqueue_t *runq, gt_spinlock_t *runq_lock, uthread_struct_t *u_elem)
{
	gt_spin_lock(runq_lock);
	runq_lock->holder = 0x03;

    if (u_elem != NULL)
	    __rem_from_runqueue(runq, u_elem);

    gt_spin_unlock(runq_lock);
	return;
}

extern void switch_runqueue(runqueue_t *from_runq, gt_spinlock_t *from_runqlock, 
				runqueue_t *to_runq, gt_spinlock_t *to_runqlock, uthread_struct_t *u_elem)
{
	rem_from_runqueue(from_runq, from_runqlock, u_elem);
	add_to_runqueue(to_runq, to_runqlock, u_elem);
	return;
}


/**********************************************************************/

extern void kthread_init_runqueue(kthread_runqueue_t *kthread_runq)
{
	kthread_runq->active_runq = &(kthread_runq->runqueues[0]);
	kthread_runq->expires_runq = &(kthread_runq->runqueues[1]);

	gt_spinlock_init(&(kthread_runq->kthread_runqlock));
	init_runqueue(kthread_runq->active_runq);
	init_runqueue(kthread_runq->expires_runq);

	TAILQ_INIT(&(kthread_runq->zombie_uthreads));
	return;
}

#if 0
static void print_runq_stats(runqueue_t *runq, char *runq_str)
{
	int inx;
	printf("******************************************************\n");
	printf("Run queue(%s) state : \n", runq_str);
	printf("******************************************************\n");
	printf("uthreads details - (tot:%d , mask:%x)\n", runq->uthread_tot, runq->uthread_mask);
	printf("******************************************************\n");
	printf("uthread priority details : \n");
	for(inx=0; inx<MAX_UTHREAD_PRIORITY; inx++)
		printf("uthread priority (%d) - (tot:%d)\n", inx, runq->uthread_prio_tot[inx]);
	return;
	printf("******************************************************\n");
	printf("uthread group details : \n");
	for(inx=0; inx<MAX_UTHREAD_GROUPS; inx++)
		printf("uthread group (%d) - (tot:%d , mask:%x)\n", inx, runq->uthread_group_tot[inx], runq->uthread_group_mask[inx]);
	printf("******************************************************\n");
	return;
}
#endif

extern uthread_struct_t *sched_find_best_uthread(kthread_runqueue_t *kthread_runq)
{
	/* [1] Tries to find the highest priority RUNNABLE uthread in active-runq.
	 * [2] Found - Jump to [FOUND]
	 * [3] Switches runqueues (active/expires)
	 * [4] Repeat [1] through [2]
	 * [NOT FOUND] Return NULL(no more jobs)
	 * [FOUND] Remove uthread from pq and return it. */

	runqueue_t *runq;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;
	unsigned int uprio, ugroup;

	gt_spin_lock(&(kthread_runq->kthread_runqlock));

	runq = kthread_runq->active_runq;

	kthread_runq->kthread_runqlock.holder = 0x04;

    kthread_context_t *k_ctx = kthread_cpu_map[kthread_apic_id()];

	if(!(runq->uthread_mask))
	{ /* No jobs in active. switch runqueue */
        #if 0
        fprintf(stderr, "Switched the runqueues in kthread(%d)\n", k_ctx->cpuid);
        #endif

        assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->active_runq;
		if(!runq->uthread_mask)
		{
			assert(!runq->uthread_tot);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));
			return NULL;
		}
	}

	/* Find the highest priority bucket */
	uprio = LOWEST_BIT_SET(runq->uthread_mask);
	prioq = &(runq->prio_array[uprio]);

	assert(prioq->group_mask);
	ugroup = LOWEST_BIT_SET(prioq->group_mask);

	u_head = &(prioq->group[ugroup]);
	u_obj = TAILQ_FIRST(u_head);
	__rem_from_runqueue(runq, u_obj);

	gt_spin_unlock(&(kthread_runq->kthread_runqlock));
#if DEBUG
	fprintf(stderr, "kthread(%d) : sched best uthread(id:%d, group:%d)\n",
            u_obj->cpu_id, u_obj->uthread_tid, u_obj->uthread_gid);
#endif
	return(u_obj);
}

uthread_struct_t *credit_find_best_uthread_single(kthread_runqueue_t *kthread_runq) {
    uthread_head_t *u_head;
    uthread_struct_t *u_thread;
    runqueue_t *runq;

    runq = kthread_runq->active_runq;

    if (!runq->uthread_tot) {
		return NULL;
    }

    // Take the first uthread on the active/expires queue, remove, and return it
    u_head = &runq->prio_array[UTHREAD_CREDIT_UNDER].group[0];
    u_thread = TAILQ_FIRST(u_head);

    // Check if head is valid before removal AND in runnable state
    if (u_thread != NULL && (u_thread->uthread_state & (UTHREAD_INIT | UTHREAD_RUNNABLE))) {
        __rem_from_runqueue(runq, u_thread);
        return u_thread;
    }

    return NULL;
}

/**
 * Finds the highest priority uthread from current kthread's runqueue.
 */
extern uthread_struct_t *credit_find_best_uthread(kthread_runqueue_t *kthread_runq) {
    uthread_head_t *u_head;
    uthread_struct_t *u_thread;
    runqueue_t *runq;
    gt_spinlock_t *lock = &(kthread_runq->kthread_runqlock);

    kthread_context_t *k_ctx = kthread_cpu_map[kthread_apic_id()];

    // Look for a viable uthread in current runq
    gt_spin_lock(lock);
    kthread_runq->kthread_runqlock.holder = 0x04;
    u_thread = credit_find_best_uthread_single(kthread_runq);
    gt_spin_unlock(lock);

    if (u_thread != NULL)
        return u_thread;

    // No candidate? Time for uthread migration!
    // At this point, NO UNDER UTHREADS LEFT IN EITHER QUEUE!
    // Perform inter-kthread migration!
    kthread_context_t *temp_k_ctx;
    gt_spinlock_t *temp_lock;
    int inx;

    for (inx = 0; inx < GT_MAX_KTHREADS; inx++) {
        if (!(temp_k_ctx = kthread_cpu_map[inx]))
            break;

        // Iterate over all OTHER kthreads
        if (temp_k_ctx != k_ctx) {
            // If target has no uthreads, ignore
            if (!temp_k_ctx->krunqueue.active_runq->uthread_tot)
                continue;

            // Acquire lock for target kthread
            temp_lock = &temp_k_ctx->krunqueue.kthread_runqlock;
            gt_spin_lock(temp_lock);

            // Look for an UNDER, RUNNABLE uthread on target kthread
            if ((u_thread = credit_find_best_uthread_single(&temp_k_ctx->krunqueue))) {
                // Found one!
                #if DEBUG
                fprintf(stderr, "kthread(%d) migrated uthread(%d) from kthread(%d)!\n", k_ctx->cpuid,
                        u_thread->uthread_tid, temp_k_ctx->cpuid);
                #endif

                gt_spin_unlock(temp_lock);

                return u_thread;
            }

            gt_spin_unlock(temp_lock);
        }
    }

    // If no UNDER uthreads ANYWHERE, let's run one of our (or someone else's) expired uthreads!
    // First, try to find an OVER from one of our uthreads
	// Switch runqueues!
    runq = kthread_runq->active_runq;
	kthread_runq->active_runq = kthread_runq->expires_runq;
	kthread_runq->expires_runq = runq;
	runq = kthread_runq->active_runq;

    gt_spin_lock(lock);
    u_head = &runq->prio_array[UTHREAD_CREDIT_UNDER].group[0];
    u_thread = TAILQ_FIRST(u_head);

    // If found, return it!
    if (u_thread != NULL) {
        __rem_from_runqueue(runq, u_thread);
        gt_spin_unlock(lock);
        return u_thread;
    }

    gt_spin_unlock(lock);

    // At this point, we need to look for a OVER uthread on some other kthread
    for (inx = 0; inx < GT_MAX_KTHREADS; inx++) {
        if (!(temp_k_ctx = kthread_cpu_map[inx]))
            break;

        // Skip if kthread NULL, or same, or no uthreads
        if (temp_k_ctx == k_ctx)
            continue;

        // Check if kthread has uthreads available
        runq = temp_k_ctx->krunqueue.expires_runq;
        if (!runq->uthread_tot)
            continue;

        // Acquire a lock for other kthread
        temp_lock = &temp_k_ctx->krunqueue.kthread_runqlock;
        gt_spin_lock(temp_lock);

        u_head = &runq->prio_array[UTHREAD_CREDIT_UNDER].group[0];
        u_thread = TAILQ_FIRST(u_head);

        // If valid, remove it and return
        if (u_thread != NULL) {
            __rem_from_runqueue(runq, u_thread);
            gt_spin_unlock(temp_lock);
            return u_thread;
        }

        gt_spin_unlock(temp_lock);
    }

    // If we get here, then NO UTHREADS ARE FOUND ANYWHERE!
	return NULL;
}

/* XXX: More work to be done !!! */
extern gt_spinlock_t uthread_group_penalty_lock;
extern unsigned int uthread_group_penalty;

extern uthread_struct_t *sched_find_best_uthread_group(kthread_runqueue_t *kthread_runq)
{
	/* [1] Tries to find a RUNNABLE uthread in active-runq from u_gid.
	 * [2] Found - Jump to [FOUND]
	 * [3] Tries to find a thread from a group with least threads in runq (XXX: NOT DONE)
	 * - [Tries to find the highest priority RUNNABLE thread (XXX: DONE)]
	 * [4] Found - Jump to [FOUND]
	 * [5] Switches runqueues (active/expires)
	 * [6] Repeat [1] through [4]
	 * [NOT FOUND] Return NULL(no more jobs)
	 * [FOUND] Remove uthread from pq and return it. */
	runqueue_t *runq;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;
	unsigned int uprio, ugroup, mask;
	uthread_group_t u_gid;

#ifndef COSCHED
	return sched_find_best_uthread(kthread_runq);
#endif

	/* XXX: Read u_gid from global uthread-select-criterion */
	u_gid = 0;
	runq = kthread_runq->active_runq;

	if(!runq->uthread_mask)
	{ /* No jobs in active. switch runqueue */
		assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->expires_runq;
		if(!runq->uthread_mask)
		{
			assert(!runq->uthread_tot);
			return NULL;
		}
	}

	
	if(!(mask = runq->uthread_group_mask[u_gid]))
	{ /* No uthreads in the desired group */
		assert(!runq->uthread_group_tot[u_gid]);
		return (sched_find_best_uthread(kthread_runq));
	}

	/* Find the highest priority bucket for u_gid */
	uprio = LOWEST_BIT_SET(mask);

	/* Take out a uthread from the bucket. Return it. */
	u_head = &(runq->prio_array[uprio].group[u_gid]);
	u_obj = TAILQ_FIRST(u_head);
	rem_from_runqueue(runq, &(kthread_runq->kthread_runqlock), u_obj);
	
	return(u_obj);
}

#ifdef PQ_DEBUG
/*****************************************************************************************/
/* Main Test Function */

runqueue_t active_runqueue, expires_runqueue;

#define MAX_UTHREADS 4
uthread_struct_t u_objs[MAX_UTHREADS];

static void fill_runq(runqueue_t *runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* create and insert */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		u_obj->uthread_tid = inx;
		u_obj->uthread_gid = 0;
		u_obj->uthread_priority = (inx % MAX_UTHREAD_PRIORITY);
		__add_to_runqueue(runq, u_obj);
		printf("Uthread (id:%d , prio:%d) inserted\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}

static void print_runq_stats(runqueue_t *runq, char *runq_str)
{
	int inx;
	printf("******************************************************\n");
	printf("Run queue(%s) state : \n", runq_str);
	printf("******************************************************\n");
	printf("uthreads details - (tot:%d , mask:%x)\n", runq->uthread_tot, runq->uthread_mask);
	printf("******************************************************\n");
	printf("uthread priority details : \n");
	for(inx=0; inx<MAX_UTHREADS; inx++)
		printf("uthread priority (%d) - (tot:%d)\n", inx, runq->uthread_prio_tot[inx]);
	printf("******************************************************\n");
	printf("uthread group details : \n");
	for(inx=0; inx<MAX_UTHREADS; inx++)
		printf("uthread group (%d) - (tot:%d , mask:%x)\n", inx, runq->uthread_group_tot[inx], runq->uthread_group_mask[inx]);
	printf("******************************************************\n");
	return;
}

static void change_runq(runqueue_t *from_runq, runqueue_t *to_runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* Remove and delete */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		printf("Uthread (id:%d , prio:%d) moved\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}


static void empty_runq(runqueue_t *runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* Remove and delete */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		__rem_from_runqueue(runq, u_obj);
		printf("Uthread (id:%d , prio:%d) removed\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}

int main()
{
    runqueue_t *active_runq, *expires_runq;
    uthread_struct_t *u_obj;
    int inx;

    active_runq = &active_runqueue;
    expires_runq = &expires_runqueue;

    init_runqueue(active_runq);
    init_runqueue(expires_runq);

    fill_runq(active_runq);
    print_runq_stats(active_runq, "ACTIVE");
    print_runq_stats(expires_runq, "EXPIRES");

    uthread_head_t *head = &active_runq->prio_array[0].group[0];

    return 0;
}

#endif
