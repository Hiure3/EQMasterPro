cmake_minimum_required(VERSION 3.22)

project(EQMasterPro VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static MSVC runtime: the plugin does not depend on the Visual C++ redistributable being installed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(EQM_BUILD_PLUGIN "Build the VST3 plugin (downloads JUCE)" ON)
option(EQM_BUILD_TESTS  "Build the DSP unit tests"               ON)
option(EQM_BUILD_PLUGIN_TESTS "Build plugin-level tests (needs EQM_BUILD_PLUGIN)" ON)

# ---------------------------------------------------------------------------------------------
# DSP unit tests (pure C++, no JUCE)
# ---------------------------------------------------------------------------------------------
if(EQM_BUILD_TESTS)
    enable_testing()
    add_executable(eqm_dsp_tests Tests/dsp_tests.cpp)
    target_compile_features(eqm_dsp_tests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(eqm_dsp_tests PRIVATE /W3 /O2)
    else()
        target_compile_options(eqm_dsp_tests PRIVATE -O2)
    endif()
    add_test(NAME dsp_tests COMMAND eqm_dsp_tests)
endif()

# ---------------------------------------------------------------------------------------------
# VST3 plugin
# ---------------------------------------------------------------------------------------------
if(EQM_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(EQMasterPro
        COMPANY_NAME                "EQ Master"
        PLUGIN_MANUFACTURER_CODE    EqMp
        PLUGIN_CODE                 EqM1
        FORMATS                     VST3
        PRODUCT_NAME                "EQMasterPro"
        VERSION                     ${PROJECT_VERSION}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             Fx EQ Analyzer)

    target_sources(EQMasterPro PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(EQMasterPro PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=0)

    target_link_libraries(EQMasterPro
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags)
endif()

if(EQM_BUILD_PLUGIN AND EQM_BUILD_PLUGIN_TESTS)
    enable_testing()
    add_executable(eqm_plugin_tests Tests/plugin_tests.cpp)
    target_link_libraries(eqm_plugin_tests PRIVATE EQMasterPro juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME plugin_tests COMMAND eqm_plugin_tests)
endif()
