// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtCore/qnamespace.h>

#include "../core/constants.h"
#include "../core/enums.h"
#include "configkeys.h"
#include "plasmazones_export.h"

namespace PlasmaZones {

/**
 * @brief Provides static access to default configuration values
 *
 * Canonical default values for all PlasmaZones configuration keys.
 * Used by Settings::load() when no persisted value exists.
 *
 * Usage:
 *   int cols = ConfigDefaults::gridColumns();  // Returns 5
 *   int rows = ConfigDefaults::maxRows();      // Returns 4
 */
class ConfigDefaults : public ConfigKeys
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Activation Settings
    // ═══════════════════════════════════════════════════════════════════════════

    // Build a single-entry trigger list with the given modifier and mouse
    // button. Shared by the default accessors below so the canonical
    // {modifier, mouseButton} shape lives in one place.
    static QVariantList makeSingleTriggerList(int modifier, int mouseButton = 0)
    {
        QVariantMap trigger;
        trigger[ConfigKeys::triggerModifierField()] = modifier;
        trigger[ConfigKeys::triggerMouseButtonField()] = mouseButton;
        return {trigger};
    }

    static QVariantList dragActivationTriggers()
    {
        return makeSingleTriggerList(static_cast<int>(DragModifier::Alt));
    }
    static bool toggleActivation()
    {
        return false;
    }
    static bool snappingEnabled()
    {
        return true;
    }
    static bool zoneSpanEnabled()
    {
        return true;
    }
    static int zoneSpanModifier()
    {
        return 2;
    }
    static QVariantList zoneSpanTriggers()
    {
        return makeSingleTriggerList(zoneSpanModifier());
    }
    static QVariantList autotileDragInsertTriggers()
    {
        // Held while dragging a window to dynamically insert it into the
        // autotile stack at the cursor position.
        return makeSingleTriggerList(static_cast<int>(DragModifier::Alt));
    }
    static bool autotileDragInsertToggle()
    {
        return false;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Display Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool showOnAllMonitors()
    {
        return false;
    }
    static QStringList disabledDesktops()
    {
        return {};
    }
    static QStringList disabledActivities()
    {
        return {};
    }
    static bool showNumbers()
    {
        return true;
    }
    static bool flashOnSwitch()
    {
        return true;
    }
    static bool showOsdOnLayoutSwitch()
    {
        return true;
    }
    static bool showNavigationOsd()
    {
        return true;
    }
    static constexpr int osdStyleMin()
    {
        return 0;
    }
    static constexpr int osdStyleMax()
    {
        return 2;
    }
    static int osdStyle()
    {
        return 2;
    }
    static constexpr int overlayDisplayModeMin()
    {
        return 0;
    }
    static constexpr int overlayDisplayModeMax()
    {
        return 1;
    }
    static int overlayDisplayMode()
    {
        return 0;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Appearance Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool useSystemColors()
    {
        return true;
    }
    static QColor highlightColor()
    {
        return Defaults::HighlightColor;
    }
    static QColor inactiveColor()
    {
        return Defaults::InactiveColor;
    }
    static QColor borderColor()
    {
        return Defaults::BorderColor;
    }
    static QColor labelFontColor()
    {
        return Defaults::LabelFontColor;
    }
    static double activeOpacity()
    {
        return Defaults::Opacity;
    }
    static constexpr qreal activeOpacityMin()
    {
        return 0.0;
    }
    static constexpr qreal activeOpacityMax()
    {
        return 1.0;
    }
    static double inactiveOpacity()
    {
        return Defaults::InactiveOpacity;
    }
    static constexpr qreal inactiveOpacityMin()
    {
        return 0.0;
    }
    static constexpr qreal inactiveOpacityMax()
    {
        return 1.0;
    }
    static int borderWidth()
    {
        return Defaults::BorderWidth;
    }
    static constexpr int borderWidthMin()
    {
        return 0;
    }
    static constexpr int borderWidthMax()
    {
        return 10;
    }
    static int borderRadius()
    {
        return Defaults::BorderRadius;
    }
    static constexpr int borderRadiusMin()
    {
        return 0;
    }
    static constexpr int borderRadiusMax()
    {
        return 50;
    }
    static bool enableBlur()
    {
        return true;
    }
    static QString labelFontFamily()
    {
        return QString();
    }
    static double labelFontSizeScale()
    {
        return 1.0;
    }
    static constexpr qreal labelFontSizeScaleMin()
    {
        return 0.25;
    }
    static constexpr qreal labelFontSizeScaleMax()
    {
        return 3.0;
    }
    static int labelFontWeight()
    {
        return 700;
    }
    static constexpr int labelFontWeightMin()
    {
        return 100;
    }
    static constexpr int labelFontWeightMax()
    {
        return 900;
    }
    static bool labelFontItalic()
    {
        return false;
    }
    static bool labelFontUnderline()
    {
        return false;
    }
    static bool labelFontStrikeout()
    {
        return false;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static int zonePadding()
    {
        return Defaults::ZonePadding;
    }
    static constexpr int zonePaddingMin()
    {
        return 0;
    }
    static constexpr int zonePaddingMax()
    {
        return Defaults::MaxGap;
    }
    static int outerGap()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapMin()
    {
        return 0;
    }
    static constexpr int outerGapMax()
    {
        return Defaults::MaxGap;
    }
    static bool usePerSideOuterGap()
    {
        return false;
    }
    static int outerGapTop()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapTopMin()
    {
        return 0;
    }
    static constexpr int outerGapTopMax()
    {
        return Defaults::MaxGap;
    }
    static int outerGapBottom()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapBottomMin()
    {
        return 0;
    }
    static constexpr int outerGapBottomMax()
    {
        return Defaults::MaxGap;
    }
    static int outerGapLeft()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapLeftMin()
    {
        return 0;
    }
    static constexpr int outerGapLeftMax()
    {
        return Defaults::MaxGap;
    }
    static int outerGapRight()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapRightMin()
    {
        return 0;
    }
    static constexpr int outerGapRightMax()
    {
        return Defaults::MaxGap;
    }
    static int adjacentThreshold()
    {
        return Defaults::AdjacentThreshold;
    }
    static constexpr int adjacentThresholdMin()
    {
        return 5;
    }
    static constexpr int adjacentThresholdMax()
    {
        return 500;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Performance Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static int pollIntervalMs()
    {
        return Defaults::PollIntervalMs;
    }
    static constexpr int pollIntervalMsMin()
    {
        return 10;
    }
    static constexpr int pollIntervalMsMax()
    {
        return 1000;
    }
    static int minimumZoneSizePx()
    {
        return Defaults::MinimumZoneSizePx;
    }
    static constexpr int minimumZoneSizePxMin()
    {
        return 50;
    }
    static constexpr int minimumZoneSizePxMax()
    {
        return 500;
    }
    static int minimumZoneDisplaySizePx()
    {
        return Defaults::MinimumZoneDisplaySizePx;
    }
    static constexpr int minimumZoneDisplaySizePxMin()
    {
        return 1;
    }
    static constexpr int minimumZoneDisplaySizePxMax()
    {
        return 50;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Window Behavior Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool keepWindowsInZonesOnResolutionChange()
    {
        return true;
    }
    static bool moveNewWindowsToLastZone()
    {
        return false;
    }
    static bool restoreOriginalSizeOnUnsnap()
    {
        return true;
    }
    static int stickyWindowHandling()
    {
        return 0;
    }
    static bool restoreWindowsToZonesOnLogin()
    {
        return true;
    }
    static bool filterLayoutsByAspectRatio()
    {
        return true;
    }
    static bool snapAssistFeatureEnabled()
    {
        return true;
    }
    static bool snapAssistEnabled()
    {
        return true;
    }
    static QVariantList snapAssistTriggers()
    {
        // Default: Middle mouse
        return makeSingleTriggerList(static_cast<int>(DragModifier::Disabled), static_cast<int>(Qt::MiddleButton));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Exclusion Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool excludeTransientWindows()
    {
        return true;
    }
    static int minimumWindowWidth()
    {
        return 200;
    }
    static constexpr int minimumWindowWidthMin()
    {
        return 0;
    }
    static constexpr int minimumWindowWidthMax()
    {
        return 2000;
    }
    static int minimumWindowHeight()
    {
        return 150;
    }
    static constexpr int minimumWindowHeightMin()
    {
        return 0;
    }
    static constexpr int minimumWindowHeightMax()
    {
        return 2000;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Selector Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool zoneSelectorEnabled()
    {
        return true;
    }
    static int triggerDistance()
    {
        return 50;
    }
    static constexpr int triggerDistanceMin()
    {
        return 10;
    }
    static constexpr int triggerDistanceMax()
    {
        return 200;
    }
    static int position()
    {
        return 1;
    }
    static int layoutMode()
    {
        return 0;
    }
    static int sizeMode()
    {
        return 0;
    }
    static int maxRows()
    {
        return 4;
    }
    static constexpr int maxRowsMin()
    {
        return 1;
    }
    static constexpr int maxRowsMax()
    {
        return 10;
    }
    static int previewWidth()
    {
        return 180;
    }
    static constexpr int previewWidthMin()
    {
        return 80;
    }
    static constexpr int previewWidthMax()
    {
        return 400;
    }
    static int previewHeight()
    {
        return 101;
    }
    static constexpr int previewHeightMin()
    {
        return 60;
    }
    static constexpr int previewHeightMax()
    {
        return 300;
    }
    static bool previewLockAspect()
    {
        return true;
    }
    static int gridColumns()
    {
        return 5;
    }
    static constexpr int gridColumnsMin()
    {
        return 1;
    }
    static constexpr int gridColumnsMax()
    {
        return 10;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Path
    // ═══════════════════════════════════════════════════════════════════════════

    // Returns the absolute path to config.json.
    // Not cached — QStandardPaths respects $XDG_CONFIG_HOME changes at runtime,
    // which tests rely on via IsolatedConfigGuard.
    PLASMAZONES_EXPORT static QString configFilePath();

    // Returns the absolute path to session.json (ephemeral window tracking state).
    // Separate from config.json to avoid write contention between user preferences
    // and high-frequency session state saves.
    PLASMAZONES_EXPORT static QString sessionFilePath();

    // Returns the absolute path to assignments.json (layout assignments and
    // quick layout shortcuts).  Separate from config.json so that Settings
    // and LayoutManager have independent ownership of their files.
    PLASMAZONES_EXPORT static QString assignmentsFilePath();

    // Returns the absolute path to the legacy plasmazonesrc file (INI format).
    // Used only by the one-time migration module.
    PLASMAZONES_EXPORT static QString legacyConfigFilePath();

    /**
     * Read the rendering backend from the config file on disk.
     *
     * QSettings::IniFormat maps ungrouped keys (before any [Section] header) into
     * the "General" group automatically. The settings app writes RenderingBackend
     * at the root level, so the default read resolves it via General/ implicitly.
     * This helper provides a single canonical read used by daemon, editor, and Settings.
     *
     * Safe to call before QCoreApplication exists (uses raw QSettings).
     * Returns the normalized backend string ("auto", "vulkan", or "opengl").
     */
    PLASMAZONES_EXPORT static QString readRenderingBackendFromDisk();

    // ═══════════════════════════════════════════════════════════════════════════
    // Rendering Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static QString renderingBackend()
    {
        return QStringLiteral("auto");
    }

    struct RenderingBackendEntry
    {
        QString key;
        QString displayName;
    };

    // Single source of truth for backend keys and display names.
    // Order here determines ComboBox order in the settings UI.
    // When adding entries, also add the display name to the translation catalog.
    static const QList<RenderingBackendEntry>& renderingBackendEntries()
    {
        static const QList<RenderingBackendEntry> entries = {
            {QStringLiteral("auto"), QStringLiteral("Automatic")},
            {QStringLiteral("vulkan"), QStringLiteral("Vulkan")},
            {QStringLiteral("opengl"), QStringLiteral("OpenGL")},
        };
        return entries;
    }

    static const QStringList& renderingBackendOptions()
    {
        static const QStringList keys = [] {
            QStringList k;
            for (const auto& e : renderingBackendEntries())
                k.append(e.key);
            return k;
        }();
        return keys;
    }

    // Untranslated display names — use for translation source only.
    // SettingsController translates these via PzI18n::tr() at runtime.
    static QStringList renderingBackendDisplayNames()
    {
        QStringList names;
        for (const auto& e : renderingBackendEntries())
            names.append(e.displayName);
        return names;
    }

    static QString normalizeRenderingBackend(const QString& raw)
    {
        const QString normalized = raw.toLower().trimmed();
        for (const auto& e : renderingBackendEntries()) {
            if (e.key == normalized)
                return normalized;
        }
        return renderingBackend();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Shader Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool enableShaderEffects()
    {
        return true;
    }
    static int shaderFrameRate()
    {
        return 60;
    }
    static constexpr int shaderFrameRateMin()
    {
        return 30;
    }
    static constexpr int shaderFrameRateMax()
    {
        return 144;
    }
    static bool enableAudioVisualizer()
    {
        return false;
    }
    static int audioSpectrumBarCount()
    {
        return 64;
    }
    static constexpr int audioSpectrumBarCountMin()
    {
        return 16;
    }
    static constexpr int audioSpectrumBarCountMax()
    {
        return 256;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotile Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool autotileEnabled()
    {
        return false;
    }
    static QString defaultAutotileAlgorithm()
    {
        return QStringLiteral("bsp");
    }
    static constexpr qreal autotileSplitRatio()
    {
        return 0.5;
    }
    static constexpr qreal autotileSplitRatioMin()
    {
        return AutotileDefaults::MinSplitRatio;
    }
    static constexpr qreal autotileSplitRatioMax()
    {
        return AutotileDefaults::MaxSplitRatio;
    }
    static constexpr qreal autotileSplitRatioStep()
    {
        return 0.05;
    }
    static constexpr qreal autotileSplitRatioStepMin()
    {
        return 0.01;
    }
    static constexpr qreal autotileSplitRatioStepMax()
    {
        return 0.25;
    }
    static constexpr int autotileMasterCount()
    {
        return 1;
    }
    static constexpr int autotileMasterCountMin()
    {
        return AutotileDefaults::MinMasterCount;
    }
    static constexpr int autotileMasterCountMax()
    {
        return AutotileDefaults::MaxMasterCount;
    }
    static constexpr int autotileInnerGap()
    {
        return Defaults::ZonePadding;
    }
    static constexpr int autotileInnerGapMin()
    {
        return AutotileDefaults::MinGap;
    }
    static constexpr int autotileInnerGapMax()
    {
        return AutotileDefaults::MaxGap;
    }
    static constexpr int autotileOuterGap()
    {
        return Defaults::OuterGap;
    }
    static constexpr int autotileOuterGapMin()
    {
        return AutotileDefaults::MinGap;
    }
    static constexpr int autotileOuterGapMax()
    {
        return AutotileDefaults::MaxGap;
    }
    static bool autotileUsePerSideOuterGap()
    {
        return false;
    }
    static int autotileOuterGapTop()
    {
        return Defaults::OuterGap;
    }
    static constexpr int autotileOuterGapTopMin()
    {
        return AutotileDefaults::MinGap;
    }
    static constexpr int autotileOuterGapTopMax()
    {
        return AutotileDefaults::MaxGap;
    }
    static int autotileOuterGapBottom()
    {
        return Defaults::OuterGap;
    }
    static constexpr int autotileOuterGapBottomMin()
    {
        return AutotileDefaults::MinGap;
    }
    static constexpr int autotileOuterGapBottomMax()
    {
        return AutotileDefaults::MaxGap;
    }
    static int autotileOuterGapLeft()
    {
        return Defaults::OuterGap;
    }
    static constexpr int autotileOuterGapLeftMin()
    {
        return AutotileDefaults::MinGap;
    }
    static constexpr int autotileOuterGapLeftMax()
    {
        return AutotileDefaults::MaxGap;
    }
    static int autotileOuterGapRight()
    {
        return Defaults::OuterGap;
    }
    static constexpr int autotileOuterGapRightMin()
    {
        return AutotileDefaults::MinGap;
    }
    static constexpr int autotileOuterGapRightMax()
    {
        return AutotileDefaults::MaxGap;
    }
    static constexpr bool autotileFocusNewWindows()
    {
        return true;
    }
    static constexpr bool autotileSmartGaps()
    {
        return true;
    }
    static constexpr int autotileInsertPosition()
    {
        return 0;
    }
    static constexpr int autotileInsertPositionMin()
    {
        return AutotileDefaults::MinInsertPosition;
    }
    static constexpr int autotileInsertPositionMax()
    {
        return AutotileDefaults::MaxInsertPosition;
    }
    static constexpr int autotileMaxWindows()
    {
        return 5;
    }
    static constexpr int autotileMaxWindowsMin()
    {
        return AutotileDefaults::MinMaxWindows;
    }
    static constexpr int autotileMaxWindowsMax()
    {
        return AutotileDefaults::MaxMaxWindows;
    }
    static bool animationsEnabled()
    {
        return true;
    }
    static constexpr int animationDuration()
    {
        return 300;
    }
    static constexpr int animationDurationMin()
    {
        return AutotileDefaults::MinAnimationDuration;
    }
    static constexpr int animationDurationMax()
    {
        return AutotileDefaults::MaxAnimationDuration;
    }
    static int animationSequenceMode()
    {
        return 1;
    }
    static constexpr int animationSequenceModeMin()
    {
        return 0;
    }
    static constexpr int animationSequenceModeMax()
    {
        return 1;
    }
    static int animationStaggerInterval()
    {
        return 50;
    }
    static constexpr int animationStaggerIntervalMin()
    {
        return AutotileDefaults::MinAnimationStaggerIntervalMs;
    }
    static constexpr int animationStaggerIntervalMax()
    {
        return AutotileDefaults::MaxAnimationStaggerIntervalMs;
    }
    static QString animationEasingCurve()
    {
        return QStringLiteral("0.33,1.00,0.68,1.00");
    }
    static int animationMinDistance()
    {
        return 0;
    }
    static constexpr int animationMinDistanceMin()
    {
        return 0;
    }
    static constexpr int animationMinDistanceMax()
    {
        return 200;
    }
    static bool autotileFocusFollowsMouse()
    {
        return false;
    }
    static bool autotileRespectMinimumSize()
    {
        return true;
    }
    static bool autotileHideTitleBars()
    {
        return true;
    }
    static bool autotileShowBorder()
    {
        return true;
    }
    static int autotileBorderWidth()
    {
        return Defaults::BorderWidth;
    }
    static constexpr int autotileBorderWidthMin()
    {
        return 0;
    }
    static constexpr int autotileBorderWidthMax()
    {
        return 10;
    }
    static int autotileBorderRadius()
    {
        return 0;
    }
    static constexpr int autotileBorderRadiusMin()
    {
        return 0;
    }
    static constexpr int autotileBorderRadiusMax()
    {
        return 20;
    }
    static QColor autotileBorderColor()
    {
        return Defaults::HighlightColor;
    }
    static QColor autotileInactiveBorderColor()
    {
        return Defaults::InactiveColor;
    }
    static bool autotileUseSystemBorderColors()
    {
        return true;
    }
    static int autotileStickyWindowHandling()
    {
        return 0;
    }
    static int autotileDragBehavior()
    {
        return 0; // AutotileDragBehavior::Float — native drag-to-float
    }
    static int autotileOverflowBehavior()
    {
        return 0; // AutotileOverflowBehavior::Float — cap-enforcing (current)
    }
    static QStringList lockedScreens()
    {
        return {};
    }

    // ── Virtual Screen Limits ──────────────────────────────────────────
    static constexpr int maxVirtualScreensPerPhysical()
    {
        return 10;
    }
    static constexpr int minVirtualScreensPerPhysical()
    {
        return 2;
    }

    // ── Virtual Screen Defaults ───────────────────────────────────────
    static QString defaultVirtualScreenName(int index)
    {
        return QStringLiteral("Screen %1").arg(index + 1);
    }
    static QRectF defaultVirtualScreenRegion()
    {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    /// Tolerance for validating that virtual screen regions cover the full physical screen.
    static constexpr qreal areaCoverageTolerance()
    {
        return 0.05;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Ordering Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static QStringList snappingLayoutOrder()
    {
        return {};
    }
    static QStringList tilingAlgorithmOrder()
    {
        return {};
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Editor Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static QString editorDuplicateShortcut()
    {
        return QStringLiteral("Ctrl+D");
    }
    static QString editorSplitHorizontalShortcut()
    {
        return QStringLiteral("Ctrl+Shift+H");
    }
    static QString editorSplitVerticalShortcut()
    {
        return QStringLiteral("Ctrl+Alt+V");
    }
    static QString editorFillShortcut()
    {
        return QStringLiteral("Ctrl+Shift+F");
    }
    static bool editorGridSnappingEnabled()
    {
        return true;
    }
    static bool editorEdgeSnappingEnabled()
    {
        return true;
    }
    static double editorSnapInterval()
    {
        return 0.1;
    }
    static int editorSnapOverrideModifier()
    {
        return static_cast<int>(Qt::ShiftModifier);
    }
    static bool fillOnDropEnabled()
    {
        return true;
    }
    static int fillOnDropModifier()
    {
        return static_cast<int>(Qt::ControlModifier);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Update Notification Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static QString dismissedUpdateVersion()
    {
        return QString();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Global Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    static QString openEditorShortcut()
    {
        return QStringLiteral("Meta+Shift+E");
    }
    static QString openSettingsShortcut()
    {
        return QStringLiteral("Meta+Shift+P");
    }
    static QString previousLayoutShortcut()
    {
        return QStringLiteral("Meta+Alt+[");
    }
    static QString nextLayoutShortcut()
    {
        return QStringLiteral("Meta+Alt+]");
    }
    static QString quickLayout1Shortcut()
    {
        return QStringLiteral("Meta+Alt+1");
    }
    static QString quickLayout2Shortcut()
    {
        return QStringLiteral("Meta+Alt+2");
    }
    static QString quickLayout3Shortcut()
    {
        return QStringLiteral("Meta+Alt+3");
    }
    static QString quickLayout4Shortcut()
    {
        return QStringLiteral("Meta+Alt+4");
    }
    static QString quickLayout5Shortcut()
    {
        return QStringLiteral("Meta+Alt+5");
    }
    static QString quickLayout6Shortcut()
    {
        return QStringLiteral("Meta+Alt+6");
    }
    static QString quickLayout7Shortcut()
    {
        return QStringLiteral("Meta+Alt+7");
    }
    static QString quickLayout8Shortcut()
    {
        return QStringLiteral("Meta+Alt+8");
    }
    static QString quickLayout9Shortcut()
    {
        return QStringLiteral("Meta+Alt+9");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    static QString moveWindowLeftShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Left");
    }
    static QString moveWindowRightShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Right");
    }
    static QString moveWindowUpShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Up");
    }
    static QString moveWindowDownShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Down");
    }
    static QString swapWindowLeftShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Left");
    }
    static QString swapWindowRightShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Right");
    }
    static QString swapWindowUpShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Up");
    }
    static QString swapWindowDownShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Down");
    }
    static QString focusZoneLeftShortcut()
    {
        return QStringLiteral("Alt+Shift+Left");
    }
    static QString focusZoneRightShortcut()
    {
        return QStringLiteral("Alt+Shift+Right");
    }
    static QString focusZoneUpShortcut()
    {
        return QStringLiteral("Alt+Shift+Up");
    }
    static QString focusZoneDownShortcut()
    {
        return QStringLiteral("Alt+Shift+Down");
    }
    static QString pushToEmptyZoneShortcut()
    {
        return QStringLiteral("Meta+Alt+Return");
    }
    static QString restoreWindowSizeShortcut()
    {
        return QStringLiteral("Meta+Alt+Escape");
    }
    static QString toggleWindowFloatShortcut()
    {
        return QStringLiteral("Meta+F");
    }
    static QString snapToZone1Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+1");
    }
    static QString snapToZone2Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+2");
    }
    static QString snapToZone3Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+3");
    }
    static QString snapToZone4Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+4");
    }
    static QString snapToZone5Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+5");
    }
    static QString snapToZone6Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+6");
    }
    static QString snapToZone7Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+7");
    }
    static QString snapToZone8Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+8");
    }
    static QString snapToZone9Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+9");
    }
    static QString rotateWindowsClockwiseShortcut()
    {
        return QStringLiteral("Meta+Ctrl+]");
    }
    static QString rotateWindowsCounterclockwiseShortcut()
    {
        return QStringLiteral("Meta+Ctrl+[");
    }
    static QString cycleWindowForwardShortcut()
    {
        return QStringLiteral("Meta+Alt+.");
    }
    static QString cycleWindowBackwardShortcut()
    {
        return QStringLiteral("Meta+Alt+,");
    }
    static QString resnapToNewLayoutShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Z");
    }
    static QString snapAllWindowsShortcut()
    {
        return QStringLiteral("Meta+Ctrl+S");
    }
    static QString layoutPickerShortcut()
    {
        return QStringLiteral("Meta+Alt+Space");
    }
    static QString toggleLayoutLockShortcut()
    {
        return QStringLiteral("Meta+Ctrl+L");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Virtual Screen Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    static QString swapVirtualScreenLeftShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Shift+Left");
    }
    static QString swapVirtualScreenRightShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Shift+Right");
    }
    static QString swapVirtualScreenUpShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Shift+Up");
    }
    static QString swapVirtualScreenDownShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Shift+Down");
    }
    static QString rotateVirtualScreensClockwiseShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Shift+]");
    }
    static QString rotateVirtualScreensCounterclockwiseShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Shift+[");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotile Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    static QString autotileToggleShortcut()
    {
        return QStringLiteral("Meta+Shift+T");
    }
    static QString autotileFocusMasterShortcut()
    {
        return QStringLiteral("Meta+Shift+M");
    }
    static QString autotileSwapMasterShortcut()
    {
        return QStringLiteral("Meta+Shift+Return");
    }
    static QString autotileIncMasterRatioShortcut()
    {
        return QStringLiteral("Meta+Shift+L");
    }
    static QString autotileDecMasterRatioShortcut()
    {
        return QStringLiteral("Meta+Shift+H");
    }
    static QString autotileIncMasterCountShortcut()
    {
        return QStringLiteral("Meta+Shift+]");
    }
    static QString autotileDecMasterCountShortcut()
    {
        return QStringLiteral("Meta+Shift+[");
    }
    static QString autotileRetileShortcut()
    {
        return QStringLiteral("Meta+Ctrl+R");
    }

private:
    // Non-instantiable
    ConfigDefaults() = delete;
};

} // namespace PlasmaZones
