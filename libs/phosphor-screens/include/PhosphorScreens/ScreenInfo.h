// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QList>
#include <QString>
#include <QVariantList>

#include "phosphorscreenscore_export.h"

namespace Phosphor::Screens {

/**
 * Lightweight descriptor for a connected screen, suitable for passing
 * across settings UIs and QML.
 *
 * Pure POD — no Qt meta-object plumbing. Apps that produce this data
 * (typically by querying their compositor over D-Bus or via QGuiApplication)
 * can pass lists across language boundaries through
 * screenInfoListToVariantList().
 */
struct PHOSPHORSCREENSCORE_EXPORT ScreenInfo
{
    QString name;
    bool isPrimary = false;
    QString manufacturer;
    QString model;
    int width = 0;
    int height = 0;
    QString screenId;
    bool isVirtualScreen = false;
    /** Physical connector (e.g. "DP-2"). */
    QString connectorName;
    /** 0-based index within the physical screen (-1 = not virtual). */
    int virtualIndex = -1;
    /** User-facing name for virtual screens (e.g. "Left"). */
    QString virtualDisplayName;
};

/**
 * Convert a ScreenInfo list to QVariantList suitable for QML consumption.
 *
 * Each entry is a QVariantMap with keys: name, isPrimary, manufacturer,
 * model, resolution, width, height, screenId, connectorName, and a
 * pre-computed displayLabel that QML selectors / context menus can render
 * without duplicating label-building logic.
 */
PHOSPHORSCREENSCORE_EXPORT QVariantList screenInfoListToVariantList(const QList<ScreenInfo>& screens);

} // namespace Phosphor::Screens
