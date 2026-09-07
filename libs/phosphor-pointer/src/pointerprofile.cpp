// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerProfile.h>

#include <QJsonArray>
#include <QJsonValue>

namespace PhosphorPointerShaders {

bool PointerLayer::operator==(const PointerLayer& other) const
{
    return effectId == other.effectId && enabled == other.enabled && parameters == other.parameters;
}

QJsonObject PointerProfile::toJson() const
{
    QJsonArray arr;
    for (const PointerLayer& layer : layers) {
        QJsonObject l;
        l.insert(QLatin1String("effectId"), layer.effectId);
        l.insert(QLatin1String("enabled"), layer.enabled);
        l.insert(QLatin1String("parameters"), QJsonObject::fromVariantMap(layer.parameters));
        arr.append(l);
    }
    QJsonObject obj;
    obj.insert(QLatin1String("layers"), arr);
    return obj;
}

PointerProfile PointerProfile::fromJson(const QJsonObject& obj)
{
    PointerProfile profile;
    const QJsonArray arr = obj.value(QLatin1String("layers")).toArray();
    profile.layers.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject l = v.toObject();
        PointerLayer layer;
        layer.effectId = l.value(QLatin1String("effectId")).toString();
        if (layer.effectId.isEmpty()) {
            continue;
        }
        layer.enabled = l.value(QLatin1String("enabled")).toBool(true);
        layer.parameters = l.value(QLatin1String("parameters")).toObject().toVariantMap();
        profile.layers.append(std::move(layer));
    }
    return profile;
}

bool PointerProfile::operator==(const PointerProfile& other) const
{
    return layers == other.layers;
}

} // namespace PhosphorPointerShaders
