cmake_minimum_required(VERSION 3.24)
project(ZYGZXG VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)

set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_USE_CURL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce json)

find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)

add_library(zygzxg_core STATIC
    src/Patch.cpp
    src/SerumImporter.cpp
    src/SynthEngine.cpp)
target_include_directories(zygzxg_core PUBLIC src)
target_link_libraries(zygzxg_core PUBLIC nlohmann_json::nlohmann_json ${ZSTD_LIBRARY})
set_target_properties(zygzxg_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(ZYGZXG
    COMPANY_NAME "ZYG-ZXG"
    BUNDLE_ID "org.zygzxg.linuxserumsynth"
    PLUGIN_MANUFACTURER_CODE ZygX
    PLUGIN_CODE ZyS2
    FORMATS VST3
    PRODUCT_NAME "ZYG-ZXG"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(ZYGZXG PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_link_libraries(ZYGZXG PRIVATE zygzxg_core juce::juce_audio_utils juce::juce_dsp)
target_compile_definitions(ZYGZXG PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
juce_generate_juce_header(ZYGZXG)

enable_testing()
add_executable(zygzxg_core_tests tests/core_tests.cpp)
target_link_libraries(zygzxg_core_tests PRIVATE zygzxg_core)
add_test(NAME core_tests COMMAND zygzxg_core_tests)
