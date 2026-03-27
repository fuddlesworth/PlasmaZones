// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AutotileConfig.h"
#include "core/constants.h"
#include <QtMath>

namespace PlasmaZones {

// Use shared JSON keys and defaults from constants.h
using namespace AutotileJsonKeys;
using namespace AutotileDefaults;

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
} // anonymous namespace

QHash<QString, QPair<qreal, int>> AutotileConfig::perAlgoFromVariantMap(const QVariantMap& map)
{
    QHash<QString, QPair<qreal, int>> result;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        const QVariantMap entry = it.value().toMap();
        qreal ratio = std::clamp(entry.value(QLatin1String("splitRatio")).toDouble(), MinSplitRatio, MaxSplitRatio);
        int count = std::clamp(entry.value(QLatin1String("masterCount")).toInt(), MinMasterCount, MaxMasterCount);
        result[it.key()] = {ratio, count};
    }
    return result;
}

QVariantMap AutotileConfig::perAlgoToVariantMap(const QHash<QString, QPair<qreal, int>>& hash)
{
    QVariantMap result;
    for (auto it = hash.constBegin(); it != hash.constEnd(); ++it) {
        QVariantMap entry;
        entry[QLatin1String("splitRatio")] = it.value().first;
        entry[QLatin1String("masterCount")] = it.value().second;
        result[it.key()] = entry;
    }
    return result;
}

bool AutotileConfig::operator==(const AutotileConfig& other) const
{
    // Use qFuzzyCompare properly (add 1.0 for values that could be near zero)
    return algorithmId == other.algorithmId && qFuzzyCompare(1.0 + splitRatio, 1.0 + other.splitRatio)
        && masterCount == other.masterCount && savedAlgorithmSettings == other.savedAlgorithmSettings
        && innerGap == other.innerGap && outerGap == other.outerGap && usePerSideOuterGap == other.usePerSideOuterGap
        && outerGapTop == other.outerGapTop && outerGapBottom == other.outerGapBottom
        && outerGapLeft == other.outerGapLeft && outerGapRight == other.outerGapRight
        && insertPosition == other.insertPosition && focusFollowsMouse == other.focusFollowsMouse
        && focusNewWindows == other.focusNewWindows && smartGaps == other.smartGaps
        && respectMinimumSize == other.respectMinimumSize && maxWindows == other.maxWindows;
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
    json[MasterCount] = masterCount;
    if (!savedAlgorithmSettings.isEmpty()) {
        QJsonObject perAlgo;
        for (auto it = savedAlgorithmSettings.constBegin(); it != savedAlgorithmSettings.constEnd(); ++it) {
            QJsonObject entry;
            entry[QLatin1String("splitRatio")] = it.value().first;
            entry[QLatin1String("masterCount")] = it.value().second;
            perAlgo[it.key()] = entry;
        }
        json[QLatin1String("PerAlgorithmSettings")] = perAlgo;
    }
    json[InnerGap] = innerGap;
    json[OuterGap] = outerGap;
    json[AutotileJsonKeys::UsePerSideOuterGap] = usePerSideOuterGap;
    json[AutotileJsonKeys::OuterGapTop] = outerGapTop;
    json[AutotileJsonKeys::OuterGapBottom] = outerGapBottom;
    json[AutotileJsonKeys::OuterGapLeft] = outerGapLeft;
    json[AutotileJsonKeys::OuterGapRight] = outerGapRight;
    json[AutotileJsonKeys::InsertPosition] = insertPositionToString(insertPosition);
    json[FocusFollowsMouse] = focusFollowsMouse;
    json[FocusNewWindows] = focusNewWindows;
    json[SmartGaps] = smartGaps;
    json[RespectMinimumSize] = respectMinimumSize;
    json[MaxWindows] = maxWindows;
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
    if (json.contains(MasterCount)) {
        config.masterCount = json[MasterCount].toInt(config.masterCount);
        config.masterCount = std::clamp(config.masterCount, MinMasterCount, MaxMasterCount);
    }
    if (json.contains(QLatin1String("PerAlgorithmSettings"))) {
        const QJsonObject perAlgo = json[QLatin1String("PerAlgorithmSettings")].toObject();
        for (auto it = perAlgo.constBegin(); it != perAlgo.constEnd(); ++it) {
            const QJsonObject entry = it.value().toObject();
            qreal ratio = std::clamp(entry[QLatin1String("splitRatio")].toDouble(0.5), MinSplitRatio, MaxSplitRatio);
            int count = std::clamp(entry[QLatin1String("masterCount")].toInt(1), MinMasterCount, MaxMasterCount);
            config.savedAlgorithmSettings[it.key()] = {ratio, count};
        }
    } else {
        // Backwards compat: migrate centered-master fields
        if (json.contains(CenteredMasterSplitRatio) || json.contains(CenteredMasterMasterCount)) {
            qreal cmRatio = json[CenteredMasterSplitRatio].toDouble(0.5);
            int cmCount = json[CenteredMasterMasterCount].toInt(1);
            cmRatio = std::clamp(cmRatio, MinSplitRatio, MaxSplitRatio);
            cmCount = std::clamp(cmCount, MinMasterCount, MaxMasterCount);
            config.savedAlgorithmSettings[QStringLiteral("centered-master")] = {cmRatio, cmCount};
        }
    }
    if (json.contains(InnerGap)) {
        config.innerGap = json[InnerGap].toInt(config.innerGap);
        config.innerGap = std::clamp(config.innerGap, MinGap, MaxGap);
    }
    if (json.contains(OuterGap)) {
        config.outerGap = json[OuterGap].toInt(config.outerGap);
        config.outerGap = std::clamp(config.outerGap, MinGap, MaxGap);
    }
    if (json.contains(AutotileJsonKeys::UsePerSideOuterGap)) {
        config.usePerSideOuterGap = json[AutotileJsonKeys::UsePerSideOuterGap].toBool(config.usePerSideOuterGap);
    }
    if (json.contains(AutotileJsonKeys::OuterGapTop)) {
        config.outerGapTop = std::clamp(json[AutotileJsonKeys::OuterGapTop].toInt(config.outerGapTop), MinGap, MaxGap);
    }
    if (json.contains(AutotileJsonKeys::OuterGapBottom)) {
        config.outerGapBottom =
            std::clamp(json[AutotileJsonKeys::OuterGapBottom].toInt(config.outerGapBottom), MinGap, MaxGap);
    }
    if (json.contains(AutotileJsonKeys::OuterGapLeft)) {
        config.outerGapLeft =
            std::clamp(json[AutotileJsonKeys::OuterGapLeft].toInt(config.outerGapLeft), MinGap, MaxGap);
    }
    if (json.contains(AutotileJsonKeys::OuterGapRight)) {
        config.outerGapRight =
            std::clamp(json[AutotileJsonKeys::OuterGapRight].toInt(config.outerGapRight), MinGap, MaxGap);
    }
    if (json.contains(AutotileJsonKeys::InsertPosition)) {
        config.insertPosition = stringToInsertPosition(json[AutotileJsonKeys::InsertPosition].toString());
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
    return config;
}

AutotileConfig AutotileConfig::defaults()
{
    return AutotileConfig();
}

} // namespace PlasmaZones