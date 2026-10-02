cmake_minimum_required(VERSION 3.22)
project(GGlue VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------------------------------------------
# G-Glue Bus Compressor
#   gglue_core          shared DSP engine + parameter table + presets (plain C++17, no JUCE) - used by every target
#   gglue_dsp_tests     shared-core test suite (ctest)
#   gglue_export_presets   writes the factory presets as .gglue files
#   GGlue_VST3 / GGlue_VST / GGlue_Standalone   desktop plugin (JUCE)
#   GGlueHostTest       loads the BUILT VST3 / VST2 binaries in a JUCE host and tests them (ctest)
#   GGlueScreenshot     renders the GUI to PNG (needs a display, e.g. xvfb-run)
# Options:
#   -DGGLUE_BUILD_PLUGIN=OFF        core + tests only (e.g. for the ARM / MPC bridge build)
#   -DGGLUE_JUCE_DIR=<path>         use a JUCE source checkout instead of an installed JUCE package
#   -DGGLUE_VST2_SDK_DIR=<path>     enable the VST2 target; folder that contains pluginterfaces/vst2.x/aeffect.h
# ---------------------------------------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(GGLUE_BUILD_PLUGIN "Build the JUCE desktop plugin" ON)
set(GGLUE_JUCE_DIR "" CACHE PATH "JUCE source folder (optional)")
set(GGLUE_VST2_SDK_DIR "" CACHE PATH "VST2 SDK folder (optional, enables the VST2 target)")

add_library(gglue_core STATIC
    DSP/GGlueEngine.cpp
    Parameters/GGlueParameters.cpp
    Presets/GGluePresets.cpp
    Presets/FactoryPresets.cpp)
target_include_directories(gglue_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# keep the engine's symbols private to each plugin binary (two builds in one host can never clash)
set_target_properties(gglue_core PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
    target_compile_options(gglue_core PRIVATE /W4)
else()
    target_compile_options(gglue_core PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_executable(gglue_dsp_tests Tests/DspTests.cpp)
target_link_libraries(gglue_dsp_tests PRIVATE gglue_core)
add_test(NAME dsp_tests COMMAND gglue_dsp_tests ${CMAKE_BINARY_DIR}/test-work)

add_executable(gglue_export_presets Presets/ExportPresets.cpp)
target_link_libraries(gglue_export_presets PRIVATE gglue_core)

# C interface for a native MPC Standalone wrapper (no official SDK exists yet: see Documentation/MPC_STANDALONE_STATUS.md)
add_library(gglue_mpc_bridge STATIC MPCStandalone/GGlueMPCBridge.cpp)
target_link_libraries(gglue_mpc_bridge PUBLIC gglue_core)
add_executable(gglue_bridge_test MPCStandalone/BridgeTest.c)
target_link_libraries(gglue_bridge_test PRIVATE gglue_mpc_bridge)
set_target_properties(gglue_bridge_test PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME mpc_bridge_test COMMAND gglue_bridge_test ${CMAKE_BINARY_DIR}/bridge-presets)

if(NOT GGLUE_BUILD_PLUGIN)
    return()
endif()

# ---------------------------------------------------------------------------------------------------------------
if(GGLUE_JUCE_DIR)
    add_subdirectory(${GGLUE_JUCE_DIR} JUCE)
else()
    find_package(JUCE CONFIG QUIET)
    if(NOT JUCE_FOUND)
        include(FetchContent)
        FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 7.0.12 GIT_SHALLOW ON)
        FetchContent_MakeAvailable(JUCE)
    endif()
endif()

set(GGLUE_FORMATS VST3 Standalone)
if(GGLUE_VST2_SDK_DIR)
    # FST (the free reverse-engineered VST2 header, e.g. Debian/Ubuntu "fst" package) declares
    # VstSpeakerArrangement::speakers as a flexible array, while JUCE sizes its allocations for the Steinberg layout
    # (speakers[8]) -> heap overflow whenever a host asks for the speaker arrangement. See VST2/README.md.
    include(VST2/PatchFst.cmake)
    gglue_prepare_vst2_sdk("${GGLUE_VST2_SDK_DIR}" GGLUE_VST2_SDK_EFFECTIVE)
    juce_set_vst2_sdk_path(${GGLUE_VST2_SDK_EFFECTIVE})
    list(APPEND GGLUE_FORMATS VST)
endif()
if(APPLE)
    list(APPEND GGLUE_FORMATS AU)
endif()

juce_add_plugin(GGlue
    PRODUCT_NAME "G-Glue Bus Compressor"
    COMPANY_NAME "G-Glue Audio"
    BUNDLE_ID com.gglueaudio.busc
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE GGlu
    PLUGIN_CODE GgBc
    FORMATS ${GGLUE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics
    VST2_CATEGORY kPlugCategEffect
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(GGLUE_UI_SOURCES
    Desktop/PluginProcessor.cpp
    Desktop/PluginEditor.cpp
    GUI/GGlueLookAndFeel.cpp
    GUI/Meters.cpp)
target_sources(GGlue PRIVATE ${GGLUE_UI_SOURCES})
target_compile_definitions(GGlue PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_FLAC=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(GGlue
    PRIVATE gglue_core juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- in-process GUI renderer (writes Resources/GUI/*.png)
juce_add_console_app(GGlueScreenshot PRODUCT_NAME "GGlueScreenshot")
target_sources(GGlueScreenshot PRIVATE Tests/Screenshot.cpp ${GGLUE_UI_SOURCES})
target_compile_definitions(GGlueScreenshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_FLAC=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(GGlueScreenshot PRIVATE gglue_core juce::juce_audio_utils juce::juce_recommended_config_flags)

# ---- host test: loads the built plugin binaries like a DAW does
juce_add_console_app(GGlueHostTest PRODUCT_NAME "GGlueHostTest")
target_sources(GGlueHostTest PRIVATE Tests/PluginHostTest.cpp)
target_compile_definitions(GGlueHostTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_FLAC=0 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=$<IF:$<BOOL:${GGLUE_VST2_SDK_DIR}>,1,0> JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_DISPLAY_SPLASH_SCREEN=0)
if(GGLUE_VST2_SDK_DIR)
    target_include_directories(GGlueHostTest BEFORE PRIVATE ${GGLUE_VST2_SDK_EFFECTIVE})
endif()
target_link_libraries(GGlueHostTest PRIVATE gglue_core juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_dependencies(GGlueHostTest GGlue_VST3)
set(GGLUE_VST3_BUNDLE "${CMAKE_BINARY_DIR}/GGlue_artefacts/$<CONFIG>/VST3/G-Glue Bus Compressor.vst3")
set(GGLUE_VST2_BINARY "")
if(GGLUE_VST2_SDK_DIR)
    add_dependencies(GGlueHostTest GGlue_VST)
    set(GGLUE_VST2_BINARY "${CMAKE_BINARY_DIR}/GGlue_artefacts/$<CONFIG>/VST/libG-Glue Bus Compressor.so")
endif()
add_test(NAME plugin_host_test COMMAND GGlueHostTest "${GGLUE_VST3_BUNDLE}" "${GGLUE_VST2_BINARY}")
