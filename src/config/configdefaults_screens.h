// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_shaders.h"

namespace PlasmaZones {

// Chain link 5: virtual-screen limit/name/region defaults and virtual-screen
// swap/rotate shortcut defaults.
class ConfigDefaultsScreens : public ConfigDefaultsShaders
{
public:
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
    /// Deliberately untranslated: this catalogue takes no i18n dependency
    /// (matching the layering note on defaultLayoutVisibilitySettings), and
    /// the name doubles as the stored fallback, which must not vary by locale.
    static QString defaultVirtualScreenName(int index)
    {
        return QStringLiteral("Screen %1").arg(index + 1);
    }
    static QRectF defaultVirtualScreenRegion()
    {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }
    // ═══════════════════════════════════════════════════════════════════════════
    // Virtual Screen Shortcuts
    //
    // VS-scope shortcuts escalate their window-scope counterpart with an
    // extra Shift: swap-window (Meta+Ctrl+Alt+Arrow) → swap-VS adds Shift;
    // rotate-window (Meta+Ctrl+]) → rotate-VS adds Shift+Alt. The Alt is
    // mandatory: Meta+Ctrl+Shift+Arrow / Meta+Ctrl+Shift+[ ] are KWin's own
    // built-in "Window One Desktop to the *" defaults, so dropping it lets
    // KWin grab the chord and the VS action silently never fires.
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
        return QStringLiteral("Meta+Ctrl+Alt+]");
    }
    static QString rotateVirtualScreensCounterclockwiseShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+[");
    }
};

} // namespace PlasmaZones
