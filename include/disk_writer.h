#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#include "file_writer.h"
#include "ring_buffer.h"

namespace veto {

class DiskWriter {
  public:
    explicit DiskWriter(RingBuffer &ring_buffer);

    void setFilePath(const std::string &path) { filepath_ = path; }
    void setRotationDuration(uint32_t seconds) {
        rotation_duration_ = seconds;
    }
    void setRotationSize(uint64_t bytes) { rotation_size_ = bytes; }

    bool start();
    void stop();
    bool running() const { return running_.load(); }

    uint64_t totalBytesWritten() const { return bytes_written_.load(); }

  private:
    void writerLoop();
    void rotate();

    RingBuffer &ring_buffer_;
    FileWriter file_writer_;
    std::thread writer_thread_;

    std::string filepath_;
    std::atomic<bool> running_{false};

    std::chrono::steady_clock::time_point file_open_time_;
    uint32_t rotation_duration_ = 0;
    uint64_t rotation_size_ = 0;
    uint64_t bytes_written_this_file_ = 0;
    std::atomic<uint64_t> bytes_written_{0};
};

} // namespace veto
