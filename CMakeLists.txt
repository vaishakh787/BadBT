cmake_minimum_required(VERSION 3.20)
project(BadBTDetector VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use static CRT on MSVC (/MT instead of /MD)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Third-party include path (header-only libs)
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

add_subdirectory(capture)
add_subdirectory(detector)
add_subdirectory(tray)
# add_subdirectory(service)  # Uncomment when service is ready
