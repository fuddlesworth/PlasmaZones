# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-pipewire-cli: headless driver for the
# phosphor-service-pipewire library. See main.cpp::usage() for the
# subcommand surface and exit codes.
#
# The CLI is the acceptance harness for the read + write paths against
# a live PipeWire + WirePlumber daemon. It also doubles as
# documentation: the source is the worked example for callers
# wondering how to use the lib from C++.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

add_executable(phosphor-service-pipewire-cli
    main.cpp
    cli_env.cpp
    cli_env.h
    cli_io.cpp
    cli_io.h
    cli_target.cpp
    cli_target.h
    cli_wait.cpp
    cli_wait.h
)

target_link_libraries(phosphor-service-pipewire-cli
    PRIVATE
        Qt6::Core
        PhosphorServicePipeWire::PhosphorServicePipeWire
)
