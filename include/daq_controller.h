#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include <asio.hpp>

#include <httplib.h>

#include "board_manager.h"
#include "disk_writer.h"

namespace veto {

enum class DaqState {
    STOPPED = 0,
    INITIALIZING,
    INITIALIZED,
    STARTING,
    RUNNING,
    STOPPING
};

class DAQController {
  public:
    static DAQController &getInstance() {
        static DAQController instance;
        return instance;
    }

    bool boot(const std::string &config_path);
    void run();
    void shutdown();

    DaqState state() const { return state_.load(); }

  private:
    DAQController();
    ~DAQController();

    DAQController(const DAQController &) = delete;
    DAQController &operator=(const DAQController &) = delete;

    std::string handleInit(const httplib::Request &req);
    std::string handleStart(const httplib::Request &req);
    std::string handleStop(const httplib::Request &req);
    std::string handleStatus(const httplib::Request &req);

    std::string processCommand(const httplib::Request &req,
                               const std::string &cmd);

    void setState(DaqState new_state);
    static std::string stateToString(DaqState s);

    std::atomic<DaqState> state_{DaqState::STOPPED};

    asio::io_context io_context_;
    std::unique_ptr<asio::io_context::work> work_;
    std::unique_ptr<BoardManager> board_manager_;
    std::unique_ptr<DiskWriter> disk_writer_;

    std::unique_ptr<httplib::Server> http_server_;
    std::thread http_thread_;
    std::thread io_thread_;

    std::string config_path_;

    using Handler = std::function<std::string(const httplib::Request &)>;
    std::unordered_map<std::string, Handler> handlers_;
};

} // namespace veto
