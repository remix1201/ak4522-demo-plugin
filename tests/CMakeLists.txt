# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ross Baker

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 317ac1ed4c0bb6e6b91eafc817e05c488feffcb3 # v3.16.0
    SYSTEM)
FetchContent_MakeAvailable(Catch2)
include(Catch)

# One test program per plugin: each plugin defines createPluginFilter().
foreach(plugin Effect Synth)
    string(TOLOWER ${plugin} name)
    add_executable(ak4522_demo_${name}_tests ${name}_test.cpp main.cpp)
    target_link_libraries(ak4522_demo_${name}_tests PRIVATE AK4522${plugin} ak4522_demo_common Catch2::Catch2)
    # JUCE's modules are compiled into the plugin's shared code; take its
    # include paths and definitions rather than compiling JUCE a second time.
    target_include_directories(ak4522_demo_${name}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        $<TARGET_PROPERTY:AK4522${plugin},INCLUDE_DIRECTORIES>)
    target_compile_definitions(ak4522_demo_${name}_tests PRIVATE
        $<TARGET_PROPERTY:AK4522${plugin},COMPILE_DEFINITIONS>)
    if(NOT MSVC) # the tests compare exactly on purpose: bitwise block-size independence
        target_compile_options(ak4522_demo_${name}_tests PRIVATE -Wno-float-equal)
    endif()
    catch_discover_tests(ak4522_demo_${name}_tests TEST_PREFIX "${name}: " DISCOVERY_MODE PRE_TEST)
endforeach()
