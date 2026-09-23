# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Developer / packaging tools that aren't part of the daemon or
# user-facing apps.  Off by default — enable with -DBUILD_TOOLS=ON.

# No option() or BUILD_TOOLS guard here. The root CMakeLists declares the
# cache entry, and plasmazones/CMakeLists.txt already gates
# add_subdirectory(tools) on it, so a second declaration only competed with
# the root's help string in `cmake -LH` and the guard could never fire.

add_subdirectory(shader-render)
add_subdirectory(settings-schema-dump)
