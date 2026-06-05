# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-clipboard-cli: headless driver + worked example for the
# phosphor-service-clipboard library. The Phase 2.8 milestone-2 skeleton just
# reports whether the compositor advertises the clipboard protocol; the full
# watch / list / copy demo lands with the later milestones (and runs under the
# shell's Wayland platform). Pinned to Qt6 6.6 so it builds stand-alone from this
# directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

add_executable(phosphor-service-clipboard-cli
    main.cpp
)

target_link_libraries(phosphor-service-clipboard-cli
    PRIVATE
        Qt6::Core
        PhosphorServiceClipboard::PhosphorServiceClipboard
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-clipboard, not a user-facing
# tool. Keeping it build-tree-only avoids polluting distro paths with a
# developer binary.
