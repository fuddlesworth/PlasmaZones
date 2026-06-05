# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceNotifications: the server side of org.freedesktop.Notifications
# (Desktop Notifications Spec 1.3) for Phosphor-based desktop shells. Phase 2.5
# of the service-library split (see
# docs/phosphor-shell-design/04-implementation-plan.md). Unlike the sibling
# service libs, this one OWNS a well-known bus name and answers it: it is the
# notification daemon, not a client of one. The interface is generated from a
# checked-in XML via qt6_add_dbus_adaptor (all methods reply synchronously, so
# no direct ExportAllSlots / delayed-reply machinery is needed, unlike
# phosphor-service-bluetooth's Agent1). Mirrors the Phase-2.2/2.3/2.4 sibling
# pattern otherwise.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICENOTIFICATIONS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceNotifications VERSION ${PHOSPHORSERVICENOTIFICATIONS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════
#
# Core / Qml: the QObject + QML surface. DBus: NotificationServer's injectable
# (connection, service) ctor exposes QDBusConnection in the public header, and
# the generated adaptor is QtDBus, so DBus is PUBLIC. Gui: the image-data hint
# decodes to a QImage that surfaces in Notification.h, so Gui is PUBLIC (a first
# for a service lib, scoped strictly to QImage, no widgets). PhosphorServiceIconTheme
# is PRIVATE: only the .cpp uses it (image-path-as-icon-name resolution). The
# (iiibiiay) image struct is demarshalled inline via QDBusArgument, so PhosphorDBus
# is not needed here.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml DBus Gui)

# PhosphorServiceIconTheme provides the XDG icon resolver used for the
# image-path-as-icon-name fallback. The root CMakeLists.txt configures it before
# this lib; guard so a standalone configure fails with a clear message.
if(NOT TARGET PhosphorServiceIconTheme::PhosphorServiceIconTheme)
    find_package(PhosphorServiceIconTheme QUIET)
endif()
if(NOT TARGET PhosphorServiceIconTheme::PhosphorServiceIconTheme)
    message(FATAL_ERROR
        "PhosphorServiceNotifications requires PhosphorServiceIconTheme. "
        "Configure libs/phosphor-service-icontheme first (the root "
        "CMakeLists.txt does this automatically) or supply it via find_package.")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# DBus adaptor (XML → generated adaptor that forwards to NotificationServer)
# ═══════════════════════════════════════════════════════════════════════════════
#
# We EXPOSE org.freedesktop.Notifications (qt6_add_dbus_adaptor): apps call us
# through it. The generated NotificationsAdaptor forwards each method to the
# matching public slot on NotificationServer and auto-relays its spec signals
# (NotificationClosed / ActionInvoked) to the bus.

set(phosphorservicenotifications_DBUS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/dbus)

qt6_add_dbus_adaptor(phosphorservicenotifications_DBUS_GENERATED
    ${phosphorservicenotifications_DBUS_ROOT}/org.freedesktop.Notifications.xml
    PhosphorServiceNotifications/NotificationServer.h
    PhosphorServiceNotifications::NotificationServer
    notificationsadaptor
    NotificationsAdaptor
)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservicenotifications_public_HDRS
    include/PhosphorServiceNotifications/PhosphorServiceNotifications.h
    include/PhosphorServiceNotifications/QmlRegistration.h
    include/PhosphorServiceNotifications/NotificationServer.h
    include/PhosphorServiceNotifications/Notification.h
    include/PhosphorServiceNotifications/NotificationModel.h
)

set(phosphorservicenotifications_SRCS
    src/qmlregistration.cpp
    src/notificationserver.cpp
    src/notification.cpp
    src/notificationmodel.cpp
)

add_library(PhosphorServiceNotifications SHARED
    ${phosphorservicenotifications_public_HDRS}
    ${phosphorservicenotifications_SRCS}
    ${phosphorservicenotifications_DBUS_GENERATED}
)

add_library(PhosphorServiceNotifications::PhosphorServiceNotifications ALIAS PhosphorServiceNotifications)

generate_export_header(PhosphorServiceNotifications
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceNotifications/phosphorservicenotifications_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICENOTIFICATIONS_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceNotifications
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceNotifications PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceNotifications
    PUBLIC
        Qt6::Core
        Qt6::Qml
        Qt6::DBus
        Qt6::Gui
    PRIVATE
        PhosphorServiceIconTheme::PhosphorServiceIconTheme
)

set_target_properties(PhosphorServiceNotifications PROPERTIES
    VERSION ${PHOSPHORSERVICENOTIFICATIONS_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServiceNotifications
    EXPORT PhosphorServiceNotificationsTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorservicenotifications_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceNotifications
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceNotifications/phosphorservicenotifications_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceNotifications
)

install(EXPORT PhosphorServiceNotificationsTargets
    FILE PhosphorServiceNotificationsTargets.cmake
    NAMESPACE PhosphorServiceNotifications::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceNotifications
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceNotificationsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceNotificationsConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceNotifications
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceNotificationsConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICENOTIFICATIONS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceNotificationsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceNotificationsConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceNotifications
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceNotifications" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
