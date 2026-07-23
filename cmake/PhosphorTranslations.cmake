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

# QML is NOT handed to lupdate directly. lupdate's QML parser only recognizes
# qsTr()/qsTranslate(), and our QML calls i18n()/i18nc()/i18np()/i18ncp() via
# PhosphorLocalizedContext, so lupdate read every .qml and extracted nothing:
# the whole QML UI was untranslatable for as long as this file has existed.
# scripts/qml-i18n-stubs.py transcribes each call into a C++ stub that lupdate
# does understand, and those stubs go to lupdate instead. See that script for
# why -tr-function-alias cannot express the shape we need.
set(_qml_stub_dir "${CMAKE_SOURCE_DIR}/translations/.qml-stubs")

set(_all_i18n_sources ${PLASMAZONES_I18N_SOURCES})

# Collect per-language .ts files (plasmazones_de.ts, plasmazones_fr.ts, etc.)
# Flat layout: translations/plasmazones_<lang>.ts → plasmazones_<lang>.qm
file(GLOB TRANSLATION_TS_FILES "${CMAKE_SOURCE_DIR}/translations/plasmazones_*.ts")
# Exclude the English source template from compilation (it has no translations)
list(FILTER TRANSLATION_TS_FILES EXCLUDE REGEX "plasmazones_en\\.ts$")

# --- update-ts target ---
# PhosphorI18n::tr() uses Q_DECLARE_TR_FUNCTIONS(plasmazones), so lupdate
# recognizes the C++ side natively. QML goes through the stub step first.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Qt6LinguistTools_FOUND AND Python3_Interpreter_FOUND)
    # All .ts files to update (en template + per-language)
    file(GLOB _all_ts_files "${CMAKE_SOURCE_DIR}/translations/plasmazones_*.ts")

    # The stub list is only known after the script runs, so lupdate is pointed
    # at the whole stub tree via the @list file the script writes.
    add_custom_target(update-ts
        COMMENT "Updating .ts translation files from source (lupdate)"
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_qml_stub_dir}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/qml-i18n-stubs.py
            --source-root ${CMAKE_SOURCE_DIR}
            --out-dir ${_qml_stub_dir}
            --list ${_qml_stub_dir}/stubs.txt
            ${PLASMAZONES_I18N_QML}
        COMMAND Qt6::lupdate
            -I ${CMAKE_SOURCE_DIR}/src
            ${_all_i18n_sources}
            "@${_qml_stub_dir}/stubs.txt"
            -ts ${_all_ts_files}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
elseif(Qt6LinguistTools_FOUND)
    add_custom_target(update-ts
        COMMAND ${CMAKE_COMMAND} -E echo "Python3 not found; needed to extract i18n() from QML"
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
