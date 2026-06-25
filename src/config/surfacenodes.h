// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PlasmaZones {

/**
 * @brief Canonical ids for the SURFACE axis of the appearance hub.
 *
 * Appearance is organized by *surface* (the thing being styled) rather than by
 * feature. Each id names one surface node; appearance facets (Colors, Borders,
 * Decorations, Gaps, ...) hang off a node, and a child surface inherits its
 * parent unless it overrides.
 *
 * This is the single spelling authority for those ids. Everything that keys data
 * by surface — node pages, scope helpers, and PR #624's DecorationProfileTree —
 * must reuse these accessors verbatim rather than re-spelling the strings, so the
 * two never desync.
 *
 * Dot-paths mirror the parent → child surface hierarchy:
 *   windows.tiled / windows.snapped / windows.floating  inherit  windows
 *   daemon.osd / daemon.overlay / daemon.zoneselector / daemon.popup
 *
 * `floating` and `popup` name surfaces that only become user-visible alongside
 * the decoration-chain work; the ids are fixed here so that work slots onto the
 * existing namespace instead of inventing a parallel one.
 */
class SurfaceNodes
{
public:
    // Window surfaces.
    static QString windows()
    {
        return QStringLiteral("windows");
    }
    static QString windowsTiled()
    {
        return QStringLiteral("windows.tiled");
    }
    static QString windowsSnapped()
    {
        return QStringLiteral("windows.snapped");
    }
    static QString windowsFloating()
    {
        return QStringLiteral("windows.floating");
    }

    // Daemon-drawn surfaces.
    static QString daemon()
    {
        return QStringLiteral("daemon");
    }
    static QString daemonOsd()
    {
        return QStringLiteral("daemon.osd");
    }
    static QString daemonOverlay()
    {
        return QStringLiteral("daemon.overlay");
    }
    static QString daemonZoneSelector()
    {
        return QStringLiteral("daemon.zoneselector");
    }
    static QString daemonPopup()
    {
        return QStringLiteral("daemon.popup");
    }
};

} // namespace PlasmaZones
