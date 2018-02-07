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

#define NUM_THREADS 16

/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

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
	
} uthread_arg_t;
	
struct timeval tv1;

/* Generates a square 2D matrix; returns pointer */
static matrix_t *generate_matrix(int size, int val) {
    int i, j;

    // Allocate a matrix
    matrix_t *m = (matrix_t *)MALLOC_SAFE(sizeof(matrix_t));

    // Allocate entire matrix as single block
    m->arr = (int *)MALLOC_SAFE(size * size * sizeof(int));

    int *row;

    // Fill with val
    for (i = 0; i < size; i++)
        for (j = 0, row = m->arr + i * size; j < size; j++)
            row[j] = val;

    m->rows = size;
    m->cols = size;

	return m;
}

static void print_matrix(matrix_t *mat)
{
	int i, j;

	for(i=0;i<mat->rows;i++)
	{
        int *row = mat->arr + i * mat->rows;

        for(j=0;j<mat->cols;j++)
			printf("%d ",row[j]);
		printf("\n");
	}

	return;
}

static void uthread_mulmat(void *p)
{
	int i, j, k;
	unsigned int cpuid;

#define ptr ((uthread_arg_t *)p)

	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	fprintf(stderr, "Thread(id:%d, group:%d, cpu:%d) started\n",ptr->tid, ptr->gid, cpuid);

    int *r1, *r2, *c1;
    int size = ptr->_A->rows;

	for(i = 0; i < size; i++) {
        r1 = ptr->_A->arr + i * ptr->_A->rows;
        r2 = ptr->_C->arr + i * ptr->_C->rows;

        for(j = 0; j < size; j++) {
            for(k = 0; k < size; k++) {
                c1 = ptr->_A->arr + k * ptr->_A->rows;
                r2[j] += r1[k] * c1[j];
            }
        }
    }

    struct timeval tv2, diff;
    gettimeofday(&tv2, NULL);
    timersub(&tv2, &tv1, &diff);

    fprintf(stderr, "Thread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)\n",
			ptr->tid, ptr->gid, cpuid, diff.tv_sec, diff.tv_usec);

#undef ptr
}

// 1 matrix per thread => 1 output ("squaring")
matrix_t *input_matrices[NUM_THREADS];
matrix_t *output_matrices[NUM_THREADS];

static void init_matrices()
{
    int i;

    for (i = 0; i < NUM_THREADS; i++) {
        input_matrices[i] = generate_matrix(512, 1);
        output_matrices[i] = generate_matrix(512, 0);
    }
}

void free_matrix(matrix_t *m) {
    free(m->arr);
    free(m);
}

static void cleanup_matrices() {
    int i;
    matrix_t *m;

    for (i = 0; i < NUM_THREADS; i++) {
        m = input_matrices[i];
        free_matrix(m);
        m = output_matrices[i];
        free_matrix(m);
    }
}

uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

int main(int argc, char **argv)
{
    kthread_sched_t sched;

    // Get scheduler to use
    if (argc == 2) {
        long v = strtol(argv[1], NULL, 10);

        if (v == 0) sched = GT_SCHED_PRIORITY;
        else sched = GT_SCHED_CREDIT;
    } else {
        printf("Usage: matrix [0=PRIORITY/1=CREDIT]\n");
        exit(0);
    }

    if (sched)
        printf("SCHEDULER = CREDIT\n");
    else
        printf("SCHEDULER = PRIORITY\n");

	uthread_arg_t *uarg;

	gtthread_app_init(sched);

    gettimeofday(&tv1, NULL);

    int i, j, k;

    int credit_values[4] = {25, 50, 75, 100};
    int credits;

    int matrix_sizes[4] = {128, 64, 256, 512};
    int size;

    int idx = 0;

    for (i = 0; i < 4; i++) {
        // For each credit type
        credits = credit_values[i];

        for (j = 0; j < 4; j++) {
            // For each size
            size = matrix_sizes[j];

            // TODO
            for (k = 0; k < (NUM_THREADS/8); k++);

            input_matrices[idx] = generate_matrix(size, 1);
            output_matrices[idx] = generate_matrix(size, 0);

            uarg = &uargs[idx];
            uarg->_A = input_matrices[idx];
            uarg->_C = output_matrices[idx];

            uarg->tid = (unsigned)idx;
            uarg->gid = 0;

            uthread_create(&utids[idx], uthread_mulmat, uarg, uarg->gid, credits);

            idx++;
        }
    }

	gtthread_app_exit();

    cleanup_matrices();

	// print_matrix(&C);
	// fprintf(stderr, "********************************");
	return(0);
}
