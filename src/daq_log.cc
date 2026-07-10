#include "daq_log.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace veto {

static std::vector<spdlog::sink_ptr> create_sinks(const std::string &name,
                                                   const YAML::Node &c) {
    std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;
    for (size_t i = 0; i < c.size(); ++i) {
        const auto &s = c[i];
        auto level = spdlog::level::warn;
        if (s["level"]) {
            level = spdlog::level::from_str(s["level"].as<std::string>());
            DAQLog::getDefaultLogger()->info("logger {} sink level = {}", name,
                                              s["level"].as<std::string>());
        }
        std::string type = "stdout";
        if (s["type"]) {
            type = s["type"].as<std::string>();
        }
        if (type == "stdout") {
            sinks.emplace_back(new spdlog::sinks::stdout_color_sink_mt());
            sinks.back()->set_level(level);
            continue;
        } else if (type == "stderr") {
            sinks.emplace_back(new spdlog::sinks::stderr_color_sink_mt());
            sinks.back()->set_level(level);
            continue;
        }
        if (type != "file") {
            DAQLog::getInstance().getDefaultLogger()->warn(
                "not supported log type: {} for logger {}, will not create it.",
                type, name);
            continue;
        }
        if (!s["path"]) {
            DAQLog::getInstance().getDefaultLogger()->warn(
                "no path provide for file logger: {}, will not create it.",
                name);
            continue;
        }
        sinks.emplace_back(new spdlog::sinks::basic_file_sink_mt(
            s["path"].as<std::string>(), true));
        if (s["level"])
            DAQLog::getInstance().getDefaultLogger()->info(
                "logger {} file sink level = {}", name,
                s["level"].as<std::string>());
        sinks.back()->set_level(level);
    }
    return sinks;
}

void DAQLog::init(const YAML::Node &conf) {
    if (conf.Type() == YAML::NodeType::Null) {
        getDefaultLogger()->warn("null config");
        return;
    }
    auto default_level = spdlog::level::warn;
    for (std::size_t i = 0; i < conf.size(); ++i) {
        const auto &c = conf[i];
        if (!c["name"]) {
            continue;
        }
        auto name = c["name"].as<std::string>();
        auto level = default_level;
        if (c["level"]) {
            level = spdlog::level::from_str(c["level"].as<std::string>());
        }
        if (!c["sinks"]) {
            auto logger = spdlog::stdout_color_mt(name);
            logger->set_level(level);
            logs_.emplace(name, logger);
            continue;
        }
        auto sinks = create_sinks(name, c["sinks"]);
        auto logger = std::make_shared<spdlog::async_logger>(
            name, sinks.begin(), sinks.end(), spdlog::thread_pool());
        if (logs_.find(name) != logs_.end()) {
            DAQLog::getDefaultLogger()->warn("remove logger {}", name);
            logs_.erase(name);
        }
        if (name == "default") {
            spdlog::set_default_logger(logger);
        }
        logs_.emplace(name, logger);
    }
}
} // namespace veto
