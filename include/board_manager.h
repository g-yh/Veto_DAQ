#pragma once

#include <memory>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "ring_buffer.h"
#include "veto_board_client.h"

namespace veto {

class BoardManager {
  public:
    BoardManager(asio::io_context &io_context);

    bool loadConfig(const std::string &yaml_path);

    void connectAll();
    void disconnectAll();

    std::shared_ptr<VetoBoardClient> getBoard(const std::string &name);

    const std::vector<std::shared_ptr<VetoBoardClient>> &boards() const {
        return boards_;
    }

    std::size_t boardCount() const { return boards_.size(); }

  private:
    asio::io_context &io_context_;
    YAML::Node common_node_;
    YAML::Node board_nodes_;
    std::shared_ptr<RingBuffer> ring_buffer_;
    std::vector<std::shared_ptr<VetoBoardClient>> boards_;
};

} // namespace veto
