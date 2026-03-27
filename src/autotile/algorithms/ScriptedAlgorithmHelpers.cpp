// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithmHelpers.h"
#include "core/constants.h"
#include "core/logging.h"
#include <QJSValue>
#include <QRegularExpression>
#include <QStringView>
#include <algorithm>

namespace PlasmaZones {
namespace ScriptedHelpers {

QString treeHelperJs()
{
    return QStringLiteral(
        "function applyTreeGeometry(node, rect, gap, _depth) {"
        "  if ((_depth || 0) > 50) return [];"
        "  if (!node) return [];"
        "  if (node.windowId !== undefined && node.windowId !== '') {"
        "    return [{x: rect.x, y: rect.y, width: rect.width, height: rect.height}];"
        "  }"
        "  if (!node.first || !node.second) {"
        "    return [{x: rect.x, y: rect.y, width: rect.width, height: rect.height}];"
        "  }"
        "  var ratio = Math.max(0.1, Math.min(0.9, node.ratio || 0.5));"
        "  var zones = [];"
        "  if (node.horizontal) {"
        "    var content = rect.height - gap;"
        "    if (content <= 0) {"
        "      zones = zones.concat(applyTreeGeometry(node.first, rect, 0, (_depth||0)+1));"
        "      zones = zones.concat(applyTreeGeometry(node.second, rect, 0, (_depth||0)+1));"
        "    } else {"
        "      var h1 = Math.round(content * ratio);"
        "      var h2 = content - h1;"
        "      zones = zones.concat(applyTreeGeometry(node.first,"
        "        {x: rect.x, y: rect.y, width: rect.width, height: h1}, gap, (_depth||0)+1));"
        "      zones = zones.concat(applyTreeGeometry(node.second,"
        "        {x: rect.x, y: rect.y + h1 + gap, width: rect.width, height: h2}, gap, (_depth||0)+1));"
        "    }"
        "  } else {"
        "    var content = rect.width - gap;"
        "    if (content <= 0) {"
        "      zones = zones.concat(applyTreeGeometry(node.first, rect, 0, (_depth||0)+1));"
        "      zones = zones.concat(applyTreeGeometry(node.second, rect, 0, (_depth||0)+1));"
        "    } else {"
        "      var w1 = Math.round(content * ratio);"
        "      var w2 = content - w1;"
        "      zones = zones.concat(applyTreeGeometry(node.first,"
        "        {x: rect.x, y: rect.y, width: w1, height: rect.height}, gap, (_depth||0)+1));"
        "      zones = zones.concat(applyTreeGeometry(node.second,"
        "        {x: rect.x + w1 + gap, y: rect.y, width: w2, height: rect.height}, gap, (_depth||0)+1));"
        "    }"
        "  }"
        "  return zones;"
        "}");
}

QString lShapeHelperJs()
{
    return QStringLiteral(
        "function lShapeLayout(area, count, gap, splitRatio, distribute, bottomWidth, rightHeight) {"
        "  if (distribute === undefined) distribute = 'alternate';"
        "  if (bottomWidth === undefined) bottomWidth = area.width * splitRatio;"
        "  if (rightHeight === undefined) rightHeight = area.height;"
        "  var masterW = Math.max(1, Math.round(area.width * splitRatio - gap / 2));"
        "  var masterH = Math.max(1, Math.round(area.height * splitRatio - gap / 2));"
        "  var zones = [{ x: area.x, y: area.y, width: masterW, height: masterH }];"
        "  if (count <= 1) return zones;"
        "  if (count === 2) {"
        "    zones.push({ x: area.x + masterW + gap, y: area.y,"
        "      width: Math.max(1, area.x + area.width - (area.x + masterW + gap)),"
        "      height: area.height });"
        "    return zones;"
        "  }"
        "  var rightCount, bottomCount;"
        "  if (distribute === 'alternate') {"
        "    rightCount = 0; bottomCount = 0;"
        "    for (var i = 1; i < count; i++) {"
        "      if ((i - 1) % 2 === 0) rightCount++; else bottomCount++;"
        "    }"
        "  } else {"
        "    var remaining = count - 1;"
        "    rightCount = Math.ceil(remaining / 2);"
        "    bottomCount = Math.floor(remaining / 2);"
        "  }"
        "  var rightX = area.x + masterW + gap;"
        "  var rightW = Math.max(1, area.x + area.width - rightX);"
        "  var rH = (rightHeight === 'master' && bottomCount > 0) ? masterH : area.height;"
        "  if (typeof rightHeight === 'number') rH = rightHeight;"
        "  var rightTotalGaps = (rightCount - 1) * gap;"
        "  var rightTileH = Math.max(1, Math.round((rH - rightTotalGaps) / rightCount));"
        "  for (var r = 0; r < rightCount; r++) {"
        "    var ry = area.y + r * (rightTileH + gap);"
        "    var rh = Math.max(1, (r === rightCount - 1) ? (area.y + rH - ry) : rightTileH);"
        "    zones.push({ x: rightX, y: ry, width: rightW, height: rh });"
        "  }"
        "  if (bottomCount > 0) {"
        "    var bottomY = area.y + masterH + gap;"
        "    var bottomH = Math.max(1, area.y + area.height - bottomY);"
        "    var btmW = (bottomWidth === 'full') ? area.width : masterW;"
        "    if (typeof bottomWidth === 'number') btmW = bottomWidth;"
        "    var bottomTotalGaps = (bottomCount - 1) * gap;"
        "    var bottomTileW = Math.max(1, Math.round((btmW - bottomTotalGaps) / bottomCount));"
        "    for (var b = 0; b < bottomCount; b++) {"
        "      var bx = area.x + b * (bottomTileW + gap);"
        "      var bw = Math.max(1, (b === bottomCount - 1) ? (area.x + btmW - bx) : bottomTileW);"
        "      zones.push({ x: bx, y: bottomY, width: bw, height: bottomH });"
        "    }"
        "  }"
        "  return zones;"
        "}");
}

QString deckHelperJs()
{
    return QStringLiteral(
        "function deckLayout(area, count, focusedFraction, horizontal) {"
        "  if (horizontal === undefined) horizontal = false;"
        "  var axisSize = horizontal ? area.height : area.width;"
        "  var bgCount = count - 1;"
        "  var focusedSize = Math.max(1, Math.round(axisSize * focusedFraction));"
        "  var peekTotal = axisSize - focusedSize;"
        "  var peekSize = bgCount > 0 ? Math.max(1, Math.round(Math.max(0, peekTotal) / bgCount)) : 0;"
        "  var zones = [];"
        "  zones.push({ x: area.x, y: area.y,"
        "    width: horizontal ? area.width : focusedSize,"
        "    height: horizontal ? focusedSize : area.height });"
        "  for (var i = 0; i < bgCount; i++) {"
        "    var peekOffset = Math.min(focusedSize + i * peekSize, axisSize - 1);"
        "    if (horizontal) {"
        "      var peekY = area.y + peekOffset;"
        "      zones.push({ x: area.x,"
        "        y: Math.min(peekY, area.y + area.height - 1),"
        "        width: area.width,"
        "        height: Math.max(1, area.y + area.height - peekY) });"
        "    } else {"
        "      var peekX = area.x + peekOffset;"
        "      zones.push({"
        "        x: Math.min(peekX, area.x + area.width - 1),"
        "        y: area.y,"
        "        width: Math.max(1, area.x + area.width - peekX),"
        "        height: area.height });"
        "    }"
        "  }"
        "  return zones;"
        "}");
}

QString distributeEvenlyHelperJs()
{
    return QStringLiteral(
        "function distributeEvenly(start, total, count, gap) {"
        "  if (count <= 0) return [];"
        "  if (count === 1) return [{pos: start, size: total}];"
        "  var totalGaps = (count - 1) * gap;"
        "  var tileSize = Math.max(1, Math.round((total - totalGaps) / count));"
        "  var result = [];"
        "  for (var i = 0; i < count; i++) {"
        "    var pos = start + i * (tileSize + gap);"
        "    var size = (i === count - 1) ? Math.max(1, start + total - pos) : tileSize;"
        "    result.push({pos: pos, size: size});"
        "  }"
        "  return result;"
        "}");
}

QVector<QRect> jsArrayToRects(const QJSValue& result, const QString& scriptId, int maxZones)
{
    QVector<QRect> rects;
    const int length = result.property(QStringLiteral("length")).toInt();
    if (length <= 0)
        return rects;
    const int effectiveLength = std::min(length, maxZones);
    if (length > maxZones)
        qCWarning(lcAutotile) << "ScriptedAlgorithm: script returned" << length << "zones, truncating to" << maxZones
                              << "script=" << scriptId;
    rects.reserve(effectiveLength);

    for (int i = 0; i < effectiveLength; ++i) {
        const QJSValue elem = result.property(static_cast<quint32>(i));
        // m1: Validate that each element is an object before extracting properties
        if (!elem.isObject()) {
            qCWarning(lcAutotile) << "Skipping non-object zone element at index" << i;
            continue;
        }
        // M10: Clamp x and y to non-negative to prevent off-screen zones
        const int x = std::max(0, elem.property(QStringLiteral("x")).toInt());
        const int y = std::max(0, elem.property(QStringLiteral("y")).toInt());
        int w = elem.property(QStringLiteral("width")).toInt();
        int h = elem.property(QStringLiteral("height")).toInt();

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
            qCWarning(lcAutotile) << "ScriptedAlgorithm: zone" << i << "outside area, using full area as fallback"
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

    ScriptMetadata meta;
    int lineCount = 0;
    const auto lines = QStringView(source).split(QLatin1Char('\n'));

    for (const auto& lineView : lines) {
        if (lineCount >= 50)
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
            // S-10: Sanitize metadata — length cap + HTML escape
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
        } else if (key != QLatin1String("icon")) {
            qCDebug(lcAutotile) << "ScriptedAlgorithm::parseMetadata: unknown metadata key" << key << "in" << filePath;
        }
    }
    return meta;
}

} // namespace ScriptedHelpers
} // namespace PlasmaZones
