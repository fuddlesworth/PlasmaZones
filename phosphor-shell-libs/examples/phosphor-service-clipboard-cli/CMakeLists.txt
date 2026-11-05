# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-clipboard-cli: headless driver + worked example for the
# phosphor-service-clipboard library. `watch` logs each new clipboard entry,
# `list` prints the persisted history, and `copy <n>` re-applies an entry to the
# selection. It runs under the phosphorwayland QPA shell integration
# (registerLayerShellPlugin, before QGuiApplication) so the wlr-data-control
# protocol is live. Pinned to Qt6 6.6 so it builds stand-alone from this
# directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

add_executable(phosphor-service-clipboard-cli
    main.cpp
)

target_link_libraries(phosphor-service-clipboard-cli
    PRIVATE
        Qt6::Core
        Qt6::Gui
        PhosphorServiceClipboard::PhosphorServiceClipboard
        PhosphorWayland::PhosphorWayland
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-clipboard, not a user-facing
# tool. Keeping it build-tree-only avoids polluting distro paths with a
# developer binary.
