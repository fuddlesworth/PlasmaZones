# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorAudio — audio spectrum provider for Phosphor-based shells.
#
# A Qt6/C++20 library providing audio spectrum data to shell components
# (overlays, desktop effects, panel widgets, lock screen animations, etc.)
# via a backend-agnostic interface:
#
#   • IAudioSpectrumProvider — abstract QObject interface for spectrum data.
#   • CavaSpectrumProvider — concrete backend that spawns CAVA as a subprocess,
#     reads raw ASCII bar data, applies EMA smoothing, and emits normalized
#     (0.0–1.0) spectrum frames. Auto-detects PipeWire vs PulseAudio.
#   • AudioDefaults — bar count / framerate constraints.
#
# Depends on Qt6::Core only (QProcess, QStandardPaths).
#
# Future backends (PipeWire native, JACK, PulseAudio direct) can implement
# IAudioSpectrumProvider without touching this library.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORAUDIO_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorAudio VERSION ${PHOSPHORAUDIO_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorAudio Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphoraudio_SRCS
    src/cavaspectrumprovider.cpp
)

set(phosphoraudio_public_HDRS
    include/PhosphorAudio/PhosphorAudio.h
    include/PhosphorAudio/IAudioSpectrumProvider.h
    include/PhosphorAudio/CavaSpectrumProvider.h
    include/PhosphorAudio/AudioDefaults.h
)

add_library(PhosphorAudio SHARED
    ${phosphoraudio_public_HDRS}
    ${phosphoraudio_SRCS}
)

add_library(PhosphorAudio::PhosphorAudio ALIAS PhosphorAudio)

generate_export_header(PhosphorAudio
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorAudio/phosphoraudio_export.h
    EXPORT_MACRO_NAME PHOSPHORAUDIO_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorAudio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorAudio PUBLIC cxx_std_20)

target_link_libraries(PhosphorAudio
    PUBLIC
        Qt6::Core
)

set_target_properties(PhosphorAudio PROPERTIES
    VERSION ${PHOSPHORAUDIO_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorAudio" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorAudio
    EXPORT PhosphorAudioTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphoraudio_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAudio
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorAudio/phosphoraudio_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAudio
)

install(EXPORT PhosphorAudioTargets
    FILE PhosphorAudioTargets.cmake
    NAMESPACE PhosphorAudio::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAudio
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorAudioConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAudioConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAudio
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAudioConfigVersion.cmake"
    VERSION ${PHOSPHORAUDIO_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAudioConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAudioConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAudio
)
