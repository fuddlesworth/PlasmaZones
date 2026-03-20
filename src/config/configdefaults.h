// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtCore/qnamespace.h>

namespace PlasmaZones {

/**
 * @brief Provides static access to default configuration values
 *
 * Hardcoded defaults matching the plasmazones.kcfg schema.
 * The .kcfg file remains the canonical reference; these values are copied
 * from it so that the daemon and editor can build without KConfigXT.
 *
 * Usage:
 *   int cols = ConfigDefaults::gridColumns();  // Returns 5
 *   int rows = ConfigDefaults::maxRows();      // Returns 4
 */
class ConfigDefaults
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Activation Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool shiftDrag()
    {
        return true;
    }
    static int dragActivationModifier()
    {
        return 3;
    }
    static int dragActivationMouseButton()
    {
        return 0;
    }
    static QVariantList dragActivationTriggers()
    {
        // Default: single trigger with Alt modifier, no mouse button
        QVariantMap trigger;
        trigger[QStringLiteral("modifier")] = dragActivationModifier();
        trigger[QStringLiteral("mouseButton")] = dragActivationMouseButton();
        return {trigger};
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
        QVariantMap trigger;
        trigger[QStringLiteral("modifier")] = zoneSpanModifier();
        trigger[QStringLiteral("mouseButton")] = 0;
        return {trigger};
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Display Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool showOnAllMonitors()
    {
        return false;
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
    static int osdStyle()
    {
        return 2;
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
        // #AARRGGBB: #800078D4 → A=0x80, R=0x00, G=0x78, B=0xD4
        return QColor(0x00, 0x78, 0xD4, 0x80);
    }
    static QColor inactiveColor()
    {
        // #40808080
        return QColor(0x80, 0x80, 0x80, 0x40);
    }
    static QColor borderColor()
    {
        // #C8FFFFFF
        return QColor(0xFF, 0xFF, 0xFF, 0xC8);
    }
    static QColor labelFontColor()
    {
        // #FFFFFFFF
        return QColor(0xFF, 0xFF, 0xFF, 0xFF);
    }
    static double activeOpacity()
    {
        return 0.5;
    }
    static double inactiveOpacity()
    {
        return 0.3;
    }
    static int borderWidth()
    {
        return 2;
    }
    static int borderRadius()
    {
        return 8;
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
    static int labelFontWeight()
    {
        return 700;
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
        return 8;
    }
    static int outerGap()
    {
        return 8;
    }
    static bool usePerSideOuterGap()
    {
        return false;
    }
    static int outerGapTop()
    {
        return 8;
    }
    static int outerGapBottom()
    {
        return 8;
    }
    static int outerGapLeft()
    {
        return 8;
    }
    static int outerGapRight()
    {
        return 8;
    }
    static int adjacentThreshold()
    {
        return 20;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Performance Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static int pollIntervalMs()
    {
        return 50;
    }
    static int minimumZoneSizePx()
    {
        return 100;
    }
    static int minimumZoneDisplaySizePx()
    {
        return 10;
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
        QVariantMap trigger;
        trigger[QStringLiteral("modifier")] = 0;
        trigger[QStringLiteral("mouseButton")] = static_cast<int>(Qt::MiddleButton);
        return {trigger};
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
    static int minimumWindowHeight()
    {
        return 150;
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
    static int previewWidth()
    {
        return 180;
    }
    static int previewHeight()
    {
        return 101;
    }
    static bool previewLockAspect()
    {
        return true;
    }
    static int gridColumns()
    {
        return 5;
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
    static bool enableAudioVisualizer()
    {
        return false;
    }
    static int audioSpectrumBarCount()
    {
        return 64;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotile Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool autotileEnabled()
    {
        return false;
    }
    static QString autotileAlgorithm()
    {
        return QStringLiteral("bsp");
    }
    static double autotileSplitRatio()
    {
        return 0.5;
    }
    static int autotileMasterCount()
    {
        return 1;
    }
    static double autotileCenteredMasterSplitRatio()
    {
        return 0.5;
    }
    static int autotileCenteredMasterMasterCount()
    {
        return 1;
    }
    static int autotileInnerGap()
    {
        return 8;
    }
    static int autotileOuterGap()
    {
        return 8;
    }
    static bool autotileUsePerSideOuterGap()
    {
        return false;
    }
    static int autotileOuterGapTop()
    {
        return 8;
    }
    static int autotileOuterGapBottom()
    {
        return 8;
    }
    static int autotileOuterGapLeft()
    {
        return 8;
    }
    static int autotileOuterGapRight()
    {
        return 8;
    }
    static bool autotileFocusNewWindows()
    {
        return true;
    }
    static bool autotileSmartGaps()
    {
        return true;
    }
    static int autotileInsertPosition()
    {
        return 0;
    }
    static int autotileMaxWindows()
    {
        return 5;
    }
    static bool animationsEnabled()
    {
        return true;
    }
    static int animationDuration()
    {
        return 300;
    }
    static int animationSequenceMode()
    {
        return 1;
    }
    static int animationStaggerInterval()
    {
        return 50;
    }
    static QString animationEasingCurve()
    {
        return QStringLiteral("0.33,1.00,0.68,1.00");
    }
    static int animationMinDistance()
    {
        return 0;
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
        return 2;
    }
    static int autotileBorderRadius()
    {
        return 0;
    }
    static QColor autotileBorderColor()
    {
        // #800078D4
        return QColor(0x00, 0x78, 0xD4, 0x80);
    }
    static QColor autotileInactiveBorderColor()
    {
        // #40808080
        return QColor(0x80, 0x80, 0x80, 0x40);
    }
    static bool autotileUseSystemBorderColors()
    {
        return true;
    }
    static QStringList lockedScreens()
    {
        return {};
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
        return QStringLiteral("Meta+Shift+R");
    }

private:
    // Non-instantiable
    ConfigDefaults() = delete;
};

} // namespace PlasmaZones
