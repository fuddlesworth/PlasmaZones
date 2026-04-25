// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineTypes/EngineTypes.h>
#include <QStringList>

namespace PhosphorEngineApi {

class ISnapSettings
{
public:
    virtual ~ISnapSettings() = default;

    virtual QStringList excludedApplications() const = 0;
    virtual QStringList excludedWindowClasses() const = 0;
    virtual StickyWindowHandling stickyWindowHandling() const = 0;
    virtual bool moveNewWindowsToLastZone() const = 0;
    virtual bool restoreWindowsToZonesOnLogin() const = 0;
};

} // namespace PhosphorEngineApi

Q_DECLARE_INTERFACE(PhosphorEngineApi::ISnapSettings, "org.plasmazones.ISnapSettings")
