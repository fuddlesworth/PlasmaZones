# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-pipewire-cli: headless driver for the
# phosphor-service-pipewire library.
#
# Subcommands:
#   list sinks                  list every Audio/Sink node with id + name + nick
#   list sources                list every Audio/Source node
#   list streams                list every Stream/Output/Audio + Stream/Input/Audio
#   default                     print current default sink + source names
#   set-volume <target> <pct>   set every channel of <target> to <pct> (0..100)
#   mute <target>               mute node
#   unmute <target>             unmute node
#   set-default-sink <name>     write default.configured.audio.sink
#   set-default-source <name>   write default.configured.audio.source
#
# <target> is one of: a numeric PipeWire id (from `list sinks`), a
# node.name string (e.g. alsa_output.pci-0000_00_1f.3.iec958-stereo),
# or `default.audio.sink` / `default.audio.source` for the current
# default.
#
# The CLI is the acceptance harness for the read + write paths against
# a live PipeWire + WirePlumber daemon. It also doubles as
# documentation: the source is the worked example for callers
# wondering how to use the lib from C++.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

set(CMAKE_AUTOMOC ON)

add_executable(phosphor-service-pipewire-cli
    main.cpp
)

target_link_libraries(phosphor-service-pipewire-cli
    PRIVATE
        Qt6::Core
        PhosphorServicePipeWire::PhosphorServicePipeWire
)
