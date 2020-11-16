#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "tls.h"
#define PAGE_SZ 4000

int page_size;
int initialized = 0;
HE *hash_table[HASH_SIZE];
int i;

int get_hash(pthread_t tid)
{
    return ((unsigned int)tid) % HASH_SIZE;
}

void hash_init()
{
    for (i = 0; i < HASH_SIZE; i++)
        hash_table[i] = NULL;
}

void hash_add(pthread_t tid, TLS *add_tls)
{
    int idx = get_hash(tid);
    HE *new_tls = malloc(sizeof(HE));
    new_tls->tls = add_tls;
    new_tls->tid = tid;
    new_tls->next = NULL;
    // If head of LL is null
    if (hash_table[idx] == NULL)
        hash_table[idx] = new_tls;
    else
    {
        HE *temp = hash_table[idx];
        while (temp->next != NULL)
            temp = temp->next;
        //Add to end of LL
        temp->next = new_tls;
    }
}

TLS *hash_get(pthread_t tid)
{
    int idx = get_hash(tid);

    HE *temp = hash_table[idx];
    // Look for tid in hash table
    while (temp != NULL && temp->tid != tid)
    {
        temp = temp->next;
    }

    if (temp == NULL)
    {
        return NULL;
    }

    return temp->tls;
}

int hash_exists(pthread_t tid)
{
    int idx = get_hash(tid);

    HE *temp = hash_table[idx];
    while (temp != NULL)
    {
        if (temp->tid == tid)
            return 0;
        temp = temp->next;
    }
    return -1;
}

void hash_remove(pthread_t tid)
{
    int idx = get_hash(tid);

    if (hash_table[idx]->tid == tid)
    {

        for (i = 0; i < hash_table[idx]->tls->page_num; i++)
        {
            if (hash_table[idx]->tls->pages[i]->ref_count == 1)
                free(hash_table[idx]->tls->pages[i]);
            else
                hash_table[idx]->tls->pages[i]->ref_count--;
        }
        free(hash_table[idx]->tls->pages);
        free(hash_table[idx]->tls);
        HE *temp = hash_table[idx]->next;
        free(hash_table[idx]);
        hash_table[idx] = temp;
    }
    else
    {
        HE *temp = hash_table[idx];
        while (temp->next->tid != tid)
            temp = temp->next;
        HE *delete_tls = temp->next;
        temp->next = temp->next->next;
        if (delete_tls->tls->pages[i]->ref_count == 1)
            free(delete_tls->tls->pages[i]);
        else
            delete_tls->tls->pages[i]->ref_count--;
        free(delete_tls->tls->pages);
        free(delete_tls->tls);
        free(delete_tls);
    }
}

void tls_handle_page_fault(int sig, siginfo_t *si, void *context)
{
    unsigned int p_fault = ((uintptr_t)si->si_addr) & ~(page_size - 1);
    int i = 0;
    int j = 0;
    while (i < HASH_SIZE)
    {
        while (j < hash_table[i]->tls->page_num)
        {
            if (hash_table[i]->tls->pages[j]->address == p_fault)
            {
                pthread_exit(NULL);
            }
            j++;
        }
        i++;
    }

    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    raise(sig);
}

void tls_protect(PAGE *p)
{
    if (mprotect((void *)p->address, page_size, 0))
    {
        fprintf(stderr, "tls_protect: could not protect page\n");
        exit(1);
    }
}

void tls_unprotect(PAGE *p)
{
    if (mprotect((void *)p->address, page_size, PROT_READ | PROT_WRITE))
    {
        fprintf(stderr, "tls_unprotect: could not unprotect page\n");
        exit(1);
    }
}

void tls_init()
{
    struct sigaction sigact;
    /* get the size of a page */
    page_size = getpagesize();
    /* install the signal handler while page faults (SIGSEGV, SIGBUS) */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO;
    /* use extended signal handling */
    sigact.sa_sigaction = tls_handle_page_fault;
    sigaction(SIGBUS, &sigact, NULL);
    sigaction(SIGSEGV, &sigact, NULL);
    initialized = 1;
    hash_init();
}

int tls_create(unsigned int size)
{
    if (initialized == 0)
        tls_init();
    // If the thread already has a page mapping exit
    if (hash_exists(pthread_self()) == 0)
        return -1;
    // Divide up the size into each page
    int num_pages = 1 + (size - 1) / page_size; //round up number of pages
    // Initialize a tls and add to hash table
    TLS *tls = (TLS *)malloc(sizeof(TLS));
    tls->page_num = num_pages;
    tls->size = size;
    tls->pages = malloc(num_pages * sizeof(PAGE *));
    // Initalize page blocks based on the given size
    for (i = 0; i < num_pages; i++)
    {
        PAGE *page = malloc(sizeof(PAGE));
        page->address = (uintptr_t)mmap(0, page_size, 0, MAP_ANON | MAP_PRIVATE, 0, 0);
        page->ref_count = 1;
        tls->pages[i] = page;
    }
    hash_add(pthread_self(), tls);
    return 0;
}

int tls_write(unsigned int offset, unsigned int length, char *buffer)
{
    // error if tls does not exist
    if (hash_exists(pthread_self()) == -1)
        return -1;
    TLS *write_tls = hash_get(pthread_self());
    // error if buffer length goes over page with given offset
    if (offset + length > write_tls->size)
        return -1;
    // unprotect tls for writing
    for (i = 0; i < write_tls->page_num; i++)
        tls_unprotect(write_tls->pages[i]);

    // start writing buffer into pages
    int cnt, idx;
    for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx)
    {
        PAGE *p, *copy;
        unsigned int pn, poff;
        pn = idx / page_size;
        poff = idx % page_size;
        p = write_tls->pages[pn];
        if (p->ref_count > 1)
        { /* this page is shared, create a private copy (COW) */
            copy = (PAGE *)calloc(1, sizeof(PAGE));
            copy->address = (uintptr_t)mmap(0, page_size, PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
            copy->ref_count = 1;
            write_tls->pages[pn] = copy; /* update original page */
            memcpy((void *)copy->address, (void *)p->address, page_size);
            p->ref_count--;
            tls_protect(p);
            p = copy;
        }
        char *dst = ((char *)p->address) + poff;
        *dst = buffer[cnt];
    }
    // protect tls after writing
    for (i = 0; i < write_tls->page_num; i++)
        tls_protect(write_tls->pages[i]);

    return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
    // error if tls does not exist
    if (hash_exists(pthread_self()) == -1)
        return -1;
    TLS *read_tls = hash_get(pthread_self());
    // error if buffer length goes over page with given offset
    if (offset + length > read_tls->size)
        return -1;
    for (i = 0; i < read_tls->page_num; i++)
        tls_unprotect(read_tls->pages[i]);
    int cnt, idx;
    for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx)
    {
        PAGE *p;
        unsigned int pn, poff;
        pn = idx / page_size;
        poff = idx % page_size;
        p = read_tls->pages[pn];
        char *src = ((char *)p->address) + poff;
        buffer[cnt] = *src;
    }
    for (i = 0; i < read_tls->page_num; i++)
        tls_protect(read_tls->pages[i]);
    return 0;
}

int tls_clone(pthread_t tid)
{
    if (hash_exists(pthread_self()) == 0)
        return -1;
    if (hash_exists(tid) == -1)
        return -1;
    TLS *new_tls = (TLS *)malloc(sizeof(TLS));
    TLS *target_tls = hash_get(tid);
    new_tls->page_num = target_tls->page_num;
    new_tls->size = target_tls->size;
    new_tls->pages = malloc(new_tls->page_num * sizeof(PAGE *));
    hash_add(pthread_self(), new_tls);

    for (i = 0; i < new_tls->page_num; i++)
        new_tls->pages[i] = target_tls->pages[i];
    for (i = 0; i < target_tls->page_num; i++)
        (target_tls->pages[i]->ref_count)++;
    return 0;
}

int tls_destroy()
{
    if (hash_exists(pthread_self()) == -1)
        return -1;
    hash_remove(pthread_self());
    return 0;
}