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
    search/searchcatalog.h
    search/searchcatalog.cpp
    search/searchproviders.h
    search/searchproviders.cpp
    utils/registryshaderpreviewbackend.h
    controller/settingscontroller.h
    controller/settingscontroller.cpp
    # SettingsController's sub-domain methods live in sibling .cpp files
    # compiled into the same target — same class, multiple translation units,
    # no API change. Order matches reading order: construction → lifecycle →
    # page registry → per-domain CRUD.
    controller/settingscontroller_lifecycle.cpp
    controller/settingscontroller_pageregistration.cpp
    controller/settingscontroller_pagetopology.cpp
    controller/settingscontroller_layouts.cpp
    controller/settingscontroller_session.cpp
    # Per-screen autotile/snapping/zone-selector override wrappers.
    controller/settingscontroller_perscreen.cpp
    controller/settingscontroller_ordering.cpp
    # Virtual-screen lifecycle methods (getVirtualScreenConfig +
    # apply/remove/stage variants).
    controller/settingscontroller_virtualscreens.cpp
    # D-Bus subscription wiring (wireDaemonSubscriptions + reloadLocal
    # RuleStore).
    controller/settingscontroller_dbuswire.cpp
    # Active-page navigation + per-page dirty tracking + external-edit envelope.
    controller/settingscontroller_pagestate.cpp
    # Per-page Reset/Discard, split from pagestate at the 1150-line ceiling.
    controller/settingscontroller_pagereset.cpp
    # Page-class predicates + shared-domain key lists both halves need (was an
    # anonymous namespace in pagestate; internal linkage cannot span two TUs).
    controller/settingscontroller_pagekeys.h
    controller/settingscontroller_pagekeys.cpp
    # Pure page→root scoping helpers for the animation and decoration domains
    # (root dispatch, prefix match, scoped tree diff), split from pagestate so
    # they unit-test without the whole settings-app object graph.
    pages/animationpagescope.h
    pages/animationpagescope.cpp
    pages/decorationpagescope.h
    pages/decorationpagescope.cpp
    # RAII begin/endExternalEdit wrapper, extracted from settingscontroller.h.
    services/externaleditscope.h
    services/externaleditscope.cpp
    # Per-page sub-controllers — hang off SettingsController as child QObjects,
    # exposing the Q_PROPERTY surface for a single settings page each.
    pages/animationspagecontroller.h
    pages/animationspagecontroller.cpp
    # Internal header carrying the inline effect/parameter/profile→QVariantMap
    # helpers shared across the four AnimationsPageController TUs. Inline
    # definitions make cross-TU linkage independent of unity-build merging.
    pages/animations_controller_detail.h
    # AnimationsPageController shader-leg methods compile into the same
    # target — same class, separate TU, no API change.
    pages/animationspagecontroller_shaders.cpp
    # AnimationsPageController override-CRUD methods (userProfilesDir,
    # hasOverride / rawProfile / resolvedProfile / setOverride /
    # clearOverride).
    pages/animationspagecontroller_overrides.cpp
    # AnimationsPageController path-discovery helpers, small per-Q_INVOKABLE
    # methods (sectionForPath / eventLabel / parentChain).
    pages/animationspagecontroller_paths.cpp
    # Shared validate+copy primitive for installing shader packs.
    # Consumed by AnimationsPageController + SnappingShadersPageController.
    # Centralises the symlink-rejection + metadata.json safety checks.
    services/shaderpackinstaller.h
    services/shaderpackinstaller.cpp
    # Preset library — a sub-service hosted by AnimationsPageController as a
    # child QObject.
    stores/animationpresetlibrary.h
    stores/animationpresetlibrary.cpp
    # Shared set store (motion sets + decoration sets) and the motion-set
    # domain closures that specialise it.
    stores/shadersetstore.h
    stores/shadersetstore.cpp
    services/motionsetdomain.h
    services/motionsetdomain.cpp
    utils/animationfileutils.h
    utils/animationfileutils.cpp
    # Settings profiles: persistence/CRUD/inheritance store + its page controller.
    stores/profilestore.h
    stores/profilestore.cpp
    stores/profilestore_diff.cpp
    stores/profilestore_revert.cpp
    pages/profilepagecontroller.h
    pages/profilepagecontroller.cpp
    pages/editorpagecontroller.h
    pages/editorpagecontroller.cpp
    pages/generalpagecontroller.h
    pages/generalpagecontroller.cpp
    pages/snappingzonescontroller.h
    pages/snappingzonescontroller.cpp
    # Slim "Window Appearance" page bounds + baseline-rule-id facade (also
    # carries the unified gap slider bounds now that gaps live on this page).
    pages/windowappearancecontroller.h
    pages/windowappearancecontroller.cpp
    pages/snappingbehaviorcontroller.h
    pages/snappingbehaviorcontroller.cpp
    pages/snappingeffectscontroller.h
    pages/snappingeffectscontroller.cpp
    pages/snappingshaderspagecontroller.h
    pages/snappingshaderspagecontroller.cpp
    pages/snappingzoneselectorcontroller.h
    pages/snappingzoneselectorcontroller.cpp
    # Decoration drill-down page — PER-SURFACE chains of decoration shader
    # packs, resolved through a
    # DecorationProfileTree (baseline + walk-up inheritance). Parallel to
    # AnimationsPageController's tree model but with no per-event staging;
    # mutations write straight to the Settings decorationProfileTree.
    pages/decorationpagecontroller.h
    pages/decorationpagecontroller.cpp
    # Surface-taxonomy helper (parentChain) in a sibling TU. Same class,
    # separate TU.
    pages/decorationpagecontroller_paths.cpp
    pages/decorationpagecontroller_browser.cpp
    pages/decorationpagecontroller_sets.cpp
    # Internal header carrying the inline effect/parameter/profile→QVariantMap
    # helpers. Consumed only by decorationpagecontroller.cpp; kept separate to
    # hold the conversion helpers out of the controller TU.
    pages/decoration_controller_detail.h
    pages/tilingalgorithmcontroller.h
    pages/tilingalgorithmcontroller.cpp
    pages/tilingbehaviorcontroller.h
    pages/tilingbehaviorcontroller.cpp
    # PhosphorControl integration scaffold. PageAdapter wraps existing
    # plain-QObject page controllers so they can be registered with
    # PhosphorControl::PageRegistry without changing each controller's
    # inheritance. SettingsStagingDomain bridges SettingsController's
    # central needsSave/save()/load() to the framework's StagingDomain
    # contract. Both are scaffolding for the in-progress src/settings
    # migration onto libs/phosphor-control.
    pageadapter.h
    pageadapter.cpp
    services/settingsstagingdomain.h
    services/settingsstagingdomain.cpp
    # Rules page — the unified rule surface. RuleController
    # owns one RuleModel (QAbstractListModel) and talks to the
    # daemon's org.plasmazones.Rules adaptor.
    rules/rulemodel.h
    rules/rulemodel.cpp
    rules/rulecontroller.h
    rules/rulecontroller.cpp
    # Read-only / projection methods of RuleController — sections,
    # rulesSnapshot, monitorOverview, ruleScreenIds, author surfaces
    # (matchFields/operatorsForField/actionTypes/defaultPayloadFor),
    # validation entry points. Same class, separate TU, no API change.
    rules/rulecontroller_views.cpp
    # RuleController label-lookup setters (screen / activity / zone /
    # virtual-desktop / snapping-layout / tiling-algorithm / shader-effect /
    # overlay-shader / decoration-pack) plus the QML curve-label resolver.
    # Thin pass-throughs into RuleModel.
    rules/rulecontroller_lookups.cpp
    # RuleController per-page dirty split + managed-baseline reset/discard.
    rules/rulecontroller_baseline.cpp
    # RuleController authoring metadata + templates — pure read-only helpers.
    # The controller's Q_INVOKABLE entry points (matchFields, operatorsForField,
    # actionTypes, newEmptyRule, ruleTemplates, newRuleFromTemplate) delegate
    # here.
    rules/ruleauthoring.h
    rules/ruleauthoring_p.h
    rules/ruleauthoring.cpp
    rules/ruleauthoring_actions.cpp
    rules/ruletemplates.h
    rules/ruletemplates.cpp
    # QAbstractItemModel adapter that exposes a rule's match expression
    # to QtQuick.Controls.TreeView for read-only preview. Registered as a
    # QML type via the qt_add_qml_module call below.
    rules/matchexpressiontreemodel.h
    rules/matchexpressiontreemodel.cpp
    # Shared helpers used by multiple sub-controllers.
    utils/triggerutils.h
    utils/triggerutils.cpp
    utils/virtualscreenutils.h
    utils/virtualscreenutils.cpp
    # Service / utility modules extracted from SettingsController.
    utils/kzonesimporter.h
    utils/kzonesimporter.cpp
    services/stagingservice.h
    services/stagingservice.cpp
    services/algorithmscaffold.h
    services/algorithmscaffold.cpp
    services/algorithmservice.h
    services/algorithmservice.cpp
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
    utils/screenhelper.h
    utils/screenhelper.cpp
    utils/screenprovider.h
    utils/screenprovider.cpp
    utils/dbusutils.h
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
    # qt_add_qml_module's generated type registration includes QML_ELEMENT
    # headers by bare basename (__has_include(<rulemodel.h>)), so the
    # directory holding those sources must be an include dir.
    ${CMAKE_CURRENT_SOURCE_DIR}/rules
)

# Mark CurvePresets.qml as a QML singleton BEFORE the qt_add_qml_module
# call below. `qt_add_qml_module`'s auto-detection of `pragma Singleton`
# is unreliable on Qt 6.11 with our generator setup: the qmldir entry
# came out as a plain `CurvePresets 1.0 qml/pages/animations/CurvePresets.qml` — no
# `singleton` keyword — and consumers ended up accessing the QML *type*
# instead of the singleton instance ("TypeError: Property 'X' of object
# CurvePresets is not a function" at first AnimationEventCard /
# CurveEditorDialog load). Setting QT_QML_SINGLETON_TYPE on the source
# file forces qt_add_qml_module to write the `singleton` keyword. This
# MUST come before qt_add_qml_module — the function reads the property
# at registration time, not at link time.
set_source_files_properties(qml/pages/animations/CurvePresets.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# TriggerLabels is a singleton for the same reason: the trigger bit tables are
# shared between the trigger editor and the profile diff.
set_source_files_properties(qml/components/TriggerLabels.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(plasmazones-settings
    URI org.plasmazones.settings
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    SOURCES
        # C++ types exposed to QML via QML_ELEMENT — qt_add_qml_module
        # needs the source listed here (separately from the executable's
        # SOURCES above) so the moc-generated qmltyperegistration picks up
        # the macro and emits the type in plasmazones-settings.qmltypes.
        rules/matchexpressiontreemodel.h
        rules/matchexpressiontreemodel.cpp
        # RuleModel — exposed as `RuleModel` (uncreatable) so
        # QML can reference its `Section` enum (e.g.
        # `RuleModel.Animation`) and `Roles` enum (e.g.
        # `RuleModel.SectionRole`) without QML hardcoding the
        # integer values.
        rules/rulemodel.h
        rules/rulemodel.cpp
    QML_FILES
        qml/Main.qml
        qml/dialogs/ConfirmDialogs.qml
        qml/components/Toast.qml
        qml/dialogs/KeyboardShortcutOverlay.qml
        qml/components/GlobalSearchField.qml
        qml/GeneralPage.qml
        # Snapping, organised by subject (Overlay / Zone Selector / Window),
        # each with a Behavior page. Shaders lives under Configuration. The
        # window border / title-bar appearance AND the unified gaps are shared
        # across modes and live on the top-level WindowAppearancePage.
        qml/pages/snapping/SnappingOverlayBehaviorPage.qml
        qml/pages/snapping/SnappingOverlayAppearancePage.qml
        qml/pages/snapping/SnappingZoneSelectorPage.qml
        qml/pages/snapping/SnappingWindowBehaviorPage.qml
        qml/pages/tiling/TilingBehaviorPage.qml
        qml/pages/tiling/TilingAlgorithmPage.qml
        qml/pages/tiling/TilingSimplePage.qml
        qml/pages/snapping/SnappingSimplePage.qml
        qml/components/GlobalTimingDefaultsCard.qml
        qml/pages/animations/AnimationWindowFilterCard.qml
        qml/pages/snapping/SnappingWindowHandlingCard.qml
        qml/pages/snapping/SnappingFocusCard.qml
        qml/pages/snapping/SnappingZoneSpanCard.qml
        qml/pages/snapping/SnappingDragTriggerRow.qml
        qml/pages/tiling/TilingWindowHandlingCard.qml
        qml/pages/tiling/TilingFocusCard.qml
        # Shared, mode-neutral page: the baseline window-appearance editor, which
        # also hosts the unified inner/outer gap controls.
        qml/pages/decoration/WindowAppearancePage.qml
        qml/EditorPage.qml
        qml/pages/layouts/LayoutsPage.qml
        qml/pages/snapping/SnappingQuickShortcutsPage.qml
        qml/OrderingPage.qml
        qml/pages/snapping/SnappingOrderingPage.qml
        qml/pages/tiling/TilingQuickShortcutsPage.qml
        qml/pages/tiling/TilingOrderingPage.qml
        # Rules — the unified rule page replacing the five legacy
        # assignment / app-rule surfaces. Every component (including the
        # recursively-instantiated MatchExpressionEditor) MUST be listed.
        qml/pages/rules/RulesPage.qml
        # Settings profiles: tree list page, expandable row, new-profile dialog.
        qml/pages/profiles/ProfilesPage.qml
        qml/pages/profiles/ProfileRow.qml
        qml/pages/profiles/ProfileComboBox.qml
        qml/pages/profiles/ProfileSwitcherHeader.qml
        qml/components/SectionHeaderPill.qml
        qml/pages/profiles/ProfileDiffView.qml
        qml/js/ProfileDiffTree.js
        qml/pages/profiles/ProfileSignature.qml
        qml/pages/profiles/NewProfileDialog.qml
        qml/pages/screens/MonitorOverview.qml
        qml/pages/screens/MonitorOverviewTile.qml
        qml/components/ExpandableRowDelegate.qml
        qml/components/ExpandChevron.qml
        qml/components/ReorderableColumn.qml
        qml/pages/rules/RuleRow.qml
        qml/pages/rules/RuleSectionList.qml
        qml/pages/rules/RuleEditorSheet.qml
        qml/pages/rules/RuleEditorBody.qml
        qml/pages/rules/RuleEditorStatusBar.qml
        qml/pages/rules/AddRuleWizard.qml
        qml/dialogs/DiscardChangesDialog.qml
        qml/pages/rules/RuleStartPicker.qml
        qml/pages/rules/RuleStartTile.qml
        qml/pages/rules/MatchExpressionEditor.qml
        qml/pages/rules/MatchExpressionView.qml
        qml/pages/rules/ActionListView.qml
        qml/pages/rules/MatchLeafEditor.qml
        qml/pages/rules/ActionListEditor.qml
        qml/pages/rules/ActionRow.qml
        qml/pages/rules/ActionParamEditors.qml
        qml/pages/animations/StockAnimationConflictChip.qml
        qml/AboutPage.qml
        qml/WhatsNewPage.qml
        qml/pages/screens/MonitorStatePage.qml
        qml/pages/screens/VirtualScreensPage.qml
        qml/pages/screens/VirtualScreenPreview.qml
        # Sub-components (copied from KCM)
        qml/pages/layouts/QuickLayoutSlotsCard.qml
        qml/dialogs/WindowPickerDialog.qml
        qml/pages/layouts/LayoutComboBox.qml
        qml/pages/layouts/LayoutContextMenu.qml
        qml/components/WideComboBox.qml
        qml/dialogs/FontPickerDialog.qml
        qml/components/ColorButton.qml
        qml/components/InputCapture.qml
        qml/components/ModifierAndMouseCheckBoxes.qml
        qml/dialogs/PositionPicker.qml
        qml/pages/tiling/AlgorithmPreview.qml
        qml/pages/tiling/AlgorithmPreviewCard.qml
        qml/pages/layouts/LayoutThumbnail.qml
        qml/pages/screens/PerScreenOverrideHelper.qml
        qml/pages/screens/DisplayMap.qml
        qml/pages/screens/MonitorScopeChip.qml
        qml/pages/layouts/LayoutGridDelegate.qml
        qml/pages/layouts/LayoutManageCard.qml
        qml/components/SegmentedViewSwitch.qml
        qml/pages/layouts/LayoutFilterBar.qml
        qml/js/LayoutFilterLogic.js
        # Shared group/sort infrastructure (Layouts + Rules pages)
        qml/components/GroupSortBar.qml
        qml/js/GroupSortLogic.js
        # Shared theme-font construction (whole-group `font:` bindings)
        qml/js/FontUtils.js
        qml/pages/screens/ZoneSelectorSection.qml
        qml/pages/animations/EasingPreview.qml
        qml/pages/animations/EasingSettings.qml
        # Animations page — Phase 1+2 salvage from PR #291. Standalone
        # widgets + JS helpers; sub-pages and per-event cards land in
        # later phases. Phase 2 widgets (CurveThumbnail, SpringPreview,
        # CurveEditorDialog) bind to (omega, zeta) per
        # `PhosphorAnimation::Spring`. CurvePresets is a singleton
        # consumed by every Animations sub-page and the editor dialog.
        qml/pages/animations/AnimatedBoxTrack.qml
        qml/pages/animations/AnimationEventCard.qml
        qml/pages/animations/AnimationEventCardList.qml
        qml/pages/animations/AnimationProfileEditor.qml
        qml/pages/animations/AnimationsWindowsPage.qml
        qml/pages/animations/AnimationsWindowMotionPage.qml
        qml/pages/animations/AnimationsWindowDraggingPage.qml
        qml/pages/animations/AnimationsOsdsPage.qml
        qml/pages/animations/AnimationsOverlaysPage.qml
        qml/pages/animations/AnimationsDesktopsPage.qml
        qml/pages/animations/AnimationsEditorPage.qml
        qml/pages/animations/AnimationsSidePanelsPage.qml
        qml/pages/animations/AnimationsWidgetsPage.qml
        qml/pages/animations/AnimationsGeneralPage.qml
        qml/pages/animations/AnimationsSimplePage.qml
        qml/pages/animations/AnimationsMotionSetsPage.qml
        qml/pages/animations/AnimationsPresetsPage.qml
        qml/pages/animations/AnimationsShadersPage.qml
        qml/pages/snapping/SnappingShadersPage.qml
        # Decoration drill-down — per-surface override cards (Windows / OSDs /
        # Popups), each driving a ChainEditor (ordered decoration-pack list with
        # per-pack params) and border/titlebar field overrides via
        # DecorationPageController.
        qml/pages/decoration/DecorationSetsPage.qml
        qml/pages/decoration/DecorationShadersPage.qml
        qml/pages/decoration/DecorationWindowsPage.qml
        qml/pages/decoration/DecorationOsdsPage.qml
        qml/pages/decoration/DecorationPopupsPage.qml
        qml/pages/decoration/DecorationSurfaceCardList.qml
        qml/pages/decoration/DecorationSurfaceCard.qml
        qml/pages/tiling/ChainEditor.qml
        # Pack-agnostic shader-browser components — shared by the
        # animation-shader, snapping-overlay-shader and decoration-shader
        # pages. All three are thin wrappers that pass the right controller
        # bridge.
        qml/pages/shaders/ShaderBrowserPage.qml
        qml/pages/shaders/ShaderBrowserCard.qml
        # Domain-agnostic sets page + rich set card — shared by the Decoration
        # Sets and Motion Sets pages. Both are thin wrappers that pass the
        # right ShaderSetStore bridge plus their domain copy.
        qml/pages/shaders/ShaderSetsPage.qml
        qml/pages/shaders/ShaderSetCard.qml
        # Shared drag-and-drop import band (shader packs, layouts /
        # algorithms, sets).
        qml/components/FileDropZone.qml
        qml/pages/shaders/ImportDropCard.qml
        # Shared row badge (neutral metadata + highlighted state).
        qml/components/MetadataChip.qml
        qml/components/FilterMenuButton.qml
        # Stay-open checkable menu item used by FilterMenuButton, the one
        # checkbox-filter-menu implementation (Layouts via LayoutFilterBar's
        # invisible instances, Rules + shader browser directly).
        qml/components/StayOpenMenuItem.qml
        qml/pages/shaders/ShaderBrowserDetailDialog.qml
        qml/pages/animations/CurveEditorDialog.qml
        qml/pages/animations/CurvePresets.qml
        qml/components/TriggerLabels.qml
        qml/pages/animations/CurveThumbnail.qml
        qml/pages/animations/SpringPreview.qml
        qml/js/EasingCurve.js
        qml/js/SpringPhysics.js
        qml/components/SettingsCard.qml
        qml/pages/layouts/GapsSettingsCard.qml
        qml/components/WindowFilterCard.qml
        qml/pages/decoration/OsdCard.qml
        qml/components/ShortcutCaptureField.qml
        qml/components/ModifierComboBox.qml
        qml/components/SettingsSlider.qml
        qml/components/SettingsSpinBox.qml
        qml/components/SettingsToggleRow.qml
        qml/components/SettingsSwitch.qml
        qml/components/SettingsRow.qml
        qml/components/SettingsSeparator.qml
        qml/components/SettingsFlickable.qml
        qml/js/AlgorithmCapabilities.js
        qml/js/SearchAnchorHelpers.js
        qml/components/SettingsButtonGroup.qml
        qml/components/ColorSwatchRow.qml
        qml/pages/layouts/NewLayoutDialog.qml
        qml/pages/tiling/NewAlgorithmDialog.qml
        qml/pages/wizard/WizardStepIndicator.qml
        qml/pages/wizard/WizardFooter.qml
        qml/pages/wizard/WizardTemplateCard.qml
        qml/pages/wizard/WizardPreviewBadge.qml
        qml/pages/wizard/WizardConfigCard.qml
        qml/pages/wizard/WizardPreviewFrame.qml
        qml/js/WizardUtils.js
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
