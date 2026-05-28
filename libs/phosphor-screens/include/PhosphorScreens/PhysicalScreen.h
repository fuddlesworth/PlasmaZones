// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorscreenscore_export.h"

#include <QMetaType>
#include <QRect>
#include <QString>

class QScreen;

namespace PhosphorScreens {

/**
 * @brief A physical output as ScreenManager sees it — decoupled from QScreen.
 *
 * ScreenManager operates on PhysicalScreen values rather than QScreen
 * pointers so the screen add / remove / move / resize sequence can be
 * driven by a fake in tests (QScreen cannot be instantiated by non-
 * platform code). The production IScreenProvider wraps real QScreens and
 * fills @ref qscreen; a test provider synthesizes screens with arbitrary
 * geometry and leaves @ref qscreen null.
 *
 * This is a value snapshot, not a live handle. IScreenProvider emits a
 * fresh PhysicalScreen with every screenGeometryChanged, and ScreenManager
 * replaces its stored copy — so a stored PhysicalScreen is current as long
 * as the manager keeps up with the provider's signals, which it does.
 *
 * Identity is the connector @ref name: it is unique among connected
 * outputs and stable for the life of a connection (geometry and even the
 * EDID-derived @ref identifier can change under it; the connector does
 * not). Equality and hashing key on it.
 */
struct PHOSPHORSCREENSCORE_EXPORT PhysicalScreen
{
    /// Connector name (e.g. "DP-3", "HDMI-A-1") — `QScreen::name()`.
    QString name;

    /// Stable EDID-aware identifier from ScreenIdentity (e.g.
    /// "Manuf:Model:Serial" or "Manuf:Model:Serial/CONNECTOR"). May be
    /// empty for a synthetic test screen that opts out of identity.
    QString identifier;

    /// Output geometry in the global desktop coordinate space.
    QRect geometry;

    /// Underlying QScreen, or nullptr for a synthetic (test) screen.
    /// Consumers that genuinely need the QScreen — to parent a window to
    /// an output, say — read this; null-check it.
    QScreen* qscreen = nullptr;

    /// A screen with no connector name is the null/absent value.
    bool isValid() const
    {
        return !name.isEmpty();
    }

    /// Identity is the connector — see the class note.
    bool operator==(const PhysicalScreen& other) const
    {
        return name == other.name;
    }
    bool operator!=(const PhysicalScreen& other) const
    {
        return !(*this == other);
    }
};

/// Hash on the connector name so PhysicalScreen can key a QHash/QSet.
inline size_t qHash(const PhysicalScreen& screen, size_t seed = 0)
{
    return qHash(screen.name, seed);
}

} // namespace PhosphorScreens

// PhysicalScreen travels on the ScreenManager / IScreenProvider signals —
// register it as a metatype so queued connections and QSignalSpy can carry it.
Q_DECLARE_METATYPE(PhosphorScreens::PhysicalScreen)
