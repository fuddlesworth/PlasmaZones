# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-lock-cli: headless driver + worked example for the
# phosphor-service-lock library. `authenticate` verifies a password through PAM
# and prints success/failure (no compositor needed); `supported` reports whether
# the compositor advertises ext-session-lock-v1, queried under the phosphorwayland
# QPA shell integration (registerLayerShellPlugin, before QGuiApplication). Pinned
# to Qt6 6.6 so it builds stand-alone from this directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

add_executable(phosphor-service-lock-cli
    main.cpp
)

target_link_libraries(phosphor-service-lock-cli
    PRIVATE
        Qt6::Core
        Qt6::Gui
        PhosphorServiceLock::PhosphorServiceLock
        PhosphorWayland::PhosphorWayland
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-lock, not a user-facing tool.
# Keeping it build-tree-only avoids polluting distro paths with a developer
# binary.
