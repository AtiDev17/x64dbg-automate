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

LogBuffer::Snapshot LogBuffer::get_since(size_t since_index) const {
    std::lock_guard<std::mutex> lk(mtx);
    Snapshot snap;
    size_t start = (since_index > base_index) ? (since_index - base_index) : 0;
    for (size_t i = start; i < entries.size(); i++)
        snap.entries.push_back(entries[i]);
    snap.next_index = base_index + entries.size();
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
