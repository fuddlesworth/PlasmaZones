// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengineapi_export.h>

namespace PhosphorEngineApi {

class PHOSPHORENGINEAPI_EXPORT IVirtualDesktopManager
{
public:
    virtual ~IVirtualDesktopManager() = default;

    virtual int currentDesktop() const = 0;
};

} // namespace PhosphorEngineApi
