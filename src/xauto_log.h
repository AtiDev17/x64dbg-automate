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
        size_t next_index;  // absolute index after the last returned entry; pass as since_index on the next call
        size_t remaining;   // matching entries beyond the returned set (0 when everything fits or limit=0)
        size_t evicted;     // entries between requested since_index and oldest available (lost to buffer cap)
    };
    // limit=0  -> no limit (return all matching entries)
    // filter="" -> no filter (return all entries); otherwise a case-sensitive plain
    //              substring (not a regex), matched per line within each entry.
    // Head semantics: the FIRST `limit` matching entries are returned.
    // next_index points just past the last returned entry (not the buffer end when truncated).
    //              Monotonic only within a session: clear() resets base_index to 0, so a
    //              next_index carried over from a prior session restarts at the new log.
    // remaining > 0 means the caller should call again with since_index=next_index.
    Snapshot get_since(size_t since_index, size_t limit = 0, const std::string& filter = "") const;

private:
    mutable std::mutex      mtx;
    std::deque<std::string> entries;
    size_t base_index  = 0;
    size_t total_bytes = 0;
};

extern LogBuffer g_log_buffer;

void log_hook_install();
void log_hook_uninstall();
