#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <vector>

namespace veto {

class RingBuffer {
  public:
    RingBuffer(std::size_t capacity = 64 * 1024 * 1024)
        : buffer_(capacity), capacity_(capacity) {}

    bool push(const std::byte *data, std::size_t size) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (size > capacity_ - (write_pos_ - read_pos_)) {
            return false;
        }
        std::size_t offset = write_pos_ % capacity_;
        std::size_t first_chunk = std::min(size, capacity_ - offset);
        std::memcpy(buffer_.data() + offset, data, first_chunk);
        if (first_chunk < size) {
            std::memcpy(buffer_.data(), data + first_chunk, size - first_chunk);
        }
        write_pos_ += size;
        avail_.store(write_pos_ - read_pos_, std::memory_order_release);
        return true;
    }

    bool pop(std::byte *data, std::size_t size) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (size > write_pos_ - read_pos_) {
            return false;
        }
        std::size_t offset = read_pos_ % capacity_;
        std::size_t first_chunk = std::min(size, capacity_ - offset);
        std::memcpy(data, buffer_.data() + offset, first_chunk);
        if (first_chunk < size) {
            std::memcpy(data + first_chunk, buffer_.data(), size - first_chunk);
        }
        read_pos_ += size;
        avail_.store(write_pos_ - read_pos_, std::memory_order_release);
        return true;
    }

    std::size_t available() const {
        return avail_.load(std::memory_order_acquire);
    }

    bool empty() const {
        return available() == 0;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        read_pos_ = 0;
        write_pos_ = 0;
        avail_.store(0, std::memory_order_release);
    }

  private:
    mutable std::mutex mtx_;
    std::vector<std::byte> buffer_;
    std::size_t capacity_;
    std::size_t write_pos_ = 0;
    std::size_t read_pos_ = 0;
    std::atomic<std::size_t> avail_{0};
};

} // namespace veto
