#include "board_manager.h"
#include "daq_log.h"

namespace veto {

BoardManager::BoardManager(asio::io_context &io_context)
    : io_context_(io_context) {}

bool BoardManager::loadConfig(const std::string &yaml_path) {
    try {
        auto root = YAML::LoadFile(yaml_path);

        common_node_ = root["all_boards_config"];
        if (!common_node_) {
            DAQLog::getInstance().getDefaultLogger()->warn(
                "all_boards_config not found in {}, using defaults",
                yaml_path);
        }

        board_nodes_ = root["board_config"];
        if (!board_nodes_) {
            DAQLog::getInstance().getDefaultLogger()->error(
                "board_config not found in {}", yaml_path);
            return false;
        }

        DAQLog::getInstance().getDefaultLogger()->info(
            "loaded config from {}: {} boards", yaml_path,
            board_nodes_.size());
        return true;

    } catch (const std::exception &e) {
        DAQLog::getInstance().getDefaultLogger()->error(
            "failed to load board config: {}", e.what());
        return false;
    }
}

void BoardManager::connectAll() {
    ring_buffer_ = std::make_shared<RingBuffer>();

    for (std::size_t i = 0; i < board_nodes_.size(); ++i) {
        const auto &b = board_nodes_[i];

        bool enabled = true;
        if (b["enable"]) {
            enabled = b["enable"].as<int>() != 0;
        }
        if (!enabled) {
            DAQLog::getInstance().getDefaultLogger()->info(
                "board [{}] is disabled, skip",
                b["name"] ? b["name"].as<std::string>() : "unknown");
            continue;
        }

        auto client =
            std::make_shared<VetoBoardClient>(io_context_, *ring_buffer_);
        client->initBoard(b, common_node_);

        boards_.push_back(client);
        client->connect();
    }
    DAQLog::getInstance().getDefaultLogger()->info(
        "connected {} boards, shared ring_buffer", boards_.size());
}

void BoardManager::disconnectAll() {
    for (auto &client : boards_) {
        client->disconnect();
    }
    boards_.clear();
    ring_buffer_.reset();
}

std::shared_ptr<VetoBoardClient> BoardManager::getBoard(
    const std::string &name) {
    for (auto &client : boards_) {
        if (client->name() == name) {
            return client;
        }
    }
    return nullptr;
}

} // namespace veto
