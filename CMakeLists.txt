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

option(ZYG_BUILD_CLAP "Build the Linux CLAP instrument" ON)
if(ZYG_BUILD_CLAP)
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e)
    FetchContent_MakeAvailable(clap_juce_extensions)
endif()

find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)

add_library(zygzxg_core STATIC
    src/Patch.cpp
    src/FxParams.cpp
    src/Assets.cpp
    src/dsp/Filters.cpp
    src/dsp/Modulators.cpp
    src/dsp/FxEngine.cpp
    src/dsp/Spectral.cpp
    src/dsp/Oscillators.cpp
    src/dsp/Sequencer.cpp
    src/Oversampling.cpp
    src/SerumImporter.cpp
    src/SynthEngine.cpp
    src/Wavetable.cpp)
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
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)
juce_add_binary_data(ZYGZXGUIAssets SOURCES
    assets/ui/logo_46.png)
set(ZYG_UI_SOURCES
    src/ui/UserLibrary.cpp
    src/ui/Retro.cpp
    src/ui/PresetBrowser.cpp
    src/ui/PixelGfx.cpp
    src/ui/Widgets.cpp
    src/ui/PanelsSupport.cpp
    src/ui/AssetBrowse.cpp
    src/ui/Displays.cpp
    src/ui/OscPage.cpp
    src/ui/TopBar.cpp
    src/ui/ModSection.cpp
    src/ui/KeyboardDock.cpp
    src/ui/Overlay.cpp
    src/ui/MixPage.cpp
    src/ui/FxTree.cpp
    src/ui/FxViz.cpp
    src/ui/FxPage.cpp
    src/ui/ModCatalog.cpp
    src/ui/MatrixPage.cpp
    src/ui/GlobalPage.cpp
    src/ui/SeqEditors.cpp)
target_sources(ZYGZXG PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp ${ZYG_UI_SOURCES})
target_link_libraries(ZYGZXG PRIVATE ZYGZXGUIAssets zygzxg_core juce::juce_audio_utils juce::juce_dsp juce::juce_opengl)
target_compile_definitions(ZYGZXG PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
juce_generate_juce_header(ZYGZXG)
if(ZYG_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET ZYGZXG
        CLAP_ID "org.zygzxg.linuxserumsynth"
        CLAP_FEATURES instrument synthesizer)
endif()

enable_testing()
add_executable(zygzxg_core_tests tests/core_tests.cpp)
target_link_libraries(zygzxg_core_tests PRIVATE zygzxg_core)
add_test(NAME core_tests COMMAND zygzxg_core_tests)
add_executable(zygzxg_dsp_tests tests/dsp_tests.cpp)
target_link_libraries(zygzxg_dsp_tests PRIVATE zygzxg_core)
add_test(NAME dsp_tests COMMAND zygzxg_dsp_tests)

add_executable(zygzxg_ui_model_tests tests/ui_model_tests.cpp src/ui/FxTree.cpp)
target_link_libraries(zygzxg_ui_model_tests PRIVATE zygzxg_core)
add_test(NAME ui_model_tests COMMAND zygzxg_ui_model_tests)

option(ZYG_BUILD_TOOLS "Build developer tools (offline preset renderer)" ON)
if(ZYG_BUILD_TOOLS)
    juce_add_console_app(zygzxg_render PRODUCT_NAME "zygzxg_render")
    target_sources(zygzxg_render PRIVATE tools/render_tool.cpp)
    target_link_libraries(zygzxg_render PRIVATE zygzxg_core juce::juce_audio_formats juce::juce_audio_basics juce::juce_core juce::juce_events
        juce::juce_recommended_config_flags)
    target_compile_definitions(zygzxg_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
endif()

# Developer tool: renders the editor (or a widget gallery) to PNG through JUCE's software renderer.
option(ZYG_BUILD_UI_TOOLS "Build the offline UI snapshot tool" ON)
if(ZYG_BUILD_UI_TOOLS)
    juce_add_console_app(zygzxg_ui_snapshot PRODUCT_NAME "zygzxg_ui_snapshot")
    target_sources(zygzxg_ui_snapshot PRIVATE tools/ui_snapshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp ${ZYG_UI_SOURCES})
    target_link_libraries(zygzxg_ui_snapshot PRIVATE ZYGZXGUIAssets zygzxg_core
        juce::juce_audio_utils juce::juce_dsp juce::juce_opengl juce::juce_gui_basics juce::juce_recommended_config_flags)
    target_compile_definitions(zygzxg_ui_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    juce_generate_juce_header(zygzxg_ui_snapshot)
    add_test(NAME ui_route_tests COMMAND zygzxg_ui_snapshot --selftest-routes)
    add_test(NAME resample_tests COMMAND zygzxg_ui_snapshot --selftest-resample)
    add_test(NAME library_tests COMMAND zygzxg_ui_snapshot --selftest-library)
endif()
