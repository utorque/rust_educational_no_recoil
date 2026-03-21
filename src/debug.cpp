#include "debug.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // GetLocalTime / SYSTEMTIME

#include <fstream>
#include <sstream>

DebugLog& DebugLog::get() {
    static DebugLog instance;
    return instance;
}

static std::string currentTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::string(buf);
}

void DebugLog::log(const char* category, const std::string& msg) {
    DebugEntry entry;
    entry.timestamp = currentTimestamp();
    entry.category  = category ? category : "INFO";
    entry.message   = msg;

    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_entries.size() >= kMaxEntries)
        m_entries.erase(m_entries.begin());
    m_entries.push_back(std::move(entry));
}

void DebugLog::clear() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_entries.clear();
}

bool DebugLog::exportToFile(const std::string& path) const {
    std::ofstream f(path);
    if (!f.good()) return false;
    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto& e : m_entries)
        f << "[" << e.timestamp << "] [" << e.category << "] " << e.message << "\n";
    return f.good();
}

std::vector<DebugEntry> DebugLog::snapshot() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_entries;
}

size_t DebugLog::count() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_entries.size();
}
