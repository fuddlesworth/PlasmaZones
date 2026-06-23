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

# The CLI inherits QT_MIN_VERSION (currently 6.6.0) from the parent
# CMakeLists. We pin Qt6 6.6 explicitly here so the example builds
# stand-alone from this directory too — no implicit cross-file coupling.
# QT_DISABLE_DEPRECATED_BEFORE is not set per-target either here or in
# the parent project; the rest of the tree relies on Qt's default
# deprecation scoping. Documented so a future maintainer adding a project-
# wide QT_DISABLE_DEPRECATED_BEFORE knob (e.g. to harden against Qt6.7+
# deprecations) knows this target inherits the parent's choice without an
# explicit override.
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

# Intentionally no `install(TARGETS ...)` rule. This CLI is the
# acceptance harness for phosphor-service-pipewire (see comment at the
# top of this file and main.cpp::usage()) and the worked C++ example
# for callers learning the lib's API surface. It is NOT a user-facing
# tool — `wpctl` / `pactl` / `pw-cli` already cover that role for
# shipped systems. Keeping it build-tree-only avoids polluting
# distro paths with a developer-only binary that would otherwise
# require its own README, man page, and `--help` polish to be
# acceptable as an installed tool.
