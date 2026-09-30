cmake_minimum_required(VERSION 3.22)

project(NeddPE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(NEDDPE_BUILD_TESTS "Build the NeddPETests console application" ON)
option(NEDDPE_BUILD_STANDALONE "Also build a Standalone application" ON)
option(NEDDPE_COPY_PLUGIN "Copy the VST3 into the system VST3 folder after building (needs write access)" OFF)
set(NEDDPE_JUCE_DIR "" CACHE PATH "Path to a local JUCE 8 checkout. Leave empty to download JUCE automatically.")

# ---------------------------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------------------------
if(NEDDPE_JUCE_DIR)
    add_subdirectory("${NEDDPE_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------------------------
file(GLOB_RECURSE NEDDPE_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")
list(FILTER NEDDPE_CORE_SOURCES EXCLUDE REGEX ".*/Source/Tests/.*")

file(GLOB_RECURSE NEDDPE_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Tests/*.h")

set(NEDDPE_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra)

set(NEDDPE_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    NEDDPE_VERSION_STRING="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------------------------
set(NEDDPE_FORMATS VST3)
if(NEDDPE_BUILD_STANDALONE)
    list(APPEND NEDDPE_FORMATS Standalone)
endif()

juce_add_plugin(NeddPE
    PRODUCT_NAME "NeddPE"
    COMPANY_NAME "Nedd Audio"
    COMPANY_WEBSITE "https://github.com/Andrew123s/Neddpe"
    BUNDLE_ID "com.neddaudio.neddpe"
    PLUGIN_MANUFACTURER_CODE Nedd
    PLUGIN_CODE NdPE
    FORMATS ${NEDDPE_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD ${NEDDPE_COPY_PLUGIN}
    DESCRIPTION "MPE-first hybrid synthesizer")

target_sources(NeddPE PRIVATE ${NEDDPE_CORE_SOURCES})
target_include_directories(NeddPE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_definitions(NeddPE PUBLIC ${NEDDPE_COMMON_DEFINITIONS})
target_link_libraries(NeddPE
    PRIVATE ${NEDDPE_JUCE_MODULES}
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(NeddPE PRIVATE /MP /bigobj)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${NEDDPE_CORE_SOURCES})

# ---------------------------------------------------------------------------------------------
# Tests (console app compiling the same sources, so no plugin host is needed to run them)
# ---------------------------------------------------------------------------------------------
if(NEDDPE_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(NeddPETests PRODUCT_NAME "NeddPETests")
    target_sources(NeddPETests PRIVATE ${NEDDPE_CORE_SOURCES} ${NEDDPE_TEST_SOURCES})
    target_include_directories(NeddPETests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_compile_definitions(NeddPETests PRIVATE ${NEDDPE_COMMON_DEFINITIONS} NEDDPE_TESTS=1)
    target_link_libraries(NeddPETests
        PRIVATE ${NEDDPE_JUCE_MODULES}
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags)
    if(MSVC)
        target_compile_options(NeddPETests PRIVATE /MP /bigobj)
    endif()

    add_test(NAME NeddPETests COMMAND NeddPETests)
endif()
