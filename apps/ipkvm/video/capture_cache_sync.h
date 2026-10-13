#ifndef A133_CAPTURE_CACHE_SYNC_H
#define A133_CAPTURE_CACHE_SYNC_H

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

// A133 Tina 4.9 SUNXI_MEM allocates cached VIN MMAP buffers, but leaves
// vb2_dc_buf::dma_sgt unset; vb2_dc_finish therefore skips CPU cache sync.
// This ABI is the board kernel's sunxi_ion_uapi.h, not a portable V4L2 API.
class CaptureCacheSync {
public:
    CaptureCacheSync() : fd_(open("/dev/ion", O_RDWR | O_CLOEXEC)) {}
    ~CaptureCacheSync() { if (fd_ >= 0) close(fd_); }
    bool ready() const { return fd_ >= 0; }
    bool before_cpu_read(const void* address, size_t length) const {
        if (fd_ < 0) { errno = EBADF; return false; }
        const uintptr_t start = reinterpret_cast<uintptr_t>(address);
        if (!address || !length || length > UINTPTR_MAX - start) {
            errno = EINVAL;
            return false;
        }
        struct { unsigned long start, end; } range = {
            static_cast<unsigned long>(start),
            static_cast<unsigned long>(start + length)
        };
        int result;
        do {
            result = ioctl(fd_, 5 /* ION_IOC_SUNXI_FLUSH_RANGE */, &range);
        } while (result < 0 && errno == EINTR);
        return result == 0;
    }
private:
    CaptureCacheSync(const CaptureCacheSync&) = delete;
    CaptureCacheSync& operator=(const CaptureCacheSync&) = delete;
    int fd_;
};
#endif
