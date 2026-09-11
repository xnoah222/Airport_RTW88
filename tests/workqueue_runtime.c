/* Userspace substitutes for XNU primitives; linked only by offline tests. */
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
struct IOLock { pthread_mutex_t mutex; pthread_cond_t cond; };
void *IOLockAlloc(void) { struct IOLock *l=calloc(1,sizeof(*l)); pthread_mutex_init(&l->mutex,0); pthread_cond_init(&l->cond,0); return l; }
void IOLockFree(struct IOLock *l) { pthread_mutex_destroy(&l->mutex); pthread_cond_destroy(&l->cond); free(l); }
void IOLockLock(struct IOLock *l) { pthread_mutex_lock(&l->mutex); }
void IOLockUnlock(struct IOLock *l) { pthread_mutex_unlock(&l->mutex); }
int IOLockSleep(struct IOLock *l,void *event,unsigned mode) { (void)event;(void)mode;return pthread_cond_wait(&l->cond,&l->mutex); }
void IOLockWakeup(struct IOLock *l,void *event,int one) { (void)event;(void)one;pthread_cond_broadcast(&l->cond); }
void *IOMalloc(size_t n) { return malloc(n); }
void IOFree(void *p,size_t n) { (void)n;free(p); }
void IOSleep(unsigned n) { usleep(n*1000); }
void IOLog(const char *s,...) { va_list a;va_start(a,s);vfprintf(stderr,s,a);va_end(a); }
struct launch { void (*fn)(void *,int); void *arg; };
static void *launch(void *v) { struct launch a=*(struct launch *)v;free(v);a.fn(a.arg,0);return 0; }
int kernel_thread_start(void (*fn)(void *,int),void *arg,void **t) { struct launch *a=malloc(sizeof(*a));a->fn=fn;a->arg=arg;pthread_t p;int e=pthread_create(&p,0,launch,a);if(!e) { *t=(void *)p;pthread_detach(p); }return e; }
void thread_deallocate(void *t) { (void)t; }
void *current_thread(void) { return (void *)pthread_self(); }
void thread_terminate(void *t) { (void)t;pthread_exit(0); }
uint64_t mach_absolute_time(void) { return 0; }
void absolutetime_to_nanoseconds(uint64_t a,uint64_t *b) { *b=a; }

/* Deterministic native thread-call mock, for the wrapper test only. */
struct native_call { void (*fn)(void *,void *); void *arg; int queued; };
void *thread_call_allocate(void (*fn)(void *,void *),void *arg) { struct native_call *c=calloc(1,sizeof(*c));c->fn=fn;c->arg=arg;return c; }
int thread_call_enter(struct native_call *c) { return __atomic_exchange_n(&c->queued,1,__ATOMIC_SEQ_CST); }
int thread_call_enter_delayed(struct native_call *c,uint64_t d) { (void)d;return thread_call_enter(c); }
int thread_call_cancel(struct native_call *c) { return __atomic_exchange_n(&c->queued,0,__ATOMIC_SEQ_CST); }
int thread_call_free(struct native_call *c) { if(__atomic_load_n(&c->queued,__ATOMIC_SEQ_CST))return 0;free(c);return 1; }
static int mock_dequeued, mock_dispatch_allowed;
static void fire(void *v,int ignored) { (void)ignored;struct native_call *c=v;void (*fn)(void *,void *)=c->fn;void *arg=c->arg;__atomic_store_n(&mock_dequeued,1,__ATOMIC_SEQ_CST);while(!__atomic_load_n(&mock_dispatch_allowed,__ATOMIC_SEQ_CST))usleep(1000);fn(arg,0); }
void mock_fire(void *p) { struct native_call *c=p;__atomic_store_n(&c->queued,0,__ATOMIC_SEQ_CST);__atomic_store_n(&mock_dequeued,0,__ATOMIC_SEQ_CST);__atomic_store_n(&mock_dispatch_allowed,0,__ATOMIC_SEQ_CST);void *t;kernel_thread_start(fire,c,&t);while(!__atomic_load_n(&mock_dequeued,__ATOMIC_SEQ_CST))usleep(1000); }
void mock_allow_dispatch(void) { __atomic_store_n(&mock_dispatch_allowed,1,__ATOMIC_SEQ_CST); }
