// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "config/configdefaults.h"
#include "core/interfaces.h"

#include <PhosphorSnapEngine/ISnapSettings.h>

namespace PlasmaZones {

/**
 * @brief Unified stub ISettings for unit tests
 *
 * Provides sensible defaults for all ISettings pure virtual methods.
 * The defaultLayoutId is mutated via setDefaultLayoutId() (the
 * ISettings setter); tests should call that directly rather than a
 * "test-only" duplicate.
 *
 * Also inherits PhosphorEngine::ISnapSettings so SnapEngine's
 * dynamic_cast<ISnapSettings*>(engineSettings()) succeeds when a stub is wired
 * via setEngineSettings(). The remaining ISnapSettings methods
 * (snappingStickyWindowHandling, moveNewWindowsToLastZone,
 * restoreWindowsToZonesOnLogin, autoAssignAllLayouts) are already
 * implemented for ISettings — the multiple inheritance just registers
 * the second base so the cast resolves.
 *
 * NOTE: This stub does NOT inherit PhosphorEngine::IAutotileSettings —
 * the AutotileEngine fetches its config via a separate code path and
 * no currently-exercised unit test routes through a
 * dynamic_cast<IAutotileSettings*> against the stub. If a future test
 * exercises autotile-engine wiring through ISettings, the stub
 * should grow that base + the 26 IAutotileSettings overrides.
 */
class StubSettings : public ISettings, public PhosphorEngine::ISnapSettings
{
    // No Q_OBJECT — this stub defines no NEW signals/slots; ISettings's
    // meta-object is reused for the inherited signal emits (e.g.
    // `renderingBackendChanged`, `settingsChanged`) that the setters trigger
    // directly.

public:
    explicit StubSettings(QObject* parent = nullptr)
        : ISettings(parent)
    {
    }

    // IDefaultLayoutSettings
    QString defaultLayoutId() const override
    {
        return m_defaultLayoutId;
    }
    void setDefaultLayoutId(const QString& id) override
    {
        if (m_defaultLayoutId == id)
            return;
        m_defaultLayoutId = id;
        Q_EMIT defaultLayoutIdChanged();
        Q_EMIT settingsChanged();
    }

    // IZoneActivationSettings
    bool snappingEnabled() const override
    {
        return true;
    }
    void setSnappingEnabled(bool) override
    {
    }
    QVariantList dragActivationTriggers() const override
    {
        return m_dragActivationTriggers;
    }
    void setDragActivationTriggers(const QVariantList& triggers) override
    {
        m_dragActivationTriggers = triggers;
    }
    bool zoneSpanEnabled() const override
    {
        return false;
    }
    void setZoneSpanEnabled(bool) override
    {
    }
    DragModifier zoneSpanModifier() const override
    {
        return DragModifier::Disabled;
    }
    void setZoneSpanModifier(DragModifier) override
    {
    }
    QVariantList zoneSpanTriggers() const override
    {
        return {};
    }
    void setZoneSpanTriggers(const QVariantList&) override
    {
    }
    bool zoneSpanToggleMode() const override
    {
        return false;
    }
    void setZoneSpanToggleMode(bool) override
    {
    }
    bool toggleActivation() const override
    {
        return false;
    }
    void setToggleActivation(bool) override
    {
    }

    // IZoneVisualizationSettings
    bool showZonesOnAllMonitors() const override
    {
        return false;
    }
    void setShowZonesOnAllMonitors(bool) override
    {
    }
    QStringList disabledMonitors(PhosphorZones::AssignmentEntry::Mode) const override
    {
        return {};
    }
    void setDisabledMonitors(PhosphorZones::AssignmentEntry::Mode, const QStringList&) override
    {
    }
    bool isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode, const QString&) const override
    {
        return false;
    }
    QStringList disabledDesktops(PhosphorZones::AssignmentEntry::Mode) const override
    {
        return {};
    }
    void setDisabledDesktops(PhosphorZones::AssignmentEntry::Mode, const QStringList&) override
    {
    }
    bool isDesktopDisabled(PhosphorZones::AssignmentEntry::Mode, const QString& /*screenIdOrName*/, int) const override
    {
        return false;
    }
    QStringList disabledActivities(PhosphorZones::AssignmentEntry::Mode) const override
    {
        return {};
    }
    void setDisabledActivities(PhosphorZones::AssignmentEntry::Mode, const QStringList&) override
    {
    }
    bool isActivityDisabled(PhosphorZones::AssignmentEntry::Mode, const QString& /*screenIdOrName*/,
                            const QString&) const override
    {
        return false;
    }
    bool showZoneNumbers() const override
    {
        return true;
    }
    void setShowZoneNumbers(bool) override
    {
    }
    bool flashZonesOnSwitch() const override
    {
        return false;
    }
    void setFlashZonesOnSwitch(bool) override
    {
    }
    bool showOsdOnLayoutSwitch() const override
    {
        return false;
    }
    void setShowOsdOnLayoutSwitch(bool) override
    {
    }
    bool showOsdOnDesktopSwitch() const override
    {
        return false;
    }
    void setShowOsdOnDesktopSwitch(bool) override
    {
    }
    bool showNavigationOsd() const override
    {
        return false;
    }
    void setShowNavigationOsd(bool) override
    {
    }
    OsdStyle osdStyle() const override
    {
        return OsdStyle::None;
    }
    void setOsdStyle(OsdStyle) override
    {
    }
    OverlayDisplayMode overlayDisplayMode() const override
    {
        return OverlayDisplayMode::ZoneRectangles;
    }
    void setOverlayDisplayMode(OverlayDisplayMode) override
    {
    }
    bool useSystemColors() const override
    {
        return false;
    }
    void setUseSystemColors(bool) override
    {
    }
    QColor highlightColor() const override
    {
        return Qt::blue;
    }
    void setHighlightColor(const QColor&) override
    {
    }
    QColor inactiveColor() const override
    {
        return Qt::gray;
    }
    void setInactiveColor(const QColor&) override
    {
    }
    QColor borderColor() const override
    {
        return Qt::white;
    }
    void setBorderColor(const QColor&) override
    {
    }
    QColor labelFontColor() const override
    {
        return Qt::white;
    }
    void setLabelFontColor(const QColor&) override
    {
    }
    qreal activeOpacity() const override
    {
        return 0.5;
    }
    void setActiveOpacity(qreal) override
    {
    }
    qreal inactiveOpacity() const override
    {
        return 0.3;
    }
    void setInactiveOpacity(qreal) override
    {
    }
    int borderWidth() const override
    {
        return 2;
    }
    void setBorderWidth(int) override
    {
    }
    int borderRadius() const override
    {
        return 8;
    }
    void setBorderRadius(int) override
    {
    }
    bool enableBlur() const override
    {
        return false;
    }
    void setEnableBlur(bool) override
    {
    }
    QString labelFontFamily() const override
    {
        return {};
    }
    void setLabelFontFamily(const QString&) override
    {
    }
    qreal labelFontSizeScale() const override
    {
        return 1.0;
    }
    void setLabelFontSizeScale(qreal) override
    {
    }
    int labelFontWeight() const override
    {
        return 400;
    }
    void setLabelFontWeight(int) override
    {
    }
    bool labelFontItalic() const override
    {
        return false;
    }
    void setLabelFontItalic(bool) override
    {
    }
    bool labelFontUnderline() const override
    {
        return false;
    }
    void setLabelFontUnderline(bool) override
    {
    }
    bool labelFontStrikeout() const override
    {
        return false;
    }
    void setLabelFontStrikeout(bool) override
    {
    }
    bool enableShaderEffects() const override
    {
        return false;
    }
    void setEnableShaderEffects(bool) override
    {
    }
    int shaderFrameRate() const override
    {
        return 60;
    }
    void setShaderFrameRate(int) override
    {
    }
    bool enableAudioVisualizer() const override
    {
        return false;
    }
    void setEnableAudioVisualizer(bool) override
    {
    }
    int audioSpectrumBarCount() const override
    {
        return 32;
    }
    void setAudioSpectrumBarCount(int) override
    {
    }

    // IZoneGeometrySettings
    int zonePadding() const override
    {
        return 8;
    }
    void setZonePadding(int) override
    {
    }
    int outerGap() const override
    {
        return 8;
    }
    void setOuterGap(int) override
    {
    }
    bool usePerSideOuterGap() const override
    {
        return false;
    }
    void setUsePerSideOuterGap(bool) override
    {
    }
    int outerGapTop() const override
    {
        return 8;
    }
    void setOuterGapTop(int) override
    {
    }
    int outerGapBottom() const override
    {
        return 8;
    }
    void setOuterGapBottom(int) override
    {
    }
    int outerGapLeft() const override
    {
        return 8;
    }
    void setOuterGapLeft(int) override
    {
    }
    int outerGapRight() const override
    {
        return 8;
    }
    void setOuterGapRight(int) override
    {
    }
    int adjacentThreshold() const override
    {
        return 20;
    }
    void setAdjacentThreshold(int) override
    {
    }
    int pollIntervalMs() const override
    {
        return 50;
    }
    void setPollIntervalMs(int) override
    {
    }
    int minimumZoneSizePx() const override
    {
        return 100;
    }
    void setMinimumZoneSizePx(int) override
    {
    }
    int minimumZoneDisplaySizePx() const override
    {
        return 10;
    }
    void setMinimumZoneDisplaySizePx(int) override
    {
    }

    // IWindowExclusionSettings — the per-app / per-class exclusion list
    // accessors retired in v4 (folded into unified WindowRule store).
    bool excludeTransientWindows() const override
    {
        return false;
    }
    void setExcludeTransientWindows(bool) override
    {
    }
    int minimumWindowWidth() const override
    {
        return 0;
    }
    void setMinimumWindowWidth(int) override
    {
    }
    int minimumWindowHeight() const override
    {
        return 0;
    }
    void setMinimumWindowHeight(int) override
    {
    }

    // Animation window filtering — pure-stub no-op accessors backed by
    // m_animation* state so tests that exercise the filter cascade can
    // round-trip values through the stub without involving the real
    // PhosphorConfig::Store. Uses the same value-changed-guarded emit
    // pattern the concrete Settings layer uses.
    bool animationExcludeTransientWindows() const override
    {
        return m_animationExcludeTransientWindows;
    }
    void setAnimationExcludeTransientWindows(bool exclude) override
    {
        if (m_animationExcludeTransientWindows == exclude) {
            return;
        }
        m_animationExcludeTransientWindows = exclude;
        Q_EMIT animationExcludeTransientWindowsChanged();
        Q_EMIT settingsChanged();
    }
    bool animationExcludeNotificationsAndOsd() const override
    {
        return m_animationExcludeNotificationsAndOsd;
    }
    void setAnimationExcludeNotificationsAndOsd(bool exclude) override
    {
        if (m_animationExcludeNotificationsAndOsd == exclude) {
            return;
        }
        m_animationExcludeNotificationsAndOsd = exclude;
        Q_EMIT animationExcludeNotificationsAndOsdChanged();
        Q_EMIT settingsChanged();
    }
    int animationMinimumWindowWidth() const override
    {
        return m_animationMinimumWindowWidth;
    }
    void setAnimationMinimumWindowWidth(int width) override
    {
        if (m_animationMinimumWindowWidth == width) {
            return;
        }
        m_animationMinimumWindowWidth = width;
        Q_EMIT animationMinimumWindowWidthChanged();
        Q_EMIT settingsChanged();
    }
    int animationMinimumWindowHeight() const override
    {
        return m_animationMinimumWindowHeight;
    }
    void setAnimationMinimumWindowHeight(int height) override
    {
        if (m_animationMinimumWindowHeight == height) {
            return;
        }
        m_animationMinimumWindowHeight = height;
        Q_EMIT animationMinimumWindowHeightChanged();
        Q_EMIT settingsChanged();
    }
    // animationExcludedApplications / animationExcludedWindowClasses
    // overrides retired in v4 alongside the ISettings virtuals — the
    // lists folded into ExcludeAnimations WindowRules.

    // IZoneSelectorSettings
    bool zoneSelectorEnabled() const override
    {
        return true;
    }
    void setZoneSelectorEnabled(bool) override
    {
    }
    int zoneSelectorTriggerDistance() const override
    {
        return 50;
    }
    void setZoneSelectorTriggerDistance(int) override
    {
    }
    ZoneSelectorPosition zoneSelectorPosition() const override
    {
        return ZoneSelectorPosition::Top;
    }
    void setZoneSelectorPosition(ZoneSelectorPosition) override
    {
    }
    ZoneSelectorLayoutMode zoneSelectorLayoutMode() const override
    {
        return ZoneSelectorLayoutMode::Grid;
    }
    void setZoneSelectorLayoutMode(ZoneSelectorLayoutMode) override
    {
    }
    int zoneSelectorPreviewWidth() const override
    {
        return 180;
    }
    void setZoneSelectorPreviewWidth(int) override
    {
    }
    int zoneSelectorPreviewHeight() const override
    {
        return 101;
    }
    void setZoneSelectorPreviewHeight(int) override
    {
    }
    bool zoneSelectorPreviewLockAspect() const override
    {
        return true;
    }
    void setZoneSelectorPreviewLockAspect(bool) override
    {
    }
    int zoneSelectorGridColumns() const override
    {
        return 5;
    }
    void setZoneSelectorGridColumns(int) override
    {
    }
    ZoneSelectorSizeMode zoneSelectorSizeMode() const override
    {
        return ZoneSelectorSizeMode::Auto;
    }
    void setZoneSelectorSizeMode(ZoneSelectorSizeMode) override
    {
    }
    int zoneSelectorMaxRows() const override
    {
        return 4;
    }
    void setZoneSelectorMaxRows(int) override
    {
    }

    // IWindowBehaviorSettings
    bool keepWindowsInZonesOnResolutionChange() const override
    {
        return true;
    }
    void setKeepWindowsInZonesOnResolutionChange(bool) override
    {
    }
    bool moveNewWindowsToLastZone() const override
    {
        return false;
    }
    void setMoveNewWindowsToLastZone(bool) override
    {
    }
    // ISnapSettings::focusNewWindows() — shares the ISettings snappingFocusNewWindows
    // member so a test can drive the snap engine's focus-new path.
    bool focusNewWindows() const override
    {
        return m_snappingFocusNewWindows;
    }
    bool restoreOriginalSizeOnUnsnap() const override
    {
        return true;
    }
    void setRestoreOriginalSizeOnUnsnap(bool) override
    {
    }
    StickyWindowHandling snappingStickyWindowHandling() const override
    {
        return StickyWindowHandling::TreatAsNormal;
    }
    void setSnappingStickyWindowHandling(StickyWindowHandling) override
    {
    }
    bool restoreWindowsToZonesOnLogin() const override
    {
        return true;
    }
    void setRestoreWindowsToZonesOnLogin(bool) override
    {
    }
    bool snappingRestoreFloatedWindowsOnLogin() const override
    {
        return m_snappingRestoreFloatedWindowsOnLogin;
    }
    void setSnappingRestoreFloatedWindowsOnLogin(bool value) override
    {
        if (m_snappingRestoreFloatedWindowsOnLogin == value)
            return;
        m_snappingRestoreFloatedWindowsOnLogin = value;
        Q_EMIT snappingRestoreFloatedWindowsOnLoginChanged();
        Q_EMIT settingsChanged();
    }
    bool autotileRestoreFloatedWindowsOnLogin() const override
    {
        return m_autotileRestoreFloatedWindowsOnLogin;
    }
    void setAutotileRestoreFloatedWindowsOnLogin(bool value) override
    {
        if (m_autotileRestoreFloatedWindowsOnLogin == value)
            return;
        m_autotileRestoreFloatedWindowsOnLogin = value;
        Q_EMIT autotileRestoreFloatedWindowsOnLoginChanged();
        Q_EMIT settingsChanged();
    }
    // ISettings getter/setter; the ISnapSettings bridge (unfloatFallbackToZone)
    // is defined alongside the other ISnapSettings overrides below.
    bool snapUnfloatFallbackToZone() const override
    {
        return m_snapUnfloatFallbackToZone;
    }
    void setSnapUnfloatFallbackToZone(bool value) override
    {
        if (m_snapUnfloatFallbackToZone == value)
            return;
        m_snapUnfloatFallbackToZone = value;
        Q_EMIT snapUnfloatFallbackToZoneChanged();
        Q_EMIT settingsChanged();
    }
    bool unfloatFallbackToZone() const override
    {
        return m_snapUnfloatFallbackToZone;
    }
    bool autoAssignAllLayouts() const override
    {
        return m_autoAssignAllLayouts;
    }
    void setAutoAssignAllLayouts(bool enabled) override
    {
        if (m_autoAssignAllLayouts == enabled)
            return;
        m_autoAssignAllLayouts = enabled;
        Q_EMIT autoAssignAllLayoutsChanged();
        Q_EMIT settingsChanged();
    }
    bool snapAssistFeatureEnabled() const override
    {
        return m_snapAssistFeatureEnabled;
    }
    void setSnapAssistFeatureEnabled(bool enabled) override
    {
        if (m_snapAssistFeatureEnabled == enabled)
            return;
        m_snapAssistFeatureEnabled = enabled;
        Q_EMIT snapAssistFeatureEnabledChanged();
        Q_EMIT settingsChanged();
    }
    bool snapAssistEnabled() const override
    {
        return m_snapAssistEnabled;
    }
    void setSnapAssistEnabled(bool enabled) override
    {
        if (m_snapAssistEnabled == enabled)
            return;
        m_snapAssistEnabled = enabled;
        Q_EMIT snapAssistEnabledChanged();
        Q_EMIT settingsChanged();
    }
    QVariantList snapAssistTriggers() const override
    {
        return {};
    }
    void setSnapAssistTriggers(const QVariantList&) override
    {
    }
    bool filterLayoutsByAspectRatio() const override
    {
        return true;
    }
    void setFilterLayoutsByAspectRatio(bool) override
    {
    }

    // IOrderingSettings
    QStringList snappingLayoutOrder() const override
    {
        return m_snappingLayoutOrder;
    }
    void setSnappingLayoutOrder(const QStringList& order) override
    {
        if (m_snappingLayoutOrder == order)
            return;
        m_snappingLayoutOrder = order;
        Q_EMIT snappingLayoutOrderChanged();
        Q_EMIT settingsChanged();
    }
    QStringList tilingAlgorithmOrder() const override
    {
        return m_tilingAlgorithmOrder;
    }
    void setTilingAlgorithmOrder(const QStringList& order) override
    {
        if (m_tilingAlgorithmOrder == order)
            return;
        m_tilingAlgorithmOrder = order;
        Q_EMIT tilingAlgorithmOrderChanged();
        Q_EMIT settingsChanged();
    }

    // Animation settings (ISettings)
    bool animationsEnabled() const override
    {
        return false;
    }
    void setAnimationsEnabled(bool) override
    {
    }
    int animationDuration() const override
    {
        return 200;
    }
    void setAnimationDuration(int) override
    {
    }
    QString animationEasingCurve() const override
    {
        return {};
    }
    void setAnimationEasingCurve(const QString&) override
    {
    }
    int animationMinDistance() const override
    {
        return 10;
    }
    void setAnimationMinDistance(int) override
    {
    }
    int animationSequenceMode() const override
    {
        return 0;
    }
    void setAnimationSequenceMode(int) override
    {
    }
    int animationStaggerInterval() const override
    {
        return 50;
    }
    void setAnimationStaggerInterval(int) override
    {
    }
    PhosphorAnimationShaders::ShaderProfileTree shaderProfileTree() const override
    {
        return {};
    }
    void setShaderProfileTree(const PhosphorAnimationShaders::ShaderProfileTree&) override
    {
    }

    // Autotile decoration settings (ISettings)
    bool autotileFocusFollowsMouse() const override
    {
        return false;
    }
    void setAutotileFocusFollowsMouse(bool) override
    {
    }
    bool snappingFocusNewWindows() const override
    {
        return m_snappingFocusNewWindows;
    }
    void setSnappingFocusNewWindows(bool v) override
    {
        m_snappingFocusNewWindows = v;
    }
    bool snappingFocusFollowsMouse() const override
    {
        return m_snappingFocusFollowsMouse;
    }
    void setSnappingFocusFollowsMouse(bool v) override
    {
        m_snappingFocusFollowsMouse = v;
    }
    bool autotileHideTitleBars() const override
    {
        return false;
    }
    void setAutotileHideTitleBars(bool) override
    {
    }
    bool autotileShowBorder() const override
    {
        return false;
    }
    void setAutotileShowBorder(bool) override
    {
    }
    int autotileBorderWidth() const override
    {
        return 2;
    }
    void setAutotileBorderWidth(int) override
    {
    }
    int autotileBorderRadius() const override
    {
        return 0;
    }
    void setAutotileBorderRadius(int) override
    {
    }
    QColor autotileBorderColor() const override
    {
        return Qt::white;
    }
    void setAutotileBorderColor(const QColor&) override
    {
    }
    QColor autotileInactiveBorderColor() const override
    {
        return {};
    }
    void setAutotileInactiveBorderColor(const QColor&) override
    {
    }
    bool autotileUseSystemBorderColors() const override
    {
        return false;
    }
    void setAutotileUseSystemBorderColors(bool) override
    {
    }
    bool snappingHideTitleBars() const override
    {
        // Distinct from snappingShowBorder so the D-Bus batch test can detect a
        // registration swap between the two adjacent bool keys via value-mirroring.
        return true;
    }
    void setSnappingHideTitleBars(bool) override
    {
    }
    bool snappingShowBorder() const override
    {
        return false;
    }
    void setSnappingShowBorder(bool) override
    {
    }
    int snappingBorderWidth() const override
    {
        return 2;
    }
    void setSnappingBorderWidth(int) override
    {
    }
    int snappingBorderRadius() const override
    {
        return 0;
    }
    void setSnappingBorderRadius(int) override
    {
    }
    QColor snappingBorderColor() const override
    {
        return Qt::white;
    }
    void setSnappingBorderColor(const QColor&) override
    {
    }
    QColor snappingInactiveBorderColor() const override
    {
        // A distinct, valid color (active is white) so the D-Bus batch test can
        // round-trip it through HexArgb and catch an active/inactive swap.
        return Qt::black;
    }
    void setSnappingInactiveBorderColor(const QColor&) override
    {
    }
    bool snappingUseSystemBorderColors() const override
    {
        // Distinct from snappingShowBorder for the same batch-test swap detection.
        return true;
    }
    void setSnappingUseSystemBorderColors(bool) override
    {
    }
    StickyWindowHandling autotileStickyWindowHandling() const override
    {
        return StickyWindowHandling::TreatAsNormal;
    }
    void setAutotileStickyWindowHandling(StickyWindowHandling) override
    {
    }
    AutotileDragBehavior autotileDragBehavior() const override
    {
        return AutotileDragBehavior::Float;
    }
    void setAutotileDragBehavior(AutotileDragBehavior) override
    {
    }
    AutotileOverflowBehavior autotileOverflowBehavior() const override
    {
        return AutotileOverflowBehavior::Float;
    }
    void setAutotileOverflowBehavior(AutotileOverflowBehavior) override
    {
    }
    QVariantList autotileDragInsertTriggers() const override
    {
        return ConfigDefaults::autotileDragInsertTriggers();
    }
    void setAutotileDragInsertTriggers(const QVariantList&) override
    {
    }
    bool autotileDragInsertToggle() const override
    {
        return false;
    }
    void setAutotileDragInsertToggle(bool) override
    {
    }
    QVariantMap autotilePerAlgorithmSettings() const override
    {
        return m_autotilePerAlgorithmSettings;
    }
    void setAutotilePerAlgorithmSettings(const QVariantMap& settings) override
    {
        if (m_autotilePerAlgorithmSettings == settings)
            return;
        m_autotilePerAlgorithmSettings = settings;
        Q_EMIT autotilePerAlgorithmSettingsChanged();
        Q_EMIT settingsChanged();
    }
    QString loadColorsFromFile(const QString&) override
    {
        // Stub returns "not supported" so a test that exercised this
        // path could distinguish a real failure from a missing impl,
        // but the controllers under test don't reach this in their
        // unit-test paths today.
        return QStringLiteral("loadColorsFromFile: stub not supported");
    }

    // Editor settings — round-trip the stub members so a test can
    // exercise the EditorPageController setter/getter contract.
    //
    // Every setter below pairs the field-specific signal with the umbrella
    // `settingsChanged()` emit. The concrete Settings class does the same
    // via the P_STORE_SET_{BOOL,STRING,INT,DOUBLE} macros (settings.cpp
    // ~2759-2784) — the stub matches so tests can rely on `settingsChanged()`
    // firing on any setter, regardless of which ISettings backend is wired.
    QString editorDuplicateShortcut() const override
    {
        return m_editorDuplicateShortcut;
    }
    void setEditorDuplicateShortcut(const QString& s) override
    {
        if (m_editorDuplicateShortcut == s)
            return;
        m_editorDuplicateShortcut = s;
        Q_EMIT editorDuplicateShortcutChanged();
        Q_EMIT settingsChanged();
    }
    QString editorSplitHorizontalShortcut() const override
    {
        return m_editorSplitHorizontalShortcut;
    }
    void setEditorSplitHorizontalShortcut(const QString& s) override
    {
        if (m_editorSplitHorizontalShortcut == s)
            return;
        m_editorSplitHorizontalShortcut = s;
        Q_EMIT editorSplitHorizontalShortcutChanged();
        Q_EMIT settingsChanged();
    }
    QString editorSplitVerticalShortcut() const override
    {
        return m_editorSplitVerticalShortcut;
    }
    void setEditorSplitVerticalShortcut(const QString& s) override
    {
        if (m_editorSplitVerticalShortcut == s)
            return;
        m_editorSplitVerticalShortcut = s;
        Q_EMIT editorSplitVerticalShortcutChanged();
        Q_EMIT settingsChanged();
    }
    QString editorFillShortcut() const override
    {
        return m_editorFillShortcut;
    }
    void setEditorFillShortcut(const QString& s) override
    {
        if (m_editorFillShortcut == s)
            return;
        m_editorFillShortcut = s;
        Q_EMIT editorFillShortcutChanged();
        Q_EMIT settingsChanged();
    }
    bool editorGridSnappingEnabled() const override
    {
        return m_editorGridSnappingEnabled;
    }
    void setEditorGridSnappingEnabled(bool e) override
    {
        if (m_editorGridSnappingEnabled == e)
            return;
        m_editorGridSnappingEnabled = e;
        Q_EMIT editorGridSnappingEnabledChanged();
        Q_EMIT settingsChanged();
    }
    bool editorEdgeSnappingEnabled() const override
    {
        return m_editorEdgeSnappingEnabled;
    }
    void setEditorEdgeSnappingEnabled(bool e) override
    {
        if (m_editorEdgeSnappingEnabled == e)
            return;
        m_editorEdgeSnappingEnabled = e;
        Q_EMIT editorEdgeSnappingEnabledChanged();
        Q_EMIT settingsChanged();
    }
    qreal editorSnapIntervalX() const override
    {
        return m_editorSnapIntervalX;
    }
    void setEditorSnapIntervalX(qreal v) override
    {
        if (qFuzzyCompare(m_editorSnapIntervalX, v))
            return;
        m_editorSnapIntervalX = v;
        Q_EMIT editorSnapIntervalXChanged();
        Q_EMIT settingsChanged();
    }
    qreal editorSnapIntervalY() const override
    {
        return m_editorSnapIntervalY;
    }
    void setEditorSnapIntervalY(qreal v) override
    {
        if (qFuzzyCompare(m_editorSnapIntervalY, v))
            return;
        m_editorSnapIntervalY = v;
        Q_EMIT editorSnapIntervalYChanged();
        Q_EMIT settingsChanged();
    }
    int editorSnapOverrideModifier() const override
    {
        return m_editorSnapOverrideModifier;
    }
    void setEditorSnapOverrideModifier(int m) override
    {
        if (m_editorSnapOverrideModifier == m)
            return;
        m_editorSnapOverrideModifier = m;
        Q_EMIT editorSnapOverrideModifierChanged();
        Q_EMIT settingsChanged();
    }
    bool fillOnDropEnabled() const override
    {
        return m_fillOnDropEnabled;
    }
    void setFillOnDropEnabled(bool e) override
    {
        if (m_fillOnDropEnabled == e)
            return;
        m_fillOnDropEnabled = e;
        Q_EMIT fillOnDropEnabledChanged();
        Q_EMIT settingsChanged();
    }
    int fillOnDropModifier() const override
    {
        return m_fillOnDropModifier;
    }
    void setFillOnDropModifier(int m) override
    {
        if (m_fillOnDropModifier == m)
            return;
        m_fillOnDropModifier = m;
        Q_EMIT fillOnDropModifierChanged();
        Q_EMIT settingsChanged();
    }
    QStringList lockedScreens() const override
    {
        return {};
    }
    void setLockedScreens(const QStringList&) override
    {
    }
    bool isScreenLocked(const QString&) const override
    {
        return false;
    }
    void setScreenLocked(const QString&, bool) override
    {
    }
    bool isContextLocked(const QString&, int, const QString&) const override
    {
        return false;
    }
    void setContextLocked(const QString&, int, const QString&, bool) override
    {
    }

    // Rendering (ISettings)
    QString renderingBackend() const override
    {
        return m_renderingBackend;
    }
    void setRenderingBackend(const QString& backend) override
    {
        const QString value = ConfigDefaults::normalizeRenderingBackend(backend);
        if (m_renderingBackend != value) {
            m_renderingBackend = value;
            Q_EMIT renderingBackendChanged();
            Q_EMIT settingsChanged();
        }
    }

    // Persistence (ISettings)
    void load() override
    {
    }
    void save() override
    {
    }
    void reset() override
    {
    }

private:
    QString m_defaultLayoutId;
    QString m_renderingBackend = ConfigDefaults::renderingBackend();
    bool m_snapAssistFeatureEnabled = false;
    bool m_snapAssistEnabled = false;
    bool m_autoAssignAllLayouts = false;
    bool m_snappingRestoreFloatedWindowsOnLogin = true;
    bool m_autotileRestoreFloatedWindowsOnLogin = true;
    bool m_snapUnfloatFallbackToZone = false;
    bool m_snappingFocusNewWindows = false;
    bool m_snappingFocusFollowsMouse = false;
    QStringList m_snappingLayoutOrder;
    QStringList m_tilingAlgorithmOrder;
    QVariantList m_dragActivationTriggers;
    // Animation-filter defaults routed through ConfigDefaults so a future
    // tweak to the production defaults flows into tests automatically — keeps
    // the stub from drifting into "tests pass against a stale baseline".
    bool m_animationExcludeTransientWindows = ConfigDefaults::animationExcludeTransientWindows();
    bool m_animationExcludeNotificationsAndOsd = ConfigDefaults::animationExcludeNotificationsAndOsd();
    int m_animationMinimumWindowWidth = ConfigDefaults::animationMinimumWindowWidth();
    int m_animationMinimumWindowHeight = ConfigDefaults::animationMinimumWindowHeight();
    QVariantMap m_autotilePerAlgorithmSettings;
    QString m_editorDuplicateShortcut = ConfigDefaults::editorDuplicateShortcut();
    QString m_editorSplitHorizontalShortcut = ConfigDefaults::editorSplitHorizontalShortcut();
    QString m_editorSplitVerticalShortcut = ConfigDefaults::editorSplitVerticalShortcut();
    QString m_editorFillShortcut = ConfigDefaults::editorFillShortcut();
    bool m_editorGridSnappingEnabled = ConfigDefaults::editorGridSnappingEnabled();
    bool m_editorEdgeSnappingEnabled = ConfigDefaults::editorEdgeSnappingEnabled();
    qreal m_editorSnapIntervalX = ConfigDefaults::editorSnapIntervalX();
    qreal m_editorSnapIntervalY = ConfigDefaults::editorSnapIntervalY();
    int m_editorSnapOverrideModifier = ConfigDefaults::editorSnapOverrideModifier();
    bool m_fillOnDropEnabled = ConfigDefaults::fillOnDropEnabled();
    int m_fillOnDropModifier = ConfigDefaults::fillOnDropModifier();
};

} // namespace PlasmaZones
