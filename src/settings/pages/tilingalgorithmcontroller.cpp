// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingalgorithmcontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"

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
    return ConfigDefaults::outerGapMin();
}

int TilingAlgorithmController::autotileGapMax() const
{
    return ConfigDefaults::outerGapMax();
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

QVariantMap TilingAlgorithmController::algorithmSettingsFor(const QString& algorithmId) const
{
    // Seed with the algorithm's declared defaults so an algorithm with no
    // saved entry still shows its own sensible values (e.g. BSP's default
    // max windows, not a global constant). Clamp the algorithm-derived
    // defaults: a scripted (Luau) algorithm's metadata is user-authored and may
    // declare an out-of-range value.
    qreal splitRatio = PhosphorTiles::AutotileDefaults::DefaultSplitRatio;
    int masterCount = PhosphorTiles::AutotileDefaults::DefaultMasterCount;
    int maxWindows = PhosphorTiles::AutotileDefaults::DefaultMaxWindows;
    if (PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(algorithmId)) {
        splitRatio = std::clamp(algo->defaultSplitRatio(), ConfigDefaults::autotileSplitRatioMin(),
                                ConfigDefaults::autotileSplitRatioMax());
        maxWindows = std::clamp(algo->defaultMaxWindows(), ConfigDefaults::autotileMaxWindowsMin(),
                                ConfigDefaults::autotileMaxWindowsMax());
    }

    // Override with saved values. Read each via toDouble(&ok): it yields
    // ok=true for both int- and double-typed QVariants (the JSON backend may
    // return either) and ok=false for missing or non-numeric entries, in which
    // case the seeded default is kept rather than collapsing to 0 (mirrors
    // customParamsForAlgorithm's `ok` guard). Clamp each saved value in the
    // double domain BEFORE narrowing to int — a hand-edited out-of-int-range
    // value would otherwise hit undefined behaviour in the double→int cast. The
    // seeded defaults are already in range, so clamping only the saved value is
    // sufficient.
    const QVariantMap entry = m_settings->autotilePerAlgorithmSettings().value(algorithmId).toMap();
    bool ok = false;
    const qreal srSaved = entry.value(PhosphorTiles::AutotileJsonKeys::SplitRatio).toDouble(&ok);
    if (ok)
        splitRatio =
            std::clamp(srSaved, ConfigDefaults::autotileSplitRatioMin(), ConfigDefaults::autotileSplitRatioMax());
    const qreal mcSaved = entry.value(PhosphorTiles::AutotileJsonKeys::MasterCount).toDouble(&ok);
    if (ok)
        masterCount = qRound(std::clamp(mcSaved, qreal(ConfigDefaults::autotileMasterCountMin()),
                                        qreal(ConfigDefaults::autotileMasterCountMax())));
    const qreal mwSaved = entry.value(PhosphorTiles::AutotileJsonKeys::MaxWindows).toDouble(&ok);
    if (ok)
        maxWindows = qRound(std::clamp(mwSaved, qreal(ConfigDefaults::autotileMaxWindowsMin()),
                                       qreal(ConfigDefaults::autotileMaxWindowsMax())));

    QVariantMap result;
    result[PhosphorTiles::AutotileJsonKeys::SplitRatio] = splitRatio;
    result[PhosphorTiles::AutotileJsonKeys::MasterCount] = masterCount;
    result[PhosphorTiles::AutotileJsonKeys::MaxWindows] = maxWindows;
    return result;
}

bool TilingAlgorithmController::fieldMatchesAlgorithmDefault(const QString& algorithmId, QLatin1String key,
                                                             const QVariant& value) const
{
    // Mirror the default derivation in algorithmSettingsFor() exactly (clamped
    // algorithm defaults, DefaultMasterCount for master count) so "reset to
    // default" here means the same value the page would show when no slot exists.
    PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(algorithmId);
    if (key == PhosphorTiles::AutotileJsonKeys::MaxWindows) {
        const int def = algo ? std::clamp(algo->defaultMaxWindows(), ConfigDefaults::autotileMaxWindowsMin(),
                                          ConfigDefaults::autotileMaxWindowsMax())
                             : PhosphorTiles::AutotileDefaults::DefaultMaxWindows;
        return value.toInt() == def;
    }
    if (key == PhosphorTiles::AutotileJsonKeys::MasterCount) {
        return value.toInt() == PhosphorTiles::AutotileDefaults::DefaultMasterCount;
    }
    if (key == PhosphorTiles::AutotileJsonKeys::SplitRatio) {
        const qreal def = algo ? std::clamp(algo->defaultSplitRatio(), ConfigDefaults::autotileSplitRatioMin(),
                                            ConfigDefaults::autotileSplitRatioMax())
                               : PhosphorTiles::AutotileDefaults::DefaultSplitRatio;
        return qFuzzyCompare(1.0 + value.toDouble(), 1.0 + def);
    }
    return false;
}

bool TilingAlgorithmController::writeAlgorithmField(const QString& algorithmId, QLatin1String key,
                                                    const QVariant& value)
{
    if (algorithmId.isEmpty())
        return false;
    QVariantMap perAlgo = m_settings->autotilePerAlgorithmSettings();
    QVariantMap entry = perAlgo.value(algorithmId).toMap();

    if (fieldMatchesAlgorithmDefault(algorithmId, key, value)) {
        // Setting a field back to the algorithm's own default is not a
        // customization: persisting it would leave a slot the profile diff
        // reports as a change the user never made. Drop the field instead, and
        // prune the whole entry when nothing customized (including customParams)
        // remains. A field that was never stored means there is nothing to undo.
        if (!entry.contains(key))
            return false;
        entry.remove(key);
    } else {
        if (entry.contains(key) && entry.value(key) == value)
            return false;
        entry[key] = value;
    }

    if (entry.isEmpty())
        perAlgo.remove(algorithmId);
    else
        perAlgo[algorithmId] = entry;
    m_settings->setAutotilePerAlgorithmSettings(perAlgo);
    return true;
}

void TilingAlgorithmController::setAlgorithmSplitRatio(const QString& algorithmId, qreal value)
{
    const qreal clamped =
        std::clamp(value, ConfigDefaults::autotileSplitRatioMin(), ConfigDefaults::autotileSplitRatioMax());
    if (writeAlgorithmField(algorithmId, PhosphorTiles::AutotileJsonKeys::SplitRatio, clamped)) {
        Q_EMIT changed();
    }
}

void TilingAlgorithmController::setAlgorithmMasterCount(const QString& algorithmId, int value)
{
    const int clamped =
        std::clamp(value, ConfigDefaults::autotileMasterCountMin(), ConfigDefaults::autotileMasterCountMax());
    if (writeAlgorithmField(algorithmId, PhosphorTiles::AutotileJsonKeys::MasterCount, clamped)) {
        Q_EMIT changed();
    }
}

void TilingAlgorithmController::setAlgorithmMaxWindows(const QString& algorithmId, int value)
{
    const int clamped =
        std::clamp(value, ConfigDefaults::autotileMaxWindowsMin(), ConfigDefaults::autotileMaxWindowsMax());
    if (writeAlgorithmField(algorithmId, PhosphorTiles::AutotileJsonKeys::MaxWindows, clamped)) {
        Q_EMIT changed();
    }
}

} // namespace PlasmaZones
