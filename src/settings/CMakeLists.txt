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
    searchcatalog.h
    searchcatalog.cpp
    searchproviders.h
    searchproviders.cpp
    registryshaderpreviewbackend.h
    settingscontroller.h
    settingscontroller.cpp
    # SettingsController's sub-domain methods live in sibling .cpp files
    # compiled into the same target — same class, multiple translation units,
    # no API change. Order matches reading order: construction → lifecycle →
    # page registry → per-domain CRUD.
    settingscontroller_lifecycle.cpp
    settingscontroller_pageregistration.cpp
    settingscontroller_pagetopology.cpp
    settingscontroller_layouts.cpp
    settingscontroller_session.cpp
    # Per-screen autotile/snapping/zone-selector override wrappers.
    settingscontroller_perscreen.cpp
    settingscontroller_ordering.cpp
    # Virtual-screen lifecycle methods (getVirtualScreenConfig +
    # apply/remove/stage variants).
    settingscontroller_virtualscreens.cpp
    # D-Bus subscription wiring (wireDaemonSubscriptions + reloadLocal
    # RuleStore).
    settingscontroller_dbuswire.cpp
    # Active-page navigation + per-page dirty tracking + external-edit envelope.
    settingscontroller_pagestate.cpp
    # Pure decoration page→root scoping helpers (root dispatch, prefix match,
    # root-scoped tree diff), split from pagestate so they unit-test without
    # the whole settings-app object graph.
    animationpagescope.h
    animationpagescope.cpp
    decorationpagescope.h
    decorationpagescope.cpp
    # RAII begin/endExternalEdit wrapper, extracted from settingscontroller.h.
    externaleditscope.h
    externaleditscope.cpp
    # Per-page sub-controllers — hang off SettingsController as child QObjects,
    # exposing the Q_PROPERTY surface for a single settings page each.
    animationspagecontroller.h
    animationspagecontroller.cpp
    # Internal header carrying the inline effect/parameter/profile→QVariantMap
    # helpers shared across the four AnimationsPageController TUs. Inline
    # definitions make cross-TU linkage independent of unity-build merging.
    animations_controller_detail.h
    # AnimationsPageController shader-leg methods compile into the same
    # target — same class, separate TU, no API change.
    animationspagecontroller_shaders.cpp
    # AnimationsPageController override-CRUD methods (userProfilesDir,
    # hasOverride / rawProfile / resolvedProfile / setOverride /
    # clearOverride).
    animationspagecontroller_overrides.cpp
    # AnimationsPageController path-discovery helpers, small per-Q_INVOKABLE
    # methods (sectionForPath / eventLabel / parentChain).
    animationspagecontroller_paths.cpp
    # Shared validate+copy primitive for installing shader packs.
    # Consumed by AnimationsPageController + SnappingShadersPageController.
    # Centralises the symlink-rejection + metadata.json safety checks.
    shaderpackinstaller.h
    shaderpackinstaller.cpp
    # Preset library — a sub-service hosted by AnimationsPageController as a
    # child QObject.
    animationpresetlibrary.h
    animationpresetlibrary.cpp
    # Shared set store (motion sets + decoration sets) and the motion-set
    # domain closures that specialise it.
    shadersetstore.h
    shadersetstore.cpp
    motionsetdomain.h
    motionsetdomain.cpp
    animationfileutils.h
    animationfileutils.cpp
    # Settings profiles: persistence/CRUD/inheritance store + its page controller.
    profilestore.h
    profilestore.cpp
    profilestore_diff.cpp
    profilestore_revert.cpp
    profilepagecontroller.h
    profilepagecontroller.cpp
    editorpagecontroller.h
    editorpagecontroller.cpp
    generalpagecontroller.h
    generalpagecontroller.cpp
    snappingzonescontroller.h
    snappingzonescontroller.cpp
    # Slim "Window Appearance" page bounds + baseline-rule-id facade (also
    # carries the unified gap slider bounds now that gaps live on this page).
    windowappearancecontroller.h
    windowappearancecontroller.cpp
    snappingbehaviorcontroller.h
    snappingbehaviorcontroller.cpp
    snappingeffectscontroller.h
    snappingeffectscontroller.cpp
    snappingshaderspagecontroller.h
    snappingshaderspagecontroller.cpp
    snappingzoneselectorcontroller.h
    snappingzoneselectorcontroller.cpp
    # Decoration drill-down page — PER-SURFACE chains of decoration shader
    # packs, resolved through a
    # DecorationProfileTree (baseline + walk-up inheritance). Parallel to
    # AnimationsPageController's tree model but with no per-event staging;
    # mutations write straight to the Settings decorationProfileTree.
    decorationpagecontroller.h
    decorationpagecontroller.cpp
    # Surface-taxonomy helper (parentChain) in a sibling TU. Same class,
    # separate TU.
    decorationpagecontroller_paths.cpp
    decorationpagecontroller_browser.cpp
    decorationpagecontroller_sets.cpp
    # Internal header carrying the inline effect/parameter/profile→QVariantMap
    # helpers. Consumed only by decorationpagecontroller.cpp; kept separate to
    # hold the conversion helpers out of the controller TU.
    decoration_controller_detail.h
    tilingalgorithmcontroller.h
    tilingalgorithmcontroller.cpp
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
    # Rules page — the unified rule surface. RuleController
    # owns one RuleModel (QAbstractListModel) and talks to the
    # daemon's org.plasmazones.Rules adaptor.
    rulemodel.h
    rulemodel.cpp
    rulecontroller.h
    rulecontroller.cpp
    # Read-only / projection methods of RuleController — sections,
    # rulesSnapshot, monitorOverview, ruleScreenIds, author surfaces
    # (matchFields/operatorsForField/actionTypes/defaultPayloadFor),
    # validation entry points. Same class, separate TU, no API change.
    rulecontroller_views.cpp
    # RuleController label-lookup setters (screen / activity / zone /
    # virtual-desktop / snapping-layout / tiling-algorithm / shader-effect /
    # overlay-shader / decoration-pack) plus the QML curve-label resolver.
    # Thin pass-throughs into RuleModel.
    rulecontroller_lookups.cpp
    # RuleController per-page dirty split + managed-baseline reset/discard.
    rulecontroller_baseline.cpp
    # RuleController authoring metadata + templates — pure read-only helpers.
    # The controller's Q_INVOKABLE entry points (matchFields, operatorsForField,
    # actionTypes, newEmptyRule, ruleTemplates, newRuleFromTemplate) delegate
    # here.
    ruleauthoring.h
    ruleauthoring.cpp
    ruletemplates.h
    ruletemplates.cpp
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
    algorithmscaffold.h
    algorithmscaffold.cpp
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

# GCC 16 LTO false positive: stageEntries() builds a QList of entries whose
# DecorationProfile holds std::optional members. A disengaged optional's payload
# bytes are legitimately uninitialized and only ever read behind the engaged
# flag, but GCC's link-time IPA maybe-uninitialized pass loses that guard across
# the QList append/reallocation inline chain and warns against the Qt headers
# (qswap.h / qshareddata_impl.h / bits/move.h). The warnings surface at the
# ltrans stage, so in-source diagnostic pragmas cannot reach them (the reported
# location is outside the pragma region) and whether they fire varies with LTO
# partitioning. Deterministic, file-scoped treatment: keep this one TU out of
# LTO so its functions are never re-analysed at link, and silence the same
# false positive class in its own compile.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(decorationpagecontroller_sets.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-lto;-Wno-maybe-uninitialized")
endif()

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
# TriggerLabels is a singleton for the same reason: the trigger bit tables are
# shared between the trigger editor and the profile diff.
set_source_files_properties(qml/TriggerLabels.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

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
        # RuleModel — exposed as `RuleModel` (uncreatable) so
        # QML can reference its `Section` enum (e.g.
        # `RuleModel.Animation`) and `Roles` enum (e.g.
        # `RuleModel.SectionRole`) without QML hardcoding the
        # integer values.
        rulemodel.h
        rulemodel.cpp
    QML_FILES
        qml/Main.qml
        qml/Toast.qml
        qml/KeyboardShortcutOverlay.qml
        qml/GlobalSearchField.qml
        qml/GeneralPage.qml
        # Snapping, organised by subject (Overlay / Zone Selector / Window),
        # each with a Behavior page. Shaders lives under Configuration. The
        # window border / title-bar appearance AND the unified gaps are shared
        # across modes and live on the top-level WindowAppearancePage.
        qml/SnappingOverlayBehaviorPage.qml
        qml/SnappingOverlayAppearancePage.qml
        qml/SnappingZoneSelectorPage.qml
        qml/SnappingWindowBehaviorPage.qml
        qml/TilingBehaviorPage.qml
        qml/TilingAlgorithmPage.qml
        qml/TilingSimplePage.qml
        qml/SnappingSimplePage.qml
        qml/GlobalTimingDefaultsCard.qml
        qml/AnimationWindowFilterCard.qml
        qml/SnappingWindowHandlingCard.qml
        qml/SnappingFocusCard.qml
        qml/SnappingZoneSpanCard.qml
        qml/TilingWindowHandlingCard.qml
        qml/TilingFocusCard.qml
        # Shared, mode-neutral page: the baseline window-appearance editor, which
        # also hosts the unified inner/outer gap controls.
        qml/WindowAppearancePage.qml
        qml/EditorPage.qml
        qml/LayoutsPage.qml
        qml/SnappingQuickShortcutsPage.qml
        qml/OrderingPage.qml
        qml/SnappingOrderingPage.qml
        qml/TilingQuickShortcutsPage.qml
        qml/TilingOrderingPage.qml
        # Rules — the unified rule page replacing the five legacy
        # assignment / app-rule surfaces. Every component (including the
        # recursively-instantiated MatchExpressionEditor) MUST be listed.
        qml/RulesPage.qml
        # Settings profiles: tree list page, expandable row, new-profile dialog.
        qml/ProfilesPage.qml
        qml/ProfileRow.qml
        qml/ProfileComboBox.qml
        qml/ProfileSwitcherHeader.qml
        qml/SectionHeaderPill.qml
        qml/ProfileDiffView.qml
        qml/ProfileDiffTree.js
        qml/ProfileSignature.qml
        qml/NewProfileDialog.qml
        qml/MonitorOverview.qml
        qml/MonitorOverviewTile.qml
        qml/ExpandableRowDelegate.qml
        qml/ExpandChevron.qml
        qml/ReorderableColumn.qml
        qml/RuleRow.qml
        qml/RuleSectionList.qml
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
        qml/StockAnimationConflictChip.qml
        qml/AboutPage.qml
        qml/WhatsNewPage.qml
        qml/MonitorStatePage.qml
        qml/VirtualScreensPage.qml
        qml/VirtualScreenPreview.qml
        # Sub-components (copied from KCM)
        qml/QuickLayoutSlotsCard.qml
        qml/WindowPickerDialog.qml
        qml/LayoutComboBox.qml
        qml/LayoutContextMenu.qml
        qml/WideComboBox.qml
        qml/FontPickerDialog.qml
        qml/ColorButton.qml
        qml/InputCapture.qml
        qml/ModifierAndMouseCheckBoxes.qml
        qml/PositionPicker.qml
        qml/AlgorithmPreview.qml
        qml/LayoutThumbnail.qml
        qml/PerScreenOverrideHelper.qml
        qml/DisplayMap.qml
        qml/MonitorScopeChip.qml
        qml/LayoutGridDelegate.qml
        qml/LayoutManageCard.qml
        qml/SegmentedViewSwitch.qml
        qml/LayoutFilterBar.qml
        qml/LayoutFilterLogic.js
        # Shared group/sort infrastructure (Layouts + Rules pages)
        qml/GroupSortBar.qml
        qml/GroupSortLogic.js
        # Shared theme-font construction (whole-group `font:` bindings)
        qml/FontUtils.js
        qml/ZoneSelectorSection.qml
        qml/EasingPreview.qml
        qml/EasingSettings.qml
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
        qml/AnimationsWindowMotionPage.qml
        qml/AnimationsWindowDraggingPage.qml
        qml/AnimationsOsdsPage.qml
        qml/AnimationsOverlaysPage.qml
        qml/AnimationsDesktopsPage.qml
        qml/AnimationsEditorPage.qml
        qml/AnimationsSidePanelsPage.qml
        qml/AnimationsWidgetsPage.qml
        qml/AnimationsGeneralPage.qml
        qml/AnimationsSimplePage.qml
        qml/AnimationsMotionSetsPage.qml
        qml/AnimationsPresetsPage.qml
        qml/AnimationsShadersPage.qml
        qml/SnappingShadersPage.qml
        # Decoration drill-down — per-surface override cards (Windows / OSDs /
        # Popups), each driving a ChainEditor (ordered decoration-pack list with
        # per-pack params) and border/titlebar field overrides via
        # DecorationPageController.
        qml/DecorationSetsPage.qml
        qml/DecorationShadersPage.qml
        qml/DecorationWindowsPage.qml
        qml/DecorationOsdsPage.qml
        qml/DecorationPopupsPage.qml
        qml/DecorationSurfaceCardList.qml
        qml/DecorationSurfaceCard.qml
        qml/ChainEditor.qml
        # Pack-agnostic shader-browser components — shared by the
        # animation-shader, snapping-overlay-shader and decoration-shader
        # pages. All three are thin wrappers that pass the right controller
        # bridge.
        qml/ShaderBrowserPage.qml
        qml/ShaderBrowserCard.qml
        # Domain-agnostic sets page + rich set card — shared by the Decoration
        # Sets and Motion Sets pages. Both are thin wrappers that pass the
        # right ShaderSetStore bridge plus their domain copy.
        qml/ShaderSetsPage.qml
        qml/ShaderSetCard.qml
        # Shared drag-and-drop import band (shader packs, layouts /
        # algorithms, sets).
        qml/FileDropZone.qml
        qml/ImportDropCard.qml
        # Shared row badge (neutral metadata + highlighted state).
        qml/MetadataChip.qml
        qml/FilterMenuButton.qml
        # Stay-open checkable menu item used by FilterMenuButton, the one
        # checkbox-filter-menu implementation (Layouts via LayoutFilterBar's
        # invisible instances, Rules + shader browser directly).
        qml/StayOpenMenuItem.qml
        qml/ShaderBrowserDetailDialog.qml
        qml/CurveEditorDialog.qml
        qml/CurvePresets.qml
        qml/TriggerLabels.qml
        qml/CurveThumbnail.qml
        qml/SpringPreview.qml
        qml/EasingCurve.js
        qml/SpringPhysics.js
        qml/SettingsCard.qml
        qml/GapsSettingsCard.qml
        qml/WindowFilterCard.qml
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
        qml/SearchAnchorHelpers.js
        qml/SettingsButtonGroup.qml
        qml/ColorSwatchRow.qml
        qml/NewLayoutDialog.qml
        qml/NewAlgorithmDialog.qml
        qml/WizardStepIndicator.qml
        qml/WizardFooter.qml
        qml/WizardTemplateCard.qml
        qml/WizardPreviewBadge.qml
        qml/WizardConfigCard.qml
        qml/WizardPreviewFrame.qml
        qml/WizardUtils.js
    NO_PLUGIN
)

# Embed What's New data + its JSON Schema (the committed
# data/schemas/whatsnew.schema.json, single source of truth shared with CI).
# whatsnew.json lands at :/whatsnew.json and the schema at
# :/schemas/whatsnew.schema.json, so SettingsController can validate it on load.
qt_add_resources(plasmazones-settings "whatsnew_data"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/data"
    FILES
        ${CMAKE_SOURCE_DIR}/data/whatsnew.json
        ${CMAKE_SOURCE_DIR}/data/schemas/whatsnew.schema.json
)

target_link_libraries(plasmazones-settings
    PRIVATE
        plasmazones_core
        plasmazones_rendering  # ZoneShaderItem (live shader preview in the browser)
        plasmazones_shaderpreview  # shared ShaderPreviewController feed (T3.1)
        PhosphorFsLoader::PhosphorFsLoader  # SchemaValidator — validates whatsnew.json on load
        PhosphorAnimation::PhosphorAnimation  # motion runtime + QML module
        PhosphorAnimationQmlplugin  # Generated by qt_add_qml_module (STATIC)
        plasmazones_shared_qmlplugin  # org.plasmazones.common (STATIC) — registered via
                                      # Q_IMPORT_QML_PLUGIN in main.cpp. Replaces the old
                                      # hand-rolled qmldir + resource embed, which silently
                                      # broke ("X is not a type") whenever a shared type
                                      # was added but not hand-listed; the generated
                                      # module can never drift.
        PhosphorSurface::PhosphorSurface  # SurfaceShaderRegistry/Effect (Decoration page)
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
