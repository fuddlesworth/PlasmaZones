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
        && showActiveBorder == other.showActiveBorder
        && activeBorderWidth == other.activeBorderWidth
        && activeBorderColor == other.activeBorderColor
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
    json[ShowActiveBorder] = showActiveBorder;
    json[ActiveBorderWidth] = activeBorderWidth;
    json[ActiveBorderColor] = activeBorderColor.name(QColor::HexArgb);
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
    if (json.contains(ShowActiveBorder)) {
        config.showActiveBorder = json[ShowActiveBorder].toBool(config.showActiveBorder);
    }
    if (json.contains(ActiveBorderWidth)) {
        config.activeBorderWidth = json[ActiveBorderWidth].toInt(config.activeBorderWidth);
        config.activeBorderWidth = std::clamp(config.activeBorderWidth, MinBorderWidth, MaxBorderWidth);
    }
    if (json.contains(ActiveBorderColor)) {
        const QString colorStr = json[ActiveBorderColor].toString();
        if (!colorStr.isEmpty()) {
            config.activeBorderColor = QColor(colorStr);
            if (!config.activeBorderColor.isValid()) {
                config.activeBorderColor = AutotileConfig::systemHighlightColor();
            }
        }
    } else {
        // No color specified in JSON, use system default
        config.activeBorderColor = AutotileConfig::systemHighlightColor();
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
    AutotileConfig config;
    // Set system highlight color (member default is invalid/empty)
    config.activeBorderColor = systemHighlightColor();
    return config;
}

QColor AutotileConfig::systemHighlightColor()
{
    // Use KColorScheme to get the system highlight/selection color
    // This respects user's color scheme (Breeze, Breeze Dark, custom themes)
    KColorScheme scheme(QPalette::Active, KColorScheme::Selection);
    return scheme.background(KColorScheme::ActiveBackground).color();
}

} // namespace PlasmaZones