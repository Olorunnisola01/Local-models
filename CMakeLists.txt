cmake_minimum_required(VERSION 3.21)
project(EdgeTTSStudioNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Network)

# ONNX Runtime (vendored prebuilt)
set(ORT_ROOT ${CMAKE_SOURCE_DIR}/third_party/onnxruntime)
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION ${ORT_ROOT}/lib/onnxruntime.dll
    IMPORTED_IMPLIB   ${ORT_ROOT}/lib/onnxruntime.lib
    INTERFACE_INCLUDE_DIRECTORIES ${ORT_ROOT}/include)

# nlohmann/json (header-only)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_SOURCE_DIR}/third_party)

# espeak-ng (vendored prebuilt, used for Kokoro/Piper phonemization)
set(ESPEAK_ROOT ${CMAKE_SOURCE_DIR}/third_party/espeak-ng)
add_library(espeak-ng SHARED IMPORTED)
set_target_properties(espeak-ng PROPERTIES
    IMPORTED_LOCATION ${ESPEAK_ROOT}/lib/libespeak-ng.dll
    IMPORTED_IMPLIB   ${ESPEAK_ROOT}/lib/espeak-ng.lib
    INTERFACE_INCLUDE_DIRECTORIES ${ESPEAK_ROOT}/include)

# Static library: core inference + DSP (no Qt dependency, testable standalone)
add_library(tts_core STATIC
    src/core/SupertonicEngine.cpp
    src/core/UnicodeProcessor.cpp
    src/core/VoiceStyle.cpp
    src/core/Phonemizer.cpp
    src/core/KokoroEngine.cpp
    src/core/PiperEngine.cpp
    src/core/VoiceCatalog.cpp
    src/core/TextMarkup.cpp
    src/core/GpuDevice.cpp
    src/core/ProviderValidation.cpp
    src/dsp/BiquadFilter.cpp
    src/dsp/DeepFilterNetEngine.cpp
    src/dsp/Fft.cpp
    src/dsp/GraphicEq.cpp
    src/dsp/Humanizer.cpp
    src/dsp/Normalizer.cpp
    src/dsp/Resampler.cpp
    src/dsp/WavReader.cpp
    src/dsp/WavWriter.cpp
)
target_include_directories(tts_core PUBLIC src)
target_link_libraries(tts_core PUBLIC onnxruntime nlohmann_json espeak-ng dxgi)

# Main GUI app
qt_add_executable(EdgeTTSStudioNative WIN32
    src/main.cpp
    src/core/EdgeTtsEngine.cpp
    src/core/RemoteKokoroEngine.cpp
    src/core/RemoteFishSpeechEngine.cpp
    src/ui/MainWindow.cpp
    src/ui/EqBandWidget.cpp
    src/ui/EqPanel.cpp
    src/ui/MultiSpeakerPanel.cpp
    src/ui/SpeakerVoiceCard.cpp
    src/ui/TimelineTypes.cpp
    src/ui/AudioTimelineBuilder.cpp
    src/ui/TimelinePanel.cpp
    src/ui/ProjectManager.cpp
    src/ui/CaptionExporter.cpp
    src/ui/PronunciationPanel.cpp
    src/ui/ExportPackageDialog.cpp
    src/ui/ReadMeDialog.cpp
    src/ui/ThemeManager.cpp
    src/ui/RecentProjects.cpp
    src/ui/TagInserter.cpp
    src/ui/TextStatsPanel.cpp
    src/ui/ProviderHintsPanel.cpp
    src/ui/RemoteGpuPanel.cpp
    src/ui/FirstRunWizard.cpp
    src/ui/ModelManagerDialog.cpp
    src/ui/BatchQueueDialog.cpp
    src/dsp/AudioExporter.cpp
)
target_link_libraries(EdgeTTSStudioNative PRIVATE tts_core Qt6::Widgets Qt6::Multimedia Qt6::Network)
target_include_directories(EdgeTTSStudioNative PRIVATE ${Qt6Core_PRIVATE_INCLUDE_DIRS})

# Post-build: copy onnxruntime.dll, espeak-ng (dll + data), and models/ next
# to the exe
add_custom_command(TARGET EdgeTTSStudioNative POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ORT_ROOT}/lib/onnxruntime.dll $<TARGET_FILE_DIR:EdgeTTSStudioNative>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ORT_ROOT}/lib/DirectML.dll $<TARGET_FILE_DIR:EdgeTTSStudioNative>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ESPEAK_ROOT}/lib/libespeak-ng.dll $<TARGET_FILE_DIR:EdgeTTSStudioNative>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${ESPEAK_ROOT}/espeak-ng-data $<TARGET_FILE_DIR:EdgeTTSStudioNative>/espeak-ng-data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/models $<TARGET_FILE_DIR:EdgeTTSStudioNative>/models
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:EdgeTTSStudioNative>/scripts
)

# Post-build: deploy Qt runtime DLLs and plugins next to the exe so it can
# run standalone without Qt's bin/ directory on PATH
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET EdgeTTSStudioNative POST_BUILD
        COMMAND ${WINDEPLOYQT_EXECUTABLE} --no-translations $<TARGET_FILE:EdgeTTSStudioNative>
    )
endif()

# Helper: copy the DLLs/data that tts_core needs at runtime next to a CLI target
function(copy_tts_core_runtime_deps target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ORT_ROOT}/lib/onnxruntime.dll $<TARGET_FILE_DIR:${target}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ORT_ROOT}/lib/DirectML.dll $<TARGET_FILE_DIR:${target}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ESPEAK_ROOT}/lib/libespeak-ng.dll $<TARGET_FILE_DIR:${target}>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${ESPEAK_ROOT}/espeak-ng-data $<TARGET_FILE_DIR:${target}>/espeak-ng-data
    )
endfunction()

# Optional CLI smoke-test target (no Qt) for fast iteration on the
# inference pipeline before wiring up the UI
add_executable(test_supertonic_cli tests/test_supertonic_cli.cpp)
target_link_libraries(test_supertonic_cli PRIVATE tts_core)
copy_tts_core_runtime_deps(test_supertonic_cli)

# CLI smoke test for the Kokoro engine
add_executable(test_kokoro_cli tests/test_kokoro_cli.cpp)
target_link_libraries(test_kokoro_cli PRIVATE tts_core)
copy_tts_core_runtime_deps(test_kokoro_cli)

# Concurrency stress test for the Kokoro engine on the DirectML GPU EP
add_executable(test_kokoro_concurrency tests/test_kokoro_concurrency.cpp)
target_link_libraries(test_kokoro_concurrency PRIVATE tts_core)
copy_tts_core_runtime_deps(test_kokoro_concurrency)

# CLI smoke test for the Piper engine
add_executable(test_piper_cli tests/test_piper_cli.cpp)
target_link_libraries(test_piper_cli PRIVATE tts_core)
copy_tts_core_runtime_deps(test_piper_cli)

# CLI smoke test for the DeepFilterNet denoising engine
add_executable(test_dfn_cli tests/test_dfn_cli.cpp)
target_link_libraries(test_dfn_cli PRIVATE tts_core)
copy_tts_core_runtime_deps(test_dfn_cli)

# CLI smoke test for the Edge TTS websocket client (requires internet access)
add_executable(test_edge_tts_cli
    tests/test_edge_tts_cli.cpp
    src/core/EdgeTtsEngine.cpp
)
target_link_libraries(test_edge_tts_cli PRIVATE tts_core Qt6::Core Qt6::Network Qt6::Multimedia)
target_include_directories(test_edge_tts_cli PRIVATE ${Qt6Core_PRIVATE_INCLUDE_DIRS})
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET test_edge_tts_cli POST_BUILD
        COMMAND ${WINDEPLOYQT_EXECUTABLE} --no-translations $<TARGET_FILE:test_edge_tts_cli>
    )
endif()

# Dev utility: dump input/output tensor names/shapes for an ONNX model
add_executable(test_text_markup tests/test_text_markup.cpp)
target_link_libraries(test_text_markup PRIVATE tts_core)

add_executable(dump_onnx_io tests/dump_onnx_io.cpp)
target_link_libraries(dump_onnx_io PRIVATE onnxruntime)
add_custom_command(TARGET dump_onnx_io POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ORT_ROOT}/lib/onnxruntime.dll $<TARGET_FILE_DIR:dump_onnx_io>
)
