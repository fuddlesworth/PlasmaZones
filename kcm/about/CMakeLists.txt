# PlasmaZones About sub-KCM
# SPDX-License-Identifier: GPL-3.0-or-later

set(kcm_about_SRCS
    kcmabout.cpp
    kcmabout.h
    ../plasmazonesmoduledata.cpp
    ../plasmazonesmoduledata.h
)

kcmutils_add_qml_kcm(kcm_plasmazones_about
    SOURCES ${kcm_about_SRCS}
)

target_link_libraries(kcm_plasmazones_about
    PRIVATE
        plasmazones_kcm_common
        plasmazones_core
        Qt6::DBus
        KF6::KCMUtils
        KF6::ConfigWidgets
)
