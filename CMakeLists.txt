cmake_minimum_required(VERSION 3.22)

project(ArmoryVST VERSION 1.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ARMORY_JUCE_PATH "" CACHE PATH "Path to the JUCE source tree")

if (NOT EXISTS "${ARMORY_JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE was not found. Set ARMORY_JUCE_PATH to a JUCE source checkout.")
endif()

add_subdirectory("${ARMORY_JUCE_PATH}" JUCE)

juce_add_plugin(ArmoryVST
    COMPANY_NAME "Armory"
    BUNDLE_ID "com.wenwen149.armory-vst"
    PLUGIN_MANUFACTURER_CODE Armt
    PLUGIN_CODE Amtr
    FORMATS VST3 Standalone
    PRODUCT_NAME "Armory VST"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ArmoryVST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

juce_generate_juce_header(ArmoryVST)

target_compile_definitions(ArmoryVST PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ArmoryVST PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
