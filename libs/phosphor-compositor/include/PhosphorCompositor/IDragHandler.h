// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorcompositor_export.h>
#include <QRect>
#include <QString>

namespace PhosphorCompositor {

class PHOSPHORCOMPOSITOR_EXPORT IDragHandler
{
public:
    virtual ~IDragHandler() = default;

    virtual void onDragStarted(const QString& windowId, const QRect& windowGeometry, const QString& screenId) = 0;
    virtual void onDragMoved(const QString& windowId, int cursorX, int cursorY) = 0;
    virtual void onDragEnded(const QString& windowId, bool cancelled) = 0;
    virtual void onDragPolicyChanged(const QString& windowId, int newPolicy) = 0;
};

} // namespace PhosphorCompositor
