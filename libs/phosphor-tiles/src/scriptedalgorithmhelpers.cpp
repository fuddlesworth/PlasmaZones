// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/ScriptedAlgorithmHelpers.h>
#include <PhosphorTiles/AutotileConstants.h>
#include "tileslogging.h"
#include <QJSValue>
#include <QRegularExpression>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <optional>

namespace PhosphorTiles {
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

QVector<CustomParamDef> parseCustomParamsFromJs(const QJSValue& jsCustomParams, const QString& filePath)
{
    QVector<CustomParamDef> result;
    if (!jsCustomParams.isArray()) {
        return result;
    }
    constexpr int MaxCustomParams = 64;
    const int len = std::min(jsCustomParams.property(QStringLiteral("length")).toInt(), MaxCustomParams);
    for (int i = 0; i < len; ++i) {
        const QJSValue entry = jsCustomParams.property(static_cast<quint32>(i));
        if (!entry.isObject()) {
            continue;
        }
        CustomParamDef def;
        def.name = entry.property(QStringLiteral("name")).toString().left(64);
        def.type = entry.property(QStringLiteral("type")).toString();
        def.description = entry.property(QStringLiteral("description")).toString().left(200);
        if (def.name.isEmpty() || def.type.isEmpty()) {
            continue;
        }
        if (def.type == QLatin1String("number")) {
            const QJSValue defVal = entry.property(QStringLiteral("default"));
            const QJSValue minVal = entry.property(QStringLiteral("min"));
            const QJSValue maxVal = entry.property(QStringLiteral("max"));
            def.minValue = (minVal.isNumber() && std::isfinite(minVal.toNumber())) ? minVal.toNumber() : 0.0;
            def.maxValue = (maxVal.isNumber() && std::isfinite(maxVal.toNumber())) ? maxVal.toNumber() : 1.0;
            if (def.minValue > def.maxValue) {
                std::swap(def.minValue, def.maxValue);
            }
            const qreal raw =
                (defVal.isNumber() && std::isfinite(defVal.toNumber())) ? defVal.toNumber() : def.minValue;
            def.defaultValue = std::clamp(raw, def.minValue, def.maxValue);
        } else if (def.type == QLatin1String("bool")) {
            def.defaultValue = entry.property(QStringLiteral("default")).toBool();
        } else if (def.type == QLatin1String("enum")) {
            const QJSValue opts = entry.property(QStringLiteral("options"));
            if (opts.isArray()) {
                constexpr int MaxEnumOptions = 256;
                const int optLen = std::min(opts.property(QStringLiteral("length")).toInt(), MaxEnumOptions);
                for (int j = 0; j < optLen; ++j) {
                    const QString opt = opts.property(static_cast<quint32>(j)).toString().left(64);
                    if (!opt.isEmpty()) {
                        def.enumOptions.append(opt);
                    }
                }
            }
            if (def.enumOptions.isEmpty()) {
                qCDebug(PhosphorTiles::lcTilesLib)
                    << "ScriptedAlgorithm: enum param" << def.name << "has no valid options, skipping"
                    << "file=" << filePath;
                continue;
            }
            const QString defStr = entry.property(QStringLiteral("default")).toString();
            def.defaultValue = def.enumOptions.contains(defStr) ? defStr : def.enumOptions.first();
        } else {
            qCDebug(PhosphorTiles::lcTilesLib)
                << "ScriptedAlgorithm: unknown param type" << def.type << "for" << def.name << "in" << filePath;
            continue;
        }
        result.append(def);
    }
    return result;
}

ScriptMetadata parseMetadataFromJs(const QJSValue& jsMetadata, const QString& filePath)
{
    using namespace AutotileDefaults;
    ScriptMetadata meta;
    if (!jsMetadata.isObject()) {
        return meta;
    }

    auto readString = [&](const char* key, int maxLen) -> QString {
        const QJSValue v = jsMetadata.property(QString::fromLatin1(key));
        return v.isString() ? v.toString().left(maxLen) : QString();
    };
    auto readBool = [&](const char* key, bool fallback) -> bool {
        const QJSValue v = jsMetadata.property(QString::fromLatin1(key));
        if (v.isBool())
            return v.toBool();
        if (v.isNumber())
            return v.toInt() != 0;
        return fallback;
    };
    auto readNumber = [&](const char* key) -> std::optional<qreal> {
        const QJSValue v = jsMetadata.property(QString::fromLatin1(key));
        if (v.isNumber() && std::isfinite(v.toNumber()))
            return v.toNumber();
        return std::nullopt;
    };
    auto readInt = [&](const char* key) -> std::optional<int> {
        const QJSValue v = jsMetadata.property(QString::fromLatin1(key));
        if (v.isNumber() && std::isfinite(v.toNumber()))
            return v.toInt();
        return std::nullopt;
    };

    const QString name = readString("name", 100);
    if (!name.isEmpty())
        meta.name = name;
    const QString desc = readString("description", 500);
    if (!desc.isEmpty())
        meta.description = desc;

    meta.supportsMasterCount = readBool("supportsMasterCount", meta.supportsMasterCount);
    meta.supportsSplitRatio = readBool("supportsSplitRatio", meta.supportsSplitRatio);
    meta.producesOverlappingZones = readBool("producesOverlappingZones", meta.producesOverlappingZones);
    meta.supportsMemory = readBool("supportsMemory", meta.supportsMemory);
    meta.centerLayout = readBool("centerLayout", meta.centerLayout);
    meta.supportsMinSizes = readBool("supportsMinSizes", meta.supportsMinSizes);

    if (auto v = readNumber("defaultSplitRatio"))
        meta.defaultSplitRatio = std::clamp(*v, MinSplitRatio, MaxSplitRatio);
    if (auto v = readInt("defaultMaxWindows"))
        meta.defaultMaxWindows = std::clamp(*v, MinMetadataWindows, MaxMetadataWindows);
    if (auto v = readInt("minimumWindows"))
        meta.minimumWindows = std::clamp(*v, MinMetadataWindows, MaxMetadataWindows);
    if (auto v = readInt("masterZoneIndex"))
        meta.masterZoneIndex = std::clamp(*v, -1, MaxZones - 1);

    const QString zndStr = readString("zoneNumberDisplay", 64);
    if (!zndStr.isEmpty()) {
        const auto decoded = PhosphorLayout::zoneNumberDisplayFromString(zndStr);
        if (decoded != PhosphorLayout::ZoneNumberDisplay::RendererDecides) {
            meta.zoneNumberDisplay = decoded;
        }
    }

    const QString bid = readString("id", 64);
    if (!bid.isEmpty()) {
        static const QRegularExpression idRe(QStringLiteral("^[a-z][a-z0-9-]*$"));
        if (bid.startsWith(QLatin1String("script:"))) {
            qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: id must not start with 'script:'"
                                                 << "value=" << bid << "in" << filePath;
        } else if (!idRe.match(bid).hasMatch()) {
            qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: invalid id" << bid << "in" << filePath;
        } else {
            meta.id = bid;
        }
    }

    const QJSValue jsCustomParams = jsMetadata.property(QStringLiteral("customParams"));
    if (jsCustomParams.isArray()) {
        const auto params = parseCustomParamsFromJs(jsCustomParams, filePath);
        if (!params.isEmpty()) {
            meta.customParams = params;
        }
    }

    return meta;
}

} // namespace ScriptedHelpers
} // namespace PhosphorTiles
