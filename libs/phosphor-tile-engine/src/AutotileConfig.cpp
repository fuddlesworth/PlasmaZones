// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorEngineApi/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include "tileenginelogging.h"
#include <QRegularExpression>
#include <QtMath>

namespace PlasmaZones {

// Use shared JSON keys and defaults from constants.h
using namespace PhosphorTiles::AutotileJsonKeys;
using namespace PhosphorTiles::AutotileDefaults;

namespace {
// Helper functions for InsertPosition serialization
QString insertPositionToString(AutotileConfig::InsertPosition pos)
{
    switch (pos) {
    case AutotileConfig::InsertPosition::AfterFocused:
        return InsertAfterFocused;
    case AutotileConfig::InsertPosition::AsMaster:
        return InsertAsMaster;
    case AutotileConfig::InsertPosition::End:
    default:
        return InsertEnd;
    }
}

AutotileConfig::InsertPosition stringToInsertPosition(const QString& str)
{
    if (str == InsertAfterFocused) {
        return AutotileConfig::InsertPosition::AfterFocused;
    }
    if (str == InsertAsMaster) {
        return AutotileConfig::InsertPosition::AsMaster;
    }
    return AutotileConfig::InsertPosition::End;
}

QString overflowBehaviorToString(PhosphorTiles::AutotileOverflowBehavior behavior)
{
    switch (behavior) {
    case PhosphorTiles::AutotileOverflowBehavior::Unlimited:
        return OverflowUnlimited;
    case PhosphorTiles::AutotileOverflowBehavior::Float:
    default:
        return OverflowFloat;
    }
}

PhosphorTiles::AutotileOverflowBehavior stringToOverflowBehavior(const QString& str)
{
    if (str == OverflowUnlimited) {
        return PhosphorTiles::AutotileOverflowBehavior::Unlimited;
    }
    return PhosphorTiles::AutotileOverflowBehavior::Float;
}
} // anonymous namespace

QHash<QString, AlgorithmSettings> AutotileConfig::perAlgoFromVariantMap(const QVariantMap& map)
{
    static const QRegularExpression validAlgoId(QStringLiteral("^[a-zA-Z0-9_:/-]+$"));
    static constexpr int MaxEntries = 100;

    QHash<QString, AlgorithmSettings> result;
    int count = 0;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it, ++count) {
        if (count >= MaxEntries) {
            qCWarning(PhosphorTileEngine::lcTileEngine)
                << "perAlgoFromVariantMap: max entries reached (" << MaxEntries << "), dropping remaining"
                << (map.size() - MaxEntries) << "entries";
            break;
        }
        if (!validAlgoId.match(it.key()).hasMatch())
            continue;
        const QVariantMap entry = it.value().toMap();
        const QVariant ratioVar = entry.value(PhosphorTiles::AutotileJsonKeys::SplitRatio);
        const qreal ratio =
            std::clamp(ratioVar.isValid() ? ratioVar.toDouble() : PhosphorTiles::AutotileDefaults::DefaultSplitRatio,
                       MinSplitRatio, MaxSplitRatio);
        const QVariant mcVar = entry.value(PhosphorTiles::AutotileJsonKeys::MasterCount);
        const int masterCount =
            std::clamp(mcVar.isValid() ? mcVar.toInt() : PhosphorTiles::AutotileDefaults::DefaultMasterCount,
                       MinMasterCount, MaxMasterCount);
        AlgorithmSettings settings{ratio, masterCount, {}};
        // Load custom params if present
        const QVariant customVar = entry.value(PhosphorTiles::AutotileJsonKeys::CustomParams);
        if (customVar.isValid() && customVar.typeId() == QMetaType::QVariantMap) {
            settings.customParams = customVar.toMap();
        }
        result[it.key()] = settings;
    }
    return result;
}

QVariantMap AutotileConfig::perAlgoToVariantMap(const QHash<QString, AlgorithmSettings>& hash)
{
    QVariantMap result;
    for (auto it = hash.constBegin(); it != hash.constEnd(); ++it) {
        QVariantMap entry;
        entry[PhosphorTiles::AutotileJsonKeys::SplitRatio] = it.value().splitRatio;
        entry[PhosphorTiles::AutotileJsonKeys::MasterCount] = it.value().masterCount;
        if (!it.value().customParams.isEmpty()) {
            entry[PhosphorTiles::AutotileJsonKeys::CustomParams] = it.value().customParams;
        }
        result[it.key()] = entry;
    }
    return result;
}

bool AutotileConfig::operator==(const AutotileConfig& other) const
{
    // Use qFuzzyCompare properly (add 1.0 for values that could be near zero)
    return algorithmId == other.algorithmId && qFuzzyCompare(1.0 + splitRatio, 1.0 + other.splitRatio)
        && qFuzzyCompare(1.0 + splitRatioStep, 1.0 + other.splitRatioStep) && masterCount == other.masterCount
        && savedAlgorithmSettings == other.savedAlgorithmSettings && innerGap == other.innerGap
        && outerGap == other.outerGap && usePerSideOuterGap == other.usePerSideOuterGap
        && outerGapTop == other.outerGapTop && outerGapBottom == other.outerGapBottom
        && outerGapLeft == other.outerGapLeft && outerGapRight == other.outerGapRight
        && insertPosition == other.insertPosition && focusFollowsMouse == other.focusFollowsMouse
        && focusNewWindows == other.focusNewWindows && smartGaps == other.smartGaps
        && respectMinimumSize == other.respectMinimumSize && maxWindows == other.maxWindows
        && overflowBehavior == other.overflowBehavior;
}

bool AutotileConfig::operator!=(const AutotileConfig& other) const
{
    return !(*this == other);
}

QJsonObject AutotileConfig::toJson() const
{
    QJsonObject json;
    json[AlgorithmId] = algorithmId;
    json[SplitRatio] = splitRatio;
    json[SplitRatioStep] = splitRatioStep;
    json[MasterCount] = masterCount;
    if (!savedAlgorithmSettings.isEmpty()) {
        const auto varMap = perAlgoToVariantMap(savedAlgorithmSettings);
        json[PerAlgorithmSettings] = QJsonObject::fromVariantMap(varMap);
    }
    json[InnerGap] = innerGap;
    json[OuterGap] = outerGap;
    json[PhosphorTiles::AutotileJsonKeys::UsePerSideOuterGap] = usePerSideOuterGap;
    json[PhosphorTiles::AutotileJsonKeys::OuterGapTop] = outerGapTop;
    json[PhosphorTiles::AutotileJsonKeys::OuterGapBottom] = outerGapBottom;
    json[PhosphorTiles::AutotileJsonKeys::OuterGapLeft] = outerGapLeft;
    json[PhosphorTiles::AutotileJsonKeys::OuterGapRight] = outerGapRight;
    json[PhosphorTiles::AutotileJsonKeys::InsertPosition] = insertPositionToString(insertPosition);
    json[FocusFollowsMouse] = focusFollowsMouse;
    json[FocusNewWindows] = focusNewWindows;
    json[SmartGaps] = smartGaps;
    json[RespectMinimumSize] = respectMinimumSize;
    json[MaxWindows] = maxWindows;
    json[OverflowBehavior] = overflowBehaviorToString(overflowBehavior);
    return json;
}

AutotileConfig AutotileConfig::fromJson(const QJsonObject& json)
{
    AutotileConfig config;

    if (json.contains(AlgorithmId)) {
        config.algorithmId = json[AlgorithmId].toString(config.algorithmId);
    }
    if (json.contains(SplitRatio)) {
        config.splitRatio = json[SplitRatio].toDouble(config.splitRatio);
        config.splitRatio = std::clamp(config.splitRatio, MinSplitRatio, MaxSplitRatio);
    }
    if (json.contains(SplitRatioStep)) {
        config.splitRatioStep = json[SplitRatioStep].toDouble(config.splitRatioStep);
        config.splitRatioStep = std::clamp(config.splitRatioStep, PhosphorTiles::AutotileDefaults::MinSplitRatioStep,
                                           PhosphorTiles::AutotileDefaults::MaxSplitRatioStep);
    }
    if (json.contains(MasterCount)) {
        config.masterCount = json[MasterCount].toInt(config.masterCount);
        config.masterCount = std::clamp(config.masterCount, MinMasterCount, MaxMasterCount);
    }
    if (json.contains(PerAlgorithmSettings)) {
        const QJsonObject perAlgo = json[PerAlgorithmSettings].toObject();
        config.savedAlgorithmSettings = perAlgoFromVariantMap(perAlgo.toVariantMap());
    }
    if (json.contains(InnerGap)) {
        config.innerGap = json[InnerGap].toInt(config.innerGap);
        config.innerGap = std::clamp(config.innerGap, MinGap, MaxGap);
    }
    if (json.contains(OuterGap)) {
        config.outerGap = json[OuterGap].toInt(config.outerGap);
        config.outerGap = std::clamp(config.outerGap, MinGap, MaxGap);
    }
    if (json.contains(PhosphorTiles::AutotileJsonKeys::UsePerSideOuterGap)) {
        config.usePerSideOuterGap =
            json[PhosphorTiles::AutotileJsonKeys::UsePerSideOuterGap].toBool(config.usePerSideOuterGap);
    }
    if (json.contains(PhosphorTiles::AutotileJsonKeys::OuterGapTop)) {
        config.outerGapTop =
            std::clamp(json[PhosphorTiles::AutotileJsonKeys::OuterGapTop].toInt(config.outerGapTop), MinGap, MaxGap);
    }
    if (json.contains(PhosphorTiles::AutotileJsonKeys::OuterGapBottom)) {
        config.outerGapBottom = std::clamp(
            json[PhosphorTiles::AutotileJsonKeys::OuterGapBottom].toInt(config.outerGapBottom), MinGap, MaxGap);
    }
    if (json.contains(PhosphorTiles::AutotileJsonKeys::OuterGapLeft)) {
        config.outerGapLeft =
            std::clamp(json[PhosphorTiles::AutotileJsonKeys::OuterGapLeft].toInt(config.outerGapLeft), MinGap, MaxGap);
    }
    if (json.contains(PhosphorTiles::AutotileJsonKeys::OuterGapRight)) {
        config.outerGapRight = std::clamp(
            json[PhosphorTiles::AutotileJsonKeys::OuterGapRight].toInt(config.outerGapRight), MinGap, MaxGap);
    }
    if (json.contains(PhosphorTiles::AutotileJsonKeys::InsertPosition)) {
        config.insertPosition =
            stringToInsertPosition(json[PhosphorTiles::AutotileJsonKeys::InsertPosition].toString());
    }
    if (json.contains(FocusFollowsMouse)) {
        config.focusFollowsMouse = json[FocusFollowsMouse].toBool(config.focusFollowsMouse);
    }
    if (json.contains(FocusNewWindows)) {
        config.focusNewWindows = json[FocusNewWindows].toBool(config.focusNewWindows);
    }
    if (json.contains(SmartGaps)) {
        config.smartGaps = json[SmartGaps].toBool(config.smartGaps);
    }
    if (json.contains(RespectMinimumSize)) {
        config.respectMinimumSize = json[RespectMinimumSize].toBool(config.respectMinimumSize);
    }
    if (json.contains(MaxWindows)) {
        config.maxWindows = json[MaxWindows].toInt(config.maxWindows);
        config.maxWindows = std::clamp(config.maxWindows, MinMaxWindows, MaxMaxWindows);
    }
    if (json.contains(OverflowBehavior)) {
        config.overflowBehavior = stringToOverflowBehavior(json[OverflowBehavior].toString());
    }
    return config;
}

AutotileConfig AutotileConfig::defaults()
{
    return AutotileConfig();
}

} // namespace PlasmaZones