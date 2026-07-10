#include "disk_writer.h"
#include "daq_log.h"

#include <ctime>
#include <sstream>

namespace veto {

DiskWriter::DiskWriter(RingBuffer &ring_buffer)
    : ring_buffer_(ring_buffer) {}

bool DiskWriter::start() {
    if (running_.load()) {
        DAQLog::getInstance().getDefaultLogger()->warn(
            "DiskWriter already running");
        return false;
    }
    if (filepath_.empty()) {
        DAQLog::getInstance().getDefaultLogger()->error(
            "DiskWriter: no file path set");
        return false;
    }
    if (!file_writer_.open(filepath_)) {
        DAQLog::getInstance().getDefaultLogger()->error(
            "DiskWriter: failed to open {}", filepath_);
        return false;
    }
    DAQLog::getInstance().getDefaultLogger()->info(
        "DiskWriter: writing to {}", filepath_);

    running_.store(true);
    file_open_time_ = std::chrono::steady_clock::now();
    bytes_written_this_file_ = 0;
    writer_thread_ = std::thread(&DiskWriter::writerLoop, this);
    return true;
}

void DiskWriter::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    file_writer_.close();
    DAQLog::getInstance().getDefaultLogger()->info(
        "DiskWriter stopped, total {} bytes written", bytes_written_.load());
}

void DiskWriter::rotate() {
    file_writer_.close();
    bytes_written_this_file_ = 0;

    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << filepath_ << "." << std::put_time(&tm, "%Y%m%d_%H%M%S");

    if (!file_writer_.open(oss.str())) {
        DAQLog::getInstance().getDefaultLogger()->error(
            "DiskWriter: rotate failed to open {}", oss.str());
        return;
    }
    file_open_time_ = std::chrono::steady_clock::now();
    DAQLog::getInstance().getDefaultLogger()->info(
        "DiskWriter: rotated to {}", oss.str());
}

void DiskWriter::writerLoop() {
    std::array<std::byte, 65536> buf;

    while (running_.load()) {
        auto avail = ring_buffer_.available();
        if (avail == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto to_read = std::min<std::size_t>(avail, buf.size());
        if (!ring_buffer_.pop(buf.data(), to_read)) {
            continue;
        }

        if (!file_writer_.write(buf.data(), to_read)) {
            DAQLog::getInstance().getDefaultLogger()->error(
                "DiskWriter: write error, stopping");
            break;
        }

        bytes_written_this_file_ += to_read;
        bytes_written_.fetch_add(to_read);

        // rotation by size
        if (rotation_size_ > 0 &&
            bytes_written_this_file_ >= rotation_size_) {
            rotate();
        }

        // rotation by duration
        if (rotation_duration_ > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - file_open_time_);
            if (elapsed.count() >= rotation_duration_) {
                rotate();
            }
        }
    }

    file_writer_.close();
}

} // namespace veto
