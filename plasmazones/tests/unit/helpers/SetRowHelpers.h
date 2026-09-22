// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "settings/stores/shadersetstore.h"

/// The ShaderSetStore row for @p name, or an empty map when no such set is
/// listed. Callers QVERIFY the result is non-empty BEFORE asserting on a field:
/// a `for (row : sets) if (row.name == X) QVERIFY(...)` idiom silently runs zero
/// assertions when the row is absent, so the test would pass vacuously.
inline QVariantMap rowFor(PlasmaZones::ShaderSetStore* sets, const QString& name)
{
    const QVariantList rows = sets->availableSets();
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("name")).toString() == name)
            return map;
    }
    return QVariantMap{};
}
