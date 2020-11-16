#include "threads.h"
#include "helper_assembly.h"
////// Global Variables //////////
thread_t *head = NULL;
thread_t *current = NULL;
int num_blocks = 0;
/////////////////////////////////
/////////LL Stuff///////////////
thread_t *create_tcb(int num_blocks)
{
    thread_t *result = malloc(sizeof(thread_t));
    tcb *block = malloc(sizeof(tcb));
    block->id = (pthread_t)num_blocks;
    block->status = READY;
    result->block = *block;
    result->next = NULL;
    result->stack_ptr = NULL;
    return result;
}
//Got algorithm from: https://www.tutorialspoint.com/data_structures_algorithms/doubly_linked_list_algorithm.htm
thread_t *add_to_queue(thread_t *new_node, thread_t *head)
{
    thread_t *temp, *p;
    p = head;
    temp = new_node;

    if (head == NULL)
    {
        head = temp;
    }
    else
    {
        while (p->next != head)
            p = p->next;
        p->next = temp;
    }
    temp->next = head;
    return head;
}
//////////////////////////////////////////////////

// implementation from https://stackoverflow.com/questions/50195942/sigprocmask-with-sig-block-doesnt-block-sigalrm
void lock()
{
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}
void unlock()
{
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
}

// Initializes new semaphore and stores its pointer in sem (sem_t) structure.
int sem_init(sem_t *sem, int pshared, unsigned value)
{
    semaphore_s *sema = malloc(sizeof(semaphore_s));
    sema->sem_value = value;
    sema->wq_head = NULL;
    sema->init_flag = SEM_INIT;
    sem->__align = (long int)sema;
    return 0;
}

int sem_wait(sem_t *sem)
{
    lock();
    semaphore_s *sema = (semaphore_s *)sem->__align;
    // If value > 0, decrement and return immediatly
    if (sema->sem_value > 0)
    {
        (sema->sem_value)--;
        unlock();
        return 0;
    }
    if (sema->wq_head == NULL)
    {
        wait_queue_q *new_block = (wait_queue_q *)malloc(sizeof(wait_queue_q));
        sema->wq_head = new_block;
        sema->wq_head->id = current->block.id;
        sema->wq_head->next = NULL;
    }
    // Else (its = 0), add the calling thread to the queue of waiting threads
    else
    {
        wait_queue_q *temp = sema->wq_head;
        while (temp->next != NULL)
        {
            temp = temp->next;
        }
        wait_queue_q *new_block = (wait_queue_q *)malloc(sizeof(wait_queue_q));
        new_block->id = current->block.id;
        new_block->next = NULL;
        temp->next = new_block;
    }
    // Skip this thread until status changes
    current->block.status = SEM_WAIT;
    unlock();
    schedule(0);
    return 0;
}

int sem_post(sem_t *sem)
{
    lock();
    semaphore_s *sema = (semaphore_s *)sem->__align;
    // There is a queue, set the status of the first thread in queue to be READY
    // And make the head of the queue the next item
    if (sema->wq_head != NULL)
    {
        thread_t *temp = current;
        do
        {
            temp = temp->next;
        } while (temp->block.id != sema->wq_head->id);
        temp->block.status = READY;
        sema->wq_head = sema->wq_head->next;
    }
    (sema->sem_value)++;
    unlock();
    return 0;
}

int sem_destroy(sem_t *sem)
{
    lock();
    semaphore_s *sema = (semaphore_s *)sem->__align;
    // Check if a wait queue exists
    if (sema->wq_head != NULL)
    {
        // Only 1 item in queue
        if (sema->wq_head->next == NULL)
        {
            free(sema->wq_head);
        }
        else
        {
            // Multiple items in queue
            wait_queue_q *temp = sema->wq_head->next;
            while (temp != NULL)
            {
                free(sema->wq_head);
                sema->wq_head = temp;
                temp = sema->wq_head->next;
            }
            free(sema->wq_head);
        }
    }
    free(sema);
    unlock();
    return 0;
}

int check_status(pthread_t join)
{
    if (join == (pthread_t)-1)
        return -1;
    thread_t *temp = head;
    temp = temp->next;
    while (temp->block.id != join)
    {
        temp = temp->next;
    }
    return temp->block.status;
}
/*
This function will postpone the execution of the thread that initiated the call until the
target thread terminates
*/
int pthread_join(pthread_t thread, void **value_ptr)
{
    lock();
    thread_t *join_thread = head;
    join_thread = join_thread->next;
    while (join_thread->block.id != thread)
    {
        join_thread = join_thread->next;
    }
    //if pthread_join was called, current must be the calling thread
    current->block.status = BLOCKED;
    current->wait_thread = thread;

    schedule(0);

    // Once schedule returns, we can store the return value of the joined thread in the address
    // referenced by value ptr
    if (value_ptr != NULL)
        *value_ptr = join_thread->return_val;

    unlock();
    return 0;
}

int pthread_create(
    pthread_t *thread,
    const pthread_attr_t *attr,
    void *(*start_routine)(void *),
    void *arg)
{
    printf("Creating\n");
    if (num_blocks == 0)
    {
        setup_threads();
    }
    if (num_blocks > MAX_THREAD_SZ)
    {
        printf("ERROR: Max number of threads reached\n");
        return -1;
    }
    thread_t *new_thread = create_tcb(num_blocks);
    *thread = new_thread->block.id;
    num_blocks++;
    new_thread->stack_ptr = malloc(sizeof(unsigned long) * STACK_SZ);
    int *temp_ptr = new_thread->stack_ptr + STACK_SZ - 8;
    *(unsigned long *)temp_ptr = (unsigned long)pthread_exit_wrapper;
    new_thread->block.buff[0].__jmpbuf[JB_R13] = (unsigned long)arg;
    new_thread->block.buff[0].__jmpbuf[JB_R12] = (unsigned long)start_routine;
    new_thread->block.buff[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long)start_thunk);
    new_thread->block.buff[0].__jmpbuf[JB_RSP] = ptr_mangle((unsigned long)temp_ptr);
    new_thread->block.status = READY;
    new_thread->wait_thread = (pthread_t)-1;
    head = add_to_queue(new_thread, head);
    return 0;
}
void pthread_exit(void *return_val)
{
    current->block.status = EXITED;
    current->return_val = return_val;
    num_blocks--;
    schedule(0);
    __builtin_unreachable();
}

void pthread_exit_wrapper()
{
    unsigned long int res;
    asm("movq %%rax, %0\n"
        : "=r"(res));
    pthread_exit((void *)res);
}

void schedule(int sig)
{
    if (current == NULL)
        return;
    if (setjmp(current->block.buff) == 0)
    {
        do
        {
            if (check_status(current->wait_thread) == EXITED)
                current->block.status = READY;
            current = current->next;
        } while (current->block.status == EXITED || current->block.status == BLOCKED || current->block.status == SEM_WAIT);
        current->block.status = RUNNING;
        longjmp(current->block.buff, 1);
    }
}
void setup_threads()
{
    printf("in setup\n");
    thread_t *new_thread = create_tcb(num_blocks);
    new_thread->block.status = READY;
    new_thread->wait_thread = (pthread_t)-1;
    num_blocks++;
    new_thread->stack_ptr = NULL;
    head = add_to_queue(new_thread, head);
    current = head;
    setjmp(new_thread->block.buff);
    //from https://stackoverflow.com/questions/31919247/c-signal-handler
    struct sigaction sa;
    sa.sa_handler = &schedule;
    sa.sa_flags = SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    ualarm(MAX_TIME_PER_THREAD, MAX_TIME_PER_THREAD);
}
pthread_t pthread_self()
{
    return current->block.id;
}