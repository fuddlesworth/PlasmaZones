# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceSni: StatusNotifierItem (system tray) host + watcher
# + com.canonical.dbusmenu context-menu model. Extracted from the
# original `phosphor-services` umbrella as the last of the Phase 2.0
# tenant splits (documented in
# docs/phosphor-shell-design/04-implementation-plan.md). Consumes
# phosphor-service-icontheme for XDG icon resolution and the Qt image
# provider that publishes raw IconPixmap blobs to QML.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICESNI_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceSni VERSION ${PHOSPHORSERVICESNI_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml DBus)

# The icon-theme resolver + image provider live in their own library
# post Phase 2.0. SNI hands raw IconPixmap blobs to the image
# provider's static registry (so QML's Image.source can route a QImage
# through a QUrl) and walks XDG icon themes for IconName /
# IconThemePath payloads. Both live in phosphor-service-icontheme.
#
# In-tree builds use add_subdirectory in the parent CMakeLists, which
# defines the imported target eagerly; this find_package call is a
# best-effort CONFIG probe for the standalone-build path. Either way
# the imported target must exist before we link, so a missing target
# is a hard failure.
find_package(PhosphorServiceIconTheme QUIET)
if(NOT TARGET PhosphorServiceIconTheme::PhosphorServiceIconTheme)
    message(FATAL_ERROR
        "PhosphorServiceSni requires PhosphorServiceIconTheme. "
        "Configure libs/phosphor-service-icontheme first (the root "
        "CMakeLists.txt does this automatically) or supply it via "
        "find_package.")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# DBus adaptors / interfaces (XML → generated headers)
# ═══════════════════════════════════════════════════════════════════════════════
#
# The Watcher we EXPOSE (qt6_add_dbus_adaptor). Apps + other hosts call us
# through this. The Item + dbusmenu we CONSUME (qt6_add_dbus_interface):
# each registered tray item lives in another process and we call it.

set(phosphorservicesni_DBUS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/dbus)

qt6_add_dbus_adaptor(phosphorservicesni_DBUS_GENERATED
    ${phosphorservicesni_DBUS_ROOT}/org.kde.StatusNotifierWatcher.xml
    src/statusnotifierwatcher.h
    PhosphorServiceSni::StatusNotifierWatcher
    statusnotifierwatcheradaptor
    StatusNotifierWatcherAdaptor
)

qt6_add_dbus_interface(phosphorservicesni_DBUS_GENERATED
    ${phosphorservicesni_DBUS_ROOT}/org.kde.StatusNotifierItem.xml
    statusnotifieritem_interface
)

qt6_add_dbus_interface(phosphorservicesni_DBUS_GENERATED
    ${phosphorservicesni_DBUS_ROOT}/com.canonical.dbusmenu.xml
    dbusmenu_interface
)

# ═══════════════════════════════════════════════════════════════════════════════
# Public headers + sources
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservicesni_public_HDRS
    include/PhosphorServiceSni/PhosphorServiceSni.h
    include/PhosphorServiceSni/QmlRegistration.h
    include/PhosphorServiceSni/StatusNotifierItem.h
    include/PhosphorServiceSni/StatusNotifierItemModel.h
    include/PhosphorServiceSni/StatusNotifierHost.h
    include/PhosphorServiceSni/DBusMenuModel.h
)

set(phosphorservicesni_SRCS
    src/qmlregistration.cpp
    src/dbustypes.cpp
    src/statusnotifierwatcher.cpp
    src/statusnotifierhost.cpp
    src/statusnotifieritem.cpp
    src/statusnotifieritemmodel.cpp
    src/dbusmenuhelpers.cpp
    src/dbusmenumodel.cpp
)

add_library(PhosphorServiceSni SHARED
    ${phosphorservicesni_public_HDRS}
    ${phosphorservicesni_SRCS}
    ${phosphorservicesni_DBUS_GENERATED}
)

add_library(PhosphorServiceSni::PhosphorServiceSni ALIAS PhosphorServiceSni)

generate_export_header(PhosphorServiceSni
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSni/phosphorservicesni_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICESNI_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceSni
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(PhosphorServiceSni PUBLIC cxx_std_20)

# Force-include dbustypes.h in every TU, including AUTOMOC's
# mocs_compilation.cpp. The generated D-Bus interface headers
# (statusnotifieritem_interface.h, dbusmenu_interface.h) reference
# PhosphorServiceSni::DBusImageList / DBusToolTip / DBusMenuLayoutItem
# in their signatures (via XML annotations), but the generated .h
# files have no #include for dbustypes.h. AUTOMOC processes them in
# isolation; without the forced include, the moc output fails.
#
# The `-include` flag is GCC/Clang-only (MSVC uses `/FI`). The project
# targets Linux/Qt6 so MSVC support is out of scope; guard the option
# explicitly so a future cross-build attempt with MSVC fails at
# configure time with a clear message rather than at compile time with
# "unknown option".
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(PhosphorServiceSni PRIVATE
        -include ${CMAKE_CURRENT_SOURCE_DIR}/src/dbustypes.h
    )
else()
    message(FATAL_ERROR
        "PhosphorServiceSni requires GCC or Clang for the dbustypes.h forced-include. "
        "MSVC support is out of scope; see CMakeLists.txt for the rationale.")
endif()

target_link_libraries(PhosphorServiceSni
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::DBus
        PhosphorServiceIconTheme::PhosphorServiceIconTheme
)

set_target_properties(PhosphorServiceSni PROPERTIES
    VERSION ${PHOSPHORSERVICESNI_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # Generated D-Bus interface headers reference custom types
    # (DBusImageList, DBusMenuLayoutItem, etc.) that are only defined
    # in dbustypes.h. In a unity build, the MOC output for these
    # generated headers can compile before dbustypes.h is included,
    # producing "not a member of" errors. Disabling unity build
    # ensures each TU includes its own headers in the correct order.
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServiceSni
    EXPORT PhosphorServiceSniTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorservicesni_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceSni
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSni/phosphorservicesni_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceSni
)

install(EXPORT PhosphorServiceSniTargets
    FILE PhosphorServiceSniTargets.cmake
    NAMESPACE PhosphorServiceSni::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceSni
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceSniConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSniConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceSni
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSniConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICESNI_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSniConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSniConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceSni
)

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceSni" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
