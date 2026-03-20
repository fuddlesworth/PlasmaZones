# PlasmaZones KCM (KDE Control Module)
# SPDX-License-Identifier: GPL-3.0-or-later

# Shared library for helpers used by sub-KCMs
add_subdirectory(common)

# Sub-KCMs (each appears as its own page under the PlasmaZones subcategory)
add_subdirectory(about)
add_subdirectory(exclusions)
add_subdirectory(general)
add_subdirectory(snapping)
add_subdirectory(autotiling)
add_subdirectory(layouts)
add_subdirectory(assignments)
add_subdirectory(editor)

# Install category desktop file for System Settings subcategory
install(FILES
    settings-windowmanagement-plasmazones.desktop
    DESTINATION ${KDE_INSTALL_DATADIR}/systemsettings/categories
)
