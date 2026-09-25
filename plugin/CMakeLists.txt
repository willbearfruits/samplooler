cmake_minimum_required(VERSION 3.22)
project(SamploolerPlugin VERSION 0.1.0 LANGUAGES C CXX)

# JUCE: pass -DFETCHCONTENT_SOURCE_DIR_JUCE=<checkout> to use a local copy (8.0.13 is on disk
# at ~/Projects/hikim/build/_deps/juce-src), otherwise it is fetched.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.13
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Samplooler
    PRODUCT_NAME "Samplooler"
    COMPANY_NAME "willbearfruits"
    PLUGIN_MANUFACTURER_CODE Wbfr
    PLUGIN_CODE Smpl
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Tools
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Samplooler PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(Samplooler PRIVATE cxx_std_17)
target_compile_definitions(Samplooler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(Samplooler PUBLIC JUCE_ALSA=1 JUCE_JACK=1)
endif()

target_link_libraries(Samplooler PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
