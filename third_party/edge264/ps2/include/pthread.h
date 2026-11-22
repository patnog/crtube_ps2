#ifndef CRTUBE_EDGE264_PS2_PTHREAD_H
#define CRTUBE_EDGE264_PS2_PTHREAD_H

#include <sys/types.h>

/* PS2 newlib already declares pthread_t, pthread_mutex_t and pthread_cond_t
   through sys/types.h. Reuse those ABI types instead of redeclaring them.
   edge264 is configured with n_threads=0; these stubs are never used for
   synchronization. pthread_create fails to reject accidental threaded use. */

static inline int pthread_mutex_init(pthread_mutex_t *m, const void *attr)
{ (void)m; (void)attr; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m)
{ (void)m; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m)
{ (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m)
{ (void)m; return 0; }
static inline int pthread_cond_init(pthread_cond_t *c, const void *attr)
{ (void)c; (void)attr; return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c)
{ (void)c; return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c)
{ (void)c; return 0; }
static inline int pthread_cond_signal(pthread_cond_t *c)
{ (void)c; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{ (void)c; (void)m; return 0; }
static inline int pthread_create(pthread_t *t, const void *attr,
                                 void *(*start)(void *), void *arg)
{ (void)t; (void)attr; (void)start; (void)arg; return -1; }
static inline int pthread_join(pthread_t t, void **value)
{ (void)t; (void)value; return 0; }

#endif
