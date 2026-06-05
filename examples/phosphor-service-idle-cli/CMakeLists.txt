# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-idle-cli: headless driver + worked example for the
# phosphor-service-idle library. The Phase 2.7 milestone-1 skeleton just reports
# whether the compositor advertises the idle protocol; the full timeout / fired-at
# log + inhibit toggle demo lands with the later milestones. Pinned to Qt6 6.6 so
# it builds stand-alone from this directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

add_executable(phosphor-service-idle-cli
    main.cpp
)

target_link_libraries(phosphor-service-idle-cli
    PRIVATE
        Qt6::Core
        PhosphorServiceIdle::PhosphorServiceIdle
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-idle, not a user-facing tool.
# Keeping it build-tree-only avoids polluting distro paths with a developer
# binary.
