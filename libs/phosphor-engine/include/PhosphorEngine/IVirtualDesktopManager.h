// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>

#include <QString>

namespace PhosphorEngine {

class PHOSPHORENGINE_EXPORT IVirtualDesktopManager
{
public:
    virtual ~IVirtualDesktopManager() = default;

    /// The global (active-screen) current virtual desktop, 1-based.
    virtual int currentDesktop() const = 0;

    /// The current virtual desktop for a specific screen, 1-based. Under Plasma
    /// 6.7 "switch desktops independently for each screen" (per-output virtual
    /// desktops) each screen can be on its own desktop. The default returns the
    /// global currentDesktop(), which is correct for single-desktop / pre-6.7
    /// setups and for screens with no per-output desktop on record.
    virtual int currentDesktopForScreen(const QString& screenId) const
    {
        Q_UNUSED(screenId);
        return currentDesktop();
    }

    /// True when at least two screens are on different virtual desktops, i.e.
    /// per-output virtual desktops are in effect. Default: false.
    virtual bool perScreenModeActive() const
    {
        return false;
    }
};

} // namespace PhosphorEngine
