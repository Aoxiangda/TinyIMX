#include "common/net/Buffer.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>

namespace tinyimx {

Buffer::Buffer(std::size_t initial_size)
    : buffer_(kCheapPrepend + initial_size),
      reader_index_(kCheapPrepend),
      writer_index_(kCheapPrepend) {}

std::size_t Buffer::ReadableBytes() const {
    return writer_index_ - reader_index_;
}

std::size_t Buffer::WritableBytes() const {
    return buffer_.size() - writer_index_;
}

std::size_t Buffer::PrependableBytes() const {
    return reader_index_;
}

const char* Buffer::Peek() const {
    return Begin() + reader_index_;
}

const char* Buffer::BeginWrite() const {
    return Begin() + writer_index_;
}

char* Buffer::BeginWrite() {
    return Begin() + writer_index_;
}

void Buffer::HasWritten(std::size_t length) {
    assert(length <= WritableBytes());
    writer_index_ += length;
}

void Buffer::Unwrite(std::size_t length) {
    assert(length <= ReadableBytes());
    writer_index_ -= length;
}

void Buffer::Retrieve(std::size_t length) {
    assert(length <= ReadableBytes());

    if (length < ReadableBytes()) {
        reader_index_ += length;
        return;
    }

    RetrieveAll();
}

void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end);
    assert(end <= BeginWrite());

    Retrieve(static_cast<std::size_t>(end - Peek()));
}

void Buffer::RetrieveAll() {
    reader_index_ = kCheapPrepend;
    writer_index_ = kCheapPrepend;
}

std::string Buffer::RetrieveAsString(std::size_t length) {
    assert(length <= ReadableBytes());

    std::string result(Peek(), length);
    Retrieve(length);

    return result;
}

std::string Buffer::RetrieveAllAsString() {
    return RetrieveAsString(ReadableBytes());
}

void Buffer::Append(const std::string& data) {
    Append(data.data(), data.size());
}

void Buffer::Append(const char* data, std::size_t length) {
    if (data == nullptr || length == 0) {
        return;
    }

    EnsureWritableBytes(length);
    std::copy(data, data + length, BeginWrite());
    HasWritten(length);
}

void Buffer::Append(const void* data, std::size_t length) {
    Append(static_cast<const char*>(data), length);
}

void Buffer::EnsureWritableBytes(std::size_t length) {
    if (WritableBytes() >= length) {
        return;
    }

    MakeSpace(length);
}

ssize_t Buffer::ReadFd(int fd, int* saved_errno) {
    char extra_buffer[65536];

    struct iovec vec[2];

    const std::size_t writable = WritableBytes();

    vec[0].iov_base = BeginWrite();
    vec[0].iov_len = writable;

    vec[1].iov_base = extra_buffer;
    vec[1].iov_len = sizeof(extra_buffer);

    const int iov_count = writable < sizeof(extra_buffer) ? 2 : 1;

    const ssize_t n = ::readv(fd, vec, iov_count);

    if (n < 0) {
        if (saved_errno != nullptr) {
            *saved_errno = errno;
        }
        return n;
    }

    if (static_cast<std::size_t>(n) <= writable) {
        writer_index_ += static_cast<std::size_t>(n);
    } else {
        writer_index_ = buffer_.size();

        const std::size_t extra_length =
            static_cast<std::size_t>(n) - writable;

        Append(extra_buffer, extra_length);
    }

    return n;
}

ssize_t Buffer::WriteFd(int fd, int* saved_errno) {
    const std::size_t readable = ReadableBytes();

    if (readable == 0) {
        return 0;
    }

    const ssize_t n = ::write(fd, Peek(), readable);

    if (n < 0) {
        if (saved_errno != nullptr) {
            *saved_errno = errno;
        }
        return n;
    }

    Retrieve(static_cast<std::size_t>(n));
    return n;
}

char* Buffer::Begin() {
    return buffer_.data();
}

const char* Buffer::Begin() const {
    return buffer_.data();
}

void Buffer::MakeSpace(std::size_t length) {
    if (WritableBytes() + PrependableBytes() < length + kCheapPrepend) {
        buffer_.resize(writer_index_ + length);
        return;
    }

    const std::size_t readable = ReadableBytes();

    std::copy(
        Begin() + reader_index_,
        Begin() + writer_index_,
        Begin() + kCheapPrepend
    );

    reader_index_ = kCheapPrepend;
    writer_index_ = reader_index_ + readable;

    assert(readable == ReadableBytes());
}

}  // namespace tinyimx