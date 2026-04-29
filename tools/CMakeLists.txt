# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Developer / packaging tools that aren't part of the daemon or
# user-facing apps.  Off by default — enable with -DBUILD_TOOLS=ON.

option(BUILD_TOOLS "Build PlasmaZones developer tools (shader-render, ...)" OFF)

if(NOT BUILD_TOOLS)
    return()
endif()

add_subdirectory(shader-render)
