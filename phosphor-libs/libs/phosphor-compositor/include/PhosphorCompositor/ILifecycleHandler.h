// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorcompositor_export.h>
#include <QString>

namespace PhosphorCompositor {

class PHOSPHORCOMPOSITOR_EXPORT ILifecycleHandler
{
public:
    virtual ~ILifecycleHandler() = default;

    virtual void onWindowOpened(const QString& windowId, const QString& screenId) = 0;
    virtual void onWindowClosed(const QString& windowId) = 0;
    virtual void onWindowActivated(const QString& windowId, const QString& screenId) = 0;
    virtual void onWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId) = 0;
};

} // namespace PhosphorCompositor
