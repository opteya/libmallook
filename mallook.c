/* libmallook: wrapper for allocation functions
 *
 * Copyright (C) 2019 OPTEYA SAS
 * Copyright (C) 2019 Yann Droneaud <ydroneaud@opteya.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char mallook_buffer[16 * 1024] = "@ Start\n";
static size_t mallook_buffer_cursor = 8; // strlen("@ Start\n")

static pthread_mutex_t mallook_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t mallook_init_once = PTHREAD_ONCE_INIT;
static pthread_once_t mallook_fini_once = PTHREAD_ONCE_INIT;
static atomic_bool    mallook_init_done = ATOMIC_VAR_INIT(false);

static char mallook_prefix[PATH_MAX];
static char mallook_filename[PATH_MAX];

static int mallook_fd = -1;

static void mallook_abort(const char *msg) __attribute__((noreturn));
static void mallook_abort(const char *msg)
{
	write(STDERR_FILENO, "mallook abort: ", 15);
	write(STDERR_FILENO, msg, strlen(msg));
	write(STDERR_FILENO, "\n", 1);

	abort();
}

// exec functions are wrapped to enforce a buffer flush
static int (*__next_execve)(const char *filename, char *const argv[],
			    char *const envp[]);
static int (*__next_execveat)(int dirfd, const char *pathname,
			      char *const argv[], char *const envp[],
			      int flags);
static int (*__next_fexecve)(int fd, char *const argv[], char *const envp[]);
static int (*__next_execv)(const char *path, char *const argv[]);
static int (*__next_execvp)(const char *file, char *const argv[]);
static int (*__next_execvpe)(const char *file, char *const argv[],
			     char *const envp[]);
static int (*__next_execl)(const char *path, const char *arg, ...);
static int (*__next_execlp)(const char *file, const char *arg, ...);
static int (*__next_execle)(const char *path, const char *arg, ...);

// allocation functions are wrapped for tracing purpose
static void *(*__next_malloc)(size_t s);
static void *(*__next_realloc)(void *p, size_t s);
static void *(*__next_calloc)(size_t n, size_t s);
static void *(*__next_reallocarray)(void *p, size_t n, size_t s);
static int (*__next_posix_memalign)(void **memptr, size_t alignment, size_t size);
static void *(*__next_aligned_alloc)(size_t alignment, size_t size);
static void *(*__next_valloc)(size_t size);
static void *(*__next_memalign)(size_t alignment, size_t size);
static void *(*__next_pvalloc)(size_t size);

static void mallook_resolve(void)
{
	// BEWARE dlsym() will trigger call to malloc().
	// So wrapper for malloc() must be prepared to
	// cope with this (hence the ugly use of glibc
	// internal symbols)

	__next_execve   = dlsym(RTLD_NEXT, "execve");
	__next_execveat = dlsym(RTLD_NEXT, "execveat");
	__next_fexecve  = dlsym(RTLD_NEXT, "fexecve");
	__next_execv    = dlsym(RTLD_NEXT, "execv");
	__next_execvp   = dlsym(RTLD_NEXT, "execvp");
	__next_execvpe  = dlsym(RTLD_NEXT, "execvpe");
	__next_execl    = dlsym(RTLD_NEXT, "execl");
	__next_execlp   = dlsym(RTLD_NEXT, "execlp");
	__next_execle   = dlsym(RTLD_NEXT, "execle");

	__next_malloc         = dlsym(RTLD_NEXT, "malloc");
	__next_realloc        = dlsym(RTLD_NEXT, "realloc");
	__next_calloc         = dlsym(RTLD_NEXT, "calloc");
	__next_reallocarray   = dlsym(RTLD_NEXT, "reallocarray");
	__next_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
	__next_aligned_alloc  = dlsym(RTLD_NEXT, "aligned_alloc");
	__next_valloc         = dlsym(RTLD_NEXT, "valloc");
	__next_memalign       = dlsym(RTLD_NEXT, "memalign");
	__next_pvalloc        = dlsym(RTLD_NEXT, "pvalloc");
}

static void append_char(char **buffer, size_t *size, char c)
{
	if (*size < 2)
		mallook_abort("string truncation");

	*(*buffer)++ = c;
	*(*buffer) = '\0';

	(*size)--;
}

static void append_str(char **buffer, size_t *size, const char *s)
{
        while (*s)
		append_char(buffer, size, *s++);
}

static void append_uint(char **buffer, size_t *size, uintmax_t u)
{
	if (u == 0)
	{
		append_char(buffer, size, '0');

		return;
	}

	char tmp[256];
	unsigned int i;

	for (i = 0; u; i++)
	{
		if (i >= sizeof(tmp))
			mallook_abort("conversion overflow");

		tmp[i] = '0' + (u % 10);
		u /= 10;
	}

	for (; i; i--)
		append_char(buffer, size, tmp[i - 1]);
}

static void append_int(char **buffer, size_t *size, intmax_t v)
{
	uintmax_t u;

	if (v >= 0)
	{
		u = (uintmax_t)v;
	}
	else
	{
		u = -(uintmax_t)v;

		append_char(buffer, size, '-');
	}

	append_uint(buffer, size, u);
}

static bool mallook_initialized(void)
{
	return atomic_load(&mallook_init_done);
}

static void mallook_open(void)
{
	unsigned int tries = 0;
	unsigned int i;

	for (i = 0; i < INT_MAX; i++)
	{
		char *p = mallook_filename;
		size_t s = sizeof(mallook_filename);

		append_str(&p, &s, mallook_prefix);
		append_char(&p, &s, '.');
		append_int(&p, &s, (intmax_t)getpid());
		append_char(&p, &s, '.');
		append_int(&p, &s, (intmax_t)time(NULL));
		append_char(&p, &s, '.');
		append_uint(&p, &s, (uintmax_t)tries);

		mallook_fd = open(mallook_filename,
				  (O_WRONLY|O_APPEND|
				   O_NOCTTY|O_CLOEXEC|
				   O_TRUNC|O_CREAT|O_EXCL),
				  (S_IRUSR|S_IWUSR|
				   S_IRGRP|S_IWGRP|
				   S_IROTH|S_IWOTH));
		if (mallook_fd >= 0)
			return;

		if (errno == EINTR)
			continue;

		if (errno == EEXIST)
		{
			tries ++;
			continue;
		}

		mallook_abort("open() failure");
	}

	mallook_abort("can't create file with uniq name");
}

static void mallook_reopen(void)
{
	mallook_fd = open(mallook_filename,
			  (O_WRONLY|O_APPEND|
			   O_NOCTTY|O_CLOEXEC));
	if (mallook_fd < 0)
		mallook_abort("open() failure");
}

static void _mallook_flush_unlocked(void)
{
	char *buffer = mallook_buffer;
	size_t size = mallook_buffer_cursor;

	if (!mallook_initialized())
		mallook_abort("can't flush buffer");

	while (size) {

		ssize_t r = write(mallook_fd, buffer, size);
		if (r < 0)
		{
			if (errno == EINTR)
				continue;

			// cope with snake eating our file descriptor after fork()
			if (errno == EBADF)
			{
				mallook_reopen();
				continue;
			}

			mallook_abort("failed to flush");
		}

		buffer += r;
		size -= r;
	}

	mallook_buffer_cursor = 0;
}

static void mallook_flush(void)
{
	pthread_mutex_lock(&mallook_mutex);

	_mallook_flush_unlocked();

	pthread_mutex_unlock(&mallook_mutex);
}

static void mallook_print(const char *str)
{
	pthread_mutex_lock(&mallook_mutex);

	while (*str)
	{

		size_t avail;
		char *buf;

		avail = sizeof(mallook_buffer) - mallook_buffer_cursor;
		buf = &mallook_buffer[mallook_buffer_cursor];

		while (avail && *str)
		{
			*buf ++ = *str++;
			avail --;
		}

		mallook_buffer_cursor = sizeof(mallook_buffer) - avail;

		if (!avail)
			_mallook_flush_unlocked();
	}

	pthread_mutex_unlock(&mallook_mutex);
}

static void _mallook_fini(void)
{
	mallook_print("@ End\n");
	mallook_flush();

	close(mallook_fd);
}

static void mallook_fini(void)
{
	pthread_once(&mallook_fini_once, _mallook_fini);
}

static void mallook_atexit(void)
{
	mallook_fini();
}

static void mallook_atfork_prepare(void)
{
	mallook_print("@ Forking\n");
	mallook_flush();

	// prevent other threads from leaving the mutex
	// locked in the child process
	pthread_mutex_lock(&mallook_mutex);
}

static void mallook_atfork_parent(void)
{
	// unlock mutex locked in prepare
	pthread_mutex_unlock(&mallook_mutex);

	mallook_print("@ Forked\n");
	mallook_flush();
}

// in a multithread process, only async safe functions
// are allowed here
static void mallook_atfork_child(void)
{
	// unlock mutex locked in prepare
	// not async safe
	pthread_mutex_unlock(&mallook_mutex);

	// don't flush in the parent file
	close(mallook_fd);

	// open a new file for the child
	mallook_open();

	// not async safe
	mallook_print("@ Restart\n");
	mallook_flush();
}

static void _mallook_init(void)
{
	const char *prefix = getenv("MALLOOK_PREFIX");
	if (!prefix)
		mallook_abort("missing MALLOOK_PREFIX=");

	char *p = mallook_prefix;
	size_t s = sizeof(mallook_prefix);

	append_str(&p, &s, prefix);

	// open log file
	mallook_open();

	// start of non async safe portion:
	// in particular malloc() can be used indirectly

	if (atexit(mallook_atexit) != 0)
		mallook_abort("atexit() failure");

	int err = pthread_atfork(mallook_atfork_prepare,
				 mallook_atfork_parent,
				 mallook_atfork_child);
	if (err)
		mallook_abort("atexit() failure");

	mallook_resolve();

	// everything is ready

	atomic_store(&mallook_init_done, true);

	mallook_flush();
}

static void mallook_init(void)
{
	pthread_once(&mallook_init_once, _mallook_init);
}

static void mallook_constructor(void) __attribute__((constructor));
static void mallook_constructor(void)
{
	mallook_init();
}

static void mallook_destructor(void) __attribute__((destructor));
static void mallook_destructor(void)
{
	mallook_fini();
}

static void mallook_print_exec(void)
{
	mallook_print("@ Possible End, Ready to exec\n");
	mallook_flush();
}

int execve(const char *filename, char *const argv[],
	   char *const envp[])
{
	mallook_init();

	mallook_print_exec();

	return __next_execve(filename, argv, envp);
}

int execveat(int dirfd, const char *pathname,
	     char *const argv[], char *const envp[],
	     int flags)
{
	mallook_init();

	mallook_print_exec();

	return __next_execveat(dirfd, pathname, argv, envp, flags);
}

int fexecve(int fd, char *const argv[], char *const envp[])
{
	mallook_init();

	mallook_print_exec();

	return __next_fexecve(fd, argv, envp);
}

int execv(const char *path, char *const argv[])
{
	mallook_init();

	mallook_print_exec();

	return __next_execv(path, argv);
}

int execvp(const char *file, char *const argv[])
{
	mallook_init();

	mallook_print_exec();

	return __next_execvp(file, argv);
}

int execvpe(const char *file, char *const argv[],
	    char *const envp[])
{
	mallook_init();

	mallook_print_exec();

	return __next_execvpe(file, argv, envp);
}

int execl(const char *path, const char *arg, ...)
{
	va_list args;
	size_t count;

	va_start(args, arg);
	for(count = 0; va_arg(args, char *); count ++);
	va_end(args);

	char *argv[count];

	va_start(args, arg);
	for (size_t i = 0; i < count; i++)
		argv[i] = va_arg(args, char *);
	va_end(args);

	return execv(path, argv);
}

int execlp(const char *file, const char *arg, ...)
{
	va_list args;
	size_t count;

	va_start(args, arg);
	for(count = 0; va_arg(args, char *); count ++);
	va_end(args);

	char *argv[count];

	va_start(args, arg);
	for (size_t i = 0; i < count; i++)
		argv[i] = va_arg(args, char *);
	va_end(args);

	return execvp(file, argv);
}

int execle(const char *path, const char *arg, ... /*, char * const envp[] */)
{
	va_list args;
	size_t count;
	char * const *envp;

	va_start(args, arg);
	for(count = 0; va_arg(args, char *); count ++);
	envp = va_arg(args, char *const *);
	va_end(args);

	char *argv[count];

	va_start(args, arg);
	for (size_t i = 0; i < count; i++)
		argv[i] = va_arg(args, char *);
	va_end(args);

	return execve(path, argv, envp);
}

// report an allocation size
static void mallook_print_alloc(const char *func, size_t size)
{
	char msg[256];
	char *p = msg;
	size_t s = sizeof(msg);

	append_str(&p, &s, func);
	append_char(&p, &s, ' ');
	append_uint(&p, &s, (uintmax_t)size);
	append_char(&p, &s, '\n');

	mallook_print(msg);
}

// UGLY this part is hooking into GNU C library internal symbols
// UGLY it's not portable, it's not maintainable, ABI can change anytime
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void *__libc_memalign(size_t, size_t);
extern void *__libc_valloc(size_t);
extern void *__libc_pvalloc(size_t);

void *malloc(size_t s)
{
	mallook_print_alloc("malloc", s);

	if (!mallook_initialized())
		return __libc_malloc(s);

	return __next_malloc(s);
}

void *realloc(void *p, size_t s)
{
	mallook_print_alloc("realloc", s);

	if (!mallook_initialized())
		return __libc_realloc(p, s);

	return __next_realloc(p, s);
}

void *calloc(size_t n, size_t s)
{
	mallook_print_alloc("calloc",
			    (uintmax_t)n * s);

	if (!mallook_initialized())
		return __libc_calloc(n, s);

	return __next_calloc(n, s);
}

void *aligned_alloc(size_t alignment, size_t size)
{
	mallook_print_alloc("aligned_alloc", size);

	if (!mallook_initialized())
		return __libc_memalign(alignment, size);

	return __next_aligned_alloc(alignment, size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	mallook_print_alloc("posix_memalign", size);

	if (!mallook_initialized())
	{
		int err_previous = errno;
		void *ptr = __libc_memalign(alignment, size);
		int err_after = errno;
		errno = err_previous;
		if (!ptr)
			return err_after;

		*memptr = ptr;

		return 0;
	}

	return __next_posix_memalign(memptr, alignment, size);
}

void *memalign(size_t alignment, size_t size)
{
	mallook_print_alloc("memalign", size);

	if (!mallook_initialized())
		return __libc_memalign(alignment, size);

	return __next_memalign(alignment, size);
}

void *valloc(size_t size)
{
	mallook_print_alloc("valloc", size);

	if (!mallook_initialized())
		return __libc_valloc(size);

	return __next_valloc(size);
}

void *pvalloc(size_t size)
{
	mallook_print_alloc("pvalloc", size);

	if (!mallook_initialized())
		return __libc_pvalloc(size);

	return __next_pvalloc(size);
}

void *reallocarray(void *p, size_t n, size_t s)
{
	mallook_print_alloc("reallocarray",
			    (uintmax_t)n * (uintmax_t)s);

	if (!mallook_initialized())
		return __libc_realloc(p, n * s);

	return __next_reallocarray(p, n, s);
}
