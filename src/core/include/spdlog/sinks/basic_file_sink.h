#pragma once
// Drop-in stub for spdlog's basic_file_sink_mt
// Writes log messages to a file via std::ofstream.
#include <spdlog/spdlog.h>
#include <fstream>

namespace spdlog { namespace sinks {
class basic_file_sink_mt : public sink {
public:
    explicit basic_file_sink_mt(const std::string& filename, bool truncate = false) {
        auto mode = truncate ? std::ios::out : std::ios::app;
        file_.open(filename, mode);
    }

    void log(const std::string& msg) override {
        if (file_.is_open()) {
            file_ << msg << std::endl;
        }
    }

    void flush() override {
        if (file_.is_open()) {
            file_.flush();
        }
    }

private:
    std::ofstream file_;
};
} }
