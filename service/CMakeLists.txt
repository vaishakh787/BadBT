# Service component — Week 3 deliverable
# Skeleton only; will be fleshed out after ML pipeline (Week 2)

# add_executable(badbt-service
#     src/main.cpp
#     src/capture_loop.cpp
#     src/inference.cpp
#     src/notifier.cpp
#     src/config.cpp
# )
#
# target_include_directories(badbt-service PRIVATE
#     src
#     "${THIRD_PARTY_DIR}"
# )
#
# target_link_libraries(badbt-service PRIVATE
#     user32
#     advapi32                    # Service Control Manager API
#     onnxruntime                 # ONNX Runtime C++ API
#     windowsapp                  # WinRT (Toast notifications)
# )
#
# target_compile_definitions(badbt-service PRIVATE
#     NOMINMAX
#     WIN32_LEAN_AND_MEAN
#     UNICODE
#     _UNICODE
# )
