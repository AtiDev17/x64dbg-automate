#include "xauto_log.h"
#include "pluginmain.h"
#include <detours/detours.h>
#include <msgpack.hpp>
#include <atomic>

LogBuffer g_log_buffer;

// ---------------------------------------------------------------------------
// LogBuffer
// ---------------------------------------------------------------------------

void LogBuffer::append(const char* msg) {
    if (!msg) return;
    std::string s(msg);
    std::lock_guard<std::mutex> lk(mtx);
    while (!entries.empty() &&
           (entries.size() >= MAX_ENTRIES || total_bytes + s.size() > MAX_BYTES)) {
        total_bytes -= entries.front().size();
        entries.pop_front();
        base_index++;
    }
    total_bytes += s.size();
    entries.push_back(std::move(s));
}

void LogBuffer::clear() {
    std::lock_guard<std::mutex> lk(mtx);
    entries.clear();
    total_bytes = 0;
    base_index  = 0;
}

LogBuffer::Snapshot LogBuffer::get_since(size_t since_index, size_t limit, const std::string& filter) const {
    std::lock_guard<std::mutex> lk(mtx);
    Snapshot snap;
    snap.evicted   = (since_index < base_index) ? (base_index - since_index) : 0;
    snap.remaining = 0;

    size_t start = (since_index > base_index) ? (since_index - base_index) : 0;

    // Head semantics: collect the first `limit` matching entries, count the rest.
    // When filter is set, filtering is done at the line level within each entry:
    // only lines containing the filter string are kept; entries with no matching
    // lines are skipped entirely.
    bool limit_hit = false;
    size_t last_i  = start;
    for (size_t i = start; i < entries.size(); i++) {
        // Line-level filtering: when a filter is active, split the entry on '\n'
        // and retain only matching lines. Skip the entry if none match.
        std::string candidate;
        if (!filter.empty()) {
            std::string_view src(entries[i]);
            std::string filtered;
            while (!src.empty()) {
                auto nl = src.find('\n');
                std::string_view line = (nl == std::string_view::npos) ? src : src.substr(0, nl + 1);
                if (line.find(filter) != std::string_view::npos)
                    filtered.append(line);
                src = (nl == std::string_view::npos) ? std::string_view{} : src.substr(nl + 1);
            }
            if (filtered.empty()) continue;
            candidate = std::move(filtered);
        } else {
            candidate = entries[i];
        }
        if (!limit_hit) {
            snap.entries.push_back(std::move(candidate));
            last_i = i + 1;
            if (limit > 0 && snap.entries.size() >= limit)
                limit_hit = true;
        } else {
            snap.remaining++;
        }
    }

    // next_index points just past the last returned entry.
    // When nothing was truncated it equals the true buffer end.
    snap.next_index = limit_hit ? (base_index + last_i) : (base_index + entries.size());

    return snap;
}

// ---------------------------------------------------------------------------
// Hook
// ---------------------------------------------------------------------------

using GuiAddLogMessage_t = void(WINAPI*)(const char*);
static GuiAddLogMessage_t orig_GuiAddLogMessage = nullptr;
static GuiAddLogMessage_t orig_GuiAddLogMessageHtml = nullptr;

// Cleared at the start of uninstall, before DetourDetach. DetourDetach does not
// drain hook invocations already in flight on x64dbg's log-task thread, so this
// flag lets such invocations bail out of the publish path during teardown.
static std::atomic<bool> g_log_hook_active{ false };

// Buffer + publish a captured log line. Runs on x64dbg's log-task thread, so it
// must never let an exception escape: one propagating out of a Detours hook into
// x64dbg's C call site would terminate the process. Dropping the line is the
// safe failure mode (e.g. when the PUB socket was never bound because session
// acquisition failed at load, or is being torn down).
static void capture_log(const char* msg) {
    if (!msg || !g_log_hook_active.load(std::memory_order_acquire)) return;

    static thread_local bool in_hook = false;
    if (in_hook) return;
    in_hook = true;

    try {
        g_log_buffer.append(msg);

        if (srv) {
            msgpack::sbuffer buf;
            msgpack::pack(buf, std::make_tuple(
                std::string("EVENT_LOG_MESSAGE"), std::string(msg)));
            srv->pub_send(buf);
        }
    } catch (...) {
        // Swallow: see note above.
    }

    in_hook = false;
}

void WINAPI hook_GuiAddLogMessage(const char* msg) {
    orig_GuiAddLogMessage(msg);
    capture_log(msg);
}

void WINAPI hook_GuiAddLogMessageHtml(const char* msg) {
    orig_GuiAddLogMessageHtml(msg);
    capture_log(msg);
}

void log_hook_install() {
    HMODULE hBridge = GetModuleHandleA("x64bridge.dll");
    if (!hBridge) return;
    orig_GuiAddLogMessage = reinterpret_cast<GuiAddLogMessage_t>(
        GetProcAddress(hBridge, "GuiAddLogMessage"));
    orig_GuiAddLogMessageHtml = reinterpret_cast<GuiAddLogMessage_t>(
        GetProcAddress(hBridge, "GuiAddLogMessageHtml"));
    if (!orig_GuiAddLogMessage && !orig_GuiAddLogMessageHtml) return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (orig_GuiAddLogMessage)
        DetourAttach(reinterpret_cast<void**>(&orig_GuiAddLogMessage),
                     hook_GuiAddLogMessage);
    if (orig_GuiAddLogMessageHtml)
        DetourAttach(reinterpret_cast<void**>(&orig_GuiAddLogMessageHtml),
                     hook_GuiAddLogMessageHtml);
    DetourTransactionCommit();

    g_log_hook_active.store(true, std::memory_order_release);
}

void log_hook_uninstall() {
    g_log_hook_active.store(false, std::memory_order_release);
    if (!orig_GuiAddLogMessage && !orig_GuiAddLogMessageHtml) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (orig_GuiAddLogMessage)
        DetourDetach(reinterpret_cast<void**>(&orig_GuiAddLogMessage),
                     hook_GuiAddLogMessage);
    if (orig_GuiAddLogMessageHtml)
        DetourDetach(reinterpret_cast<void**>(&orig_GuiAddLogMessageHtml),
                     hook_GuiAddLogMessageHtml);
    DetourTransactionCommit();
}
