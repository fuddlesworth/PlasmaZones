// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorcompositor_export.h>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PhosphorCompositor {

struct GeometryRequest
{
    QString windowId;
    QRect geometry;
    QString zoneId;
    QString screenId;
    bool sizeOnly = false;
};

enum class BatchAction {
    Resnap,
    Rotate,
    Autotile
};

class PHOSPHORCOMPOSITOR_EXPORT IGeometryHandler
{
public:
    virtual ~IGeometryHandler() = default;

    virtual void onApplyGeometry(const GeometryRequest& request) = 0;
    virtual void onApplyGeometriesBatch(const QVector<GeometryRequest>& requests, BatchAction action) = 0;
    virtual void onRaiseWindows(const QStringList& windowIds) = 0;
    virtual void onActivateWindow(const QString& windowId) = 0;
    virtual void onMoveWindowToZone(const QString& windowId, const QString& screenId, int x, int y, int w, int h) = 0;
    virtual void onSnapAllWindows(const QString& screenId) = 0;
};

} // namespace PhosphorCompositor
