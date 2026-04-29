// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengineapi_export.h>
#include <QString>

namespace PhosphorEngineApi {

class PHOSPHORENGINEAPI_EXPORT IWindowRegistry
{
public:
    virtual ~IWindowRegistry() = default;

    virtual QString canonicalizeWindowId(const QString& rawWindowId) = 0;
    virtual QString canonicalizeForLookup(const QString& rawWindowId) const = 0;
    virtual QString appIdFor(const QString& instanceId) const = 0;
};

} // namespace PhosphorEngineApi
