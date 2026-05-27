// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingalgorithmcontroller.h"

#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "../core/logging.h"

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/TilingAlgorithm.h>

#include <QLatin1String>
#include <QLoggingCategory>
#include <algorithm>
#include <cmath>

namespace PlasmaZones {

TilingAlgorithmController::TilingAlgorithmController(Settings* settings, PhosphorTiles::AlgorithmRegistry* registry,
                                                     QObject* parent)
    : PhosphorSettingsUi::PageController(QStringLiteral("tiling-algorithm"), parent)
    , m_settings(settings)
    , m_registry(registry)
{
    Q_ASSERT(m_settings);
    Q_ASSERT(m_registry);
}

int TilingAlgorithmController::autotileGapMin() const
{
    return ConfigDefaults::autotileOuterGapMin();
}

int TilingAlgorithmController::autotileGapMax() const
{
    return ConfigDefaults::autotileOuterGapMax();
}

int TilingAlgorithmController::autotileMaxWindowsMin() const
{
    return ConfigDefaults::autotileMaxWindowsMin();
}

int TilingAlgorithmController::autotileMasterCountMin() const
{
    return ConfigDefaults::autotileMasterCountMin();
}

qreal TilingAlgorithmController::autotileSplitRatioMin() const
{
    return ConfigDefaults::autotileSplitRatioMin();
}

qreal TilingAlgorithmController::autotileSplitRatioStepMin() const
{
    return ConfigDefaults::autotileSplitRatioStepMin();
}

qreal TilingAlgorithmController::autotileSplitRatioStepMax() const
{
    return ConfigDefaults::autotileSplitRatioStepMax();
}

QVariantMap TilingAlgorithmController::savedCustomParams(const QString& algorithmId) const
{
    const QVariantMap perAlgo = m_settings->autotilePerAlgorithmSettings();
    const QVariant algoEntry = perAlgo.value(algorithmId);
    if (algoEntry.isValid()) {
        const QVariant customVar = algoEntry.toMap().value(PhosphorTiles::AutotileJsonKeys::CustomParams);
        if (customVar.isValid()) {
            return customVar.toMap();
        }
    }
    return {};
}

QVariantList TilingAlgorithmController::customParamsForAlgorithm(const QString& algorithmId) const
{
    PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(algorithmId);
    if (!algo || !algo->supportsCustomParams()) {
        return {};
    }

    const QVariantMap savedCustom = savedCustomParams(algorithmId);
    const QVariantList defs = algo->customParamDefList();

    QVariantList result;
    for (const auto& defVar : defs) {
        QVariantMap paramMap = defVar.toMap();
        const QString name = paramMap.value(QLatin1String("name")).toString();
        // Current value: saved value if exists (clamped/validated against
        // the def so a stale on-disk value from an older bounds doesn't
        // crash the QML slider), else default.
        if (savedCustom.contains(name)) {
            const QVariant saved = savedCustom.value(name);
            const QString type = paramMap.value(QLatin1String("type")).toString();
            if (type == QLatin1String("number")) {
                bool ok = false;
                const qreal num = saved.toDouble(&ok);
                if (!ok) {
                    paramMap[QLatin1String("value")] = paramMap.value(QLatin1String("defaultValue"));
                } else {
                    const qreal minVal = paramMap.value(QLatin1String("minValue")).toDouble();
                    const qreal maxVal = paramMap.value(QLatin1String("maxValue")).toDouble();
                    paramMap[QLatin1String("value")] = std::clamp(num, minVal, maxVal);
                }
            } else if (type == QLatin1String("enum")) {
                const QString str = saved.toString();
                const QStringList options = paramMap.value(QLatin1String("enumOptions")).toStringList();
                paramMap[QLatin1String("value")] =
                    options.contains(str) ? QVariant(str) : paramMap.value(QLatin1String("defaultValue"));
            } else {
                paramMap[QLatin1String("value")] = saved;
            }
        } else {
            paramMap[QLatin1String("value")] = paramMap.value(QLatin1String("defaultValue"));
        }
        result.append(paramMap);
    }
    return result;
}

void TilingAlgorithmController::setCustomParam(const QString& algorithmId, const QString& paramName,
                                               const QVariant& value)
{
    if (algorithmId.isEmpty() || paramName.isEmpty()) {
        return;
    }

    // Validate paramName exists in the algorithm's declared custom params.
    PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(algorithmId);
    if (!algo || !algo->supportsCustomParams()) {
        return;
    }
    const QVariantList defs = algo->customParamDefList();
    auto defIt = std::find_if(defs.cbegin(), defs.cend(), [&paramName](const QVariant& v) {
        return v.toMap().value(QLatin1String("name")).toString() == paramName;
    });
    if (defIt == defs.cend()) {
        qCWarning(lcCore) << "setCustomParam: unknown param" << paramName << "for algorithm" << algorithmId;
        return;
    }
    const QVariantMap defMap = defIt->toMap();
    const QString defType = defMap.value(QLatin1String("type")).toString();

    // Coerce value to the declared type so QML callers can't persist wrong types.
    QVariant coerced = value;
    if (defType == QLatin1String("number")) {
        bool ok = false;
        const qreal num = value.toDouble(&ok);
        if (!ok) {
            qCWarning(lcCore) << "setCustomParam: value" << value << "is not a valid number for" << paramName;
            return;
        }
        const qreal minVal = defMap.value(QLatin1String("minValue")).toDouble();
        const qreal maxVal = defMap.value(QLatin1String("maxValue")).toDouble();
        coerced = std::clamp(num, minVal, maxVal);
    } else if (defType == QLatin1String("bool")) {
        coerced = value.toBool();
    } else if (defType == QLatin1String("enum")) {
        const QString str = value.toString();
        const QStringList options = defMap.value(QLatin1String("enumOptions")).toStringList();
        if (!options.contains(str)) {
            qCWarning(lcCore) << "setCustomParam: value" << str << "not in enum options for" << paramName
                              << "(valid:" << options << ")";
            return;
        }
        coerced = str;
    } else {
        qCWarning(lcCore) << "setCustomParam: unknown param type" << defType << "for" << paramName;
        return;
    }

    QVariantMap perAlgo = m_settings->autotilePerAlgorithmSettings();
    QVariantMap algoEntry = perAlgo.value(algorithmId).toMap();
    QVariantMap customParams = algoEntry.value(PhosphorTiles::AutotileJsonKeys::CustomParams).toMap();

    // No-op if the coerced value already matches what's stored — avoids
    // emitting NOTIFY + flipping the dirty flag for slider re-emits that
    // resolve to the same snapped value.
    if (customParams.contains(paramName) && customParams.value(paramName) == coerced) {
        return;
    }

    customParams[paramName] = coerced;
    algoEntry[PhosphorTiles::AutotileJsonKeys::CustomParams] = customParams;

    // Preserve existing splitRatio/masterCount if not already in the entry.
    if (!algoEntry.contains(PhosphorTiles::AutotileJsonKeys::SplitRatio)) {
        algoEntry[PhosphorTiles::AutotileJsonKeys::SplitRatio] = algo->defaultSplitRatio();
    }
    if (!algoEntry.contains(PhosphorTiles::AutotileJsonKeys::MasterCount)) {
        algoEntry[PhosphorTiles::AutotileJsonKeys::MasterCount] = ConfigDefaults::autotileMasterCount();
    }

    perAlgo[algorithmId] = algoEntry;
    m_settings->setAutotilePerAlgorithmSettings(perAlgo);
    Q_EMIT customParamChanged(algorithmId, paramName);
    Q_EMIT changed();
}

} // namespace PlasmaZones
