# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# plasmazones-settings-schema-dump — offline JSON dump of the settings
# schema.  Consumed by the documentation site to generate its settings
# reference; see main.cpp for why this exists alongside `phosphorctl schema`.

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core)

add_executable(plasmazones-settings-schema-dump main.cpp)

target_link_libraries(plasmazones-settings-schema-dump PRIVATE
    Qt6::Core
    # buildSettingsSchema() and its ConfigDefaults baseline both live in
    # core. Nothing else is needed: the call is pure, so no daemon, D-Bus,
    # or GUI dependency comes with it.
    plasmazones_core
)

# config/settingsschema.h is included by its in-tree path, matching how the
# daemon's own translation units reach it.
target_include_directories(plasmazones-settings-schema-dump PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Developer tool, not a shipped binary — deliberately not install()ed. The
# docs sync invokes it out of the build tree.
