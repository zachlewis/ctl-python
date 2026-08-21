# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences

include_guard(GLOBAL)
include(FetchContent)

# To build against a local CTL checkout instead of the pinned tag, use
# FetchContent's stock override (no pre-built CTL libraries required):
#   pip install -e . -Ccmake.define.FETCHCONTENT_SOURCE_DIR_CTL=/path/to/CTL
FetchContent_Declare(ctl
    GIT_REPOSITORY https://github.com/aces-aswf/CTL.git
    GIT_TAG        ctl-1.5.5
    GIT_SHALLOW    TRUE
    SYSTEM           # CTL's warnings are not ours to fix
    EXCLUDE_FROM_ALL # build only what we link; skip CTL's install rules
)
set(CTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
option(CTL_BUILD_TOOLS "Build CTL command-line tools (ctlrender)" OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ctl)

add_library(CTL::IlmCtl     ALIAS IlmCtl)
add_library(CTL::IlmCtlSimd ALIAS IlmCtlSimd)
add_library(CTL::IlmCtlMath ALIAS IlmCtlMath)

find_package(Imath CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)
