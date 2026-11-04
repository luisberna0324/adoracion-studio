/* Linux/glibc test-only LD_PRELOAD audit. Enabled on the calling render
 * thread between begin/end markers. Never linked into PlayWorship.
 * The benchmark verifies deliberate allocation/free/mutex calls first. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <dlfcn.h>
#include <pthread.h>

/* Avoid recursively resolving an allocator through dlsym during startup. */
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void __libc_free(void *);

static __thread int watching;
static __thread uint64_t allocations, frees, locks;
static int (*real_mutex_lock)(pthread_mutex_t *);

__attribute__((constructor)) static void init(void) {
  real_mutex_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");
}

void playworship_rt_audit_begin(void) {
  allocations = frees = locks = 0;
  watching = 1;
}

void playworship_rt_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) {
  watching = 0;
  *a = allocations;
  *f = frees;
  *l = locks;
}

void *malloc(size_t size) {
  if (watching) ++allocations;
  return __libc_malloc(size);
}

void *calloc(size_t count, size_t size) {
  if (watching) ++allocations;
  return __libc_calloc(count, size);
}

void *realloc(void *pointer, size_t size) {
  if (watching) ++allocations;
  return __libc_realloc(pointer, size);
}

void free(void *pointer) {
  if (watching && pointer) ++frees;
  __libc_free(pointer);
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
  if (watching) ++locks;
  return real_mutex_lock(mutex);
}
