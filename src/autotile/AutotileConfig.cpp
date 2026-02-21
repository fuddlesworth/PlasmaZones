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

AutotileConfig::InsertPosition stringToInsertPosition(const QString &str)
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

bool AutotileConfig::operator==(const AutotileConfig &other) const
{
    // Use qFuzzyCompare properly (add 1.0 for values that could be near zero)
    return algorithmId == other.algorithmId
        && qFuzzyCompare(1.0 + splitRatio, 1.0 + other.splitRatio)
        && masterCount == other.masterCount
        && innerGap == other.innerGap
        && outerGap == other.outerGap
        && insertPosition == other.insertPosition
        && focusFollowsMouse == other.focusFollowsMouse
        && focusNewWindows == other.focusNewWindows
        && monocleHideOthers == other.monocleHideOthers
        && monocleShowTabs == other.monocleShowTabs
        && smartGaps == other.smartGaps
        && respectMinimumSize == other.respectMinimumSize;
}

bool AutotileConfig::operator!=(const AutotileConfig &other) const
{
    return !(*this == other);
}

QJsonObject AutotileConfig::toJson() const
{
    QJsonObject json;
    json[AlgorithmId] = algorithmId;
    json[SplitRatio] = splitRatio;
    json[MasterCount] = masterCount;
    json[InnerGap] = innerGap;
    json[OuterGap] = outerGap;
    json[AutotileJsonKeys::InsertPosition] = insertPositionToString(insertPosition);
    json[FocusFollowsMouse] = focusFollowsMouse;
    json[FocusNewWindows] = focusNewWindows;
    json[MonocleHideOthers] = monocleHideOthers;
    json[MonocleShowTabs] = monocleShowTabs;
    json[SmartGaps] = smartGaps;
    json[RespectMinimumSize] = respectMinimumSize;
    return json;
}

AutotileConfig AutotileConfig::fromJson(const QJsonObject &json)
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
    if (json.contains(InnerGap)) {
        config.innerGap = json[InnerGap].toInt(config.innerGap);
        config.innerGap = std::clamp(config.innerGap, MinGap, MaxGap);
    }
    if (json.contains(OuterGap)) {
        config.outerGap = json[OuterGap].toInt(config.outerGap);
        config.outerGap = std::clamp(config.outerGap, MinGap, MaxGap);
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
    if (json.contains(MonocleHideOthers)) {
        config.monocleHideOthers = json[MonocleHideOthers].toBool(config.monocleHideOthers);
    }
    if (json.contains(MonocleShowTabs)) {
        config.monocleShowTabs = json[MonocleShowTabs].toBool(config.monocleShowTabs);
    }
    if (json.contains(SmartGaps)) {
        config.smartGaps = json[SmartGaps].toBool(config.smartGaps);
    }
    if (json.contains(RespectMinimumSize)) {
        config.respectMinimumSize = json[RespectMinimumSize].toBool(config.respectMinimumSize);
    }
    return config;
}

AutotileConfig AutotileConfig::defaults()
{
    return AutotileConfig();
}

} // namespace PlasmaZones