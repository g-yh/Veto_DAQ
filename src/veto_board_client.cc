#include "veto_board_client.h"
#include "daq_log.h"

#include <cstring>
#include <endian.h>

namespace veto {

VetoBoardClient::VetoBoardClient(asio::io_context &io_context,
                                 RingBuffer &ring_buffer)
    : io_context_(io_context), strand_(asio::make_strand(io_context_)),
      socket_(strand_), ring_buffer_(ring_buffer) {}

void VetoBoardClient::initBoard(const YAML::Node &board_node,
                                const YAML::Node &common_node) {
    name_ = board_node["name"].as<std::string>();
    server_ip_ = board_node["ip"].as<std::string>();
    if (board_node["port"]) {
        server_port_ = board_node["port"].as<uint16_t>();
    }

    if (common_node["seq_chk"]) {
        seq_chk_ = common_node["seq_chk"].as<int>() != 0;
    }
    if (common_node["head_length"]) {
        head_length_ = common_node["head_length"].as<size_t>();
    }
    if (common_node["data_length"]) {
        data_length_ = common_node["data_length"].as<size_t>();
    }
    if (common_node["event_number"]) {
        event_number_ = common_node["event_number"].as<size_t>();
    }
    event_size_ = (head_length_ + data_length_) * 2;

    DAQLog::getInstance().getDefaultLogger()->info(
        "[{}] initBoard: {}:{}, seq_chk={}, head={}, data={}, event_num={}, "
        "event_size={}",
        name_, server_ip_, server_port_, seq_chk_, head_length_, data_length_,
        event_number_, event_size_);
}

void VetoBoardClient::connect() {
    asio::post(strand_, [self = shared_from_this()]() { self->doConnect(); });
}

void VetoBoardClient::doConnect() {
    auto expected = ConnState::Disconnected;
    if (!conn_state_.compare_exchange_strong(expected, ConnState::Connecting)) {
        DAQLog::getInstance().getDefaultLogger()->warn(
            "[{}] not in Disconnected state, refuse to connect", name_);
        return;
    }

    tcp::resolver resolver(socket_.get_executor());
    auto endpoints = resolver.resolve(server_ip_, std::to_string(server_port_));
    if (endpoints.empty()) {
        DAQLog::getInstance().getDefaultLogger()->error(
            "[{}] could not resolve {}:{}", name_, server_ip_, server_port_);
        setConnState(ConnState::Disconnected);
        return;
    }

    asio::async_connect(
        socket_, endpoints,
        [self = shared_from_this()](asio::error_code ec,
                                    tcp::endpoint endpoint) {
            self->handleConnect(ec, endpoint);
        });
}

void VetoBoardClient::handleConnect(const asio::error_code &ec,
                                    tcp::endpoint endpoint) {
    asio::post(strand_, [self = shared_from_this(), ec]() {
        if (ec) {
            DAQLog::getInstance().getDefaultLogger()->error(
                "[{}] connect failed: {}", self->name_, ec.message());
            self->setConnState(ConnState::Disconnected);
            return;
        }
        DAQLog::getInstance().getDefaultLogger()->info(
            "[{}] connected to {}:{}", self->name_, self->server_ip_,
            self->server_port_);
        self->setConnState(ConnState::Connected);
    });
}

void VetoBoardClient::startReceiving() {
    asio::post(strand_, [this, self = shared_from_this()]() {
        if (conn_state_.load() != ConnState::Connected) {
            DAQLog::getInstance().getDefaultLogger()->warn(
                "[{}] not connected, refuse to receive events", name_);
            return;
        }

        receiving_.store(true);
        events_received_ = 0;
        recv_buffer_.clear();

        DAQLog::getInstance().getDefaultLogger()->info(
            "[{}] start continuous receiving", name_);

        doReceiveEvents();
    });
}

void VetoBoardClient::stopReceiving() {
    receiving_.store(false);
    asio::post(strand_, [this, self = shared_from_this()]() {
        asio::error_code ec;
        socket_.cancel(ec);
    });
}

void VetoBoardClient::stopReceivingSync() {
    receiving_.store(false);
    asio::error_code ec;
    socket_.close(ec);
}

void VetoBoardClient::doReceiveEvents() {
    auto self = shared_from_this();
    socket_.async_read_some(
        asio::buffer(raw_buffer_),
        [this, self](asio::error_code ec, std::size_t length) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    DAQLog::getInstance().getDefaultLogger()->error(
                        "[{}] read error: {}", name_, ec.message());
                    handleDisconnect();
                }
                return;
            }

            bytes_read_.fetch_add(length);

            recv_buffer_.insert(recv_buffer_.end(), raw_buffer_.begin(),
                                raw_buffer_.begin() +
                                    static_cast<std::ptrdiff_t>(length));

            while (recv_buffer_.size() >= event_size_ &&
                   receiving_.load()) {

                BoardEventHeader hdr;
                if (!parseEvent(recv_buffer_.data(), hdr)) {
                    DAQLog::getInstance().getDefaultLogger()->error(
                        "[{}] failed to parse event header", name_);
                    handleDisconnect();
                    return;
                }

                if (seq_chk_ &&
                    !checkContinuity(hdr.channel_num, hdr.trig_id)) {
                    DAQLog::getInstance().getDefaultLogger()->error(
                        "[{}] continuity check failed, stopping", name_);
                    handleDisconnect();
                    return;
                }

                ring_buffer_.push(recv_buffer_.data(), event_size_);
                recv_buffer_.erase(
                    recv_buffer_.begin(),
                    recv_buffer_.begin() +
                        static_cast<std::ptrdiff_t>(event_size_));
                events_received_++;
            }

            if (!receiving_.load()) {
                DAQLog::getInstance().getDefaultLogger()->info(
                    "[{}] receiving stopped, {} events received", name_,
                    events_received_);
                return;
            }

            doReceiveEvents();
        });
}

bool VetoBoardClient::parseEvent(const std::byte *raw,
                                 BoardEventHeader &hdr) {
    std::memcpy(&hdr, raw, sizeof(BoardEventHeader));
    hdr.trig_id = be32toh(hdr.trig_id);
    hdr.channel_num = be16toh(hdr.channel_num);
    hdr.tdc_bin = be16toh(hdr.tdc_bin);
    return true;
}

bool VetoBoardClient::checkContinuity(uint16_t channel_num,
                                      uint32_t trig_id) {
    auto it = last_trig_id_.find(channel_num);
    if (it != last_trig_id_.end()) {
        uint32_t expected = it->second + 1;
        if (trig_id != expected) {
            DAQLog::getInstance().getDefaultLogger()->error(
                "[{}] channel {} trig_id discontinuity: expected {}, got {}",
                name_, channel_num, expected, trig_id);
            if (cont_err_cb_) {
                cont_err_cb_(channel_num, expected, trig_id);
            }
            return false;
        }
    }
    last_trig_id_[channel_num] = trig_id;
    return true;
}

void VetoBoardClient::disconnect() {
    asio::post(strand_, [self = shared_from_this()]() {
        auto expected = ConnState::Connected;
        if (!self->conn_state_.compare_exchange_strong(
                expected, ConnState::Disconnecting)) {
            return;
        }
        self->handleDisconnect();
    });
}

bool VetoBoardClient::connected() const {
    return socket_.is_open() &&
           conn_state_.load() == ConnState::Connected;
}

void VetoBoardClient::handleDisconnect() {
    asio::error_code ec;
    socket_.cancel(ec);
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    recv_buffer_.clear();
    last_trig_id_.clear();
    setConnState(ConnState::Disconnected);
    DAQLog::getInstance().getDefaultLogger()->info("[{}] disconnected", name_);
}

void VetoBoardClient::setConnState(ConnState new_state) {
    conn_state_.store(new_state, std::memory_order_release);
}

} // namespace veto
