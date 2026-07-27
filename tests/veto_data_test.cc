#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <endian.h>
#include <iostream>
#include <thread>

using asio::ip::tcp;

constexpr int PORT = 12345;
constexpr size_t PACKET_SIZE = 128;
constexpr size_t HEADER_SIZE = 14;
constexpr size_t DATA_SIZE = PACKET_SIZE - HEADER_SIZE;
constexpr int FREQ_HZ = 1000;
constexpr int DURATION_SEC = 10;

#pragma pack(push, 1)
struct PMTHeader {
    uint32_t trig_id;
    uint16_t channel_id;
    uint8_t coarse_time_cnt[6];
    uint16_t tdc_bin;
};
#pragma pack(pop)

static_assert(sizeof(PMTHeader) == HEADER_SIZE,
              "PMTHeader must be 14 bytes");

int main() {
    try {
        asio::io_context io;
        tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), PORT));

        std::cout << "============================================" << std::endl;
        std::cout << "  veto_data_test" << std::endl;
        std::cout << "  listening on port " << PORT << std::endl;
        std::cout << "============================================" << std::endl;

        tcp::socket socket(io);
        acceptor.accept(socket);
        std::cout << "[client connected]" << std::endl;

        std::cout << "Press Enter to start sending data "
                  << "(" << FREQ_HZ << " Hz, " << DURATION_SEC << " s) ..."
                  << std::endl;
        std::cin.get();

        std::vector<uint8_t> packet(PACKET_SIZE);
        auto *hdr = reinterpret_cast<PMTHeader *>(packet.data());
        uint8_t *data = packet.data() + HEADER_SIZE;

        hdr->channel_id = htobe16(4);
        std::memset(hdr->coarse_time_cnt, 0, sizeof(hdr->coarse_time_cnt));
        hdr->tdc_bin = htobe16(0);

        uint8_t data_byte = 0;
        int total = FREQ_HZ * DURATION_SEC;

        auto interval = std::chrono::microseconds(1000000 / FREQ_HZ);
        auto next = std::chrono::steady_clock::now();
        auto start_time = next;

        for (int i = 0; i < total; ++i) {
            next += interval;

            hdr->trig_id = htobe32(static_cast<uint32_t>(i));

            for (size_t j = 0; j < DATA_SIZE; ++j) {
                data[j] = data_byte++;
            }

            asio::write(socket, asio::buffer(packet));

            std::this_thread::sleep_until(next);
        }

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

        std::cout << "[done] sent " << total << " packets in " << ms.count()
                  << " ms" << std::endl;
        std::cout << "  packet size : " << PACKET_SIZE << " B" << std::endl;
        std::cout << "  header      : " << HEADER_SIZE << " B"
                  << " (trig_id 4B + channel_id 2B + timestamp 6B + tdc 2B)"
                  << std::endl;
        std::cout << "  data        : " << DATA_SIZE << " B"
                  << " (0x00-0xFF cycling)" << std::endl;
        std::cout << "  channel_id  : 4" << std::endl;
        std::cout << "  total data  : "
                  << (static_cast<uint64_t>(total) * PACKET_SIZE) / 1024
                  << " KB" << std::endl;

    } catch (std::exception &e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
