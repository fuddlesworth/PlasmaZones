# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-lock-cli: headless driver + worked example for the
# phosphor-service-lock library. The Phase 2.9 milestone-2 skeleton just reports
# whether the compositor advertises the session-lock protocol; the full
# authenticate / lock demo lands with the later milestones (the authenticate
# subcommand runs anywhere, the lock subcommand under the shell's Wayland
# platform). Pinned to Qt6 6.6 so it builds stand-alone from this directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

add_executable(phosphor-service-lock-cli
    main.cpp
)

target_link_libraries(phosphor-service-lock-cli
    PRIVATE
        Qt6::Core
        PhosphorServiceLock::PhosphorServiceLock
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-lock, not a user-facing tool.
# Keeping it build-tree-only avoids polluting distro paths with a developer
# binary.
