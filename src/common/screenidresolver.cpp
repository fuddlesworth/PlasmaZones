// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenidresolver.h"

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/Layout.h>

#include <QString>

namespace PlasmaZones {

void ensureScreenIdResolver()
{
    // Function-local static — Meyer's singleton, C++11-guaranteed thread-safe.
    // First call across the process installs the resolver; subsequent calls
    // are no-ops. Used by daemon, editor, and settings composition roots so
    // layouts loaded from disk normalise connector names identically.
    static const bool installed = [] {
        PhosphorZones::Layout::setScreenIdResolver([](const QString& name) -> QString {
            if (name.isEmpty() || !Phosphor::Screens::ScreenIdentity::isConnectorName(name)) {
                return name;
            }
            return Phosphor::Screens::ScreenIdentity::idForName(name);
        });
        return true;
    }();
    (void)installed;
}

} // namespace PlasmaZones
