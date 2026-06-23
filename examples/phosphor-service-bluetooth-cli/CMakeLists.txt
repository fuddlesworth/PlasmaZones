# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-bluetooth-cli: headless driver for the
# phosphor-service-bluetooth library. See main.cpp::usage() for the
# subcommand surface and exit codes.
#
# Acceptance harness for the read paths (status, list-adapters,
# list-devices) and the write paths (power, scan, pair, connect, trust,
# remove) against a live org.bluez. Doubles as the worked C++ example for
# callers learning the lib. Pinned to Qt6 6.6 so it builds stand-alone from
# this directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

add_executable(phosphor-service-bluetooth-cli
    main.cpp
)

target_link_libraries(phosphor-service-bluetooth-cli
    PRIVATE
        Qt6::Core
        PhosphorServiceBluetooth::PhosphorServiceBluetooth
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-bluetooth, not a user-facing
# tool. `bluetoothctl` already covers that role on shipped systems. Keeping
# it build-tree-only avoids polluting distro paths with a developer binary.
