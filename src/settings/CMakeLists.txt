# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Standalone Settings Application —
# compositor-agnostic settings app. Uses Kirigami for UI, no KDE System
# Settings dependency.

find_package(KF6Kirigami REQUIRED)

# Suppress -Wmissing-include-dirs for Qt-managed AUTOMOC scratch dirs.
# KDECompilerSettings enables the warning globally, and qt_add_qml_module
# adds `<target>_autogen/include` to each generated sub-target's -I list;
# the no-Q_OBJECT sub-targets (qmltyperegistration, resource init) leave
# that dir empty or — after `--clean-first` — missing, so cc1plus fires
# before AUTOMOC recreates it. DIRECTORY level so every implicitly-named
# sub-target inherits it. Mirrors src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

set(plasmazones_settings_SRCS
    main.cpp
    settingscontroller.h
    settingscontroller.cpp
    # SettingsController is large enough to violate the 800-line cap
    # if kept as a single TU. The sub-domain methods live in sibling
    # .cpp files compiled into the same target — same class, multiple
    # translation units, no API change. Order matches reading order:
    # construction → lifecycle → page registry → per-domain CRUD.
    settingscontroller_lifecycle.cpp
    settingscontroller_pageregistration.cpp
    settingscontroller_layouts.cpp
    settingscontroller_session.cpp
    # Per-screen autotile/snapping/zone-selector override wrappers — split
    # out so settingscontroller_session.cpp stays under the 800-line cap.
    settingscontroller_perscreen.cpp
    settingscontroller_ordering.cpp
    # Virtual-screen lifecycle methods (getVirtualScreenConfig +
    # apply/remove/stage variants). Split out of _session.cpp to
    # keep that TU under the 800-line cap (CLAUDE.md).
    settingscontroller_virtualscreens.cpp
    # D-Bus subscription wiring (wireDaemonSubscriptions + reloadLocal
    # RuleStore) — extracted from settingscontroller.cpp so the central
    # TU stays under the 800-line cap (CLAUDE.md).
    settingscontroller_dbuswire.cpp
    # Active-page navigation + per-page dirty tracking + external-edit
    # envelope — extracted from settingscontroller.cpp so the central TU
    # stays under the 800-line cap (CLAUDE.md).
    settingscontroller_pagestate.cpp
    # RAII begin/endExternalEdit wrapper — extracted from
    # settingscontroller.h so the header stays under the 800-line cap.
    externaleditscope.h
    externaleditscope.cpp
    # Per-page sub-controllers — hang off SettingsController as child QObjects,
    # exposing the Q_PROPERTY surface for a single settings page each.
    animationspagecontroller.h
    animationspagecontroller.cpp
    # Internal header carrying the inline effect/parameter/profile→QVariantMap
    # helpers shared between the two AnimationsPageController TUs. Inline
    # definitions make cross-TU linkage independent of unity-build merging.
    animations_controller_detail.h
    # AnimationsPageController shader-leg methods compile into the same
    # target — same class, separate TU, no API change. Keeps the main
    # cpp under the 800-line cap (CLAUDE.md).
    animationspagecontroller_shaders.cpp
    # AnimationsPageController override-CRUD methods (userProfilesDir,
    # hasOverride / rawProfile / resolvedProfile / setOverride /
    # clearOverride). Same split rationale as _shaders.cpp — the main
    # TU grew past 800 lines after the F#2c async-revert addition.
    animationspagecontroller_overrides.cpp
    # AnimationsPageController path-discovery helpers
    # (sectionForPath / eventLabel / parentPath / parentChain). Same
    # split rationale — small per-Q_INVOKABLE methods extracted so the
    # main TU stays under the 800-line cap.
    animationspagecontroller_paths.cpp
    # Shared validate+copy primitive for installing shader packs.
    # Consumed by AnimationsPageController + SnappingShadersPageController.
    # Centralises the symlink-rejection + metadata.json safety checks.
    shaderpackinstaller.h
    shaderpackinstaller.cpp
    # Sub-services hosted by AnimationsPageController as child QObjects:
    # preset library + motion-set store. Split out to keep
    # animationspagecontroller.cpp under the project's 800-line cap.
    animationpresetlibrary.h
    animationpresetlibrary.cpp
    motionsetstore.h
    motionsetstore.cpp
    animationfileutils.h
    animationfileutils.cpp
    editorpagecontroller.h
    editorpagecontroller.cpp
    generalpagecontroller.h
    generalpagecontroller.cpp
    snappingzonescontroller.h
    snappingzonescontroller.cpp
    snappingwindowappearancecontroller.h
    snappingwindowappearancecontroller.cpp
    snappingbehaviorcontroller.h
    snappingbehaviorcontroller.cpp
    snappingeffectscontroller.h
    snappingeffectscontroller.cpp
    snappingshaderspagecontroller.h
    snappingshaderspagecontroller.cpp
    snappingzoneselectorcontroller.h
    snappingzoneselectorcontroller.cpp
    tilingalgorithmcontroller.h
    tilingalgorithmcontroller.cpp
    tilingappearancecontroller.h
    tilingappearancecontroller.cpp
    tilingbehaviorcontroller.h
    tilingbehaviorcontroller.cpp
    # PhosphorControl integration scaffold. PageAdapter wraps existing
    # plain-QObject page controllers so they can be registered with
    # PhosphorControl::PageRegistry without changing each controller's
    # inheritance. SettingsStagingDomain bridges SettingsController's
    # central needsSave/save()/load() to the framework's StagingDomain
    # contract. Both are scaffolding for the in-progress src/settings
    # migration onto libs/phosphor-control.
    pageadapter.h
    pageadapter.cpp
    settingsstagingdomain.h
    settingsstagingdomain.cpp
    # Window Rules page — the unified rule surface. WindowRuleController
    # owns one WindowRuleModel (QAbstractListModel) and talks to the
    # daemon's org.plasmazones.WindowRules adaptor.
    windowrulemodel.h
    windowrulemodel.cpp
    windowrulecontroller.h
    windowrulecontroller.cpp
    # Read-only / projection methods of WindowRuleController — sections,
    # rulesSnapshot, monitorOverview, ruleScreenIds, author surfaces
    # (matchFields/operatorsForField/actionTypes/defaultPayloadFor),
    # validation entry points. Split out so windowrulecontroller.cpp stays
    # under the 800-line cap; same class, separate TU, no API change.
    windowrulecontroller_views.cpp
    # WindowRuleController label-lookup setters (screen/activity/layout/
    # snapping-layout/tiling-algorithm) plus the wiring-completion gate.
    # Five tiny pass-throughs into WindowRuleModel; split out so
    # windowrulecontroller.cpp stays under the project's 800-line cap.
    windowrulecontroller_lookups.cpp
    # WindowRuleController authoring metadata + templates — pure read-only
    # helpers split out so windowrulecontroller.cpp stays under the project's
    # 800-line cap. The controller's Q_INVOKABLE entry points (matchFields,
    # operatorsForField, actionTypes, newEmptyRule, ruleTemplates,
    # newRuleFromTemplate) delegate here.
    windowruleauthoring.h
    windowruleauthoring.cpp
    windowruletemplates.h
    windowruletemplates.cpp
    # QAbstractItemModel adapter that exposes a rule's match expression
    # to QtQuick.Controls.TreeView for read-only preview. Registered as a
    # QML type via the qt_add_qml_module call below.
    matchexpressiontreemodel.h
    matchexpressiontreemodel.cpp
    # Shared helpers used by multiple sub-controllers.
    triggerutils.h
    triggerutils.cpp
    virtualscreenutils.h
    virtualscreenutils.cpp
    # Service / utility modules extracted from SettingsController.
    kzonesimporter.h
    kzonesimporter.cpp
    stagingservice.h
    stagingservice.cpp
    algorithmservice.h
    algorithmservice.cpp
    settingslaunchcontroller.h
    settingslaunchcontroller.cpp
    settingsappadaptor.h
    settingsappadaptor.cpp
    # DaemonController — daemon lifecycle management (start/stop/status)
    ${CMAKE_SOURCE_DIR}/src/common/daemoncontroller.h
    ${CMAKE_SOURCE_DIR}/src/common/daemoncontroller.cpp
    # LayoutPreview ↔ JSON/QVariantMap: provided by plasmazones_core
    # (was duplicated here previously; consolidated to avoid ODR drift).
    # Qt-only utilities (standalone-only, no KDE Framework deps)
    screenhelper.h
    screenhelper.cpp
    screenprovider.h
    screenprovider.cpp
    dbusutils.h
)

add_executable(plasmazones-settings ${plasmazones_settings_SRCS})

target_include_directories(plasmazones-settings PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Mark CurvePresets.qml as a QML singleton BEFORE the qt_add_qml_module
# call below. `qt_add_qml_module`'s auto-detection of `pragma Singleton`
# is unreliable on Qt 6.11 with our generator setup: the qmldir entry
# came out as a plain `CurvePresets 1.0 qml/CurvePresets.qml` — no
# `singleton` keyword — and consumers ended up accessing the QML *type*
# instead of the singleton instance ("TypeError: Property 'X' of object
# CurvePresets is not a function" at first AnimationEventCard /
# CurveEditorDialog load). Setting QT_QML_SINGLETON_TYPE on the source
# file forces qt_add_qml_module to write the `singleton` keyword. This
# MUST come before qt_add_qml_module — the function reads the property
# at registration time, not at link time.
set_source_files_properties(qml/CurvePresets.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(plasmazones-settings
    URI org.plasmazones.settings
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    SOURCES
        # C++ types exposed to QML via QML_ELEMENT — qt_add_qml_module
        # needs the source listed here (separately from the executable's
        # SOURCES above) so the moc-generated qmltyperegistration picks up
        # the macro and emits the type in plasmazones-settings.qmltypes.
        matchexpressiontreemodel.h
        matchexpressiontreemodel.cpp
        # WindowRuleModel — exposed as `WindowRuleModel` (uncreatable) so
        # QML can reference its `Section` enum (e.g.
        # `WindowRuleModel.Animation`) and `Roles` enum (e.g.
        # `WindowRuleModel.SectionRole`) without QML hardcoding the
        # integer values.
        windowrulemodel.h
        windowrulemodel.cpp
    QML_FILES
        qml/Main.qml
        qml/Toast.qml
        qml/KeyboardShortcutOverlay.qml
        qml/GeneralPage.qml
        # Snapping, organised by subject (Overlay / Zone Selector / Window),
        # each with a Behavior + Appearance page. Window Appearance also hosts
        # the per-screen Gaps; Shaders lives under Configuration.
        qml/SnappingOverlayBehaviorPage.qml
        qml/SnappingOverlayAppearancePage.qml
        qml/SnappingZoneSelectorPage.qml
        qml/SnappingWindowBehaviorPage.qml
        qml/SnappingWindowAppearancePage.qml
        qml/TilingAppearancePage.qml
        qml/TilingBehaviorPage.qml
        qml/TilingAlgorithmPage.qml
        qml/EditorPage.qml
        qml/LayoutsPage.qml
        qml/SnappingQuickShortcutsPage.qml
        qml/OrderingPage.qml
        qml/SnappingOrderingPage.qml
        qml/TilingQuickShortcutsPage.qml
        qml/TilingOrderingPage.qml
        # Window Rules — the unified rule page replacing the five legacy
        # assignment / app-rule surfaces. Every component (including the
        # recursively-instantiated MatchExpressionEditor) MUST be listed.
        qml/WindowRulesPage.qml
        qml/MonitorOverview.qml
        qml/MonitorOverviewTile.qml
        qml/WindowRuleRow.qml
        qml/WindowRuleSectionList.qml
        qml/RuleEditorSheet.qml
        qml/RuleEditorBody.qml
        qml/RuleEditorStatusBar.qml
        qml/AddRuleWizard.qml
        qml/DiscardChangesDialog.qml
        qml/RuleStartPicker.qml
        qml/RuleStartTile.qml
        qml/MatchExpressionEditor.qml
        qml/MatchExpressionView.qml
        qml/ActionListView.qml
        qml/MatchLeafEditor.qml
        qml/ActionListEditor.qml
        qml/ActionRow.qml
        qml/AboutPage.qml
        qml/WhatsNewPage.qml
        qml/MonitorStatePage.qml
        qml/VirtualScreensPage.qml
        qml/VirtualScreenPreview.qml
        # Sub-components (copied from KCM)
        qml/QuickLayoutSlotsCard.qml
        qml/WindowPickerDialog.qml
        qml/LayoutComboBox.qml
        qml/WideComboBox.qml
        qml/FontPickerDialog.qml
        qml/ColorButton.qml
        qml/InputCapture.qml
        qml/KeySequenceInput.qml
        qml/ModifierAndMouseCheckBoxes.qml
        qml/PositionPicker.qml
        qml/AlgorithmPreview.qml
        qml/LayoutThumbnail.qml
        qml/PerScreenOverrideHelper.qml
        qml/DisplayMap.qml
        qml/MonitorScopeChip.qml
        qml/LayoutGridDelegate.qml
        qml/LayoutToolbar.qml
        qml/LayoutFilterBar.qml
        qml/LayoutFilterLogic.js
        qml/ZoneSelectorSection.qml
        qml/EasingPreview.qml
        qml/EasingSettings.qml
        qml/SpringSettings.qml
        # Animations page — Phase 1+2 salvage from PR #291. Standalone
        # widgets + JS helpers; sub-pages and per-event cards land in
        # later phases. Phase 2 widgets (CurveThumbnail, SpringPreview,
        # CurveEditorDialog) bind to (omega, zeta) per
        # `PhosphorAnimation::Spring`. CurvePresets is a singleton
        # consumed by every Animations sub-page and the editor dialog.
        qml/AnimatedBoxTrack.qml
        qml/AnimationEventCard.qml
        qml/AnimationEventCardList.qml
        qml/AnimationProfileEditor.qml
        qml/AnimationsWindowsPage.qml
        qml/AnimationsEditorPage.qml
        qml/AnimationsOsdsPage.qml
        qml/AnimationsOverlaysPage.qml
        qml/AnimationsSidePanelsPage.qml
        qml/AnimationsWidgetsPage.qml
        qml/AnimationsGeneralPage.qml
        qml/AnimationsMotionSetsPage.qml
        qml/AnimationsPresetsPage.qml
        qml/AnimationsShadersPage.qml
        qml/SnappingShadersPage.qml
        # Pack-agnostic shader-browser components — shared by the
        # animation-shader and snapping-overlay-shader pages. Both pages
        # are thin wrappers that pass the right controller bridge.
        qml/ShaderBrowserPage.qml
        qml/ShaderBrowserCard.qml
        qml/ShaderBrowserDetailDialog.qml
        qml/CurveEditorDialog.qml
        qml/CurvePresets.qml
        qml/CurveThumbnail.qml
        qml/SpringPreview.qml
        qml/EasingCurve.js
        qml/SpringPhysics.js
        qml/SettingsCard.qml
        qml/GapsSettingsCard.qml
        qml/OsdCard.qml
        qml/ShortcutCaptureField.qml
        qml/ModifierComboBox.qml
        qml/SettingsSlider.qml
        qml/SettingsSpinBox.qml
        qml/SettingsToggleRow.qml
        qml/SettingsSwitch.qml
        qml/SettingsRow.qml
        qml/SettingsSeparator.qml
        qml/SettingsFlickable.qml
        qml/SettingsButtonGroup.qml
        qml/ColorSwatchRow.qml
        qml/NewLayoutDialog.qml
        qml/NewAlgorithmDialog.qml
        qml/WizardStepIndicator.qml
        qml/WizardFooter.qml
        qml/WizardTemplateCard.qml
        qml/WizardPreviewBadge.qml
        qml/WizardConfigCard.qml
        qml/WizardUtils.js
    NO_PLUGIN
)

# Embed What's New data
qt_add_resources(plasmazones-settings "whatsnew_data"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/data"
    FILES
        ${CMAKE_SOURCE_DIR}/data/whatsnew.json
)

# Embed org.plasmazones.common QML module — settings uses a hand-rolled
# qmldir (see org_plasmazones_common/qmldir) instead of linking
# plasmazones_shared_qmlplugin so the binary stays standalone. Every type
# the settings UI consumes from the shared module must be both listed
# here AND in the qmldir.
qt_add_resources(plasmazones-settings "shared_common_qml"
    PREFIX "/qt/qml/org/plasmazones/common"
    BASE "${CMAKE_SOURCE_DIR}/src/shared"
    FILES
        ${CMAKE_SOURCE_DIR}/src/shared/AspectRatioBadge.qml
        ${CMAKE_SOURCE_DIR}/src/shared/ZonePreview.qml
        ${CMAKE_SOURCE_DIR}/src/shared/CategoryBadge.qml
        ${CMAKE_SOURCE_DIR}/src/shared/CategoryMenuButton.qml
        ${CMAKE_SOURCE_DIR}/src/shared/ShaderParameterRow.qml
        ${CMAKE_SOURCE_DIR}/src/shared/ShaderParameterSection.qml
        ${CMAKE_SOURCE_DIR}/src/shared/ShaderParameterEditor.qml
        ${CMAKE_SOURCE_DIR}/src/shared/ShaderCompileErrorBanner.qml
        ${CMAKE_SOURCE_DIR}/src/shared/ZoneShaderRenderer.qml
)
qt_add_resources(plasmazones-settings "shared_common_qmldir"
    PREFIX "/qt/qml/org/plasmazones/common"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/org_plasmazones_common"
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/org_plasmazones_common/qmldir
)

target_link_libraries(plasmazones-settings
    PRIVATE
        plasmazones_core
        plasmazones_rendering  # ZoneShaderItem (live shader preview in the browser)
        plasmazones_shaderpreview  # shared ShaderPreviewController feed (T3.1)
        PhosphorAnimation::PhosphorAnimation  # motion runtime + QML module
        PhosphorAnimationQmlplugin  # Generated by qt_add_qml_module (STATIC)
        PhosphorControl::PhosphorControl  # ApplicationController, PageController, IStagingDomain
        PhosphorControl::PhosphorControlQml  # QML chrome: SettingsAppWindow, Sidebar, footer, etc.
        PhosphorControlQmlplugin  # Generated by qt_add_qml_module (STATIC) — registers the
                                  # org.phosphor.control module in-process. Without this the
                                  # backing lib links but the module is unregistered, so the
                                  # engine searches the (uninstalled) QML import path and fails
                                  # with "plugin PhosphorControlQmlplugin not found". Mirrors the
                                  # PhosphorAnimationQmlplugin link above.
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::DBus
        Qt6::Widgets
        KF6::Kirigami
)

install(TARGETS plasmazones-settings DESTINATION ${KDE_INSTALL_BINDIR})
install(FILES org.plasmazones.settings.desktop DESTINATION ${KDE_INSTALL_APPDIR})
