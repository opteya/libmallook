# libmallook - malloc()-hook

libmallook is a wrapper around various allocation functions, such as `malloc()`, `calloc()`, and `realloc()`, to be used to report every allocations done by a program and its children.

libmallook is intended to be injected into the target process with `LD_PRELOAD`.

[libmtraceall] was initially developed for enabling [GNU C Library][glibc]'s [mtrace()] in a process and all its children to gather allocation statistics.

Unfortunately [mtrace()] is
- not exec() aware, producing empty file for the process calling exec()
- really slow, see [bug #24561]

Additionally, [libmtraceall] is not `fork()` aware, not creating a new filename for the child process. It's both a limitation of [mtrace()] and [libmtraceall].

So if one is only interested in allocation sizes, using [mtrace()], even with [libmtraceall], is not the best tool.

A fast replacement was needed, and libmallook was developed as this replacement for [libmtraceall].

## Usage

```
$ make
$ LD_PRELOAD=./libmallook.so MALLOOK_PREFIX=mallook.out nm mallook.o
$ ls mallook.out.*.*.*
mallook.out.23064.1558374285.0
```

The generated filenames are built from the filename prefix in `MALLOOK_PREFIX` variable, process identifier (PID), Unix epoch timestamp, and a counter to cope with `exec()`'ed program having the same PID, or for other process reusing the PID.

## `fork()` safety

On `fork()`, libmallook open a new file.

## Thread safety

libmallook should be thread-safe.

## `fork()` and threads safety

libmallook is not safe in a process with multiple threads with one thread calling `fork()`, but it was carefully designed to work in such process, at least with current [glibc] version.

## *BEWARE*

libmallook is using internal [glibc] symbols, those starting for `__libc_`, to access the allocators before it's fully initialized. It should *never* be done. This is not something one should be be proud of. Unfortunately there's no way to avoid it, as calling `dlsym()` to retrieve address of a symbol is triggering a call to `malloc()`. So using `dlsym()` in libmallook's `malloc()` to retrieve glibc's `malloc()` is never going to work ...

## Hooks

It should be noted existing [glibc] allocator [hook infrastructure][hook] is deprecated.

[libmtraceall]: https://github.com/opteya/libmtraceall/
[glibc]:        https://www.gnu.org/software/libc/ "GNU C library"
[mtrace()]:     https://www.gnu.org/software/libc/manual/html_node/Allocation-Debugging.html "Allocation Debugging"
[bug #24561]:   https://sourceware.org/bugzilla/show_bug.cgi?id=24561 "Bug 24561 - _dl_addr: linear lookup inefficient, making mtrace() feature very slow"
[hook]:         http://www.gnu.org/software/libc/manual/html_node/Hooks-for-Malloc.html "Memory Allocation Hooks"
