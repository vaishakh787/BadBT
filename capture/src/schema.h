#pragma once

// BadBT Detection — Key Event Schema
// Matches the JSONL format defined in IMPLEMENTATION_PLAN.md §2

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace badbt {

// ─── Enums ───────────────────────────────────────────────────────────

enum class Label {
    Human,
    Injected
};

enum class PayloadVariant {
    None,
    MaxSpeed,
    Fixed50ms,
    Fixed100ms,
    Random20_150ms
};

// ─── Key Event ───────────────────────────────────────────────────────

struct KeyEvent {
    uint64_t    seq;                // Monotonic per-session event counter
    int64_t     timestamp_ns;
    int64_t     qpc_ticks;          // QueryPerformanceCounter — monotonic, high-res, adjustment-immune
    std::string device_path;       // UTF-8 encoded device path
    std::string device_handle;     // Hex HANDLE value (detects reconnects within a session)
    uint16_t    vkey;
    uint16_t    scan_code;
    uint16_t    flags;             // RI_KEY_MAKE (0) or RI_KEY_BREAK (1) + others
    uint32_t    message;           // WM_KEYDOWN / WM_KEYUP / WM_SYSKEYDOWN / WM_SYSKEYUP
    bool        is_autorepeat;     // True if keydown arrived while same key already held
    Label       label;
    std::string subject_id;
    std::string session_id;
    PayloadVariant payload_variant;
};

// ─── JSON Serialization (nlohmann/json) ──────────────────────────────

// clang-format off
NLOHMANN_JSON_SERIALIZE_ENUM(Label, {
    {Label::Human,    "human"},
    {Label::Injected, "injected"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(PayloadVariant, {
    {PayloadVariant::None,           nullptr},
    {PayloadVariant::MaxSpeed,       "max_speed"},
    {PayloadVariant::Fixed50ms,      "fixed_50ms"},
    {PayloadVariant::Fixed100ms,     "fixed_100ms"},
    {PayloadVariant::Random20_150ms, "random_20_150ms"},
})
// clang-format on

inline void to_json(nlohmann::json& j, const KeyEvent& e) {
    j = nlohmann::json{
        {"seq",              e.seq},
        {"timestamp_ns",     e.timestamp_ns},
        {"qpc_ticks",        e.qpc_ticks},
        {"device_path",      e.device_path},
        {"device_handle",    e.device_handle},
        {"vkey",             e.vkey},
        {"scan_code",        e.scan_code},
        {"flags",            e.flags},
        {"message",          e.message},
        {"is_autorepeat",    e.is_autorepeat},
        {"label",            e.label},
        {"subject_id",       e.subject_id},
        {"session_id",       e.session_id},
        {"payload_variant",  e.payload_variant}
    };
}

} // namespace badbt
