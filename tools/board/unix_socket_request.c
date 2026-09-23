#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/var/run/aitvbox/capture.sock";
	const char *request = argc > 2 ? argv[2] : "SNAPSHOT\n";
	struct sockaddr_un address;
	char buffer[512];
	ssize_t length;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", path) >=
	    (int)sizeof(address.sun_path)) {
		fprintf(stderr, "socket path is too long\n");
		close(fd);
		return 1;
	}
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		fprintf(stderr, "connect %s: %s\n", path, strerror(errno));
		close(fd);
		return 1;
	}
	if (write(fd, request, strlen(request)) != (ssize_t)strlen(request)) {
		perror("write");
		close(fd);
		return 1;
	}
	if (!request[0] || request[strlen(request) - 1] != '\n') {
		if (write(fd, "\n", 1) != 1) {
			perror("write newline");
			close(fd);
			return 1;
		}
	}
	while ((length = read(fd, buffer, sizeof(buffer))) > 0) {
		if (write(STDOUT_FILENO, buffer, (size_t)length) != length) {
			perror("stdout");
			close(fd);
			return 1;
		}
	}
	close(fd);
	return length < 0 ? 1 : 0;
}
