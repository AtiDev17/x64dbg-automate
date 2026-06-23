#include "xauto_log.h"
#include "pluginmain.h"
#include <detours/detours.h>
#include <msgpack.hpp>

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

void WINAPI hook_GuiAddLogMessage(const char* msg) {
    orig_GuiAddLogMessage(msg);

    static thread_local bool in_hook = false;
    if (in_hook || !msg) return;
    in_hook = true;

    g_log_buffer.append(msg);

    if (srv) {
        msgpack::sbuffer buf;
        msgpack::pack(buf, std::make_tuple(
            std::string("EVENT_LOG_MESSAGE"), std::string(msg)));
        srv->pub_send(buf);
    }

    in_hook = false;
}

void log_hook_install() {
    HMODULE hBridge = GetModuleHandleA("x64bridge.dll");
    if (!hBridge) return;
    orig_GuiAddLogMessage = reinterpret_cast<GuiAddLogMessage_t>(
        GetProcAddress(hBridge, "GuiAddLogMessage"));
    if (!orig_GuiAddLogMessage) return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<void**>(&orig_GuiAddLogMessage),
                 hook_GuiAddLogMessage);
    DetourTransactionCommit();
}

void log_hook_uninstall() {
    if (!orig_GuiAddLogMessage) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<void**>(&orig_GuiAddLogMessage),
                 hook_GuiAddLogMessage);
    DetourTransactionCommit();
}
