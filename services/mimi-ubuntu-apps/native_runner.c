#define _GNU_SOURCE
/* Ubuntu platform execution primitive. The broker must verify the complete
 * signed installation and native ABI profile BEFORE passing the executable FD.
 * This program is not an installer or a signature/compatibility bypass. */
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <dirent.h>
#include <limits.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error "Ubuntu native runner requires Linux x86_64; other targets are not admitted"
#endif

#define LOAD(field) BPF_STMT(BPF_LD|BPF_W|BPF_ABS, offsetof(struct seccomp_data, field))
#define ACCEPT BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW)
#define REFUSE BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRNO|EPERM)
#define SIMPLE(name) BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_##name, 0, 1), ACCEPT
/* Each special syscall block ends in ACCEPT/REFUSE and cannot fall through
 * with an argument value accidentally treated as another syscall number. */
static int restrict_calls(void)
{
    const struct sock_filter code[] = {
        LOAD(arch),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL_PROCESS),
        LOAD(nr),
        /* Reserve code and the sealed listener-transfer channel. */
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_close, 0, 5),
        LOAD(args[0]),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 3, 1, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 4, 0, 1), REFUSE, ACCEPT,
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_execveat, 0, 1),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_USER_NOTIF),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_sendmsg, 0, 4),
        LOAD(args[0]),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 4, 0, 1), ACCEPT, REFUSE,
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_connect, 0, 4),
        LOAD(args[0]),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 4, 0, 1), REFUSE, ACCEPT,
        /* O_TMPFILE includes O_DIRECTORY: test its distinct high flag bit. */
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_openat, 0, 4),
        LOAD(args[2]),
        BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,
                 O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND|(O_TMPFILE & ~O_DIRECTORY), 0, 1),
        REFUSE, ACCEPT,
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_socket, 0, 9),
        LOAD(args[0]),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, AF_UNIX, 0, 5),
        LOAD(args[1]),
        BPF_STMT(BPF_ALU|BPF_AND|BPF_K, ~(SOCK_CLOEXEC|SOCK_NONBLOCK)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SOCK_STREAM, 0, 2),
        LOAD(args[2]),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 0, 1, 0), REFUSE, ACCEPT,
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_fcntl, 0, 5),
        LOAD(args[1]),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, F_GETFD, 2, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, F_GETFL, 1, 0), REFUSE, ACCEPT,
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_prlimit64, 0, 8),
        LOAD(args[0]),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 0, 0, 5),
        LOAD(args[2]),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 0, 0, 3),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS, offsetof(struct seccomp_data, args[2]) + 4),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 0, 0, 1), ACCEPT, REFUSE,
        SIMPLE(read), SIMPLE(pread64), SIMPLE(write), SIMPLE(readv), SIMPLE(writev),
        SIMPLE(fstat), SIMPLE(newfstatat), SIMPLE(stat), SIMPLE(lstat),
        SIMPLE(access), SIMPLE(lseek), SIMPLE(readlink), SIMPLE(readlinkat),
        SIMPLE(mmap), SIMPLE(mprotect), SIMPLE(munmap), SIMPLE(brk),
        SIMPLE(mremap), SIMPLE(madvise),
        SIMPLE(rt_sigaction), SIMPLE(rt_sigprocmask), SIMPLE(rt_sigreturn), SIMPLE(sigaltstack),
        SIMPLE(futex), SIMPLE(clock_gettime), SIMPLE(gettimeofday), SIMPLE(time),
        SIMPLE(nanosleep), SIMPLE(clock_nanosleep), SIMPLE(getrandom),
        SIMPLE(getpid), SIMPLE(getppid), SIMPLE(gettid),
        SIMPLE(getuid), SIMPLE(geteuid), SIMPLE(getgid), SIMPLE(getegid),
        SIMPLE(arch_prctl), SIMPLE(set_tid_address), SIMPLE(set_robust_list),
#ifdef SYS_rseq
        SIMPLE(rseq),
#endif
        SIMPLE(getrlimit), SIMPLE(uname), SIMPLE(sched_yield),
        SIMPLE(getsockopt), SIMPLE(shutdown),
        SIMPLE(poll), SIMPLE(ppoll), SIMPLE(select), SIMPLE(pselect6),
        SIMPLE(exit), SIMPLE(exit_group),
        REFUSE
    };
    struct sock_fprog program = { .len = sizeof(code)/sizeof(code[0]),
                                 .filter = (struct sock_filter *)code };
    return syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER,
                   SECCOMP_FILTER_FLAG_NEW_LISTENER, &program);
}

/* CONTINUE is used ONCE, while our single-threaded trusted stub is executing.
 * It is never used for a notification produced by native application code:
 * pathname pointers are not security checks (execveat absolute paths ignore fd).
 * The monitor holds no application parser, package writer or data operation. */
static volatile sig_atomic_t monitored_pid;
static void deadline(int sig)
{
    (void)sig;
    if (monitored_pid > 0) {
        kill(monitored_pid, SIGKILL);
        while (waitpid(monitored_pid, NULL, 0) < 0 && errno == EINTR) {}
    }
    _exit(124);
}

static int send_listener(int fd)
{
    char byte = 'L';
    union { struct cmsghdr align; char data[CMSG_SPACE(sizeof(int))]; } control = {0};
    struct iovec iov = { &byte, 1 };
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1,
                         .msg_control = control.data, .msg_controllen = sizeof(control.data) };
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &fd, sizeof(fd));
    return sendmsg(4, &msg, MSG_NOSIGNAL) == 1 ? 0 : -1;
}

static int monitor(int channel, pid_t child)
{
    monitored_pid = child;
    struct sigaction sa = { .sa_handler = deadline };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGALRM, &sa, NULL)) goto fail;
    alarm(5); /* Also bounds a notification ioctl interrupted by child exit. */
    close(3); close(1);
    struct pollfd p = { .fd = channel, .events = POLLIN };
    if (poll(&p, 1, 2000) != 1 || !(p.revents & POLLIN)) goto fail;
    char byte;
    union { struct cmsghdr align; char data[CMSG_SPACE(sizeof(int))]; } control = {0};
    struct iovec iov = { &byte, 1 };
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1,
                         .msg_control = control.data, .msg_controllen = sizeof(control.data) };
    if (recvmsg(channel, &msg, MSG_CMSG_CLOEXEC|MSG_DONTWAIT) != 1 ||
        byte != 'L' || (msg.msg_flags & (MSG_CTRUNC|MSG_TRUNC))) goto fail;
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    if (!c || c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS ||
        c->cmsg_len != CMSG_LEN(sizeof(int))) goto fail;
    int listener;
    memcpy(&listener, CMSG_DATA(c), sizeof(listener));
    close(channel); /* App inherits an un-reusable fd4 with no surviving peer. */
    p.fd = listener;
    int first = 1;
    for (;;) {
        int status;
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            alarm(0); close(listener);
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status);
        }
        if (done < 0 && errno != EINTR) goto fail;
        int ready = poll(&p, 1, 50);
        if (ready < 0) { if (errno == EINTR) continue; goto fail; }
        if (!ready || !(p.revents & POLLIN)) continue;
        struct seccomp_notif req = {0};
        if (ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, &req)) {
            if (errno == EINTR || errno == ENOENT) continue;
            goto fail;
        }
        struct seccomp_notif_resp resp = { .id = req.id, .error = -EPERM };
        if (first && req.pid == (unsigned)child && req.data.arch == AUDIT_ARCH_X86_64 &&
            req.data.nr == SYS_execveat && req.data.args[0] == 3 &&
            req.data.args[4] == AT_EMPTY_PATH) {
            resp.error = 0; resp.flags = SECCOMP_USER_NOTIF_FLAG_CONTINUE;
        }
        first = 0; /* Even a canceled/failed initial notification cannot retry. */
        if (ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp) && errno != ENOENT)
            goto fail;
    }
fail:
    kill(child, SIGKILL);
    while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
    return 126;
}

static int limit(int which, rlim_t value)
{
    struct rlimit r = { value, value };
    return setrlimit(which, &r);
}

static int close_unneeded(void)
{
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return -1;
    struct dirent *item;
    while ((item = readdir(dir))) {
        char *end;
        long fd = strtol(item->d_name, &end, 10);
        if (!*end && fd > 3 && fd != dirfd(dir)) close((int)fd);
    }
    return closedir(dir);
}

int main(int argc, char **argv)
{
    if (geteuid() != 0 || getuid() != 0 || argc != 7 ||
        strcmp(argv[1], "--executable-fd") || strcmp(argv[3], "--action") ||
        strcmp(argv[5], "--socket")) return 125;
    char *end;
    errno = 0;
    long input = strtol(argv[2], &end, 10);
    if (errno || *end || input < 3 || input > INT_MAX) return 125;
    size_t n = strlen(argv[4]);
    if (!n || n > 64 || !((argv[4][0] >= 'A' && argv[4][0] <= 'Z') ||
                         (argv[4][0] >= 'a' && argv[4][0] <= 'z'))) return 125;
    for (size_t i=0; i<n; ++i) {
        char c=argv[4][i];
        if (!((c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') ||
               c=='.' || c=='_' || c=='-')) return 125;
    }
    if (argv[6][0] != '/' || strlen(argv[6]) >= 108) return 125;
    pid_t parent = getppid();
    if (parent <= 1) return 125;
    struct stat output;
    /* Never give the low-privilege child an already-open privileged file. */
    if (fstat(1, &output) || !S_ISFIFO(output.st_mode)) return 125;
    int flags = fcntl((int)input, F_GETFL);
    if (flags < 0 || (flags & O_ACCMODE) != O_RDONLY) return 125;
    struct stat st;
    Elf64_Ehdr header;
    if (fstat((int)input, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
        (st.st_mode & (0022|S_ISUID|S_ISGID)) || !(st.st_mode & 0111) ||
        st.st_nlink != 1 || st.st_size < (off_t)sizeof(header) ||
        pread((int)input, &header, sizeof(header), 0) != sizeof(header) ||
        memcmp(header.e_ident, "\177ELF\002\001\001", 7) ||
        header.e_machine != EM_X86_64 || header.e_version != EV_CURRENT ||
        (header.e_type != ET_EXEC && header.e_type != ET_DYN) || !header.e_entry)
        return 125;
    if (input != 3 && dup2((int)input, 3) != 3) return 125;
    if (fcntl(3, F_SETFD, 0) || close_unneeded()) return 125;
    int nullfd = open("/dev/null", O_RDWR|O_CLOEXEC);
    if (nullfd < 0 || dup2(nullfd, 0) != 0 || dup2(nullfd, 2) != 2) return 125;
    if (nullfd > 3) close(nullfd);
    if (clearenv() || setenv("AITVBOX_APP_SOCKET", argv[6], 1) ||
        setenv("LANG", "C", 1) || chdir("/")) return 125;
    if (prctl(PR_SET_DUMPABLE, 0) || prctl(PR_SET_PDEATHSIG, SIGKILL) ||
        getppid() != parent) return 125;
    int channels[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET|SOCK_CLOEXEC, 0, channels)) return 125;
    pid_t monitor_pid = getpid();
    pid_t child = fork();
    if (child < 0) return 125;
    if (child > 0) { close(channels[1]); return monitor(channels[0], child); }
    close(channels[0]);
    if (dup2(channels[1], 4) != 4 || fcntl(4, F_SETFD, 0)) return 126;
    if (channels[1] != 4) close(channels[1]);
    if (setgroups(0, NULL) || setgid(65534) || setuid(65534) ||
        limit(RLIMIT_CPU, 2) || limit(RLIMIT_AS, 128*1024*1024) ||
        limit(RLIMIT_FSIZE, 0) || limit(RLIMIT_NPROC, 1) ||
        limit(RLIMIT_NOFILE, 32) || limit(RLIMIT_CORE, 0) ||
        prctl(PR_SET_DUMPABLE, 0) || prctl(PR_SET_PDEATHSIG, SIGKILL) ||
        getppid() != monitor_pid || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) return 126;
    int listener = restrict_calls();
    if (listener < 0 || send_listener(listener)) return 126;
    close(listener);
    char *child_argv[] = { "mimi-native-app", "--action", argv[4], NULL };
    extern char **environ;
    syscall(SYS_execveat, 3, "", child_argv, environ, AT_EMPTY_PATH);
    return 127;
}
