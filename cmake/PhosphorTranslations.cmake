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
#   make (default)   - compiles translations/*.ts → .qm
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
# BatchOperationScope.h, src/config/settingsvaluelabels.cpp
# and src/core/utils/unifiedlayoutlist.cpp were
# each unreachable until someone happened to notice. Headers are included
# because PhosphorI18n::tr() calls live in them too.
#
# "Whole app tree" means src/, kcm/ and kwin-effect/. libs/ is NOT swept
# wholesale: those are LGPL components with their own consumers, and most of
# them ship untranslated by design. The one exception is phosphor-control,
# whose QML IS the settings app's chrome — see the qsTr glob below. If another
# library starts carrying user-facing text, it has to be added deliberately,
# and this comment is the reason it will not happen by accident.
#
# CONFIGURE_DEPENDS makes the build re-run the glob, so a newly-added file is
# extractable without a manual `cmake` invocation.
file(GLOB_RECURSE PLASMAZONES_I18N_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/kcm/*.cpp"
    "${CMAKE_SOURCE_DIR}/kcm/*.h"
    # The KWin effect carries user-facing text of its own since the scrolling
    # tab indicators moved into it (the untitled-tab placeholder); it loads
    # the same "plasmazones" catalog at construction.
    "${CMAKE_SOURCE_DIR}/kwin-effect/*.cpp"
    "${CMAKE_SOURCE_DIR}/kwin-effect/*.h"
)
file(GLOB_RECURSE PLASMAZONES_I18N_QML CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.qml"
    # phosphor-control extracts nothing HERE today — its chrome calls qsTr(),
    # which lupdate reads natively via the qsTr glob below, and the one `i18n`
    # string in that tree is inside a code comment (Sidebar.qml). Listed anyway
    # so that the day someone adds a real i18n() call to the settings chrome it
    # is picked up instead of silently going missing, which is the failure this
    # whole file exists to prevent.
    "${CMAKE_SOURCE_DIR}/libs/phosphor-control/qml/*.qml"
)

# phosphor-control's QML is ALSO handed to lupdate raw, below. Its chrome
# (the apply/discard footer, sidebar, breadcrumbs, page-loading placeholder)
# calls qsTr() rather than i18n(), which lupdate understands natively — but the
# glob above never reached libs/, so none of it was ever extracted and the
# settings window's most-used controls read "Save" / "Discard" / "Back" /
# "Search..." in English in every locale. qsTr keeps the enclosing component as
# the message context instead of "plasmazones"; that is fine, because a .qm
# holds every context and QTranslator resolves per context at lookup time.
#
# Deliberately NOT extended to libs/phosphor-shell-*: those surfaces follow the
# shell subtree's own convention of shipping untranslated, and pulling them in
# would add contexts nobody translates.
#
# kcm/ is here rather than in the stub glob above for a different reason: the
# About KCM is a plugin inside systemsettings, which installs no
# PhosphorLocalizedContext, so i18n() there had no backing at all and its nine
# extracted messages could never be served. Its QML calls qsTr() and the plugin
# installs a plain QTranslator itself (kcm/about/kcmabout.cpp), which needs no
# link against plasmazones_core.
file(GLOB_RECURSE PLASMAZONES_I18N_QML_QSTR CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/libs/phosphor-control/qml/*.qml"
    "${CMAKE_SOURCE_DIR}/kcm/*.qml"
)

# QML is NOT handed to lupdate directly. lupdate's QML parser only recognizes
# qsTr()/qsTranslate(), and our QML calls i18n()/i18nc()/i18np()/i18ncp() via
# PhosphorLocalizedContext, so lupdate read every .qml and extracted nothing:
# the whole QML UI was untranslatable for as long as this file has existed.
# scripts/qml-i18n-stubs.py transcribes each call into a C++ stub that lupdate
# does understand, and those stubs go to lupdate instead. See that script for
# why -tr-function-alias cannot express the shape we need.
#
# KNOWN CAVEAT: the committed catalogs' <location> entries for QML strings
# point into this gitignored stub tree, so on a fresh clone a translator has
# no on-disk source context for them until update-ts regenerates the stubs.
# The stub filenames mirror the real .qml paths (translations/.qml-stubs/
# src/.../Foo.qml.cpp, same line numbers), so the mapping back to the real
# source is mechanical.
set(_qml_stub_dir "${CMAKE_SOURCE_DIR}/translations/.qml-stubs")

# Collect all .ts files once (en template + per-language); the compile list
# below filters the template back out.
file(GLOB _all_ts_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/translations/plasmazones_*.ts")

# Per-language .ts files (plasmazones_de.ts, plasmazones_fr.ts, etc.)
# Flat layout: translations/plasmazones_<lang>.ts → plasmazones_<lang>.qm
# The English source template is excluded from compilation (no translations).
set(TRANSLATION_TS_FILES ${_all_ts_files})
list(FILTER TRANSLATION_TS_FILES EXCLUDE REGEX "plasmazones_en\\.ts$")

# --- update-ts target ---
# PhosphorI18n::tr() uses Q_DECLARE_TR_FUNCTIONS(plasmazones), so lupdate
# recognizes the C++ side natively. QML goes through the stub step first.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Qt6LinguistTools_FOUND AND Python3_Interpreter_FOUND)
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
        # -no-obsolete: drop `type="vanished"` / `type="obsolete"` entries for
        # sources that no longer exist. Without it every rename leaves its old
        # string behind in all seven catalogs forever, and a routine refresh
        # produces dozens of dead <message> blocks that bury the real diff. The
        # committed catalogs already carry none, so this makes the target
        # reproduce the state the repo is actually kept in rather than relying
        # on whoever runs it remembering the flag.
        #
        # The trade-off is deliberate: if a scrape gap (see the incident notes
        # in this file's history) makes lupdate miss a still-live string, this
        # flag deletes its translations from all seven catalogs in the same
        # run. That deletion is NOT data loss — the catalogs are committed, so
        # `git diff` shows every dropped <message> before it is ever pushed,
        # and the translations come back verbatim from git history once the
        # scrape gap is fixed. Review the catalog diff after every update-ts
        # run; a wave of deletions you did not expect IS the scrape-gap alarm.
        COMMAND Qt6::lupdate
            -no-obsolete
            -I ${CMAKE_SOURCE_DIR}/src
            ${PLASMAZONES_I18N_SOURCES}
            ${PLASMAZONES_I18N_QML_QSTR}
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
