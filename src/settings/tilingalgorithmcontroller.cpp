// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingalgorithmcontroller.h"

#include "../config/configdefaults.h"
#include "../core/isettings.h"
#include "../core/logging.h"

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/TilingAlgorithm.h>

#include <QLatin1String>
#include <QLoggingCategory>
#include <algorithm>
#include <cmath> // std::isfinite — guards the NaN/inf clamp in setCustomParam

namespace PlasmaZones {

namespace {
// Custom-param def schema field names — the wire contract between
// PhosphorTiles::TilingAlgorithm::customParamDefList() and this
// controller. Centralised here so a typo in one branch can't silently
// fall through to the default-value path (which would look like a
// "saved value didn't stick" UI bug at runtime).
namespace ParamDefKeys {
constexpr QLatin1String Name{"name"};
constexpr QLatin1String Type{"type"};
constexpr QLatin1String Value{"value"};
constexpr QLatin1String DefaultValue{"defaultValue"};
constexpr QLatin1String MinValue{"minValue"};
constexpr QLatin1String MaxValue{"maxValue"};
constexpr QLatin1String EnumOptions{"enumOptions"};
} // namespace ParamDefKeys

namespace ParamTypes {
constexpr QLatin1String Number{"number"};
constexpr QLatin1String Bool{"bool"};
constexpr QLatin1String Enum{"enum"};
} // namespace ParamTypes
} // namespace

TilingAlgorithmController::TilingAlgorithmController(ISettings& settings, PhosphorTiles::AlgorithmRegistry& registry,
                                                     QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("tiling-algorithm"), parent)
    , m_settings(&settings)
    , m_registry(&registry)
{
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

int TilingAlgorithmController::autotileMaxWindowsMax() const
{
    return ConfigDefaults::autotileMaxWindowsMax();
}

int TilingAlgorithmController::autotileMasterCountMax() const
{
    return ConfigDefaults::autotileMasterCountMax();
}

qreal TilingAlgorithmController::autotileSplitRatioMax() const
{
    return ConfigDefaults::autotileSplitRatioMax();
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
        const QString name = paramMap.value(ParamDefKeys::Name).toString();
        // Current value: saved value if exists (clamped/validated against
        // the def so a stale on-disk value from an older bounds doesn't
        // crash the QML slider), else default.
        if (savedCustom.contains(name)) {
            const QVariant saved = savedCustom.value(name);
            const QString type = paramMap.value(ParamDefKeys::Type).toString();
            if (type == ParamTypes::Number) {
                bool ok = false;
                const qreal num = saved.toDouble(&ok);
                if (!ok) {
                    paramMap[ParamDefKeys::Value] = paramMap.value(ParamDefKeys::DefaultValue);
                } else {
                    // Defensive: a malformed schema entry without min/max
                    // would collapse std::clamp(num, 0.0, 0.0) to 0. Skip
                    // the clamp instead of silently zeroing the value.
                    if (!paramMap.contains(ParamDefKeys::MinValue) || !paramMap.contains(ParamDefKeys::MaxValue)) {
                        paramMap[ParamDefKeys::Value] = num;
                    } else {
                        const qreal minVal = paramMap.value(ParamDefKeys::MinValue).toDouble();
                        const qreal maxVal = paramMap.value(ParamDefKeys::MaxValue).toDouble();
                        // An inverted range (min > max) from a malformed
                        // (e.g. user-authored Luau) schema is undefined behaviour
                        // for std::clamp; skip the clamp rather than invoke UB.
                        paramMap[ParamDefKeys::Value] = (minVal <= maxVal) ? std::clamp(num, minVal, maxVal) : num;
                    }
                }
            } else if (type == ParamTypes::Enum) {
                const QString str = saved.toString();
                const QStringList options = paramMap.value(ParamDefKeys::EnumOptions).toStringList();
                paramMap[ParamDefKeys::Value] =
                    options.contains(str) ? QVariant(str) : paramMap.value(ParamDefKeys::DefaultValue);
            } else if (type == ParamTypes::Bool) {
                // Coerce to bool so a saved string "true"/"false" (older
                // schema or hand-edited config) round-trips into QML as
                // a bool — QML switches/checkboxes read string values
                // as truthy regardless of the content, which would
                // silently turn a `"false"` storage entry into a
                // checked switch in the UI.
                paramMap[ParamDefKeys::Value] = saved.toBool();
            } else {
                paramMap[ParamDefKeys::Value] = saved;
            }
        } else {
            paramMap[ParamDefKeys::Value] = paramMap.value(ParamDefKeys::DefaultValue);
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
        return v.toMap().value(ParamDefKeys::Name).toString() == paramName;
    });
    if (defIt == defs.cend()) {
        qCWarning(lcCore) << "setCustomParam: unknown param" << paramName << "for algorithm" << algorithmId;
        return;
    }
    const QVariantMap defMap = defIt->toMap();
    const QString defType = defMap.value(ParamDefKeys::Type).toString();

    // Coerce value to the declared type so QML callers can't persist wrong types.
    QVariant coerced = value;
    if (defType == ParamTypes::Number) {
        bool ok = false;
        const qreal num = value.toDouble(&ok);
        if (!ok) {
            qCWarning(lcCore) << "setCustomParam: value" << value << "is not a valid number for" << paramName;
            return;
        }
        // QString::toDouble accepts "inf"/"-inf"/"nan" with ok=true,
        // and std::clamp(NaN, ...) returns NaN unchanged — silently
        // persisting NaN would crash the downstream tiling algorithm.
        if (!std::isfinite(num)) {
            qCWarning(lcCore) << "setCustomParam: refusing non-finite value" << num << "for" << paramName;
            return;
        }
        if (!defMap.contains(ParamDefKeys::MinValue) || !defMap.contains(ParamDefKeys::MaxValue)) {
            coerced = num;
        } else {
            const qreal minVal = defMap.value(ParamDefKeys::MinValue).toDouble();
            const qreal maxVal = defMap.value(ParamDefKeys::MaxValue).toDouble();
            // An inverted range (min > max) from a malformed schema is UB for
            // std::clamp; skip the clamp rather than invoke UB.
            coerced = (minVal <= maxVal) ? std::clamp(num, minVal, maxVal) : num;
        }
    } else if (defType == ParamTypes::Bool) {
        coerced = value.toBool();
    } else if (defType == ParamTypes::Enum) {
        const QString str = value.toString();
        const QStringList options = defMap.value(ParamDefKeys::EnumOptions).toStringList();
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
    // Persist only the key the user actually set. A missing splitRatio /
    // masterCount is defaulted by perAlgoFromVariantMap on read, so injecting
    // them here would just bake a redundant (and potentially-stale) per-algorithm
    // override into the entry for a value the user never touched.

    perAlgo[algorithmId] = algoEntry;
    m_settings->setAutotilePerAlgorithmSettings(perAlgo);
    Q_EMIT customParamChanged(algorithmId, paramName);
    Q_EMIT changed();
}

} // namespace PlasmaZones
