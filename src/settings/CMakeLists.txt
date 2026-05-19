# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Standalone Settings Application —
# compositor-agnostic settings app. Uses Kirigami for UI, no KDE System
# Settings dependency.

find_package(KF6Kirigami REQUIRED)

set(plasmazones_settings_SRCS
    main.cpp
    settingscontroller.h
    settingscontroller.cpp
    # Per-page sub-controllers — hang off SettingsController as child QObjects,
    # exposing the Q_PROPERTY surface for a single settings page each.
    animationspagecontroller.h
    animationspagecontroller.cpp
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
    snappingappearancecontroller.h
    snappingappearancecontroller.cpp
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
    QML_FILES
        qml/Main.qml
        qml/GeneralPage.qml
        qml/SnappingAppearancePage.qml
        qml/SnappingEffectsPage.qml
        qml/SnappingBehaviorPage.qml
        qml/SnappingZoneSelectorPage.qml
        qml/TilingAppearancePage.qml
        qml/TilingBehaviorPage.qml
        qml/TilingAlgorithmPage.qml
        qml/ScrollingLayoutPage.qml
        qml/ScrollingAppearancePage.qml
        qml/ScrollingBehaviorPage.qml
        qml/ScrollingAssignmentsPage.qml
        qml/ScrollingPresetListCard.qml
        qml/ScrollingBridge.qml
        qml/ExclusionsPage.qml
        qml/EditorPage.qml
        qml/LayoutsPage.qml
        qml/AssignmentsAppRulesPage.qml
        qml/SnappingAssignmentsPage.qml
        qml/SnappingQuickShortcutsPage.qml
        qml/OrderingPage.qml
        qml/SnappingOrderingPage.qml
        qml/TilingAssignmentsPage.qml
        qml/TilingQuickShortcutsPage.qml
        qml/TilingOrderingPage.qml
        qml/SharedBridge.qml
        qml/SnappingBridge.qml
        qml/TilingBridge.qml
        qml/AboutPage.qml
        qml/WhatsNewPage.qml
        qml/MonitorStatePage.qml
        qml/VirtualScreensPage.qml
        qml/VirtualScreenPreview.qml
        # Sub-components (copied from KCM)
        qml/MonitorAssignmentsCard.qml
        qml/ActivityAssignmentsCard.qml
        qml/AppRulesCard.qml
        qml/QuickLayoutSlotsCard.qml
        qml/WindowPickerDialog.qml
        qml/AssignmentRow.qml
        qml/LayoutComboBox.qml
        qml/ScreenComboBox.qml
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
        qml/MonitorSelectorSection.qml
        qml/ExclusionListCard.qml
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
        qml/AnimationProfileEditor.qml
        qml/AnimationsWindowsPage.qml
        qml/AnimationsAppRulesPage.qml
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
        ${CMAKE_SOURCE_DIR}/src/shared/ShaderPickerButton.qml
        ${CMAKE_SOURCE_DIR}/src/shared/ShaderParameterRow.qml
        ${CMAKE_SOURCE_DIR}/src/shared/ShaderParameterSection.qml
        ${CMAKE_SOURCE_DIR}/src/shared/ShaderParameterEditor.qml
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
        PhosphorAnimation::PhosphorAnimation  # motion runtime + QML module
        PhosphorAnimationQmlplugin  # Generated by qt_add_qml_module (STATIC)
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::DBus
        Qt6::Widgets
        KF6::Kirigami
)

install(TARGETS plasmazones-settings DESTINATION ${KDE_INSTALL_BINDIR})
install(FILES org.plasmazones.settings.desktop DESTINATION ${KDE_INSTALL_APPDIR})
