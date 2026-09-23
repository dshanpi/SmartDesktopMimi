#include <new>

/*
 * The A133 KWS archive is built with Arm GNU Toolchain 13, while Tina 5.0
 * uses GCC 8 and glibc 2.29.  Keep the archive on the board's native ABI by
 * providing the two newer runtime entry points it references.
 *
 * A zero value forces libstdc++'s shared_ptr implementation down its safe,
 * atomic path.  Older glibc does not expose or maintain this optimization
 * flag itself.
 */
extern "C" char __libc_single_threaded = 0;

extern "C" unsigned int __aarch64_ldadd4_acq_rel(
    unsigned int value, unsigned int *address)
{
    unsigned int previous;
    unsigned int updated;
    unsigned int status;

    /*
     * Do not implement this helper with __atomic_fetch_add().  Tina's GCC 8
     * lowers that builtin back to __aarch64_ldadd4_acq_rel when outline
     * atomics are enabled, turning the compatibility helper into an infinite
     * recursion.  The A133 is ARMv8.0, so use the baseline acquire/release
     * exclusive loop directly instead of requiring ARMv8.1 LSE instructions.
     */
    __asm__ __volatile__(
        "1: ldaxr %w0, [%3]\n"
        "add %w1, %w0, %w4\n"
        "stlxr %w2, %w1, [%3]\n"
        "cbnz %w2, 1b\n"
        : "=&r"(previous), "=&r"(updated), "=&r"(status)
        : "r"(address), "r"(value)
        : "memory");

    return previous;
}

namespace std {
[[noreturn]] void __throw_bad_array_new_length()
{
    throw bad_alloc();
}
}
