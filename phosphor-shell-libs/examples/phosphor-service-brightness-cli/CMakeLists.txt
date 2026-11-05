# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-brightness-cli: headless driver for the
# phosphor-service-brightness library. See main.cpp::usage() for the
# subcommand surface and exit codes.
#
# Acceptance harness for the read path (list, get) and the write path (set)
# against real sysfs backlights + logind. Doubles as the worked C++ example
# for callers learning the lib. Pinned to Qt6 6.6 so it builds stand-alone
# from this directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

add_executable(phosphor-service-brightness-cli
    main.cpp
)

target_link_libraries(phosphor-service-brightness-cli
    PRIVATE
        Qt6::Core
        PhosphorServiceBrightness::PhosphorServiceBrightness
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-brightness, not a user-facing
# tool. Keeping it build-tree-only avoids polluting distro paths with a
# developer binary.
