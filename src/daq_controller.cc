#include "daq_controller.h"
#include "daq_log.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include <httplib.h>

namespace veto {

DAQController::DAQController()
    : http_server_(std::make_unique<httplib::Server>()) {

    handlers_["init"] = [this](const httplib::Request &req) { return handleInit(req); };
    handlers_["start"] = [this](const httplib::Request &req) { return handleStart(req); };
    handlers_["stop"] = [this](const httplib::Request &req) { return handleStop(req); };
    handlers_["status"] = [this](const httplib::Request &req) { return handleStatus(req); };

    auto daq_handler = [this](const httplib::Request &req,
                              httplib::Response &res) {
        std::string cmd;
        if (req.has_param("cmd")) {
            cmd = req.get_param_value("cmd");
        } else if (!req.body.empty()) {
            cmd = req.body;
        }
        if (cmd.empty()) {
            res.status = 400;
            res.set_content("missing cmd", "text/plain");
            return;
        }
        auto msg = processCommand(req, cmd);
        res.set_content(msg + "\n", "application/json");
    };

    http_server_->Get("/daq", daq_handler);
    http_server_->Post("/daq", daq_handler);
}

DAQController::~DAQController() { shutdown(); }

bool DAQController::boot(const std::string &config_path) {
    config_path_ = config_path;
    DAQLog::getInstance().getDefaultLogger()->info(
        "DAQController booting with config: {}", config_path_);

    work_ = std::make_unique<asio::io_context::work>(io_context_);

    return true;
}

void DAQController::run() {
    io_thread_ = std::thread([this]() {
        DAQLog::getInstance().getDefaultLogger()->info(
            "io_context thread started");
        io_context_.run();
    });

    http_thread_ = std::thread([this]() {
        DAQLog::getInstance().getDefaultLogger()->info(
            "HTTP server listening on 0.0.0.0:3080");
        http_server_->listen("0.0.0.0", 3080);
    });
}

void DAQController::shutdown() {
    if (http_server_ && http_server_->is_running()) {
        http_server_->stop();
    }
    if (http_thread_.joinable()) {
        http_thread_.join();
    }

    for (auto &b : board_manager_->boards()) {
        b->stopReceivingSync();
    }

    if (disk_writer_ && disk_writer_->running()) {
        DAQLog::getInstance().getDefaultLogger()->info(
            "shutdown: stopping disk writer (draining)...");
        disk_writer_->stop();
    }

    if (board_manager_) {
        board_manager_->disconnectAll();
    }

    work_.reset();
    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    disk_writer_.reset();

    DAQLog::getInstance().getDefaultLogger()->info(
        "DAQController shutdown complete");
}

void DAQController::setState(DaqState new_state) {
    state_.store(new_state);
    DAQLog::getInstance().getDefaultLogger()->info(
        "state -> {}", stateToString(new_state));
}

std::string DAQController::stateToString(DaqState s) {
    switch (s) {
    case DaqState::STOPPED:
        return "STOPPED";
    case DaqState::INITIALIZING:
        return "INITIALIZING";
    case DaqState::INITIALIZED:
        return "INITIALIZED";
    case DaqState::STARTING:
        return "STARTING";
    case DaqState::RUNNING:
        return "RUNNING";
    case DaqState::STOPPING:
        return "STOPPING";
    }
    return "UNKNOWN";
}

std::string DAQController::processCommand(const httplib::Request &req,
                                          const std::string &cmd) {
    auto it = handlers_.find(cmd);
    if (it != handlers_.end()) {
        return it->second(req);
    }
    return "{\"error\":\"invalid command\"}";
}

std::string DAQController::handleInit(const httplib::Request &) {
    auto expected = DaqState::STOPPED;
    if (!state_.compare_exchange_strong(expected, DaqState::INITIALIZING)) {
        return "{\"error\":\"not in STOPPED state, current: " +
               stateToString(state_.load()) + "\"}";
    }

    board_manager_ = std::make_unique<BoardManager>(io_context_);

    if (!board_manager_->loadConfig(config_path_)) {
        setState(DaqState::STOPPED);
        return "{\"error\":\"failed to load config\"}";
    }

    board_manager_->connectAll();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        io_context_.poll();
        bool all_done = true;
        for (auto &b : board_manager_->boards()) {
            auto s = b->connState();
            if (s == ConnState::Disconnected ||
                s == ConnState::Connecting) {
                all_done = false;
                break;
            }
        }
        if (all_done)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::ostringstream oss;
    oss << "{";
    oss << "\"status\":\"initialized\",\"boards\":[";
    bool first = true;
    for (auto &b : board_manager_->boards()) {
        if (!first)
            oss << ",";
        first = false;
        bool ok = b->connected();
        oss << "{\"name\":\"" << b->name() << "\",\"ip\":\"" << b->ip()
            << "\",\"port\":" << b->port() << ",\"connected\":"
            << (ok ? "true" : "false") << "}";
        std::cout << b->name() << " (" << b->ip() << "): "
                  << (ok ? "connected" : "failed") << std::endl;
    }
    oss << "]}";

    setState(DaqState::INITIALIZED);
    return oss.str();
}

std::string DAQController::handleStart(const httplib::Request &req) {
    auto expected = DaqState::INITIALIZED;
    if (!state_.compare_exchange_strong(expected, DaqState::STARTING)) {
        return "{\"error\":\"not in INITIALIZED state, current: " +
               stateToString(state_.load()) + "\"}";
    }

    std::string output = "veto_data.bin";
    if (req.has_param("output")) {
        output = req.get_param_value("output");
    } else {
        output = board_manager_->commonStr("output_path", output);
    }

    auto rb = board_manager_->ringBuffer();
    disk_writer_ = std::make_unique<DiskWriter>(*rb);
    disk_writer_->setFilePath(output);
    disk_writer_->setRotationDuration(3600);

    DAQLog::getInstance().getDefaultLogger()->info(
        "DiskWriter output: {}", output);

    if (!disk_writer_->start()) {
        setState(DaqState::INITIALIZED);
        return "{\"error\":\"failed to start DiskWriter\"}";
    }

    for (auto &b : board_manager_->boards()) {
        b->startReceiving();
    }

    setState(DaqState::RUNNING);
    DAQLog::getInstance().getDefaultLogger()->info(
        "started run on {} boards, output={}", board_manager_->boardCount(),
        output);

    return "{\"status\":\"running\",\"boards\":" +
           std::to_string(board_manager_->boardCount()) + ",\"output\":\"" +
           output + "\"}";
}

std::string DAQController::handleStop(const httplib::Request &) {
    auto expected = DaqState::RUNNING;
    if (!state_.compare_exchange_strong(expected, DaqState::STOPPING)) {
        return "{\"error\":\"not in RUNNING state, current: " +
               stateToString(state_.load()) + "\"}";
    }

    DAQLog::getInstance().getDefaultLogger()->info("stopping all boards...");

    for (auto &b : board_manager_->boards()) {
        b->stopReceivingSync();
    }

    DAQLog::getInstance().getDefaultLogger()->info("draining ring buffer...");

    disk_writer_->stop();

    board_manager_->disconnectAll();
    disk_writer_.reset();

    setState(DaqState::STOPPED);
    DAQLog::getInstance().getDefaultLogger()->info("stop complete");

    return "{\"status\":\"stopped\"}";
}

std::string DAQController::handleStatus(const httplib::Request &) {
    return "{\"status\":\"" + stateToString(state_.load()) + "\"}";
}

} // namespace veto
