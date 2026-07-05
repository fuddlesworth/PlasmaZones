// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/DecorationProfile.h>

#include <QJsonArray>
#include <QJsonValue>

namespace PhosphorSurfaceShaders {

DecorationProfile DecorationProfile::withDefaults() const
{
    DecorationProfile out = *this;
    if (!out.chain)
        out.chain = QStringList();
    if (!out.parameters)
        out.parameters = QVariantMap();
    if (!out.disabledPacks)
        out.disabledPacks = QStringList();
    return out;
}

QJsonObject DecorationProfile::toJson() const
{
    QJsonObject obj;
    if (chain) {
        QJsonArray chainArr;
        for (const QString& packId : *chain)
            chainArr.append(packId);
        obj.insert(QLatin1String(JsonFieldChain), chainArr);
    }
    if (parameters) {
        QJsonObject paramsObj;
        for (auto it = parameters->constBegin(); it != parameters->constEnd(); ++it)
            paramsObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
        obj.insert(QLatin1String(JsonFieldParameters), paramsObj);
    }
    if (disabledPacks) {
        QJsonArray disabledArr;
        for (const QString& packId : *disabledPacks)
            disabledArr.append(packId);
        obj.insert(QLatin1String(JsonFieldDisabledPacks), disabledArr);
    }
    return obj;
}

DecorationProfile DecorationProfile::fromJson(const QJsonObject& obj)
{
    DecorationProfile p;

    if (obj.contains(QLatin1String(JsonFieldChain))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldChain));
        if (v.isArray()) {
            QStringList chain;
            const QJsonArray arr = v.toArray();
            for (const QJsonValue& entry : arr)
                chain.append(entry.toString());
            p.chain = std::move(chain);
        }
    }

    if (obj.contains(QLatin1String(JsonFieldParameters))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldParameters));
        if (v.isObject()) {
            QVariantMap params;
            const QJsonObject paramsObj = v.toObject();
            for (auto it = paramsObj.constBegin(); it != paramsObj.constEnd(); ++it)
                params.insert(it.key(), it.value().toVariant());
            p.parameters = std::move(params);
        }
    }

    // Absent field = nullopt = "inherit / nothing disabled", so a config
    // written before the per-layer toggle existed loads with every pack on.
    if (obj.contains(QLatin1String(JsonFieldDisabledPacks))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldDisabledPacks));
        if (v.isArray()) {
            QStringList disabled;
            const QJsonArray arr = v.toArray();
            for (const QJsonValue& entry : arr)
                disabled.append(entry.toString());
            p.disabledPacks = std::move(disabled);
        }
    }

    return p;
}

void DecorationProfile::overlay(DecorationProfile& dst, const DecorationProfile& src)
{
    if (src.chain)
        dst.chain = src.chain;
    if (src.parameters)
        dst.parameters = src.parameters;
    if (src.disabledPacks)
        dst.disabledPacks = src.disabledPacks;
}

bool DecorationProfile::operator==(const DecorationProfile& other) const
{
    return chain == other.chain && parameters == other.parameters && disabledPacks == other.disabledPacks;
}

} // namespace PhosphorSurfaceShaders
