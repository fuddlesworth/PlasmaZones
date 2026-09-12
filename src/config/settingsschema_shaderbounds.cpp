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

QStringList boundedIdList(const QStringList& in, int maxCount, IdListDuplicates duplicates)
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
        // De-duplication is order-preserving and keeps the FIRST occurrence, so
        // a chain reads the way the user arranged it.
        //
        // It matters for a decoration chain specifically, where the count cap is
        // not the real resource: the compositor folds the chain per ENTRY, with
        // per-entry buffer textures and FBO slots indexed by position, so 64
        // copies of one animated pack is 64 draws and 64 buffer slots per frame
        // even though the shader compiles once. A disable set or a pack-id list
        // is a set already and loses nothing by it either.
        if (duplicates == IdListDuplicates::Drop && out.contains(id)) {
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
