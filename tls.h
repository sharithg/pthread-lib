#ifndef TLS_H
#define TLS_H

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#define HASH_SIZE 20

typedef struct page
{
    uintptr_t address; /* start address of page */
    int ref_count;     /* counter for shared pages */
} PAGE;

typedef struct thread_local_storage
{
    unsigned int size;     /* size in bytes */
    unsigned int page_num; /* number of pages */
    struct page **pages;   /* array of pointers to pages */
} TLS;

typedef struct hash_element
{
    pthread_t tid;
    TLS *tls;
    struct hash_element *next;
} HE;

// hash functions
void hash_init();
void hash_add(pthread_t tid, TLS *add_tls);
TLS *hash_get(pthread_t tid);
int hash_exists(pthread_t tid);
void hash_remove(pthread_t tid);
// tls functions
void tls_init();
int tls_create(unsigned int size);
int tls_write(unsigned int offset, unsigned int length, char *buffer);
int tls_read(unsigned int offset, unsigned int length, char *buffer);
int tls_destroy();
int tls_clone(pthread_t tid);
void tls_handle_page_fault(int sig, siginfo_t *si, void *context);
void tls_protect(PAGE *p);
void tls_unprotect(PAGE *p);

#endif