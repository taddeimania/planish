cmake_minimum_required(VERSION 3.22)

# macOS 11+. Builds for the machine you're on by default; for a universal
# binary pass -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64". Set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Planish VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE ---------------------------------------------------------------------
# By default JUCE is downloaded on first configure. To use a local checkout:
#   cmake -B build -DPLANISH_JUCE_DIR=/path/to/JUCE
set(PLANISH_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")
option(PLANISH_ENGINE_ONLY "Build only the engine and its tests (no JUCE)" OFF)

if(NOT PLANISH_ENGINE_ONLY)
    if(PLANISH_JUCE_DIR)
        add_subdirectory(${PLANISH_JUCE_DIR} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.9
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
endif()

# --- Signal path (no JUCE) -----------------------------------------------------
# Every module, the chain, metering, the limiter and the assistant's
# analysis. The tests build against this alone.
add_library(PlanishDSP STATIC
    Source/Engine/Module.cpp
    Source/Engine/Loudness.cpp
    Source/Engine/Chain.cpp
    Source/Engine/Analysis.cpp
    Source/Engine/Targets.cpp
    Source/Engine/Presets.cpp
    Source/Engine/Modules/Gate.cpp
    Source/Engine/Modules/Equalizer.cpp
    Source/Engine/Modules/Compressor.cpp
    Source/Engine/Modules/TransientShaper.cpp
    Source/Engine/Modules/Exciter.cpp
    Source/Engine/Modules/Spectral.cpp
    Source/Engine/Modules/Clipper.cpp
    Source/Engine/Modules/Density.cpp
    Source/Engine/Modules/Phase.cpp)
target_include_directories(PlanishDSP PUBLIC Source)
set_target_properties(PlanishDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(PlanishDSP PRIVATE /utf-8)
endif()

# --- Engine tests (no JUCE needed) --------------------------------------------
enable_testing()
add_executable(PlanishEngineTests Tests/EngineTests.cpp)
target_link_libraries(PlanishEngineTests PRIVATE PlanishDSP)
add_test(NAME engine COMMAND PlanishEngineTests)

if(PLANISH_ENGINE_ONLY)
    return()
endif()

# --- Plugin -------------------------------------------------------------------
# Copy the built plugin into the system VST3 folder after each build:
#   macOS   ~/Library/Audio/Plug-Ins/VST3
#   Linux   ~/.vst3
#   Windows C:\Program Files\Common Files\VST3  (needs an administrator shell,
#           so it is off by default there)
if(WIN32)
    set(_planish_copy_default OFF)
else()
    set(_planish_copy_default ON)
endif()
option(PLANISH_COPY_PLUGIN "Install the plugin into the system VST3 folder after building" ${_planish_copy_default})

juce_add_plugin(Planish
    COMPANY_NAME                "Taddei"
    BUNDLE_ID                   com.taddei.planish
    PLUGIN_MANUFACTURER_CODE    Tadd
    PLUGIN_CODE                 Plns
    PRODUCT_NAME                "Planish"
    DESCRIPTION                 "Mixing suite"
    FORMATS                     VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx Dynamics EQ
    COPY_PLUGIN_AFTER_BUILD     ${PLANISH_COPY_PLUGIN})

set(PLANISH_PLUGIN_SOURCES
    Source/Settings.cpp
    Source/Link.cpp
    Source/Assistant.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Widgets.cpp
    Source/UI/Views.cpp
    Source/UI/Analyzer.cpp
    Source/UI/ModulePanels.cpp
    Source/UI/ModulePanels2.cpp
    Source/UI/AssistantView.cpp
    Source/UI/MixerView.cpp)

target_sources(Planish PRIVATE ${PLANISH_PLUGIN_SOURCES})
target_include_directories(Planish PRIVATE Source)
if(MSVC)
    target_compile_options(Planish PRIVATE /utf-8)
endif()
target_compile_definitions(Planish PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Planish
    PRIVATE
        PlanishDSP
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Optional dev tool --------------------------------------------------------
#   PlanishSnapshot out.png [view]   render the panel headless
option(PLANISH_BUILD_TOOLS "Build the snapshot tool" OFF)
if(PLANISH_BUILD_TOOLS)
    juce_add_console_app(PlanishSnapshot PRODUCT_NAME "PlanishSnapshot")
    target_sources(PlanishSnapshot PRIVATE Tools/Snapshot.cpp ${PLANISH_PLUGIN_SOURCES})
    target_include_directories(PlanishSnapshot PRIVATE Source)
    if(MSVC)
        target_compile_options(PlanishSnapshot PRIVATE /utf-8)
    endif()
    target_compile_definitions(PlanishSnapshot PRIVATE
        JucePlugin_Name="Planish"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(PlanishSnapshot PRIVATE
        PlanishDSP
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
