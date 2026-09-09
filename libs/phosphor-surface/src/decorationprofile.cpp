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

    // An ENGAGED chain, even an empty one, is a statement: "this surface
    // runs exactly these packs", which stops the seed defaults from being
    // injected and overrides whatever an ancestor set (see the class doc).
    // So a non-string entry must not coerce to "" and quietly engage that
    // statement on the author's behalf: it is skipped, and an array holding
    // only non-strings is treated as if the field were absent, which leaves
    // the optional disengaged and the seeds in force.
    if (obj.contains(QLatin1String(JsonFieldChain))) {
        const QJsonValue v = obj.value(QLatin1String(JsonFieldChain));
        if (v.isArray()) {
            QStringList chain;
            bool anyString = false;
            const QJsonArray arr = v.toArray();
            for (const QJsonValue& entry : arr) {
                if (!entry.isString())
                    continue;
                anyString = true;
                chain.append(entry.toString());
            }
            if (arr.isEmpty() || anyString)
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
        // Same rule as the chain: non-string entries are skipped, and an
        // array of nothing but non-strings leaves the field absent.
        if (v.isArray()) {
            QStringList disabled;
            bool anyString = false;
            const QJsonArray arr = v.toArray();
            for (const QJsonValue& entry : arr) {
                if (!entry.isString())
                    continue;
                anyString = true;
                disabled.append(entry.toString());
            }
            if (arr.isEmpty() || anyString)
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
