// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QString>

namespace PlasmaZones::ShaderRender {

/// Parse a hex string into a QColor, falling back to @p fallback when the
/// string is empty or QColor can't make sense of it. Centralised here so
/// the layout and metadata loaders don't drift on what counts as "valid".
inline QColor parseHexColor(const QString& hex, const QColor& fallback = Qt::black)
{
    if (hex.isEmpty())
        return fallback;
    const QColor c(hex);
    return c.isValid() ? c : fallback;
}

} // namespace PlasmaZones::ShaderRender
