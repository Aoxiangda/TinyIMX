#include "tests/concurrency/TestFramework.h"

#include "common/net/Buffer.h"

#include <cstring>
#include <string>
#include <unistd.h>

namespace tinyimx::test {

void RegisterBufferTests(TestRunner& runner) {
    runner.Add("Buffer.AppendAndRetrieve", []() {
        Buffer buffer;

        buffer.Append("hello");
        buffer.Append(", ");
        buffer.Append("TinyIMX");

        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), static_cast<std::size_t>(14));
        TINYIMX_EXPECT_EQ(buffer.RetrieveAsString(5), std::string("hello"));
        TINYIMX_EXPECT_EQ(buffer.RetrieveAllAsString(), std::string(", TinyIMX"));
        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), static_cast<std::size_t>(0));
    });

    runner.Add("Buffer.RetrievePartial", []() {
        Buffer buffer;

        buffer.Append("abcdef", 6);

        TINYIMX_EXPECT_EQ(buffer.RetrieveAsString(2), std::string("ab"));
        TINYIMX_EXPECT_EQ(buffer.ReadableBytes(), static_cast<std::size_t>(4));
        TINYIMX_EXPECT_EQ(buffer.RetrieveAllAsString(), std::string("cdef"));
    });

    runner.Add("Buffer.ReadFdFromPipe", []() {
        int pipe_fd[2] = {-1, -1};

        TINYIMX_EXPECT_EQ(::pipe(pipe_fd), 0);

        const std::string message = "message from pipe";

        const ssize_t written = ::write(
            pipe_fd[1],
            message.data(),
            message.size()
        );

        TINYIMX_EXPECT_EQ(written, static_cast<ssize_t>(message.size()));

        Buffer buffer;
        int saved_errno = 0;

        const ssize_t read_bytes = buffer.ReadFd(pipe_fd[0], &saved_errno);

        TINYIMX_EXPECT_EQ(read_bytes, static_cast<ssize_t>(message.size()));
        TINYIMX_EXPECT_EQ(buffer.RetrieveAllAsString(), message);

        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
    });
}

}  // namespace tinyimx::test