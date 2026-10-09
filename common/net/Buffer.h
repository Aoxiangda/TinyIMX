#pragma once

#include <cstddef>
#include <string>
#include <vector>


namespace tinyimx {
    class Buffer {
        public:
            static constexpr std::size_t kCheapPrepend = 8;
            static constexpr std::size_t kInitialSize = 1024;

            explicit Buffer(std::size_t initial_size = kInitialSize);

            std::size_t ReadableBytes() const;
            std::size_t WritableBytes() const;
            std::size_t PrependableBytes() const;

            const char* Peek() const;
            const char* BeginWrite() const;
            char* BeginWrite();

            void HasWritten(std::size_t length);
            void Unwrite(std::size_t length);

            void Retrieve(std::size_t length);
            void RetrieveUntil(const char* end);
            void RetrieveAll();


            std::string RetrieveAsString(std::size_t length);
            std::string RetrieveAllAsString();

            void Append(const std::string& data);
            void Append(const char* data, std::size_t length);
            void Append(const void* data, std::size_t length);


            void EnsureWritableBytes(std::size_t length);

            ssize_t ReadFd(int fd, int* saved_errno);
            ssize_t WriteFd(int fd, int* saved_errno);

        private:
            char* Begin();
            const char* Begin() const;
            void MakeSpace(std::size_t length);

        private:
            std::vector<char> buffer_;
            std::size_t reader_index_{kCheapPrepend};
            std::size_t writer_index_{kCheapPrepend};
        };
} // namespace tinyimx