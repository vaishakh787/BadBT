#pragma once

// BadBT Detection — CLI Argument Parsing
// Handles: --output, --label, --subject, --session, --duration,
//          --payload-variant, --no-bt-filter, --help

#include <string>
#include "schema.h"

namespace badbt {

struct CaptureConfig {
    std::string    output_path;                           // Required
    Label          label           = Label::Human;        // Required
    std::string    subject_id;                            // Required
    std::string    session_id;                            // Required
    PayloadVariant payload_variant = PayloadVariant::None; // Only for injected
    int            duration_secs   = 0;                   // 0 = unlimited (Ctrl+C to stop)
    bool           filter_bluetooth = true;               // --no-bt-filter disables
};

// Parse command-line arguments into config.
// Returns true on success, false on error (error printed to stderr).
bool parse_args(int argc, char* argv[], CaptureConfig& config);

// Print usage/help text to stdout.
void print_usage(const char* program_name);

} // namespace badbt
