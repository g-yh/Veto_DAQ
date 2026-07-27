#include "daq_controller.h"

#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>

static std::atomic<bool> running{true};

static void signalHandler(int) { running.store(false); }

int main(int argc, char *argv[]) {
    namespace fs = std::filesystem;

    std::string config_path;
    if (argc > 1) {
        config_path = argv[1];
    } else {
        auto exe_dir = fs::path(argv[0]).parent_path();
        config_path = (exe_dir / "../config/board_config.yaml").lexically_normal();
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    auto &controller = veto::DAQController::getInstance();
    if (!controller.boot(config_path)) {
        std::cerr << "Failed to boot DAQController" << std::endl;
        return 1;
    }

    std::cout << "veto-daq started. Send commands via:" << std::endl;
    std::cout << "  curl \"http://localhost:3080/daq?cmd=init\"" << std::endl;
    std::cout << "  curl \"http://localhost:3080/daq?cmd=start\"" << std::endl;
    std::cout << "  curl \"http://localhost:3080/daq?cmd=stop\"" << std::endl;
    std::cout << "  curl \"http://localhost:3080/daq?cmd=status\"" << std::endl;

    controller.run();

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "Shutting down..." << std::endl;
    controller.shutdown();

    return 0;
}
