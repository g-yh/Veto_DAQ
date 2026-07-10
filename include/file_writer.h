#pragma once

#include <cstddef>
#include <fstream>
#include <functional>
#include <string>

namespace veto {

class FileWriter {
  public:
    using ErrorCallback = std::function<void(const std::string &)>;

    explicit FileWriter(ErrorCallback error_cb = {})
        : error_callback_(std::move(error_cb)) {}

    ~FileWriter() { close(); }

    FileWriter(const FileWriter &) = delete;
    FileWriter &operator=(const FileWriter &) = delete;

    bool open(const std::string &path) {
        close();
        ofs_.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!ofs_) {
            reportError("failed to open file: " + path);
            return false;
        }
        return true;
    }

    void close() {
        if (ofs_.is_open()) {
            ofs_.close();
        }
    }

    bool isOpen() const noexcept { return ofs_.is_open(); }

    bool write(const std::byte *data, std::size_t size) {
        if (!ofs_) {
            reportError("write on closed file");
            return false;
        }
        ofs_.write(reinterpret_cast<const char *>(data),
                   static_cast<std::streamsize>(size));
        if (!ofs_) {
            reportError("ofstream write failed");
            return false;
        }
        return true;
    }

    void flush() {
        if (ofs_) {
            ofs_.flush();
        }
    }

    void setErrorCallback(ErrorCallback cb) {
        error_callback_ = std::move(cb);
    }

  private:
    void reportError(const std::string &msg) {
        if (error_callback_) {
            error_callback_(msg);
        }
    }

    std::ofstream ofs_;
    ErrorCallback error_callback_;
};

} // namespace veto
