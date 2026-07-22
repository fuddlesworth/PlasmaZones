# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Translations (Qt Linguist)
#
# Daemon and editor use Qt Linguist (.ts/.qm).  lupdate scans source files
# directly - i18n()/i18nc() are backed by Q_DECLARE_TR_FUNCTIONS(plasmazones)
# in phosphor_i18n.h, so lupdate recognizes them as tr() calls.
#
#   make update-ts   - run lupdate to refresh .ts from source
#   make (default)   - compiles translations/*/*.ts → .qm
#
# Included from the top-level CMakeLists.txt via include(), so it runs in
# that scope (every variable set here stays visible to the caller, exactly
# as if the block were still inline).
find_package(Qt6LinguistTools QUIET)

# Source files for lupdate string extraction (daemon + autotile + editor)
set(PLASMAZONESD_I18N_SOURCES
    ${CMAKE_SOURCE_DIR}/src/daemon/main.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/daemon.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/daemon/osd.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/overlayservice.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/shortcutmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/dbus/windowdragadaptor/windowdragadaptor.cpp
    ${CMAKE_SOURCE_DIR}/src/dbus/windowdragadaptor/drag.cpp
)
file(GLOB PLASMAZONESD_AUTOTILE_SOURCES
    "${CMAKE_SOURCE_DIR}/src/autotile/algorithms/*.cpp"
)
list(APPEND PLASMAZONESD_I18N_SOURCES ${PLASMAZONESD_AUTOTILE_SOURCES})
file(GLOB PLASMAZONESD_QML "${CMAKE_SOURCE_DIR}/src/ui/*.qml")

file(GLOB PLASMAZONES_EDITOR_I18N_SOURCES
    "${CMAKE_SOURCE_DIR}/src/editor/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/EditorController.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/gaps.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/shader.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/layout.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/selection.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/multiselect.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/visibility.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/clipboard.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/zones.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/zoneops.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/undo/commands/*.cpp"
)
file(GLOB PLASMAZONES_EDITOR_QML "${CMAKE_SOURCE_DIR}/src/editor/qml/*.qml")

# KCM QML sources (also covered by lupdate now)
file(GLOB_RECURSE KCM_PLASMAZONES_QML "${CMAKE_SOURCE_DIR}/kcm/*.qml")
list(APPEND KCM_PLASMAZONES_QML ${CMAKE_SOURCE_DIR}/src/shared/AspectRatioBadge.qml)
list(APPEND KCM_PLASMAZONES_QML ${CMAKE_SOURCE_DIR}/src/shared/CategoryBadge.qml)

# Standalone settings app — all of its C++ (PhosphorI18n::tr) and QML (i18n)
# strings. Globbed recursively so a newly-added controller or page is picked up
# without touching this list. The settings app already installs the plasmazones
# catalog at runtime via PlasmaZones::loadTranslations(), so these strings are
# the same translation context as the daemon / editor / KCM.
file(GLOB_RECURSE PLASMAZONES_SETTINGS_I18N_SOURCES "${CMAKE_SOURCE_DIR}/src/settings/*.cpp")
file(GLOB_RECURSE PLASMAZONES_SETTINGS_QML "${CMAKE_SOURCE_DIR}/src/settings/*.qml")

# All sources for lupdate (daemon + editor + KCM + settings - same translation context)
set(_all_i18n_sources
    ${PLASMAZONESD_I18N_SOURCES} ${PLASMAZONESD_QML}
    ${PLASMAZONES_EDITOR_I18N_SOURCES} ${PLASMAZONES_EDITOR_QML}
    ${KCM_PLASMAZONES_QML}
    ${PLASMAZONES_SETTINGS_I18N_SOURCES} ${PLASMAZONES_SETTINGS_QML}
)

# Collect per-language .ts files (plasmazones_de.ts, plasmazones_fr.ts, etc.)
# Flat layout: translations/plasmazones_<lang>.ts → plasmazones_<lang>.qm
file(GLOB TRANSLATION_TS_FILES "${CMAKE_SOURCE_DIR}/translations/plasmazones_*.ts")
# Exclude the English source template from compilation (it has no translations)
list(FILTER TRANSLATION_TS_FILES EXCLUDE REGEX "plasmazones_en\\.ts$")

# --- update-ts target: lupdate scans source directly ---
# PhosphorI18n::tr() uses Q_DECLARE_TR_FUNCTIONS(plasmazones) - lupdate
# recognizes tr() natively.  No custom scripts or intermediate files.
if(Qt6LinguistTools_FOUND)
    # All .ts files to update (en template + per-language)
    file(GLOB _all_ts_files "${CMAKE_SOURCE_DIR}/translations/plasmazones_*.ts")

    add_custom_target(update-ts
        COMMENT "Updating .ts translation files from source (lupdate)"
        COMMAND Qt6::lupdate
            -I ${CMAKE_SOURCE_DIR}/src
            ${_all_i18n_sources}
            -ts ${_all_ts_files}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
else()
    add_custom_target(update-ts
        COMMAND ${CMAKE_COMMAND} -E echo "Qt6LinguistTools not found; install qt6-tools-dev"
    )
endif()

# --- Compile .ts → .qm and install ---
# Output: plasmazones_de.qm, plasmazones_fr.qm, etc.
# QTranslator::load(locale, "plasmazones", "_", dir) finds these by name.
if(Qt6LinguistTools_FOUND AND TRANSLATION_TS_FILES)
    qt_add_lrelease(plasmazones_translations
        TS_FILES ${TRANSLATION_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE QM_FILES
    )
    install(FILES ${QM_FILES} DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/translations)
endif()
