# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-service-polkit-cli: headless driver for the phosphor-service-polkit
# library. See main.cpp's usage() for the flags. Runs as the session's PolicyKit
# authentication agent and answers PAM prompts; trigger a request with e.g.
# `pkexec true` from another terminal. Doubles as the worked C++ example for
# callers learning the lib. Pinned to Qt6 6.6 so it builds stand-alone from this
# directory too.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

add_executable(phosphor-service-polkit-cli
    main.cpp
)

target_link_libraries(phosphor-service-polkit-cli
    PRIVATE
        Qt6::Core
        PhosphorServicePolkit::PhosphorServicePolkit
)

# Intentionally no `install(TARGETS ...)` rule. This CLI is the acceptance
# harness + worked example for phosphor-service-polkit, not a user-facing tool.
# Keeping it build-tree-only avoids polluting distro paths with a developer
# binary.
