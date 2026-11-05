// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

namespace PlasmaZones {

/**
 * Reads KWin's per-output virtual desktops flag from kwinrc.
 *
 * This lives here because BOTH processes ask the question and they must
 * agree: the daemon gates the whole dynamic-workspaces feature on it, and
 * the settings app decides whether to show the consent prompt. They used to
 * carry separate readers and the daemon's was the weaker of the two, so a
 * user whose kwinrc already had the flag on could get the feature refused
 * with a warning while the settings page showed no prompt at all, or get a
 * redundant kwinrc write plus a KWin reconfigure on every daemon start.
 *
 * The value is read as flat INI rather than through KConfig, so the Qt-only
 * build needs no KConfig dependency. That means decoding two KConfig
 * spellings QSettings does not handle on its own, both of which otherwise
 * read as "off":
 *   - a marker suffix on the key ("PerOutputVirtualDesktops[$i]" for an
 *     immutable entry, or a locale tag), which QSettings keeps as part of
 *     the key name, so an exact-key lookup misses it;
 *   - KConfig's boolean words. It accepts "true", "on", "yes" and "1";
 *     QVariant::toBool understands only the first and the last.
 *
 * A missing file, a missing key or an unrecognised value all answer false,
 * which is the safe direction: the feature stays dormant and the consent
 * prompt appears, rather than the daemon assuming a mode KWin is not in.
 */
PLASMAZONES_EXPORT bool kwinPerOutputVirtualDesktopsEnabled();

} // namespace PlasmaZones
