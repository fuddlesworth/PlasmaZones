# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-network-cli: headless driver for the
# phosphor-service-network library. See main.cpp::usage() for the
# subcommand surface and exit codes.
#
# Acceptance harness for the read paths (status, list-devices,
# list-connections, list-aps) and the write paths (scan, connect)
# against a live org.freedesktop.NetworkManager. Doubles as the worked
# C++ example for callers learning the lib. Pinned to Qt6 6.6 so it
# builds stand-alone from this directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

add_executable(phosphor-service-network-cli
    main.cpp
)

target_link_libraries(phosphor-service-network-cli
    PRIVATE
        Qt6::Core
        PhosphorServiceNetwork::PhosphorServiceNetwork
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the
# acceptance harness + worked example for phosphor-service-network, not a
# user-facing tool — `nmcli` already covers that role on shipped systems.
# Keeping it build-tree-only avoids polluting distro paths with a
# developer-only binary.
