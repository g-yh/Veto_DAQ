#pragma once

#include <map>
#include <string>

#include <spdlog/async.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

namespace veto {

class DAQLog {
  public:
    static DAQLog &getInstance() {
        static DAQLog instance;
        return instance;
    }

    void init(const YAML::Node &conf);

    static auto getDefaultLogger() { return getInstance().logs_["default"]; }

    static auto getLogger(const std::string &name) {
        if (getInstance().logs_.find(name) != getInstance().logs_.end()) {
            return getInstance().logs_[name];
        }
        return getInstance().logs_["default"];
    }

  private:
    DAQLog() : logs_{{"default", spdlog::stdout_color_mt("default")}} {
        spdlog::cfg::load_env_levels();
        spdlog::init_thread_pool(8000, 1);
    };

    DAQLog(const DAQLog &) = delete;
    DAQLog &operator=(const DAQLog &) = delete;

    std::map<std::string, std::shared_ptr<spdlog::logger>> logs_;
};

} // namespace veto
