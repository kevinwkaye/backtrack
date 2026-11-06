cmake_minimum_required(VERSION 3.22)

project(Backtrack VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: use a local checkout if JUCE_DIR is given, otherwise fetch it.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

juce_add_plugin(Backtrack
    COMPANY_NAME "Kevin Kaye"
    PLUGIN_MANUFACTURER_CODE Kwky
    PLUGIN_CODE Bktk
    FORMATS VST3
    PRODUCT_NAME "Backtrack"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Delay)

juce_add_binary_data(BacktrackData SOURCES Resources/RobotoCondensed-Bold.ttf)

target_sources(Backtrack PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PedalControl.cpp
    Source/Keeper.cpp)

target_include_directories(Backtrack PRIVATE Source)

target_compile_definitions(Backtrack PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Backtrack
    PRIVATE
        BacktrackData
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Engine tests (no JUCE needed)
option(BACKTRACK_TESTS "Build the engine tests" ON)
if(BACKTRACK_TESTS)
    add_executable(engine_test tests/engine_test.cpp)
    add_executable(cpu_report tests/cpu_report.cpp)
endif()

# Developer tool: renders the editor to PNGs (needs a display, not built in CI)
option(BACKTRACK_SNAPSHOT "Build the UI snapshot tool" OFF)
if(BACKTRACK_SNAPSHOT)
    juce_add_console_app(BacktrackSnapshot PRODUCT_NAME "BacktrackSnapshot")
    target_sources(BacktrackSnapshot PRIVATE tests/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PedalControl.cpp Source/Keeper.cpp)
    target_include_directories(BacktrackSnapshot PRIVATE Source)
    target_compile_definitions(BacktrackSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(BacktrackSnapshot PRIVATE BacktrackData juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)
endif()
