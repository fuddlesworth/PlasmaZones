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

# Source files for lupdate string extraction.
#
# The daemon, editor, KCM and settings app all load the same plasmazones
# catalog at runtime via PlasmaZones::loadTranslations(), so they share one
# translation context and one source set. That set is the whole app tree, not
# a per-binary list: partitioning it by binary is what produced every
# extraction gap this file has had. src/daemon/daemon/lifecycle.cpp lost a
# user-facing notification when the daemon.cpp split moved it out of a listed
# file, and src/editor/EditorGapsModel.cpp, src/editor/helpers/
# BatchOperationScope.h, src/config/settingsvaluelabels.cpp,
# src/config/updatechecker.cpp and src/core/utils/unifiedlayoutlist.cpp were
# each unreachable until someone happened to notice. Headers are included
# because PhosphorI18n::tr() calls live in them too.
#
# CONFIGURE_DEPENDS makes the build re-run the glob, so a newly-added file is
# extractable without a manual `cmake` invocation.
file(GLOB_RECURSE PLASMAZONES_I18N_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/kcm/*.cpp"
    "${CMAKE_SOURCE_DIR}/kcm/*.h"
)
file(GLOB_RECURSE PLASMAZONES_I18N_QML CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.qml"
    "${CMAKE_SOURCE_DIR}/kcm/*.qml"
)

set(_all_i18n_sources ${PLASMAZONES_I18N_SOURCES} ${PLASMAZONES_I18N_QML})

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
