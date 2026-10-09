#include "common/net/Buffer.h"

#include <cstring>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <unistd.h>

int main() {
    tinyimx::Buffer buffer;

    std::cout << "========== Buffer Demo ==========\n";

    buffer.Append("hello");
    buffer.Append(", ");
    buffer.Append("TinyIMX");

    std::cout << "readable_bytes = "
              << buffer.ReadableBytes() << '\n';

    std::cout << "retrieve 5 bytes = "
              << buffer.RetrieveAsString(5) << '\n';

    std::cout << "remaining = "
              << buffer.RetrieveAllAsString() << '\n';

    int pipe_fd[2] = {-1, -1};

    if (::pipe(pipe_fd) != 0) {
        std::cerr << "pipe failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    const std::string pipe_message = "message from pipe";

    const ssize_t written = ::write(
        pipe_fd[1],
        pipe_message.data(),
        pipe_message.size()
    );

    if (written < 0) {
        std::cerr << "write pipe failed: " << std::strerror(errno) << '\n';
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return 1;
    }

    int saved_errno = 0;

    const ssize_t read_bytes = buffer.ReadFd(pipe_fd[0], &saved_errno);

    if (read_bytes < 0) {
        std::cerr << "buffer ReadFd failed: "
                  << std::strerror(saved_errno) << '\n';
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return 1;
    }

    std::cout << "read_fd_bytes = "
              << read_bytes << '\n';

    std::cout << "read_fd_content = "
              << buffer.RetrieveAllAsString() << '\n';

    ::close(pipe_fd[0]);
    ::close(pipe_fd[1]);

    std::cout << "=================================\n";

    return 0;
}