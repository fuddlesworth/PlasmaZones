# PlasmaZones KCM (KDE Control Module)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Minimal KCM — version info + launcher for plasmazones-settings app.
# All settings configuration is handled by the standalone plasmazones-settings app.

add_subdirectory(about)

# Install category desktop file for System Settings subcategory
install(FILES
    settings-windowmanagement-plasmazones.desktop
    DESTINATION ${KDE_INSTALL_DATADIR}/systemsettings/categories
)
