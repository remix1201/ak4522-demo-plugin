# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ross Baker

cmake_minimum_required(VERSION 3.24)

project(ak4522_demo_plugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20) # the ak4522 library requires C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AK4522_DEMO_TESTS "Build the plugin tests" ON)
option(AK4522_DEMO_COPY_PLUGINS "Copy the plugins to the user plugin folders after building" OFF)

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# JUCE 9.0.4, used under its AGPLv3 option (LICENSES/). Pinned by commit and
# checksum. For a local JUCE checkout, configure with
# -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE.
FetchContent_Declare(juce
    URL https://codeload.github.com/juce-framework/JUCE/tar.gz/3fb5cbc0340758596dac39f7c8de47d562b4896e
    URL_HASH SHA256=2bb7095497a7bb7269dbdf841a7c6aa0939eaeb2a2bc9724ac4758d3ae3565a7)

# The ak4522 library, as any outside user gets it: a tagged release. To test
# a library change before it is released, configure with
# -DFETCHCONTENT_SOURCE_DIR_AK4522=/path/to/ak4522 (development only).
FetchContent_Declare(ak4522
    GIT_REPOSITORY https://github.com/remix1201/ak4522.git
    GIT_TAG v0.1.0
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(juce ak4522)

set(AK4522_DEMO_COMPANY "ak4522")
set(AK4522_DEMO_MANUFACTURER_CODE Ak45)

# Code shared by both plugins: parameters, state, compensated bypass, the
# status editor. Header-only parts plus one translation unit per plugin.
add_library(ak4522_demo_common INTERFACE)
target_include_directories(ak4522_demo_common INTERFACE Source "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(ak4522_demo_common INTERFACE ak4522::ak4522)

# generated/BuildInfo.h: the commit shown in the editor, refreshed every build.
add_custom_target(ak4522_demo_build_info
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            -DOUTPUT="${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildInfo.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h"
    COMMENT "Recording the build commit")

function(ak4522_demo_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT_NAME;PLUGIN_CODE;BUNDLE_ID;IS_SYNTH" "SOURCES" ${ARGN})
    juce_add_plugin(${target}
        COMPANY_NAME "${AK4522_DEMO_COMPANY}"
        BUNDLE_ID "${ARG_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE ${AK4522_DEMO_MANUFACTURER_CODE}
        PLUGIN_CODE ${ARG_PLUGIN_CODE}
        IS_SYNTH ${ARG_IS_SYNTH}
        NEEDS_MIDI_INPUT ${ARG_IS_SYNTH}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        FORMATS VST3 AU
        PRODUCT_NAME "${ARG_PRODUCT_NAME}"
        COPY_PLUGIN_AFTER_BUILD ${AK4522_DEMO_COPY_PLUGINS}
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE)
    target_sources(${target} PRIVATE Source/common/ChipPlugin.cpp ${ARG_SOURCES})
    add_dependencies(${target} ak4522_demo_build_info)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target}
        PRIVATE
            ak4522_demo_common
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

ak4522_demo_plugin(AK4522Effect
    PRODUCT_NAME "AK4522 DAC"
    PLUGIN_CODE Akfx
    BUNDLE_ID io.github.remix1201.ak4522.demo.fx
    IS_SYNTH FALSE
    SOURCES Source/effect/EffectProcessor.cpp)

ak4522_demo_plugin(AK4522Synth
    PRODUCT_NAME "AK4522 Synth"
    PLUGIN_CODE Aksy
    BUNDLE_ID io.github.remix1201.ak4522.demo.synth
    IS_SYNTH TRUE
    SOURCES Source/synth/SynthProcessor.cpp)

if(AK4522_DEMO_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
