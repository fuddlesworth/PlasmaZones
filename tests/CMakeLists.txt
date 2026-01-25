# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Test Suite

# Enable testing
enable_testing()

# Find Qt Test module
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

# Include directories for tests
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/dbus
    ${CMAKE_SOURCE_DIR}/src/config
    ${CMAKE_BINARY_DIR}/src  # For generated headers like plasmazones_export.h
)

# Add subdirectories
add_subdirectory(unit)
# add_subdirectory(integration)  # Enable when integration tests are ready
