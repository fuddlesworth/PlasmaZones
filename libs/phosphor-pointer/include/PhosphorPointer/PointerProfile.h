// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorPointer/phosphorpointer_export.h>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace PhosphorPointerShaders {

/// One layer of the user's pointer chain: a pack id, its friendly parameter
/// overrides and an enable switch.
struct PHOSPHORPOINTER_EXPORT PointerLayer
{
    QString effectId;
    QVariantMap parameters;
    bool enabled = true;

    bool operator==(const PointerLayer& other) const;
    bool operator!=(const PointerLayer& other) const
    {
        return !(*this == other);
    }
};

/// The user's pointer chain, persisted under the `Pointer.Chain` config key as
/// `{"layers":[{"effectId":"phosphor-trail","enabled":true,"parameters":{"width":6}}]}`.
/// Layers paint in list order.
class PHOSPHORPOINTER_EXPORT PointerProfile
{
public:
    QList<PointerLayer> layers;

    QJsonObject toJson() const;

    /// Parse the persisted shape. Entries with an empty `effectId` are
    /// dropped; a missing `enabled` reads true; a missing `parameters` reads
    /// empty.
    static PointerProfile fromJson(const QJsonObject& obj);

    bool isEmpty() const
    {
        return layers.isEmpty();
    }

    bool operator==(const PointerProfile& other) const;
    bool operator!=(const PointerProfile& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorPointerShaders
