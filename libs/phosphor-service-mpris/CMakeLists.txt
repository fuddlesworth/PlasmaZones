# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceMpris: MPRIS2 (`org.mpris.MediaPlayer2.*`) media-player
# discovery and control. Extracted from the original `phosphor-services`
# umbrella as part of the Phase 2.0 tenant splits documented in
# docs/phosphor-shell-design/04-implementation-plan.md. Pure D-Bus
# client; no UI.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICEMPRIS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceMpris VERSION ${PHOSPHORSERVICEMPRIS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml DBus)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(phosphorservicempris_public_HDRS
    include/PhosphorServiceMpris/PhosphorServiceMpris.h
    include/PhosphorServiceMpris/QmlRegistration.h
    include/PhosphorServiceMpris/MprisHost.h
    include/PhosphorServiceMpris/MprisPlayer.h
    include/PhosphorServiceMpris/MprisPlayerModel.h
)

set(phosphorservicempris_SRCS
    src/qmlregistration.cpp
    src/mprishost.cpp
    src/mprisplayer.cpp
    src/mprisplayermodel.cpp
)

add_library(PhosphorServiceMpris SHARED
    ${phosphorservicempris_public_HDRS}
    ${phosphorservicempris_SRCS}
)

add_library(PhosphorServiceMpris::PhosphorServiceMpris ALIAS PhosphorServiceMpris)

generate_export_header(PhosphorServiceMpris
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceMpris/phosphorservicempris_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICEMPRIS_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceMpris
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceMpris PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceMpris
    PUBLIC
        Qt6::Core
        Qt6::Qml
        Qt6::DBus
)

set_target_properties(PhosphorServiceMpris PROPERTIES
    VERSION ${PHOSPHORSERVICEMPRIS_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

install(TARGETS PhosphorServiceMpris
    EXPORT PhosphorServiceMprisTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorservicempris_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceMpris
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceMpris/phosphorservicempris_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceMpris
)

install(EXPORT PhosphorServiceMprisTargets
    FILE PhosphorServiceMprisTargets.cmake
    NAMESPACE PhosphorServiceMpris::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceMpris
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceMprisConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceMprisConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceMpris
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceMprisConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICEMPRIS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceMprisConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceMprisConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceMpris
)

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceMpris" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
