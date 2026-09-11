// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsschema_shaderbounds.h"

#include <QMetaType>

namespace PlasmaZones {

bool overLongShaderString(const QVariant& value)
{
    return value.typeId() == QMetaType::QString && value.toString().size() > kMaxShaderStringChars;
}

QVariantMap boundedShaderParams(const QVariantMap& in)
{
    QVariantMap out;
    for (auto it = in.cbegin(); it != in.cend(); ++it) {
        if (out.size() >= kMaxShaderParameters) {
            break;
        }
        if (it.key().size() > kMaxShaderStringChars || overLongShaderString(it.value())) {
            continue;
        }
        if (it.value().typeId() == QMetaType::QVariantMap || it.value().typeId() == QMetaType::QVariantList) {
            continue;
        }
        out.insert(it.key(), it.value());
    }
    return out;
}

QStringList boundedIdList(const QStringList& in, int maxCount)
{
    QStringList out;
    out.reserve(qMin(in.size(), static_cast<qsizetype>(maxCount)));
    for (const QString& id : in) {
        if (out.size() >= maxCount) {
            break;
        }
        if (id.size() > kMaxShaderStringChars) {
            continue;
        }
        out.append(id);
    }
    return out;
}

QVariantMap boundedIdMap(const QVariantMap& in, int maxCount)
{
    QVariantMap out;
    for (auto it = in.cbegin(); it != in.cend(); ++it) {
        if (out.size() >= maxCount) {
            break;
        }
        if (it.key().size() > kMaxShaderStringChars) {
            continue;
        }
        if (it.value().typeId() != QMetaType::QString || overLongShaderString(it.value())) {
            continue;
        }
        out.insert(it.key(), it.value());
    }
    return out;
}

} // namespace PlasmaZones
