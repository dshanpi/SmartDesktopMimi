#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

struct key {
    unsigned char code;
    unsigned char modifier;
};

static int integer(const char *text, int minimum, int maximum, const char *name)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < minimum || value > maximum) {
        fprintf(stderr, "invalid %s: %s\n", name, text);
        exit(2);
    }
    return (int)value;
}

static int ascii_key(unsigned char character, struct key *key)
{
    key->modifier = 0;
    if (character >= 'a' && character <= 'z') {
        key->code = (unsigned char)(4 + character - 'a');
        return 0;
    }
    if (character >= 'A' && character <= 'Z') {
        key->code = (unsigned char)(4 + character - 'A');
        key->modifier = 2;
        return 0;
    }
    if (character >= '1' && character <= '9') {
        key->code = (unsigned char)(30 + character - '1');
        return 0;
    }
    if (character == '0') {
        key->code = 39;
        return 0;
    }
    static const struct {
        unsigned char character;
        unsigned char code;
        unsigned char modifier;
    } mappings[] = {
        {'\n', 40, 0}, {'\t', 43, 0}, {' ', 44, 0},
        {'-', 45, 0}, {'_', 45, 2}, {'=', 46, 0}, {'+', 46, 2},
        {'[', 47, 0}, {'{', 47, 2}, {']', 48, 0}, {'}', 48, 2},
        {'\\', 49, 0}, {'|', 49, 2}, {';', 51, 0}, {':', 51, 2},
        {'\'', 52, 0}, {'"', 52, 2}, {'`', 53, 0}, {'~', 53, 2},
        {',', 54, 0}, {'<', 54, 2}, {'.', 55, 0}, {'>', 55, 2},
        {'/', 56, 0}, {'?', 56, 2}, {'!', 30, 2}, {'@', 31, 2},
        {'#', 32, 2}, {'$', 33, 2}, {'%', 34, 2}, {'^', 35, 2},
        {'&', 36, 2}, {'*', 37, 2}, {'(', 38, 2}, {')', 39, 2},
    };
    for (size_t i = 0; i < sizeof(mappings) / sizeof(mappings[0]); i++) {
        if (mappings[i].character == character) {
            key->code = mappings[i].code;
            key->modifier = mappings[i].modifier;
            return 0;
        }
    }
    return -1;
}

static int write_report(int fd, const unsigned char report[8])
{
    ssize_t written;
    do {
        written = write(fd, report, 8);
    } while (written < 0 && errno == EINTR);
    return written == 8 ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr,
                "usage: %s DEVICE HOLD_US GAP_US ASCII_FILE\n", argv[0]);
        return 2;
    }
    int hold = integer(argv[2], 1000, 1000000, "HOLD_US");
    int gap = integer(argv[3], 0, 1000000, "GAP_US");
    int input = open(argv[4], O_RDONLY | O_CLOEXEC);
    if (input < 0) {
        perror(argv[4]);
        return 1;
    }
    struct stat st;
    if (fstat(input, &st) || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || st.st_size > 4096) {
        fprintf(stderr, "ASCII_FILE must be a non-empty regular file <= 4096 bytes\n");
        close(input);
        return 2;
    }
    unsigned char *text = malloc((size_t)st.st_size);
    if (!text) {
        close(input);
        return 1;
    }
    ssize_t length = read(input, text, (size_t)st.st_size);
    close(input);
    if (length != st.st_size) {
        perror("read ASCII_FILE");
        free(text);
        return 1;
    }
    for (ssize_t i = 0; i < length; i++) {
        struct key key;
        if (ascii_key(text[i], &key)) {
            fprintf(stderr, "unsupported byte 0x%02x at offset %ld\n",
                    text[i], (long)i);
            free(text);
            return 2;
        }
    }

    int device = open(argv[1], O_WRONLY | O_CLOEXEC);
    if (device < 0) {
        perror(argv[1]);
        free(text);
        return 1;
    }
    static const unsigned char released[8] = {0};
    for (ssize_t i = 0; i < length; i++) {
        struct key key;
        (void)ascii_key(text[i], &key);
        unsigned char pressed[8] = {
            key.modifier, 0, key.code, 0, 0, 0, 0, 0
        };
        if (write_report(device, pressed)) {
            perror("write pressed report");
            close(device);
            free(text);
            return 1;
        }
        usleep((useconds_t)hold);
        if (write_report(device, released)) {
            perror("write released report");
            close(device);
            free(text);
            return 1;
        }
        if (gap)
            usleep((useconds_t)gap);
    }
    close(device);
    free(text);
    return 0;
}
