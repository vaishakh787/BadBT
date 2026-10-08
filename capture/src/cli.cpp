#include "cli.h"
#include <iostream>
#include <cstring>

namespace badbt {

void print_usage(const char* program_name) {
    std::cout
        << "BadBT Capture Tool — Bluetooth HID keystroke logger\n\n"
        << "Usage:\n"
        << "  " << program_name << " --output PATH --label LABEL --subject ID --session ID [options]\n\n"
        << "Required:\n"
        << "  --output PATH            Output JSONL file path\n"
        << "  --label LABEL            Event label: human | injected\n"
        << "  --subject ID             Subject identifier (e.g. s03)\n"
        << "  --session ID             Session identifier (e.g. s03_free_01)\n\n"
        << "Optional:\n"
        << "  --duration SECONDS       Capture duration (default: unlimited, Ctrl+C to stop)\n"
        << "  --payload-variant VAR    Injection variant: max_speed | fixed_50ms | fixed_100ms | random_20_150ms\n"
        << "  --no-bt-filter           Capture all keyboards, not just Bluetooth\n"
        << "  --help                   Show this help message\n\n"
        << "Examples:\n"
        << "  " << program_name << " --output data/raw/s03_free_01.jsonl --label human --subject s03 --session s03_free_01 --duration 300\n"
        << "  " << program_name << " --output data/raw/inj_max_01.jsonl --label injected --subject inj --session inj_max_01 --payload-variant max_speed\n";
}

static bool str_eq(const char* a, const char* b) {
    return std::strcmp(a, b) == 0;
}

static PayloadVariant parse_variant(const char* s) {
    if (str_eq(s, "max_speed"))        return PayloadVariant::MaxSpeed;
    if (str_eq(s, "fixed_50ms"))       return PayloadVariant::Fixed50ms;
    if (str_eq(s, "fixed_100ms"))      return PayloadVariant::Fixed100ms;
    if (str_eq(s, "random_20_150ms"))  return PayloadVariant::Random20_150ms;
    return PayloadVariant::None; // sentinel — will trigger validation error
}

bool parse_args(int argc, char* argv[], CaptureConfig& config) {
    bool has_output  = false;
    bool has_label   = false;
    bool has_subject = false;
    bool has_session = false;

    for (int i = 1; i < argc; ++i) {
        if (str_eq(argv[i], "--help") || str_eq(argv[i], "-h")) {
            print_usage(argv[0]);
            return false;
        }
        else if (str_eq(argv[i], "--output") && i + 1 < argc) {
            config.output_path = argv[++i];
            has_output = true;
        }
        else if (str_eq(argv[i], "--label") && i + 1 < argc) {
            ++i;
            if (str_eq(argv[i], "human")) {
                config.label = Label::Human;
            } else if (str_eq(argv[i], "injected")) {
                config.label = Label::Injected;
            } else {
                std::cerr << "Error: --label must be 'human' or 'injected', got '" << argv[i] << "'\n";
                return false;
            }
            has_label = true;
        }
        else if (str_eq(argv[i], "--subject") && i + 1 < argc) {
            config.subject_id = argv[++i];
            has_subject = true;
        }
        else if (str_eq(argv[i], "--session") && i + 1 < argc) {
            config.session_id = argv[++i];
            has_session = true;
        }
        else if (str_eq(argv[i], "--duration") && i + 1 < argc) {
            config.duration_secs = std::atoi(argv[++i]);
            if (config.duration_secs <= 0) {
                std::cerr << "Error: --duration must be a positive integer\n";
                return false;
            }
        }
        else if (str_eq(argv[i], "--payload-variant") && i + 1 < argc) {
            config.payload_variant = parse_variant(argv[++i]);
            if (config.payload_variant == PayloadVariant::None) {
                std::cerr << "Error: invalid --payload-variant '" << argv[i]
                          << "' (expected: max_speed, fixed_50ms, fixed_100ms, random_20_150ms)\n";
                return false;
            }
        }
        else if (str_eq(argv[i], "--no-bt-filter")) {
            config.filter_bluetooth = false;
        }
        else {
            std::cerr << "Error: unknown argument '" << argv[i] << "'\n";
            print_usage(argv[0]);
            return false;
        }
    }

    // ── Validation ──
    if (!has_output || !has_label || !has_subject || !has_session) {
        std::cerr << "Error: missing required arguments (--output, --label, --subject, --session)\n\n";
        print_usage(argv[0]);
        return false;
    }

    if (config.label == Label::Injected && config.payload_variant == PayloadVariant::None) {
        std::cerr << "Warning: --label injected without --payload-variant; variant will be null in output\n";
    }

    if (config.label == Label::Human && config.payload_variant != PayloadVariant::None) {
        std::cerr << "Error: --payload-variant is only valid with --label injected\n";
        return false;
    }

    return true;
}

} // namespace badbt
