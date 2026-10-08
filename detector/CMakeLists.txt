set(ONNXRUNTIME_DIR "${THIRD_PARTY_DIR}/onnxruntime")

add_library(badbt-detector-core STATIC
    src/feature_extractor.cpp
    src/model_runner.cpp
    src/detector.cpp
    src/path_utils.cpp
    "${CMAKE_SOURCE_DIR}/capture/src/device_info.cpp"
)

target_include_directories(badbt-detector-core PUBLIC
    include
    "${THIRD_PARTY_DIR}"
    "${ONNXRUNTIME_DIR}/include"
    "${CMAKE_SOURCE_DIR}/capture/src"
)

target_link_directories(badbt-detector-core PUBLIC
    "${ONNXRUNTIME_DIR}/lib"
)

target_link_libraries(badbt-detector-core PUBLIC
    onnxruntime
)

target_compile_definitions(badbt-detector-core PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

# ── Live detector executable ──
add_executable(badbt-detector
    src/main.cpp
    "${CMAKE_SOURCE_DIR}/capture/src/raw_input.cpp"
)

target_link_libraries(badbt-detector PRIVATE
    badbt-detector-core
    user32
)

target_link_options(badbt-detector PRIVATE
    -static
)

# ── Test: Feature Parity ──
add_executable(test_feature_parity
    tests/test_feature_parity.cpp
    src/feature_extractor.cpp
)

target_include_directories(test_feature_parity PRIVATE
    include
    "${THIRD_PARTY_DIR}"
)

# ── Test: ONNX Inference & Replay ──
add_executable(test_onnx_inference
    tests/test_onnx_inference.cpp
)

target_link_libraries(test_onnx_inference PRIVATE
    badbt-detector-core
    user32
)

# ── Copy ONNX Runtime DLLs to target directories ──
add_custom_command(TARGET badbt-detector POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
        $<TARGET_FILE_DIR:badbt-detector>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll"
        $<TARGET_FILE_DIR:badbt-detector>
)

add_custom_command(TARGET test_onnx_inference POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
        $<TARGET_FILE_DIR:test_onnx_inference>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll"
        $<TARGET_FILE_DIR:test_onnx_inference>
)
