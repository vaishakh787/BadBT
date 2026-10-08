add_executable(badbt-capture
    src/main.cpp
    src/cli.cpp
    src/device_info.cpp
    src/raw_input.cpp
)

target_include_directories(badbt-capture PRIVATE
    src
    "${THIRD_PARTY_DIR}"
)

# Win32 API libs
target_link_libraries(badbt-capture PRIVATE
    user32
)

# Disable min/max macros from Windows.h (conflict with std::min/max)
target_compile_definitions(badbt-capture PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

# Warning level
if(MSVC)
    target_compile_options(badbt-capture PRIVATE /W4)
endif()
