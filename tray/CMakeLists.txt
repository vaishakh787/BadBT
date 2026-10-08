set(ONNXRUNTIME_DIR "${THIRD_PARTY_DIR}/onnxruntime")

add_executable(badbt-tray
    src/main.cpp
    src/tray_app.cpp
    src/notification.cpp
    "${CMAKE_SOURCE_DIR}/capture/src/raw_input.cpp"
)

target_include_directories(badbt-tray PRIVATE
    include
    "${CMAKE_SOURCE_DIR}/detector/include"
    "${CMAKE_SOURCE_DIR}/capture/src"
    "${THIRD_PARTY_DIR}"
    "${ONNXRUNTIME_DIR}/include"
)

target_link_directories(badbt-tray PRIVATE
    "${ONNXRUNTIME_DIR}/lib"
)

target_link_libraries(badbt-tray PRIVATE
    badbt-detector-core
    user32
    shell32
    advapi32
    onnxruntime
)

target_link_options(badbt-tray PRIVATE
    -static
)

target_compile_definitions(badbt-tray PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

# ── Post-build: Copy ONNX Runtime DLLs and Model to binary folder ──
add_custom_command(TARGET badbt-tray POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
        $<TARGET_FILE_DIR:badbt-tray>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll"
        $<TARGET_FILE_DIR:badbt-tray>
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:badbt-tray>/model"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/model/rf_model.onnx"
        "$<TARGET_FILE_DIR:badbt-tray>/model/rf_model.onnx"
)

# ── Test: Tray State & Controls ──
add_executable(test_tray_state
    tests/test_tray_state.cpp
    src/tray_app.cpp
    src/notification.cpp
    "${CMAKE_SOURCE_DIR}/capture/src/raw_input.cpp"
)

target_include_directories(test_tray_state PRIVATE
    include
    "${CMAKE_SOURCE_DIR}/detector/include"
    "${CMAKE_SOURCE_DIR}/capture/src"
    "${THIRD_PARTY_DIR}"
    "${ONNXRUNTIME_DIR}/include"
)

target_link_directories(test_tray_state PRIVATE
    "${ONNXRUNTIME_DIR}/lib"
)

target_link_libraries(test_tray_state PRIVATE
    badbt-detector-core
    user32
    shell32
    advapi32
    onnxruntime
)

target_link_options(test_tray_state PRIVATE
    -static
)

target_compile_definitions(test_tray_state PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

add_custom_command(TARGET test_tray_state POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
        $<TARGET_FILE_DIR:test_tray_state>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll"
        $<TARGET_FILE_DIR:test_tray_state>
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:test_tray_state>/model"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/model/rf_model.onnx"
        "$<TARGET_FILE_DIR:test_tray_state>/model/rf_model.onnx"
)
