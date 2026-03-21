# PlasmaZones KCM (KDE Control Module)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Minimal KCM — daemon toggle + launcher for plasmazones-settings app.
# All settings configuration is handled by the standalone plasmazones-settings app.

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Config
    ConfigWidgets
)

# Single KCM module: daemon toggle + settings app launcher
# Common sources (DaemonController, etc.) are compiled directly by the about KCM.
add_subdirectory(about)

# Install category desktop file for System Settings subcategory
install(FILES
    settings-windowmanagement-plasmazones.desktop
    DESTINATION ${KDE_INSTALL_DATADIR}/systemsettings/categories
)
