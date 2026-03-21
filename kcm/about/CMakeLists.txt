# PlasmaZones About sub-KCM
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Minimal KCM: daemon toggle, version info, update checks, links.
# All settings configuration is in the standalone plasmazones-settings app.

set(kcm_about_SRCS
    kcmabout.cpp
    kcmabout.h
    ../plasmazonesmoduledata.cpp
    ../plasmazonesmoduledata.h
    # DaemonController compiled directly (Qt-only, no KDE Framework deps)
    ../common/daemoncontroller.cpp
    ../common/daemoncontroller.h
)

kcmutils_add_qml_kcm(kcm_plasmazones_about
    SOURCES ${kcm_about_SRCS}
)

target_include_directories(kcm_plasmazones_about PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/kcm/common
)

target_link_libraries(kcm_plasmazones_about
    PRIVATE
        plasmazones_core
        Qt6::DBus
        KF6::KCMUtils
        KF6::ConfigWidgets
)
