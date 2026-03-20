# PlasmaZones KCM shared library
# Contains helpers decoupled from the KCM monolith, reusable by all sub-KCMs.
# SPDX-License-Identifier: GPL-3.0-or-later

# KConfigXT — each consumer needs its own copy of the generated symbols
kconfig_add_kcfg_files(kcm_common_kcfg_SRCS
    ../../src/config/plasmazones.kcfgc
    GENERATE_MOC
)

add_library(plasmazones_kcm_common STATIC
    assignmentmanager.cpp
    assignmentmanager_saveload.cpp
    assignmentmanager.h
    daemoncontroller.cpp
    daemoncontroller.h
    dbusutils.h
    layoutmanager.cpp
    layoutmanager.h
    perscreenhelpers.h
    screenhelper.cpp
    screenhelper.h
    screenprovider.cpp
    screenprovider.h
    ${kcm_common_kcfg_SRCS}
)

target_include_directories(plasmazones_kcm_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Static lib linked into shared modules (.so) needs position-independent code
set_target_properties(plasmazones_kcm_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(plasmazones_kcm_common
    PUBLIC
        plasmazones_core
        Qt6::Quick
        Qt6::DBus
        KF6::ConfigWidgets
        KF6::GlobalAccel
)
