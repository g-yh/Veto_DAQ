#pragma once

#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "ring_buffer.h"

namespace veto {

enum class ConnState {
    Disconnected = 0,
    Connecting,
    Connected,
    Disconnecting
};

#pragma pack(push, 1)
struct BoardEventHeader {
    uint32_t trig_id;
    uint16_t channel_num;
    uint8_t  coarse_time_cnt[6];
    uint16_t tdc_bin;
};
#pragma pack(pop)

using ContinuityErrorCallback = std::function<void(uint16_t channel_num,
                                                   uint32_t expected,
                                                   uint32_t got)>;

class VetoBoardClient : public std::enable_shared_from_this<VetoBoardClient> {
  public:
    using tcp = asio::ip::tcp;

    VetoBoardClient(asio::io_context &io_context, RingBuffer &ring_buffer);

    void initBoard(const YAML::Node &board_node,
                   const YAML::Node &common_node);

    void setContinuityErrorCallback(ContinuityErrorCallback cb) {
        cont_err_cb_ = std::move(cb);
    }

    void connect();
    void disconnect();
    bool connected() const;

    void receiveEvents();

    const std::string &name() const { return name_; }

    uint64_t totalBytesRead() const { return bytes_read_.load(); }
    size_t eventsReceived() const { return events_received_; }

    size_t dataLength() const { return data_length_; }
    size_t headLength() const { return head_length_; }
    size_t eventNumber() const { return event_number_; }
    bool seqChkEnabled() const { return seq_chk_; }

  private:
    void doConnect();
    void handleConnect(const asio::error_code &ec, tcp::endpoint endpoint);
    void doReceiveEvents();
    bool parseEvent(const std::byte *raw, BoardEventHeader &hdr);
    bool checkContinuity(uint16_t channel_num, uint32_t trig_id);
    void handleDisconnect();
    void setConnState(ConnState new_state);

    std::string name_;
    std::string server_ip_;
    uint16_t server_port_ = 12345;

    asio::io_context &io_context_;
    asio::strand<asio::io_context::executor_type> strand_;
    tcp::socket socket_;
    std::array<std::byte, 65536> raw_buffer_;

    RingBuffer &ring_buffer_;
    std::atomic<ConnState> conn_state_{ConnState::Disconnected};
    std::atomic<uint64_t> bytes_read_{0};

    // event protocol
    size_t head_length_ = 7;
    size_t data_length_ = 57;
    size_t event_size_ = 128;
    size_t event_number_ = 1000;
    size_t events_received_ = 0;
    std::vector<std::byte> recv_buffer_;

    // continuity check
    bool seq_chk_ = false;
    std::map<uint16_t, uint32_t> last_trig_id_;
    ContinuityErrorCallback cont_err_cb_ = nullptr;
};

} // namespace veto
