# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-session-cli: headless driver for the phosphor-service-session
# library. See main.cpp::printUsage() for the subcommand surface and exit codes.
#
# Acceptance harness for the capability + action surface against the real
# system-bus logind. Doubles as the worked C++ example for callers learning the
# lib. Pinned to Qt6 6.6 so it builds stand-alone from this directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

add_executable(phosphor-service-session-cli
    main.cpp
)

target_link_libraries(phosphor-service-session-cli
    PRIVATE
        Qt6::Core
        PhosphorServiceSession::PhosphorServiceSession
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-session, not a user-facing
# tool. Keeping it build-tree-only avoids polluting distro paths with a
# developer binary.
