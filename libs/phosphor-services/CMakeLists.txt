# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServices — DBus + platform-integration primitives for Phosphor-based
# desktop shells. First tenant: StatusNotifierItem (system tray) host + watcher
# + dbusmenu. Future siblings: org.freedesktop.Notifications, MPRIS, UPower,
# NetworkManager, logind.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICES_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServices VERSION ${PHOSPHORSERVICES_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick DBus)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# DBus adaptors / interfaces (XML → generated headers)
# ═══════════════════════════════════════════════════════════════════════════════
#
# The Watcher we EXPOSE (qt6_add_dbus_adaptor) — apps + other hosts call us
# through this. The Item + dbusmenu we CONSUME (qt6_add_dbus_interface) —
# each registered tray item lives in another process and we call it.

set(phosphorservices_DBUS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/dbus)

qt6_add_dbus_adaptor(phosphorservices_DBUS_GENERATED
    ${phosphorservices_DBUS_ROOT}/org.kde.StatusNotifierWatcher.xml
    src/statusnotifier/statusnotifierwatcher.h
    PhosphorServices::StatusNotifierWatcher
    statusnotifierwatcheradaptor
    StatusNotifierWatcherAdaptor
)

qt6_add_dbus_interface(phosphorservices_DBUS_GENERATED
    ${phosphorservices_DBUS_ROOT}/org.kde.StatusNotifierItem.xml
    statusnotifieritem_interface
)

qt6_add_dbus_interface(phosphorservices_DBUS_GENERATED
    ${phosphorservices_DBUS_ROOT}/com.canonical.dbusmenu.xml
    dbusmenu_interface
)

# MPRIS and UPower use hand-rolled QDBusInterface/QDBusMessage calls
# (matching the pattern in phosphor-screens, the daemon, etc.) rather
# than qt6_add_dbus_interface code generation.

# ═══════════════════════════════════════════════════════════════════════════════
# Public Headers
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservices_public_HDRS
    include/PhosphorServices/PhosphorServices.h
    include/PhosphorServices/QmlRegistration.h
    include/PhosphorServices/StatusNotifierItem.h
    include/PhosphorServices/StatusNotifierItemModel.h
    include/PhosphorServices/StatusNotifierHost.h
    include/PhosphorServices/DBusMenuModel.h
    include/PhosphorServices/IconThemeResolver.h
    include/PhosphorServices/MprisPlayer.h
    include/PhosphorServices/MprisHost.h
    include/PhosphorServices/MprisPlayerModel.h
    include/PhosphorServices/UPowerDevice.h
    include/PhosphorServices/UPowerHost.h
    include/PhosphorServices/UPowerDeviceModel.h
)

# ═══════════════════════════════════════════════════════════════════════════════
# Library Sources
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservices_SRCS
    src/iconimageprovider.cpp
    src/qmlregistration.cpp
    src/statusnotifier/dbustypes.cpp
    src/statusnotifier/statusnotifierwatcher.cpp
    src/statusnotifier/statusnotifierhost.cpp
    src/statusnotifier/statusnotifieritem.cpp
    src/statusnotifier/statusnotifieritemmodel.cpp
    src/statusnotifier/dbusmenumodel.cpp
    src/icontheme/iconthemeresolver.cpp
    src/mpris/mprishost.cpp
    src/mpris/mprisplayer.cpp
    src/mpris/mprisplayermodel.cpp
    src/upower/upowerhost.cpp
    src/upower/upowerdevice.cpp
    src/upower/upowerdevicemodel.cpp
)

# ═══════════════════════════════════════════════════════════════════════════════
# Main Shared Library
# ═══════════════════════════════════════════════════════════════════════════════

add_library(PhosphorServices SHARED
    ${phosphorservices_public_HDRS}
    ${phosphorservices_SRCS}
    ${phosphorservices_DBUS_GENERATED}
)

add_library(PhosphorServices::PhosphorServices ALIAS PhosphorServices)

generate_export_header(PhosphorServices
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServices/phosphorservices_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICES_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServices
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(PhosphorServices PUBLIC cxx_std_20)

# Force-include dbustypes.h in every TU — including AUTOMOC's
# mocs_compilation.cpp. The generated D-Bus interface headers
# (statusnotifieritem_interface.h, dbusmenu_interface.h) reference
# PhosphorServices::DBusImageList/DBusToolTip/DBusMenuLayoutItem in
# their signatures (via XML annotations), but the generated .h files
# have no #include for dbustypes.h. AUTOMOC processes them in
# isolation; without the forced include, the moc output fails.
target_compile_options(PhosphorServices PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/src/statusnotifier/dbustypes.h
)

target_link_libraries(PhosphorServices
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::DBus
)

set_target_properties(PhosphorServices PROPERTIES
    VERSION ${PHOSPHORSERVICES_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # Generated D-Bus interface headers reference custom types (DBusImageList,
    # DBusMenuLayoutItem, etc.) that are only defined in dbustypes.h. In a unity
    # build, the MOC output for these generated headers can compile before
    # dbustypes.h is included, producing "not a member of" errors. Disabling
    # unity build ensures each TU includes its own headers in the correct order.
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServices
    EXPORT PhosphorServicesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(FILES ${phosphorservices_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServices
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServices/phosphorservices_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServices
)

install(EXPORT PhosphorServicesTargets
    FILE PhosphorServicesTargets.cmake
    NAMESPACE PhosphorServices::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServices
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServicesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicesConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServices
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicesConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICES_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicesConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServices
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServices" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
