# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-theme-cli, headless driver for the phosphor-theme library.
#
# Subcommands:
#   set-wallpaper <path> [--mode dark|light] [--apply]
#       Spawn matugen on the wallpaper, parse its output, optionally
#       persist to ~/.local/share/phosphor/palettes/current.json.
#   dump [--source <path>]
#       Print the active palette as JSON to stdout.
#   render-template <template> <out> [--palette <path>]
#       Render a {{token}} template against the active palette.
#
# This is the acceptance test for matugen round-trip + template engine.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

set(CMAKE_AUTOMOC ON)

add_executable(phosphor-theme-cli
    main.cpp
)

target_link_libraries(phosphor-theme-cli
    PRIVATE
        Qt6::Core
        Qt6::Gui
        PhosphorTheme::PhosphorTheme
)
