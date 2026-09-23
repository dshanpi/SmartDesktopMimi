#define _GNU_SOURCE

#include <json-c/json.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#include "safe_io.h"

#ifndef APP_SOCKET
#define APP_SOCKET "/var/run/aitvbox/apps.sock"
#endif
#ifndef APP_ROOT
#define APP_ROOT "/overlay/aitvbox/apps"
#endif
#ifndef MCP_PATH
#define MCP_PATH "/usr/bin/aitvbox-mcpd"
#endif
#ifndef APP_UID
#define APP_UID 65534
#endif
#ifndef APP_GID
#define APP_GID 65534
#endif
#ifndef APP_OWNER_UID
#define APP_OWNER_UID 0
#endif

#define REQUEST_MAX 8192
#define APP_OUTPUT_MAX 4096
#define MCP_OUTPUT_MAX (12 * 1024 * 1024)
#define APP_TIMEOUT_MS 5000
#define APP_STORAGE_VALUE_MAX 4096
#ifndef APP_CLIENT_TIMEOUT_MS
#define APP_CLIENT_TIMEOUT_MS 5000
#endif
#ifndef APP_MAX_WORKERS
#define APP_MAX_WORKERS 8
#endif
#ifndef APP_BUSY_GRACE_MS
#define APP_BUSY_GRACE_MS 25
#endif

static volatile sig_atomic_t shutdown_requested;

struct app_policy {
    char id[97];
    char executable[512];
    bool native;
    bool capture;
    bool keyboard;
    bool pointer;
    bool gpio_read;
    bool gpio_write;
    bool i2c;
    bool spi;
    bool uart;
    bool usb;
    bool network_https;
    bool storage;
};

static void signal_handler(int signal_number)
{
    (void)signal_number;
    shutdown_requested = 1;
}

static int write_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t count = write(fd, cursor, length);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

static bool valid_app_id(const char *value)
{
    size_t length = value ? strlen(value) : 0;
    if (length < 3 || length > 96 || value[0] < 'a' || value[0] > 'z')
        return false;
    bool dot = false;
    bool segment_start = true;
    for (size_t i = 0; i < length; i++) {
        char c = value[i];
        if (c == '.') {
            if (segment_start || i + 1 == length)
                return false;
            dot = true;
            segment_start = true;
        } else if (segment_start) {
            if ((c < 'a' || c > 'z') && (c < '0' || c > '9'))
                return false;
            segment_start = false;
        } else if ((c < 'a' || c > 'z') && (c < '0' || c > '9') &&
                   c != '-') {
            return false;
        }
    }
    return dot;
}

static bool valid_action(const char *value)
{
    size_t length = value ? strlen(value) : 0;
    if (!length || length > 64 ||
        !((value[0] >= 'a' && value[0] <= 'z') ||
          (value[0] >= 'A' && value[0] <= 'Z')))
        return false;
    return strspn(value, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                         "abcdefghijklmnopqrstuvwxyz"
                         "0123456789._-") == length;
}

static bool secure_regular_file(const char *path, bool executable)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_uid == APP_OWNER_UID && !(st.st_mode & 0022) &&
           (!executable || (st.st_mode & 0111));
}

static bool secure_directory(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) &&
           st.st_uid == APP_OWNER_UID && !(st.st_mode & 0022);
}

static bool valid_fingerprint_file(const char *path)
{
    if (!secure_regular_file(path, false))
        return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    char value[66];
    size_t used = 0;
    while (used < sizeof(value)) {
        ssize_t count = read(fd, value + used, sizeof(value) - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
    }
    close(fd);
    if (used != 65 || value[64] != '\n')
        return false;
    for (size_t i = 0; i < 64; i++) {
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f')))
            return false;
    }
    return true;
}

static json_object *read_json(const char *path)
{
    struct stat st;
    if (lstat(path, &st) || !S_ISREG(st.st_mode) ||
        st.st_uid != APP_OWNER_UID ||
        (st.st_mode & 0022) || st.st_size <= 0 || st.st_size > 1024 * 1024)
        return NULL;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return NULL;
    size_t length = (size_t)st.st_size;
    char *buffer = malloc(length + 1);
    if (!buffer) {
        close(fd);
        return NULL;
    }
    size_t used = 0;
    while (used < length) {
        ssize_t count = read(fd, buffer + used, length - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
    }
    close(fd);
    buffer[used] = '\0';
    json_tokener *tokener = json_tokener_new();
    if (!tokener) {
        free(buffer);
        return NULL;
    }
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    json_object *result = used == length ?
        json_tokener_parse_ex(tokener, buffer, (int)length) : NULL;
    enum json_tokener_error error = json_tokener_get_error(tokener);
    size_t end = (size_t)tokener->char_offset;
    while (end < length && (buffer[end] == ' ' || buffer[end] == '\t' ||
                            buffer[end] == '\r' || buffer[end] == '\n'))
        end++;
    if (error != json_tokener_success || end != length) {
        if (result)
            json_object_put(result);
        result = NULL;
    }
    json_tokener_free(tokener);
    memset(buffer, 0, length);
    free(buffer);
    return result;
}

static bool set_permission(struct app_policy *policy, const char *name)
{
    bool *target = NULL;
    if (!strcmp(name, "capture.snapshot")) target = &policy->capture;
    else if (!strcmp(name, "input.keyboard")) target = &policy->keyboard;
    else if (!strcmp(name, "input.pointer")) target = &policy->pointer;
    else if (!strcmp(name, "hardware.gpio.read")) target = &policy->gpio_read;
    else if (!strcmp(name, "hardware.gpio.write")) target = &policy->gpio_write;
    else if (!strcmp(name, "hardware.i2c")) target = &policy->i2c;
    else if (!strcmp(name, "hardware.spi")) target = &policy->spi;
    else if (!strcmp(name, "hardware.uart")) target = &policy->uart;
    else if (!strcmp(name, "hardware.usb")) target = &policy->usb;
    else if (!strcmp(name, "network.https")) target = &policy->network_https;
    else if (!strcmp(name, "storage.app")) target = &policy->storage;
    else return false;
    if (*target)
        return false;
    *target = true;
    return true;
}

static bool load_policy_by_id(const char *id, struct app_policy *policy)
{
    if (!valid_app_id(id))
        return false;
    char app_path[512], manifest_path[512], marker_path[512];
    if (!secure_directory(APP_ROOT) ||
        snprintf(app_path, sizeof(app_path), "%s/%s", APP_ROOT, id) >=
            (int)sizeof(app_path) ||
        !secure_directory(app_path))
        return false;
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.json",
                 APP_ROOT, id) >= (int)sizeof(manifest_path) ||
        snprintf(marker_path, sizeof(marker_path),
                 "%s/%s/.aitvbox-publisher-key.sha256",
                 APP_ROOT, id) >= (int)sizeof(marker_path) ||
        !secure_regular_file(manifest_path, false) ||
        !valid_fingerprint_file(marker_path))
        return false;

    json_object *manifest = read_json(manifest_path);
    json_object *schema = NULL, *manifest_id = NULL, *runtime = NULL;
    json_object *type = NULL, *entry = NULL, *api = NULL, *permissions = NULL;
    bool valid = manifest && json_object_is_type(manifest, json_type_object) &&
        json_object_object_get_ex(manifest, "schema", &schema) &&
        json_object_is_type(schema, json_type_int) &&
        (json_object_get_int(schema) == 1 ||
         json_object_get_int(schema) == 2) &&
        json_object_object_get_ex(manifest, "id", &manifest_id) &&
        json_object_is_type(manifest_id, json_type_string) &&
        !strcmp(json_object_get_string(manifest_id), id) &&
        json_object_object_get_ex(manifest, "permissions", &permissions) &&
        json_object_is_type(permissions, json_type_array);
    if (!valid) {
        if (manifest)
            json_object_put(manifest);
        return false;
    }

    if (json_object_get_int(schema) == 2) {
        if (!json_object_object_get_ex(manifest, "runtime", &runtime) ||
            !json_object_is_type(runtime, json_type_object) ||
            !json_object_object_get_ex(runtime, "type", &type) ||
            !json_object_is_type(type, json_type_string) ||
            strcmp(json_object_get_string(type), "native-rpc") ||
            !json_object_object_get_ex(runtime, "entry", &entry) ||
            !json_object_is_type(entry, json_type_string) ||
            !json_object_object_get_ex(runtime, "api", &api) ||
            !json_object_is_type(api, json_type_int) ||
            json_object_get_int(api) != 1) {
            json_object_put(manifest);
            return false;
        }
        const char *entry_text = json_object_get_string(entry);
        char bin_path[512];
        if (strncmp(entry_text, "bin/", 4) || strstr(entry_text, "/../") ||
            strchr(entry_text + 4, '/') ||
            snprintf(bin_path, sizeof(bin_path), "%s/%s/bin", APP_ROOT, id) >=
                (int)sizeof(bin_path) ||
            !secure_directory(bin_path) ||
            snprintf(policy->executable, sizeof(policy->executable),
                     "%s/%s/%s", APP_ROOT, id, entry_text) >=
                (int)sizeof(policy->executable) ||
            !secure_regular_file(policy->executable, true)) {
            json_object_put(manifest);
            return false;
        }
        policy->native = true;
    }
    snprintf(policy->id, sizeof(policy->id), "%s", id);
    size_t count = json_object_array_length(permissions);
    for (size_t i = 0; i < count; i++) {
        json_object *item = json_object_array_get_idx(permissions, i);
        if (!json_object_is_type(item, json_type_string) ||
            !set_permission(policy, json_object_get_string(item))) {
            json_object_put(manifest);
            return false;
        }
    }
    json_object_put(manifest);
    return true;
}

static bool read_process_executable(pid_t pid, char *buffer, size_t size)
{
    char proc_path[64];
    if (snprintf(proc_path, sizeof(proc_path), "/proc/%ld/exe", (long)pid) >=
        (int)sizeof(proc_path))
        return false;
    ssize_t length = readlink(proc_path, buffer, size - 1);
    if (length <= 0 || length >= (ssize_t)size - 1)
        return false;
    buffer[length] = '\0';
    return true;
}

static bool sandbox_process_state(pid_t pid)
{
    char status_path[64];
    if (snprintf(status_path, sizeof(status_path), "/proc/%ld/status",
                 (long)pid) >= (int)sizeof(status_path))
        return false;
    int fd = open(status_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    char status[8193];
    ssize_t length;
    do {
        length = read(fd, status, sizeof(status) - 1);
    } while (length < 0 && errno == EINTR);
    close(fd);
    if (length <= 0)
        return false;
    status[length] = '\0';

    long parent_pid = -1;
    int no_new_privs = -1, seccomp = -1;
    char *save = NULL;
    for (char *line = strtok_r(status, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "PPid:", 5))
            (void)sscanf(line + 5, "%ld", &parent_pid);
        else if (!strncmp(line, "NoNewPrivs:", 11))
            (void)sscanf(line + 11, "%d", &no_new_privs);
        else if (!strncmp(line, "Seccomp:", 8))
            (void)sscanf(line + 8, "%d", &seccomp);
    }
    /*
     * Tina's 4.9 kernel enforces PR_SET_NO_NEW_PRIVS and seccomp filters but
     * does not expose the NoNewPrivs field in /proc/<pid>/status.  When the
     * field exists it must be set; on older kernels the seccomp filter,
     * unprivileged uid/gid, exact installed executable, and appd parent checks
     * below still bind the caller to the sandbox created by invoke_app().
     */
    if (parent_pid <= 1 || seccomp != 2 ||
        (no_new_privs != -1 && no_new_privs != 1))
        return false;

    char parent_executable[512], service_executable[512];
    return read_process_executable((pid_t)parent_pid, parent_executable,
                                   sizeof(parent_executable)) &&
           read_process_executable(getpid(), service_executable,
                                   sizeof(service_executable)) &&
           !strcmp(parent_executable, service_executable);
}

static bool policy_matches_process(const struct ucred *credentials,
                                   struct app_policy *policy)
{
    if (credentials->uid != APP_UID || credentials->gid != APP_GID ||
        !sandbox_process_state(credentials->pid))
        return false;
    char executable[512];
    if (!read_process_executable(credentials->pid, executable,
                                 sizeof(executable)))
        return false;
    size_t prefix_length = strlen(APP_ROOT);
    if (strncmp(executable, APP_ROOT, prefix_length) ||
        executable[prefix_length] != '/')
        return false;
    const char *id_start = executable + prefix_length + 1;
    const char *slash = strchr(id_start, '/');
    if (!slash || (size_t)(slash - id_start) >= sizeof(policy->id))
        return false;
    char id[97];
    memcpy(id, id_start, (size_t)(slash - id_start));
    id[slash - id_start] = '\0';
    return load_policy_by_id(id, policy) && policy->native &&
           !strcmp(executable, policy->executable);
}

static bool object_has_only(json_object *object, const char *const *fields,
                            size_t field_count)
{
    if (!json_object_is_type(object, json_type_object))
        return false;
    json_object_object_foreach(object, name, value) {
        (void)value;
        bool found = false;
        for (size_t i = 0; i < field_count; i++) {
            if (!strcmp(name, fields[i])) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

static json_object *reply(bool ok, const char *message)
{
    json_object *root = json_object_new_object();
    json_object_object_add(root, "ok", json_object_new_boolean(ok));
    if (message)
        json_object_object_add(root, "message", json_object_new_string(message));
    return root;
}

static bool permission_granted(const struct app_policy *policy,
                               const char *capability)
{
    if (!strcmp(capability, "hardware.capabilities"))
        return true;
    if (!strcmp(capability, "capture.snapshot")) return policy->capture;
    if (!strcmp(capability, "input.keyboard")) return policy->keyboard;
    if (!strcmp(capability, "input.pointer")) return policy->pointer;
    if (!strcmp(capability, "hardware.gpio.read")) return policy->gpio_read;
    if (!strcmp(capability, "hardware.gpio.write")) return policy->gpio_write;
    if (!strcmp(capability, "hardware.i2c")) return policy->i2c;
    if (!strcmp(capability, "hardware.spi")) return policy->spi;
    if (!strcmp(capability, "hardware.uart")) return policy->uart;
    if (!strcmp(capability, "hardware.usb")) return policy->usb;
    if (!strcmp(capability, "network.https")) return policy->network_https;
    if (!strcmp(capability, "storage.app")) return policy->storage;
    return false;
}

static const char *mcp_tool_name(const char *capability)
{
    if (!strcmp(capability, "hardware.capabilities"))
        return "hardware.capabilities";
    if (!strcmp(capability, "capture.snapshot"))
        return "computer.capture";
    if (!strcmp(capability, "input.keyboard"))
        return "computer.key";
    if (!strcmp(capability, "input.pointer"))
        return "computer.mouse";
    return NULL;
}

static int64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int install_app_seccomp(void)
{
#if defined(__aarch64__)
#define AITVBOX_AUDIT_ARCH AUDIT_ARCH_AARCH64
#elif defined(__x86_64__)
#define AITVBOX_AUDIT_ARCH AUDIT_ARCH_X86_64
#else
#error "AITVBox app sandbox requires a supported 64-bit architecture"
#endif
#define DENY_SYSCALL(number) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (number), 0, 1), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)
    const unsigned int write_flags =
        O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND;
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (unsigned int)offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AITVBOX_AUDIT_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (unsigned int)offsetof(struct seccomp_data, nr)),
#ifdef __NR_ioctl
        DENY_SYSCALL(__NR_ioctl),
#endif
#ifdef __NR_ptrace
        DENY_SYSCALL(__NR_ptrace),
#endif
#ifdef __NR_mount
        DENY_SYSCALL(__NR_mount),
#endif
#ifdef __NR_umount2
        DENY_SYSCALL(__NR_umount2),
#endif
#ifdef __NR_mknod
        DENY_SYSCALL(__NR_mknod),
#endif
#ifdef __NR_mknodat
        DENY_SYSCALL(__NR_mknodat),
#endif
#ifdef __NR_clone
        DENY_SYSCALL(__NR_clone),
#endif
#ifdef __NR_fork
        DENY_SYSCALL(__NR_fork),
#endif
#ifdef __NR_vfork
        DENY_SYSCALL(__NR_vfork),
#endif
#ifdef __NR_clone3
        DENY_SYSCALL(__NR_clone3),
#endif
#ifdef __NR_openat2
        DENY_SYSCALL(__NR_openat2),
#endif
#ifdef __NR_creat
        DENY_SYSCALL(__NR_creat),
#endif
#ifdef __NR_truncate
        DENY_SYSCALL(__NR_truncate),
#endif
#ifdef __NR_ftruncate
        DENY_SYSCALL(__NR_ftruncate),
#endif
#ifdef __NR_rename
        DENY_SYSCALL(__NR_rename),
#endif
#ifdef __NR_renameat
        DENY_SYSCALL(__NR_renameat),
#endif
#ifdef __NR_renameat2
        DENY_SYSCALL(__NR_renameat2),
#endif
#ifdef __NR_unlink
        DENY_SYSCALL(__NR_unlink),
#endif
#ifdef __NR_unlinkat
        DENY_SYSCALL(__NR_unlinkat),
#endif
#ifdef __NR_mkdir
        DENY_SYSCALL(__NR_mkdir),
#endif
#ifdef __NR_mkdirat
        DENY_SYSCALL(__NR_mkdirat),
#endif
#ifdef __NR_rmdir
        DENY_SYSCALL(__NR_rmdir),
#endif
#ifdef __NR_link
        DENY_SYSCALL(__NR_link),
#endif
#ifdef __NR_linkat
        DENY_SYSCALL(__NR_linkat),
#endif
#ifdef __NR_symlink
        DENY_SYSCALL(__NR_symlink),
#endif
#ifdef __NR_symlinkat
        DENY_SYSCALL(__NR_symlinkat),
#endif
#ifdef __NR_chmod
        DENY_SYSCALL(__NR_chmod),
#endif
#ifdef __NR_fchmod
        DENY_SYSCALL(__NR_fchmod),
#endif
#ifdef __NR_fchmodat
        DENY_SYSCALL(__NR_fchmodat),
#endif
#ifdef __NR_chown
        DENY_SYSCALL(__NR_chown),
#endif
#ifdef __NR_fchown
        DENY_SYSCALL(__NR_fchown),
#endif
#ifdef __NR_lchown
        DENY_SYSCALL(__NR_lchown),
#endif
#ifdef __NR_fchownat
        DENY_SYSCALL(__NR_fchownat),
#endif
#ifdef __NR_setxattr
        DENY_SYSCALL(__NR_setxattr),
#endif
#ifdef __NR_lsetxattr
        DENY_SYSCALL(__NR_lsetxattr),
#endif
#ifdef __NR_fsetxattr
        DENY_SYSCALL(__NR_fsetxattr),
#endif
#ifdef __NR_removexattr
        DENY_SYSCALL(__NR_removexattr),
#endif
#ifdef __NR_lremovexattr
        DENY_SYSCALL(__NR_lremovexattr),
#endif
#ifdef __NR_fremovexattr
        DENY_SYSCALL(__NR_fremovexattr),
#endif
#ifdef __NR_io_uring_setup
        DENY_SYSCALL(__NR_io_uring_setup),
#endif
#ifdef __NR_io_uring_enter
        DENY_SYSCALL(__NR_io_uring_enter),
#endif
#ifdef __NR_io_uring_register
        DENY_SYSCALL(__NR_io_uring_register),
#endif
#ifdef __NR_open
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_open, 0, 3),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (unsigned int)offsetof(struct seccomp_data, args[1])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, write_flags, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EACCES),
#endif
#ifdef __NR_openat
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (unsigned int)offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat, 0, 3),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (unsigned int)offsetof(struct seccomp_data, args[2])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, write_flags, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EACCES),
#endif
#ifdef __NR_socket
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (unsigned int)offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_socket, 0, 3),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (unsigned int)offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_UNIX, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EACCES),
#endif
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program);
#undef DENY_SYSCALL
#undef AITVBOX_AUDIT_ARCH
}

static char *run_process(char *const argv[], const char *input,
                         size_t maximum, int timeout_ms, int *status_out)
{
    int input_pipe[2], output_pipe[2];
    if (pipe2(input_pipe, O_CLOEXEC) || pipe2(output_pipe, O_CLOEXEC))
        return NULL;
    pid_t child = fork();
    if (child == 0) {
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(input_pipe[0]);
    close(output_pipe[1]);
    if (child < 0) {
        close(input_pipe[1]);
        close(output_pipe[0]);
        return NULL;
    }
    if (input)
        (void)write_all(input_pipe[1], input, strlen(input));
    close(input_pipe[1]);
    int flags = fcntl(output_pipe[0], F_GETFL);
    if (flags >= 0)
        (void)fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);

    char *output = malloc(maximum + 1);
    if (!output) {
        kill(child, SIGKILL);
        close(output_pipe[0]);
        waitpid(child, NULL, 0);
        return NULL;
    }
    size_t used = 0;
    int64_t deadline = monotonic_ms() + timeout_ms;
    bool complete = false;
    while (monotonic_ms() < deadline) {
        struct pollfd descriptor = {.fd = output_pipe[0], .events = POLLIN};
        int remaining = (int)(deadline - monotonic_ms());
        int ready = poll(&descriptor, 1, remaining > 0 ? remaining : 1);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0)
            break;
        if (ready == 0)
            break;
        ssize_t count = read(output_pipe[0], output + used, maximum - used);
        if (count > 0) {
            used += (size_t)count;
            if (used == maximum)
                break;
        } else if (count == 0) {
            complete = true;
            break;
        } else if (errno != EAGAIN && errno != EINTR) {
            break;
        }
    }
    close(output_pipe[0]);
    int status_code = 0;
    pid_t waited = waitpid(child, &status_code, WNOHANG);
    while (complete && waited == 0 && monotonic_ms() < deadline) {
        usleep(1000);
        waited = waitpid(child, &status_code, WNOHANG);
    }
    if (!complete || waited == 0) {
        kill(child, SIGKILL);
        waitpid(child, &status_code, 0);
    } else if (waited < 0) {
        status_code = 1 << 8;
    }
    output[used] = '\0';
    *status_out = status_code;
    return output;
}

static json_object *invoke_app(json_object *request, uid_t peer_uid)
{
    static const char *const fields[] = {"command", "appId", "action"};
    json_object *id_object = NULL, *action_object = NULL;
    if (peer_uid != geteuid())
        return reply(false, "only the platform UI may invoke applications");
    if (!object_has_only(request, fields, 3) ||
        json_object_object_length(request) != 3 ||
        !json_object_object_get_ex(request, "appId", &id_object) ||
        !json_object_is_type(id_object, json_type_string) ||
        !json_object_object_get_ex(request, "action", &action_object) ||
        !json_object_is_type(action_object, json_type_string))
        return reply(false, "invalid invoke request");
    const char *id = json_object_get_string(id_object);
    const char *action = json_object_get_string(action_object);
    struct app_policy policy = {0};
    if (!valid_action(action) || !load_policy_by_id(id, &policy) ||
        !policy.native)
        return reply(false, "application is unavailable or invalid");

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC))
        return reply(false, "cannot create application channel");
    pid_t child = fork();
    if (child == 0) {
        struct rlimit cpu = {3, 3};
        struct rlimit address_space = {64 * 1024 * 1024, 64 * 1024 * 1024};
        struct rlimit file_size = {1024 * 1024, 1024 * 1024};
        struct rlimit processes = {1, 1};
        struct rlimit open_files = {16, 16};
        struct rlimit core = {0, 0};
        setrlimit(RLIMIT_CPU, &cpu);
        setrlimit(RLIMIT_AS, &address_space);
        setrlimit(RLIMIT_FSIZE, &file_size);
        setrlimit(RLIMIT_NPROC, &processes);
        setrlimit(RLIMIT_NOFILE, &open_files);
        setrlimit(RLIMIT_CORE, &core);
        int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
        dup2(pipefd[1], STDOUT_FILENO);
        if (nullfd >= 0) {
            dup2(nullfd, STDIN_FILENO);
            dup2(nullfd, STDERR_FILENO);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        if (nullfd > STDERR_FILENO)
            close(nullfd);
        if ((geteuid() == 0 &&
             (setgroups(0, NULL) || setgid(APP_GID) || setuid(APP_UID))) ||
            (geteuid() != 0 &&
             (geteuid() != APP_UID || getegid() != APP_GID)) ||
            prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
            install_app_seccomp())
            _exit(126);
        clearenv();
        setenv("AITVBOX_APP_SOCKET", APP_SOCKET, 1);
        if (chdir("/"))
            _exit(126);
        char *const argv[] = {
            policy.executable, "--action", (char *)action, NULL
        };
        execv(policy.executable, argv);
        _exit(127);
    }
    close(pipefd[1]);
    if (child < 0) {
        close(pipefd[0]);
        return reply(false, "cannot start application");
    }

    int flags = fcntl(pipefd[0], F_GETFL);
    if (flags >= 0)
        (void)fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    char output[APP_OUTPUT_MAX + 1];
    size_t used = 0;
    int64_t deadline = monotonic_ms() + APP_TIMEOUT_MS;
    bool eof = false;
    while (monotonic_ms() < deadline && used < APP_OUTPUT_MAX) {
        struct pollfd descriptor = {.fd = pipefd[0], .events = POLLIN};
        int remaining = (int)(deadline - monotonic_ms());
        int ready = poll(&descriptor, 1, remaining > 0 ? remaining : 1);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
            break;
        ssize_t count = read(pipefd[0], output + used, APP_OUTPUT_MAX - used);
        if (count > 0)
            used += (size_t)count;
        else if (count == 0) {
            eof = true;
            break;
        } else if (errno != EAGAIN && errno != EINTR) {
            break;
        }
    }
    close(pipefd[0]);
    int status_code = 0;
    pid_t waited = waitpid(child, &status_code, WNOHANG);
    while (eof && waited == 0 && monotonic_ms() < deadline) {
        usleep(1000);
        waited = waitpid(child, &status_code, WNOHANG);
    }
    if (!eof || waited == 0) {
        kill(child, SIGKILL);
        waitpid(child, &status_code, 0);
        return reply(false, "application timed out or exceeded output limit");
    }
    if (waited < 0 || !WIFEXITED(status_code) || WEXITSTATUS(status_code) != 0)
        return reply(false, "application failed");
    output[used] = '\0';
    json_tokener *tokener = json_tokener_new();
    json_object *app_reply = tokener ?
        json_tokener_parse_ex(tokener, output, (int)used) : NULL;
    enum json_tokener_error parse_error = tokener ?
        json_tokener_get_error(tokener) : json_tokener_error_parse_unexpected;
    size_t parsed = tokener ? (size_t)tokener->char_offset : 0;
    while (parsed < used && (output[parsed] == ' ' || output[parsed] == '\t' ||
                             output[parsed] == '\r' || output[parsed] == '\n'))
        parsed++;
    if (tokener)
        json_tokener_free(tokener);
    memset(output, 0, sizeof(output));
    json_object *ok = NULL, *message = NULL;
    static const char *const result_fields[] = {"ok", "message"};
    if (parse_error != json_tokener_success || parsed != used ||
        !app_reply || !object_has_only(app_reply, result_fields, 2) ||
        json_object_object_length(app_reply) != 2 ||
        !json_object_object_get_ex(app_reply, "ok", &ok) ||
        !json_object_is_type(ok, json_type_boolean) ||
        !json_object_object_get_ex(app_reply, "message", &message) ||
        !json_object_is_type(message, json_type_string) ||
        json_object_get_string_len(message) > 512) {
        if (app_reply)
            json_object_put(app_reply);
        return reply(false, "application returned an invalid response");
    }
    return app_reply;
}

static json_object *run_capability(const struct app_policy *policy,
                                   const char *capability,
                                   json_object *arguments,
                                   bool include_result)
{
    if (!permission_granted(policy, capability))
        return reply(false, "capability is not declared by this application");
    if (!strcmp(capability, "storage.app")) {
        json_object *operation = NULL, *key_object = NULL, *value_object = NULL;
        static const char *const read_fields[] = {"op", "key"};
        static const char *const write_fields[] = {"op", "key", "value"};
        if (!json_object_object_get_ex(arguments, "op", &operation) ||
            !json_object_is_type(operation, json_type_string) ||
            !json_object_object_get_ex(arguments, "key", &key_object) ||
            !json_object_is_type(key_object, json_type_string))
            return reply(false, "invalid application storage request");
        const char *op = json_object_get_string(operation);
        const char *key = json_object_get_string(key_object);
        size_t key_length = strlen(key);
        if (!key_length || key_length > 64 ||
            strspn(key, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                       "abcdefghijklmnopqrstuvwxyz"
                       "0123456789._-") != key_length)
            return reply(false, "invalid application storage key");

        char data_directory[512], path[640];
        if (snprintf(data_directory, sizeof(data_directory), "%s/%s/data",
                     APP_ROOT, policy->id) >= (int)sizeof(data_directory) ||
            snprintf(path, sizeof(path), "%s/%s.value",
                     data_directory, key) >= (int)sizeof(path) ||
            !secure_directory(data_directory))
            return reply(false, "application storage is unavailable");

        if (!strcmp(op, "read")) {
            if (!object_has_only(arguments, read_fields, 2) ||
                json_object_object_length(arguments) != 2)
                return reply(false, "invalid application storage read");
            int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0 && errno == ENOENT) {
                json_object *response = reply(true, NULL);
                json_object *result = json_object_new_object();
                json_object_object_add(result, "found",
                                       json_object_new_boolean(false));
                json_object_object_add(response, "result", result);
                return response;
            }
            struct stat st;
            if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) ||
                st.st_uid != APP_OWNER_UID || (st.st_mode & 0077) ||
                st.st_size < 0 || st.st_size > APP_STORAGE_VALUE_MAX) {
                if (fd >= 0)
                    close(fd);
                return reply(false, "application storage value is invalid");
            }
            size_t size = (size_t)st.st_size;
            char *value = malloc(size + 1);
            if (!value) {
                close(fd);
                return reply(false, "out of memory");
            }
            size_t used = 0;
            while (used < size) {
                ssize_t count = read(fd, value + used, size - used);
                if (count < 0 && errno == EINTR)
                    continue;
                if (count <= 0)
                    break;
                used += (size_t)count;
            }
            close(fd);
            if (used != size) {
                aitvbox_secure_clear(value, size + 1);
                free(value);
                return reply(false, "cannot read application storage");
            }
            value[size] = '\0';
            json_object *response = reply(true, NULL);
            json_object *result = json_object_new_object();
            json_object_object_add(result, "found",
                                   json_object_new_boolean(true));
            json_object_object_add(result, "value",
                                   json_object_new_string_len(value, (int)size));
            json_object_object_add(response, "result", result);
            aitvbox_secure_clear(value, size + 1);
            free(value);
            return response;
        }

        if (strcmp(op, "write") ||
            !object_has_only(arguments, write_fields, 3) ||
            json_object_object_length(arguments) != 3 ||
            !json_object_object_get_ex(arguments, "value", &value_object) ||
            !json_object_is_type(value_object, json_type_string))
            return reply(false, "invalid application storage write");
        const char *value = json_object_get_string(value_object);
        size_t value_length = (size_t)json_object_get_string_len(value_object);
        if (value_length > APP_STORAGE_VALUE_MAX)
            return reply(false, "application storage value is too large");

        if (aitvbox_atomic_write_file(path, value, value_length, 0600)) {
            return reply(false, "cannot save application storage value");
        }
        json_object *response = reply(true, include_result ? NULL :
                                      "capability completed");
        if (include_result) {
            json_object *result = json_object_new_object();
            json_object_object_add(result, "saved",
                                   json_object_new_boolean(true));
            json_object_object_add(response, "result", result);
        }
        return response;
    }
    const char *tool = mcp_tool_name(capability);
    if (!tool)
        return reply(false, "capability interface is not available");

    json_object *initialize = json_object_new_object();
    json_object_object_add(initialize, "jsonrpc",
                           json_object_new_string("2.0"));
    json_object_object_add(initialize, "id", json_object_new_int(1));
    json_object_object_add(initialize, "method",
                           json_object_new_string("initialize"));
    json_object *initialize_params = json_object_new_object();
    json_object_object_add(initialize_params, "protocolVersion",
                           json_object_new_string("2025-11-25"));
    json_object_object_add(initialize, "params", initialize_params);
    json_object *initialized = json_object_new_object();
    json_object_object_add(initialized, "jsonrpc",
                           json_object_new_string("2.0"));
    json_object_object_add(initialized, "method",
                           json_object_new_string("notifications/initialized"));
    json_object *call = json_object_new_object();
    json_object_object_add(call, "jsonrpc", json_object_new_string("2.0"));
    json_object_object_add(call, "id", json_object_new_int(2));
    json_object_object_add(call, "method", json_object_new_string("tools/call"));
    json_object *params = json_object_new_object();
    json_object_object_add(params, "name", json_object_new_string(tool));
    json_object_get(arguments);
    json_object_object_add(params, "arguments", arguments);
    json_object_object_add(call, "params", params);
    const char *initialize_text = json_object_to_json_string_ext(
        initialize, JSON_C_TO_STRING_PLAIN);
    const char *initialized_text = json_object_to_json_string_ext(
        initialized, JSON_C_TO_STRING_PLAIN);
    const char *call_text = json_object_to_json_string_ext(
        call, JSON_C_TO_STRING_PLAIN);
    size_t input_length = strlen(initialize_text) + strlen(initialized_text) +
                          strlen(call_text) + 4;
    char *input = malloc(input_length);
    if (!input) {
        json_object_put(initialize);
        json_object_put(initialized);
        json_object_put(call);
        return reply(false, "out of memory");
    }
    snprintf(input, input_length, "%s\n%s\n%s\n",
             initialize_text, initialized_text, call_text);
    char *const argv[] = {MCP_PATH, NULL};
    int status_code = 0;
    char *output = run_process(argv, input, MCP_OUTPUT_MAX, APP_TIMEOUT_MS,
                               &status_code);
    memset(input, 0, input_length);
    free(input);
    json_object_put(initialize);
    json_object_put(initialized);
    json_object_put(call);
    if (!output || !WIFEXITED(status_code) || WEXITSTATUS(status_code) != 0) {
        free(output);
        return reply(false, "hardware service failed");
    }
    char *last = NULL;
    for (char *line = strtok(output, "\n"); line; line = strtok(NULL, "\n"))
        last = line;
    json_object *mcp_reply = last ? json_tokener_parse(last) : NULL;
    json_object *result = NULL, *error = NULL;
    if (!mcp_reply || json_object_object_get_ex(mcp_reply, "error", &error) ||
        !json_object_object_get_ex(mcp_reply, "result", &result)) {
        if (mcp_reply)
            json_object_put(mcp_reply);
        memset(output, 0, strlen(output));
        free(output);
        return reply(false, "hardware service rejected the request");
    }
    json_object *is_error = NULL;
    if (json_object_object_get_ex(result, "isError", &is_error) &&
        json_object_is_type(is_error, json_type_boolean) &&
        json_object_get_boolean(is_error)) {
        const char *message = "hardware capability request failed";
        json_object *content = NULL;
        if (json_object_object_get_ex(result, "content", &content) &&
            json_object_is_type(content, json_type_array) &&
            json_object_array_length(content) > 0) {
            json_object *first = json_object_array_get_idx(content, 0);
            json_object *text = NULL;
            if (json_object_is_type(first, json_type_object) &&
                json_object_object_get_ex(first, "text", &text) &&
                json_object_is_type(text, json_type_string))
                message = json_object_get_string(text);
        }
        json_object *failure = reply(false, message);
        json_object_put(mcp_reply);
        memset(output, 0, strlen(output));
        free(output);
        return failure;
    }
    json_object *response = reply(true, include_result ? NULL :
                                  "capability completed");
    if (include_result) {
        json_object_get(result);
        json_object_object_add(response, "result", result);
    }
    json_object_put(mcp_reply);
    memset(output, 0, strlen(output));
    free(output);
    return response;
}

static json_object *call_capability(json_object *request,
                                    const struct ucred *credentials)
{
    static const char *const fields[] = {
        "command", "capability", "arguments"
    };
    json_object *capability_object = NULL, *arguments = NULL;
    if (!object_has_only(request, fields, 3) ||
        json_object_object_length(request) != 3 ||
        !json_object_object_get_ex(request, "capability", &capability_object) ||
        !json_object_is_type(capability_object, json_type_string) ||
        !json_object_object_get_ex(request, "arguments", &arguments) ||
        !json_object_is_type(arguments, json_type_object))
        return reply(false, "invalid capability request");
    struct app_policy policy = {0};
    if (!policy_matches_process(credentials, &policy))
        return reply(false, "caller is not an installed native application");
    return run_capability(&policy, json_object_get_string(capability_object),
                          arguments, true);
}

static json_object *call_platform_action(json_object *request, uid_t peer_uid)
{
    static const char *const fields[] = {
        "command", "appId", "capability", "arguments"
    };
    json_object *id = NULL, *capability = NULL, *arguments = NULL;
    if (peer_uid != geteuid())
        return reply(false, "only the platform UI may request an action");
    if (!object_has_only(request, fields, 4) ||
        json_object_object_length(request) != 4 ||
        !json_object_object_get_ex(request, "appId", &id) ||
        !json_object_is_type(id, json_type_string) ||
        !json_object_object_get_ex(request, "capability", &capability) ||
        !json_object_is_type(capability, json_type_string) ||
        !json_object_object_get_ex(request, "arguments", &arguments) ||
        !json_object_is_type(arguments, json_type_object))
        return reply(false, "invalid platform action request");
    struct app_policy policy = {0};
    if (!load_policy_by_id(json_object_get_string(id), &policy))
        return reply(false, "application is unavailable or invalid");
    return run_capability(&policy, json_object_get_string(capability),
                          arguments, false);
}

static json_object *parse_request(const char *buffer, size_t length)
{
    json_tokener *tokener = json_tokener_new();
    if (!tokener)
        return NULL;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    json_object *object = json_tokener_parse_ex(tokener, buffer, (int)length);
    enum json_tokener_error error = json_tokener_get_error(tokener);
    size_t consumed = (size_t)tokener->char_offset;
    while (consumed < length &&
           (buffer[consumed] == ' ' || buffer[consumed] == '\t' ||
            buffer[consumed] == '\r' || buffer[consumed] == '\n'))
        consumed++;
    json_tokener_free(tokener);
    if (error != json_tokener_success || consumed != length) {
        if (object)
            json_object_put(object);
        return NULL;
    }
    return object;
}

static void handle_client(int client)
{
    struct timeval timeout = {
        .tv_sec = APP_CLIENT_TIMEOUT_MS / 1000,
        .tv_usec = (APP_CLIENT_TIMEOUT_MS % 1000) * 1000,
    };
    (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout, sizeof(timeout));
    (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                     &timeout, sizeof(timeout));
    struct ucred credentials;
    socklen_t credentials_length = sizeof(credentials);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED,
                   &credentials, &credentials_length)) {
        return;
    }
    char buffer[REQUEST_MAX + 1];
    size_t used = 0;
    int64_t deadline = monotonic_ms() + APP_CLIENT_TIMEOUT_MS;
    while (used < REQUEST_MAX && monotonic_ms() < deadline) {
        int remaining = (int)(deadline - monotonic_ms());
        struct pollfd descriptor = {
            .fd = client,
            .events = POLLIN,
        };
        int ready = poll(&descriptor, 1, remaining > 0 ? remaining : 1);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
            break;
        ssize_t count = read(client, buffer + used, REQUEST_MAX - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
        if (memchr(buffer, '\n', used))
            break;
    }
    char *newline = memchr(buffer, '\n', used);
    size_t request_length = newline ? (size_t)(newline - buffer) : used;
    json_object *request = request_length ?
        parse_request(buffer, request_length) : NULL;
    json_object *response = NULL, *command = NULL;
    if (!request || !json_object_is_type(request, json_type_object) ||
        !json_object_object_get_ex(request, "command", &command) ||
        !json_object_is_type(command, json_type_string)) {
        response = reply(false, "invalid request");
    } else if (!strcmp(json_object_get_string(command), "invoke")) {
        response = invoke_app(request, credentials.uid);
    } else if (!strcmp(json_object_get_string(command), "capability")) {
        response = call_capability(request, &credentials);
    } else if (!strcmp(json_object_get_string(command), "platformAction")) {
        response = call_platform_action(request, credentials.uid);
    } else {
        response = reply(false, "unknown command");
    }
    memset(buffer, 0, sizeof(buffer));
    const char *text = json_object_to_json_string_ext(
        response, JSON_C_TO_STRING_PLAIN);
    (void)write_all(client, text, strlen(text));
    (void)write_all(client, "\n", 1);
    json_object_put(response);
    if (request)
        json_object_put(request);
}

static void reap_workers(unsigned int *workers)
{
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        if (*workers > 0)
            (*workers)--;
    }
}

static void wait_for_worker_slot(unsigned int *workers)
{
    int64_t deadline = monotonic_ms() + APP_BUSY_GRACE_MS;
    while (*workers >= APP_MAX_WORKERS && monotonic_ms() < deadline) {
        reap_workers(workers);
        if (*workers < APP_MAX_WORKERS)
            return;
        usleep(1000);
    }
    reap_workers(workers);
}

int main(void)
{
    struct sigaction action = {0};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);

    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) {
        perror("application socket");
        return 1;
    }
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", APP_SOCKET) >=
        (int)sizeof(address.sun_path)) {
        close(server);
        return 1;
    }
    unlink(APP_SOCKET);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) ||
        chmod(APP_SOCKET, 0666) || listen(server, 16)) {
        perror("application listen");
        close(server);
        unlink(APP_SOCKET);
        return 1;
    }

    unsigned int workers = 0;
    while (!shutdown_requested) {
        reap_workers(&workers);
        int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        reap_workers(&workers);
        /*
         * A capability worker can write its reply just before it exits.  A
         * sandboxed app may immediately open the next broker request while
         * the parent still counts that completed worker.  Give only that
         * short exit/reap window a bounded grace period; genuinely saturated
         * services still fail closed with the busy response below.
         */
        if (workers >= APP_MAX_WORKERS)
            wait_for_worker_slot(&workers);
        if (workers >= APP_MAX_WORKERS) {
            static const char busy[] =
                "{\"ok\":false,\"message\":\"application service is busy\"}\n";
            (void)write_all(client, busy, sizeof(busy) - 1);
            close(client);
            continue;
        }
        pid_t worker = fork();
        if (worker == 0) {
            close(server);
            handle_client(client);
            close(client);
            _exit(0);
        }
        if (worker > 0)
            workers++;
        else {
            static const char unavailable[] =
                "{\"ok\":false,\"message\":\"application service is unavailable\"}\n";
            (void)write_all(client, unavailable, sizeof(unavailable) - 1);
        }
        close(client);
    }
    close(server);
    unlink(APP_SOCKET);
    reap_workers(&workers);
    return 0;
}
