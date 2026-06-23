#pragma once
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class LogBuffer {
public:
    static constexpr size_t MAX_ENTRIES = 50'000;
    static constexpr size_t MAX_BYTES   = 20 * 1024 * 1024; // 20 MB

    void append(const char* msg);
    void clear();

    struct Snapshot {
        std::vector<std::string> entries;
        size_t next_index;
    };
    Snapshot get_since(size_t since_index) const;

private:
    mutable std::mutex      mtx;
    std::deque<std::string> entries;
    size_t base_index  = 0;
    size_t total_bytes = 0;
};

extern LogBuffer g_log_buffer;

void log_hook_install();
void log_hook_uninstall();
