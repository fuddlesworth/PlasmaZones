// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/ScriptedAlgorithmHelpers.h>
#include <PhosphorTiles/AutotileConstants.h>
#include "tileslogging.h"
#include <QJSValue>
#include <QRegularExpression>
#include <QStringView>
#include <QVariantMap>
#include <algorithm>

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

namespace {

// Parse a permissive boolean: {true,1,yes,on} → true, {false,0,no,off} → false
// (case-insensitive). Unknown values log a warning and fall back to @p fallback.
bool parseMetadataBool(QStringView key, QStringView value, const QString& filePath, bool fallback)
{
    const QString lowered = value.toString().toLower();
    if (lowered == QLatin1String("true") || lowered == QLatin1String("1") || lowered == QLatin1String("yes")
        || lowered == QLatin1String("on"))
        return true;
    if (lowered == QLatin1String("false") || lowered == QLatin1String("0") || lowered == QLatin1String("no")
        || lowered == QLatin1String("off"))
        return false;
    qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm::parseMetadata: unrecognised boolean for @" << key << "="
                                         << value << "(accepted: true/1/yes/on or false/0/no/off) in" << filePath;
    return fallback;
}

} // namespace

ScriptMetadata parseMetadata(const QString& source, const QString& filePath)
{
    using namespace AutotileDefaults;
    // Accept three leading comment styles:
    //   // @key value           (line comment)
    //   /* @key value ...       (block-comment opener)
    //    * @key value           (block-comment continuation)
    // The outer loop already limits parsing to the leading comment block (it
    // breaks on the first non-comment line), so broadening the regex here
    // just lets multi-line /* ... */ headers expose metadata too. The value
    // capture strips any trailing `*/` so "/** @name Foo */" yields "Foo".
    static const QRegularExpression metaRe(QStringLiteral(R"(^\s*(?://|/\*+|\*+)\s*@(\w+)\s+(.+?)(?:\s*\*+/)?\s*$)"));
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
            // Store plain text; any caller that renders the string into rich
            // text must escape at render time (single source of escape policy).
            meta.name = value.left(100);
        } else if (key == QLatin1String("description")) {
            meta.description = value.left(500);
        } else if (key == QLatin1String("supportsMasterCount")) {
            meta.supportsMasterCount = parseMetadataBool(key, value, filePath, meta.supportsMasterCount);
        } else if (key == QLatin1String("supportsSplitRatio")) {
            meta.supportsSplitRatio = parseMetadataBool(key, value, filePath, meta.supportsSplitRatio);
        } else if (key == QLatin1String("producesOverlappingZones")) {
            meta.producesOverlappingZones = parseMetadataBool(key, value, filePath, meta.producesOverlappingZones);
        } else if (key == QLatin1String("supportsMemory")) {
            meta.supportsMemory = parseMetadataBool(key, value, filePath, meta.supportsMemory);
        } else if (key == QLatin1String("centerLayout")) {
            meta.centerLayout = parseMetadataBool(key, value, filePath, meta.centerLayout);
        } else if (key == QLatin1String("supportsMinSizes")) {
            meta.supportsMinSizes = parseMetadataBool(key, value, filePath, meta.supportsMinSizes);
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
            // Forgiving decode: unknown / "first" / empty all fall back to
            // RendererDecides (the TilingAlgorithm default kicks in).
            const auto decoded = PhosphorLayout::zoneNumberDisplayFromString(value);
            if (decoded != PhosphorLayout::ZoneNumberDisplay::RendererDecides) {
                meta.zoneNumberDisplay = decoded;
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
        } else if (key == QLatin1String("param") || key == QLatin1String("returns") || key == QLatin1String("return")) {
            // @param and @returns/@return are silently ignored in metadata comments.
            // Custom parameters are declared via a JS-exported `customParams` array
            // (read by ScriptedAlgorithm::loadScript after evaluation). JSDoc-style
            // @param/{Type} annotations in /** */ blocks are documentation only.
            continue;
        } else if (key != QLatin1String("icon")) {
            qCDebug(PhosphorTiles::lcTilesLib)
                << "ScriptedAlgorithm::parseMetadata: unknown metadata key" << key << "in" << filePath;
        }
    }
    return meta;
}

} // namespace ScriptedHelpers
} // namespace PhosphorTiles
