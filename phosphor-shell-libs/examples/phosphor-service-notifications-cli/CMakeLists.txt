# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-notifications-cli: headless driver for the
# phosphor-service-notifications library. See main.cpp's usage() for the
# subcommand surface and exit codes.
#
# Both a server (watch: own the name, log incoming Notify + close events) and a
# client (send / close / info: call the running daemon over D-Bus). Doubles as
# the worked C++ example for callers learning the lib. Pinned to Qt6 6.6 so it
# builds stand-alone from this directory too. DBus is linked directly for the
# client-side QDBusInterface calls.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core DBus)

add_executable(phosphor-service-notifications-cli
    main.cpp
)

target_link_libraries(phosphor-service-notifications-cli
    PRIVATE
        Qt6::Core
        Qt6::DBus
        PhosphorServiceNotifications::PhosphorServiceNotifications
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-notifications, not a user-facing
# tool. Keeping it build-tree-only avoids polluting distro paths with a
# developer binary.
