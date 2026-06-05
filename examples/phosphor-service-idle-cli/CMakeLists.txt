# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-idle-cli: headless driver + worked example for the
# phosphor-service-idle library. It registers idle timeouts and logs each stage
# as it fires (with a wall-clock timestamp), and can hold an inhibition for a
# while to show the ladder disarm. It runs under the phosphorwayland QPA shell
# integration (registerLayerShellPlugin, before QGuiApplication) so the
# ext-idle-notify-v1 protocol is live; trigger stages by leaving input idle.
# Pinned to Qt6 6.6 so it builds stand-alone from this directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

add_executable(phosphor-service-idle-cli
    main.cpp
)

target_link_libraries(phosphor-service-idle-cli
    PRIVATE
        Qt6::Core
        Qt6::Gui
        PhosphorServiceIdle::PhosphorServiceIdle
        PhosphorWayland::PhosphorWayland
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-idle, not a user-facing tool.
# Keeping it build-tree-only avoids polluting distro paths with a developer
# binary.
