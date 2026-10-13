// Host-only syscall seam: never opens a real camera or ION device.
#include "../../apps/ipkvm/video/capture_cache_sync.h"
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <cstdio>

static int open_result = 37, open_calls, close_calls, ioctl_calls;
static int ioctl_error, interrupted;
static unsigned long expected_start, expected_end;
extern "C" int __wrap_open(const char* path, int flags, ...) {
    assert(std::strcmp(path, "/dev/ion") == 0);
    assert((flags & O_RDWR) && (flags & O_CLOEXEC));
    ++open_calls;
    if (open_result < 0) errno = ENOENT;
    return open_result;
}
extern "C" int __wrap_close(int fd) {
    assert(fd == 37); ++close_calls; return 0;
}
extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
    assert(fd == 37 && request == 5);
    va_list args; va_start(args, request);
    const unsigned long* range = va_arg(args, const unsigned long*);
    assert(range[0] == expected_start && range[1] == expected_end);
    va_end(args); ++ioctl_calls;
    if (interrupted) { --interrupted; errno = EINTR; return -1; }
    if (ioctl_error) { errno = ioctl_error; return -1; }
    return 0;
}
int main() {
    expected_start = 0x10000000;
    expected_end = expected_start + 3133440;
    const void* frame = reinterpret_cast<const void*>(expected_start);
    {
        CaptureCacheSync sync;
        assert(sync.ready());
        assert(sync.before_cpu_read(frame, 3133440));
        interrupted = 1;
        assert(sync.before_cpu_read(frame, 3133440));
        assert(ioctl_calls == 3);
        ioctl_error = ENOTTY;
        assert(!sync.before_cpu_read(frame, 3133440) && errno == ENOTTY);
        const int calls = ioctl_calls;
        assert(!sync.before_cpu_read(nullptr, 3133440) && errno == EINVAL);
        assert(!sync.before_cpu_read(frame, 0) && errno == EINVAL);
        assert(!sync.before_cpu_read(reinterpret_cast<void*>(UINTPTR_MAX - 7), 16));
        assert(ioctl_calls == calls);
    }
    assert(close_calls == 1);
    open_result = -1;
    {
        CaptureCacheSync missing;
        assert(!missing.ready());
        assert(!missing.before_cpu_read(frame, 3133440) && errno == EBADF);
    }
    assert(close_calls == 1 && open_calls == 2);
    puts("PASS: exact range ABI, success, EINTR retry, unsupported ioctl, invalid ranges, missing ION, descriptor cleanup");
}
