// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorTiles/ScriptedAlgorithmHelpers.h>
#include <PhosphorTiles/AutotileConstants.h>
#include "tileslogging.h"
#include <QJSValue>
#include <QRegularExpression>
#include <QStringView>
#include <QVariantMap>
#include <algorithm>

namespace PlasmaZones {
namespace ScriptedHelpers {

QVector<QRect> jsArrayToRects(const QJSValue& result, const QString& scriptId, int maxZones)
{
    QVector<QRect> rects;
    const int length = result.property(QStringLiteral("length")).toInt();
    if (length <= 0)
        return rects;
    const int effectiveLength = std::min(length, maxZones);
    if (length > maxZones)
        qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: script returned" << length << "zones, truncating to"
                                             << maxZones << "script=" << scriptId;
    rects.reserve(effectiveLength);

    for (int i = 0; i < effectiveLength; ++i) {
        const QJSValue elem = result.property(static_cast<quint32>(i));
        // Validate that each element is an object before extracting properties
        if (!elem.isObject()) {
            qCWarning(PhosphorTiles::lcTilesLib) << "Skipping non-object zone element at index" << i;
            continue;
        }
        // Skip zones with missing width/height (e.g. script returned partial objects)
        const QJSValue wProp = elem.property(QStringLiteral("width"));
        const QJSValue hProp = elem.property(QStringLiteral("height"));
        if (wProp.isUndefined() || hProp.isUndefined()) {
            qCWarning(PhosphorTiles::lcTilesLib)
                << "Skipping zone with missing width/height at index" << i << "script=" << scriptId;
            continue;
        }
        // Clamp x and y to non-negative to prevent off-screen zones
        const int x = std::max(0, elem.property(QStringLiteral("x")).toInt());
        const int y = std::max(0, elem.property(QStringLiteral("y")).toInt());
        int w = wProp.toInt();
        int h = hProp.toInt();

        // Validate: non-negative dimensions, clamp to at least 1
        w = std::max(1, w);
        h = std::max(1, h);

        rects.append(QRect(x, y, w, h));
    }

    return rects;
}

QVector<QRect> clampZonesToArea(const QVector<QRect>& zones, const QRect& area, const QString& scriptId)
{
    QVector<QRect> clamped;
    clamped.reserve(zones.size());
    for (int i = 0; i < zones.size(); ++i) {
        const QRect& zone = zones[i];
        const QRect bounded = zone.intersected(area);
        if (bounded.isEmpty()) {
            qCWarning(PhosphorTiles::lcTilesLib)
                << "ScriptedAlgorithm: zone" << i << "outside area, using full area as fallback"
                << "zone=" << zone << "area=" << area << "script=" << scriptId;
            clamped.append(area);
            continue;
        }
        clamped.append(bounded);
    }
    return clamped;
}

ScriptMetadata parseMetadata(const QString& source, const QString& filePath)
{
    using namespace AutotileDefaults;
    static const QRegularExpression metaRe(QStringLiteral(R"(^\s*// @(\w+)\s+(.+)$)"));
    static constexpr int MaxMetadataLines = 50;

    ScriptMetadata meta;
    int lineCount = 0;
    const auto lines = QStringView(source).split(QLatin1Char('\n'));

    for (const auto& lineView : lines) {
        if (lineCount >= MaxMetadataLines)
            break;
        ++lineCount;
        const QString line = lineView.toString();

        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1String("//")) && !trimmed.startsWith(QLatin1String("/*"))
            && !trimmed.startsWith(QLatin1String("*"))) {
            break;
        }

        const QRegularExpressionMatch match = metaRe.match(line);
        if (!match.hasMatch()) {
            continue;
        }

        const QString key = match.captured(1);
        const QString value = match.captured(2).trimmed();

        if (key == QLatin1String("name")) {
            // Sanitize metadata — length cap + HTML escape
            meta.name = value.left(100).toHtmlEscaped();
        } else if (key == QLatin1String("description")) {
            meta.description = value.left(500).toHtmlEscaped();
        } else if (key == QLatin1String("supportsMasterCount")) {
            meta.supportsMasterCount = (value == QLatin1String("true"));
        } else if (key == QLatin1String("supportsSplitRatio")) {
            meta.supportsSplitRatio = (value == QLatin1String("true"));
        } else if (key == QLatin1String("producesOverlappingZones")) {
            meta.producesOverlappingZones = (value == QLatin1String("true"));
        } else if (key == QLatin1String("supportsMemory")) {
            meta.supportsMemory = (value == QLatin1String("true"));
        } else if (key == QLatin1String("centerLayout")) {
            meta.centerLayout = (value == QLatin1String("true"));
        } else if (key == QLatin1String("supportsMinSizes")) {
            meta.supportsMinSizes = (value == QLatin1String("true"));
        } else if (key == QLatin1String("defaultSplitRatio")) {
            bool ok = false;
            const qreal v = value.toDouble(&ok);
            if (ok)
                meta.defaultSplitRatio = std::clamp(v, MinSplitRatio, MaxSplitRatio);
        } else if (key == QLatin1String("defaultMaxWindows")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok)
                meta.defaultMaxWindows = std::clamp(v, MinMetadataWindows, MaxMetadataWindows);
        } else if (key == QLatin1String("minimumWindows")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok)
                meta.minimumWindows = std::clamp(v, MinMetadataWindows, MaxMetadataWindows);
        } else if (key == QLatin1String("masterZoneIndex")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok)
                meta.masterZoneIndex = std::clamp(v, -1, MaxZones - 1);
        } else if (key == QLatin1String("zoneNumberDisplay")) {
            if (value == QLatin1String("all") || value == QLatin1String("last") || value == QLatin1String("first")
                || value == QLatin1String("firstAndLast") || value == QLatin1String("none")) {
                meta.zoneNumberDisplay = value;
            }
        } else if (key == QLatin1String("builtinId")) {
            static const QRegularExpression builtinIdRe(QStringLiteral("^[a-z][a-z0-9-]*$"));
            if (value.startsWith(QLatin1String("script:"))) {
                qCWarning(PhosphorTiles::lcTilesLib)
                    << "ScriptedAlgorithm::parseMetadata: @builtinId must not start with 'script:'"
                    << "value=" << value << "in" << filePath;
            } else if (!builtinIdRe.match(value).hasMatch()) {
                qCWarning(PhosphorTiles::lcTilesLib)
                    << "ScriptedAlgorithm::parseMetadata: invalid @builtinId" << value << "in" << filePath;
            } else {
                meta.builtinId = value.left(64);
            }
        } else if (key == QLatin1String("param")) {
            // Format: // @param name type [typeArgs...] "description"
            // number: // @param name number default min max "description"
            // bool:   // @param name bool default "description"
            // enum:   // @param name enum "default" ["opt1","opt2"] "description"
            static const QRegularExpression paramNumberRe(QStringLiteral(
                R"RX(^(\w+)\s+number\s+([+-]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s+([+-]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s+([+-]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s+"([^"]*)")RX"));
            static const QRegularExpression paramBoolRe(
                QStringLiteral(R"RX(^(\w+)\s+bool\s+(true|false)\s+"([^"]*)")RX"));
            static const QRegularExpression paramEnumRe(
                QStringLiteral(R"RX(^(\w+)\s+enum\s+"([^"]*)"\s+\[([^\]]*)\]\s+"([^"]*)")RX"));

            CustomParamDef param;
            QRegularExpressionMatch pm = paramNumberRe.match(value);
            if (pm.hasMatch()) {
                param.name = pm.captured(1).left(64);
                param.type = QStringLiteral("number");
                param.defaultValue = pm.captured(2).toDouble();
                param.minValue = pm.captured(3).toDouble();
                param.maxValue = pm.captured(4).toDouble();
                param.description = pm.captured(5).left(200).toHtmlEscaped();
                if (param.minValue > param.maxValue) {
                    qCWarning(PhosphorTiles::lcTilesLib)
                        << "ScriptedAlgorithm::parseMetadata: @param number min" << param.minValue << "> max"
                        << param.maxValue << "for" << param.name << "in" << filePath;
                    std::swap(param.minValue, param.maxValue);
                }
                meta.customParams.append(param);
            } else if ((pm = paramBoolRe.match(value)).hasMatch()) {
                param.name = pm.captured(1).left(64);
                param.type = QStringLiteral("bool");
                param.defaultValue = (pm.captured(2) == QLatin1String("true"));
                param.description = pm.captured(3).left(200).toHtmlEscaped();
                meta.customParams.append(param);
            } else if ((pm = paramEnumRe.match(value)).hasMatch()) {
                param.name = pm.captured(1).left(64);
                param.type = QStringLiteral("enum");
                param.defaultValue = pm.captured(2);
                // Parse ["opt1","opt2"] — split on comma, strip quotes
                const QString optionsStr = pm.captured(3);
                const auto optParts = QStringView(optionsStr).split(QLatin1Char(','));
                for (const auto& opt : optParts) {
                    QString trimmed = opt.trimmed().toString();
                    if (trimmed.startsWith(QLatin1Char('"')) && trimmed.endsWith(QLatin1Char('"'))) {
                        trimmed = trimmed.mid(1, trimmed.size() - 2);
                    }
                    if (!trimmed.isEmpty()) {
                        param.enumOptions.append(trimmed.left(64));
                    }
                }
                param.description = pm.captured(4).left(200).toHtmlEscaped();
                if (param.enumOptions.isEmpty()) {
                    qCWarning(PhosphorTiles::lcTilesLib)
                        << "ScriptedAlgorithm::parseMetadata: @param enum has no valid options for" << param.name
                        << "in" << filePath;
                } else {
                    if (!param.enumOptions.contains(param.defaultValue.toString())) {
                        qCWarning(PhosphorTiles::lcTilesLib)
                            << "ScriptedAlgorithm::parseMetadata: @param enum default" << param.defaultValue.toString()
                            << "not in options list for" << param.name << "in" << filePath
                            << "— falling back to first option";
                        param.defaultValue = param.enumOptions.first();
                    }
                    meta.customParams.append(param);
                }
            } else {
                qCWarning(PhosphorTiles::lcTilesLib)
                    << "ScriptedAlgorithm::parseMetadata: malformed @param" << value << "in" << filePath;
            }
        } else if (key != QLatin1String("icon")) {
            qCDebug(PhosphorTiles::lcTilesLib)
                << "ScriptedAlgorithm::parseMetadata: unknown metadata key" << key << "in" << filePath;
        }
    }
    return meta;
}

} // namespace ScriptedHelpers
} // namespace PlasmaZones
