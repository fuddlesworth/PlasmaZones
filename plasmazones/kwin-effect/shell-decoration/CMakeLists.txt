# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
find_package(KDecoration3 6.7 REQUIRED)
kcoreaddons_add_plugin(phosphor_decoration
    SOURCES PhosphorDecoration.h phosphordecoration.cpp
    INSTALL_NAMESPACE "org.kde.kdecoration3"
)
set_target_properties(phosphor_decoration PROPERTIES
    OUTPUT_NAME "org.phosphor.decoration"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/org.kde.kdecoration3"
    UNITY_BUILD OFF
)
target_link_libraries(phosphor_decoration PRIVATE
    KDecoration3::KDecoration KF6::CoreAddons Qt6::Gui
    PhosphorTheme::PhosphorTheme
)
