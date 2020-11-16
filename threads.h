#ifndef THREADS_H
#define THREADS_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <setjmp.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>
#include "semaphore.h"

#define RUNNING 101
#define READY 100
#define EXITED 99
#define BLOCKED 102

#define SEM_INIT 1
#define SEM_UNINIT 0
#define SEM_WAIT 98

#define STACK_SZ 32767
#define MAX_THREAD_SZ 128
#define MAX_TIME_PER_THREAD 50000
#define MAX_DEPENDENCIES 127

#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7

typedef struct
{
    pthread_t id;
    jmp_buf buff;
    int status;
} tcb;

//Circular LL of blocks
typedef struct t
{
    tcb block;
    int *stack_ptr;
    void *return_val;
    pthread_t wait_thread;
    struct t *next;
} thread_t;

typedef struct q
{
    pthread_t id;
    struct q *next;
} wait_queue_q;

typedef struct s
{
    unsigned int sem_value;
    wait_queue_q *wq_head;
    int init_flag;
} semaphore_s;

thread_t *create_tcb();
thread_t *add_to_queue(thread_t *new_node, thread_t *head);
void print_blocks();
int pthread_create(
    pthread_t *thread,
    const pthread_attr_t *attr,
    void *(*start_routine)(void *),
    void *arg);

void pthread_exit(void *value_ptr);

pthread_t pthread_self(void);

void setup_threads(void);

void schedule(int sig);

void lock(void);
void unlock(void);
int pthread_join(pthread_t thread, void **value_ptr);
void pthread_exit_wrapper(void);
// void test_ll(void);
int sem_init(sem_t *sem, int pshared, unsigned value);
int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_destroy(sem_t *sem);
#endif