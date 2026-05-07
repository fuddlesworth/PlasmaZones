// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>

namespace PhosphorEngine {

class PHOSPHORENGINE_EXPORT IVirtualDesktopManager
{
public:
    virtual ~IVirtualDesktopManager() = default;

    virtual int currentDesktop() const = 0;
};

} // namespace PhosphorEngine
