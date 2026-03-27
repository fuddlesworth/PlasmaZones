// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithm.h"
#include "../SplitTree.h"
#include "../TilingState.h"
#include "core/constants.h"
#include "core/logging.h"
#include "pz_i18n.h"
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QRegularExpression>
#include <QStringView>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace {
constexpr int MaxZones = 256;
}

namespace PlasmaZones {

using namespace AutotileDefaults;

// L14: Named constant for watchdog timeout (was magic number 100)
static constexpr int ScriptWatchdogTimeoutMs = 100;

// H2: Template helpers for JS override resolution and caching

template<typename T>
T ScriptedAlgorithm::resolveJsOverride(const QJSValue& jsFn, T cachedValue, T metadataFallback) const
{
    // H5: Return cached value if available
    if (m_cachedValuesLoaded && jsFn.isCallable()) {
        return cachedValue;
    }
    // C1: Runtime thread guard — JS engine is not thread-safe
    if (!m_cachedValuesLoaded && m_valid) {
        if (QThread::currentThread() != thread()) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm::resolveJsOverride called from wrong thread";
            return metadataFallback;
        }
    }
    if (m_valid && jsFn.isCallable()) {
        const QJSValue result = jsFn.call();
        if (!result.isError() && detail::jsValueHasType<T>(result))
            return detail::jsValueTo<T>(result);
    }
    return metadataFallback;
}

template<typename T>
T ScriptedAlgorithm::resolveJsOverrideClamped(const QJSValue& jsFn, T cachedValue, T metadataFallback, T minVal,
                                              T maxVal) const
{
    return std::clamp(resolveJsOverride<T>(jsFn, cachedValue, metadataFallback), minVal, maxVal);
}

template<typename T>
T ScriptedAlgorithm::cacheJsValue(const QJSValue& jsFn, T fallback)
{
    if (jsFn.isCallable()) {
        const QJSValue r = jsFn.call();
        if (!r.isError() && detail::jsValueHasType<T>(r))
            return detail::jsValueTo<T>(r);
    }
    return fallback;
}

ScriptedAlgorithm::ScriptedAlgorithm(const QString& filePath, QObject* parent)
    : TilingAlgorithm(parent)
    , m_engine(new QJSEngine(this))
    , m_watchdog(std::make_shared<WatchdogContext>())
{
    m_watchdog->engine = m_engine;
    loadScript(filePath);
}

ScriptedAlgorithm::~ScriptedAlgorithm()
{
    // Acquire the mutex so no watchdog thread is between the alive-check
    // and the setInterrupted() call while we tear down.
    std::lock_guard<std::mutex> lock(m_watchdog->mutex);
    m_watchdog->alive.store(false, std::memory_order_release);
    m_watchdog->engine = nullptr;
}

bool ScriptedAlgorithm::isValid() const
{
    return m_valid;
}

QString ScriptedAlgorithm::filePath() const
{
    return m_filePath;
}

QString ScriptedAlgorithm::scriptId() const
{
    return m_scriptId;
}

void ScriptedAlgorithm::setUserScript(bool isUser)
{
    m_isUserScript = isUser;
}

bool ScriptedAlgorithm::loadScript(const QString& filePath)
{
    m_filePath = filePath;
    // L10: Include "script:" prefix for consistency with ScriptedAlgorithmLoader's registry key
    m_scriptId = QStringLiteral("script:") + QFileInfo(filePath).completeBaseName();
    m_valid = false;

    // M2: Reset all stale state from any previous load
    m_cachedValuesLoaded = false;
    m_cachedMasterZoneIndex = -1;
    m_cachedDefaultMaxWindows = 0;
    m_cachedDefaultSplitRatio = AutotileDefaults::DefaultSplitRatio;
    m_cachedMinimumWindows = -1;
    m_cachedSupportsMasterCount = false;
    m_cachedSupportsSplitRatio = false;
    m_cachedProducesOverlappingZones = false;
    m_name.clear();
    m_description.clear();
    m_zoneNumberDisplay.clear();
    m_producesOverlappingZones = false;
    m_supportsMasterCount = false;
    m_supportsSplitRatio = false;
    m_supportsMemory = false;
    m_centerLayout = false;
    m_defaultSplitRatio = 0.0;
    m_defaultMaxWindows = 0;
    m_minimumWindows = 0;
    m_masterZoneIndex = -1;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: failed to open file=" << filePath;
        return false;
    }

    // M12: Reject scripts larger than 1 MB to prevent resource exhaustion
    static constexpr qint64 MaxScriptSizeBytes = 1024 * 1024; // 1 MB
    if (file.size() > MaxScriptSizeBytes) {
        qCWarning(lcAutotile) << "Script file too large:" << filePath << file.size() << "bytes (max"
                              << MaxScriptSizeBytes << ")";
        return false;
    }

    const QString source = QTextStream(&file).readAll();
    file.close();

    if (source.isEmpty()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: empty script file=" << filePath;
        return false;
    }

    parseMetadata(source);

    // Inject built-in helper: applyTreeGeometry(node, rect, gap)
    // Scripts can use this to get memory-aware tiling with one line:
    //   if (params.tree) return applyTreeGeometry(params.tree, params.area, params.innerGap);
    static const QString treeHelper = QStringLiteral(
        "function applyTreeGeometry(node, rect, gap) {"
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
        "      zones = zones.concat(applyTreeGeometry(node.first, rect, 0));"
        "      zones = zones.concat(applyTreeGeometry(node.second, rect, 0));"
        "    } else {"
        "      var h1 = Math.round(content * ratio);"
        "      var h2 = content - h1;"
        "      zones = zones.concat(applyTreeGeometry(node.first,"
        "        {x: rect.x, y: rect.y, width: rect.width, height: h1}, gap));"
        "      zones = zones.concat(applyTreeGeometry(node.second,"
        "        {x: rect.x, y: rect.y + h1 + gap, width: rect.width, height: h2}, gap));"
        "    }"
        "  } else {"
        "    var content = rect.width - gap;"
        "    if (content <= 0) {"
        "      zones = zones.concat(applyTreeGeometry(node.first, rect, 0));"
        "      zones = zones.concat(applyTreeGeometry(node.second, rect, 0));"
        "    } else {"
        "      var w1 = Math.round(content * ratio);"
        "      var w2 = content - w1;"
        "      zones = zones.concat(applyTreeGeometry(node.first,"
        "        {x: rect.x, y: rect.y, width: w1, height: rect.height}, gap));"
        "      zones = zones.concat(applyTreeGeometry(node.second,"
        "        {x: rect.x + w1 + gap, y: rect.y, width: w2, height: rect.height}, gap));"
        "    }"
        "  }"
        "  return zones;"
        "}");
    m_engine->evaluate(treeHelper, QStringLiteral("builtin:applyTreeGeometry"));

    // H3: Freeze the injected helper so user scripts cannot overwrite it
    m_engine->evaluate(
        QStringLiteral("Object.defineProperty(this, 'applyTreeGeometry', "
                       "{writable: false, configurable: false});"));

    // DRY-1: Inject built-in helper: lShapeLayout(area, count, gap, splitRatio, distribute, bottomWidth, rightHeight)
    // Produces an L-shaped master zone with right and bottom stacks.
    // Matches the per-script signature so JS files can drop their local copies.
    static const QString lShapeHelper = QStringLiteral(
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
    m_engine->evaluate(lShapeHelper, QStringLiteral("builtin:lShapeLayout"));

    m_engine->evaluate(
        QStringLiteral("Object.defineProperty(this, 'lShapeLayout', "
                       "{writable: false, configurable: false});"));

    // DRY-2: Inject built-in helper: deckLayout(area, count, focusedFraction, horizontal)
    // Card-deck layout with a focused foreground window and peeking background windows.
    // Matches the per-script signature so JS files can drop their local copies.
    static const QString deckHelper = QStringLiteral(
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
    m_engine->evaluate(deckHelper, QStringLiteral("builtin:deckLayout"));

    m_engine->evaluate(
        QStringLiteral("Object.defineProperty(this, 'deckLayout', "
                       "{writable: false, configurable: false});"));

    // H2: Disable eval() and Function constructor to prevent dynamic code generation
    m_engine->globalObject().deleteProperty(QStringLiteral("eval"));
    m_engine->evaluate(QStringLiteral(
        "Object.defineProperty(this, 'eval', {value: undefined, writable: false, configurable: false});"));
    m_engine->evaluate(
        QStringLiteral("Object.defineProperty(Function.prototype, 'constructor', "
                       "{value: undefined, writable: false, configurable: false});"));

    // Evaluate the user script
    const QJSValue result = m_engine->evaluate(source, filePath);
    if (result.isError()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: evaluation error file=" << filePath
                              << "line=" << result.property(QStringLiteral("lineNumber")).toInt()
                              << "message=" << result.toString();
        return false;
    }

    // Look up the required calculateZones function
    m_calculateZonesFn = m_engine->globalObject().property(QStringLiteral("calculateZones"));
    if (!m_calculateZonesFn.isCallable()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: no callable calculateZones() file=" << filePath;
        return false;
    }

    // Look up optional JS function overrides
    m_jsMasterZoneIndex = m_engine->globalObject().property(QStringLiteral("masterZoneIndex"));
    m_jsSupportsMasterCount = m_engine->globalObject().property(QStringLiteral("supportsMasterCount"));
    m_jsSupportsSplitRatio = m_engine->globalObject().property(QStringLiteral("supportsSplitRatio"));
    m_jsDefaultSplitRatio = m_engine->globalObject().property(QStringLiteral("defaultSplitRatio"));
    m_jsMinimumWindows = m_engine->globalObject().property(QStringLiteral("minimumWindows"));
    m_jsDefaultMaxWindows = m_engine->globalObject().property(QStringLiteral("defaultMaxWindows"));
    m_jsProducesOverlappingZones = m_engine->globalObject().property(QStringLiteral("producesOverlappingZones"));

    m_valid = true;

    // H5: Cache JS virtual method overrides at load time to avoid repeated JS calls
    // H2: Use cacheJsValue helper to eliminate duplication
    m_cachedMasterZoneIndex = cacheJsValue<int>(m_jsMasterZoneIndex, m_cachedMasterZoneIndex);
    m_cachedSupportsMasterCount = cacheJsValue<bool>(m_jsSupportsMasterCount, m_cachedSupportsMasterCount);
    m_cachedSupportsSplitRatio = cacheJsValue<bool>(m_jsSupportsSplitRatio, m_cachedSupportsSplitRatio);
    m_cachedMinimumWindows = std::clamp(cacheJsValue<int>(m_jsMinimumWindows, m_cachedMinimumWindows), 1, 100);
    m_cachedDefaultMaxWindows = std::clamp(cacheJsValue<int>(m_jsDefaultMaxWindows, m_cachedDefaultMaxWindows), 1, 100);
    m_cachedDefaultSplitRatio =
        std::clamp(cacheJsValue<qreal>(m_jsDefaultSplitRatio, m_cachedDefaultSplitRatio), MinSplitRatio, MaxSplitRatio);
    m_cachedProducesOverlappingZones =
        cacheJsValue<bool>(m_jsProducesOverlappingZones, m_cachedProducesOverlappingZones);
    m_cachedValuesLoaded = true;

    qCInfo(lcAutotile) << "ScriptedAlgorithm: loaded script=" << m_scriptId << "file=" << filePath;
    return true;
}

void ScriptedAlgorithm::parseMetadata(const QString& source)
{
    // L19: Metadata must use // line comments, not /* */ block comments.
    // The regex only matches single-line // @key value patterns.
    static const QRegularExpression metaRe(QStringLiteral(R"(^\s*// @(\w+)\s+(.+)$)"));

    int lineCount = 0;
    const auto lines = QStringView(source).split(QLatin1Char('\n'));

    for (const auto& lineView : lines) {
        if (lineCount >= 50)
            break;
        ++lineCount;
        const QString line = lineView.toString();

        // Stop at first non-comment, non-empty line
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1String("//"))) {
            break;
        }

        const QRegularExpressionMatch match = metaRe.match(line);
        if (!match.hasMatch()) {
            continue;
        }

        const QString key = match.captured(1);
        const QString value = match.captured(2).trimmed();

        // @icon is accepted but not stored — icon() was removed from the
        // TilingAlgorithm interface. Scripts may include it for documentation.
        if (key == QLatin1String("name")) {
            m_name = value;
        } else if (key == QLatin1String("description")) {
            m_description = value;
        } else if (key == QLatin1String("supportsMasterCount")) {
            m_supportsMasterCount = (value == QLatin1String("true"));
        } else if (key == QLatin1String("supportsSplitRatio")) {
            m_supportsSplitRatio = (value == QLatin1String("true"));
        } else if (key == QLatin1String("producesOverlappingZones")) {
            m_producesOverlappingZones = (value == QLatin1String("true"));
        } else if (key == QLatin1String("supportsMemory")) {
            m_supportsMemory = (value == QLatin1String("true"));
        } else if (key == QLatin1String("centerLayout")) {
            m_centerLayout = (value == QLatin1String("true"));
        } else if (key == QLatin1String("defaultSplitRatio")) {
            bool ok = false;
            const qreal v = value.toDouble(&ok);
            if (ok) {
                m_defaultSplitRatio = std::clamp(v, MinSplitRatio, MaxSplitRatio);
            }
        } else if (key == QLatin1String("defaultMaxWindows")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                m_defaultMaxWindows = std::clamp(v, 1, 100);
            }
        } else if (key == QLatin1String("minimumWindows")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                m_minimumWindows = std::clamp(v, 1, 100);
            }
        } else if (key == QLatin1String("masterZoneIndex")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                m_masterZoneIndex = std::clamp(v, -1, MaxZones - 1);
            }
        } else if (key == QLatin1String("zoneNumberDisplay")) {
            if (value == QLatin1String("all") || value == QLatin1String("last") || value == QLatin1String("first")
                || value == QLatin1String("firstAndLast") || value == QLatin1String("none")) {
                m_zoneNumberDisplay = value;
            }
        } else if (key != QLatin1String("icon")) {
            // M1: Log unknown metadata keys (icon is silently accepted but not stored)
            qCDebug(lcAutotile) << "ScriptedAlgorithm::parseMetadata: unknown metadata key" << key << "in"
                                << m_filePath;
        }
    }
}

QVector<QRect> ScriptedAlgorithm::calculateZones(const TilingParams& params) const
{
    // C1: Runtime thread guard — QJSEngine is not thread-safe
    if (QThread::currentThread() != thread()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm::calculateZones called from wrong thread";
        return {};
    }

    if (!m_valid || params.windowCount <= 0 || !params.screenGeometry.isValid()) {
        return {};
    }

    // Compute the usable area after outer gaps
    const QRect area = innerRect(params.screenGeometry, params.outerGaps);

    // L2: Early return for empty area (e.g., gaps exceed screen geometry)
    if (area.isEmpty()) {
        return {};
    }

    // DRY-3: Single-window shortcut — skip JS entirely
    if (params.windowCount == 1) {
        return {area};
    }

    // Build the JS params object
    QJSValue jsParams = m_engine->newObject();
    jsParams.setProperty(QStringLiteral("windowCount"), params.windowCount);
    jsParams.setProperty(QStringLiteral("innerGap"), params.innerGap);

    // area sub-object
    QJSValue jsArea = m_engine->newObject();
    jsArea.setProperty(QStringLiteral("x"), area.x());
    jsArea.setProperty(QStringLiteral("y"), area.y());
    jsArea.setProperty(QStringLiteral("width"), area.width());
    jsArea.setProperty(QStringLiteral("height"), area.height());
    jsParams.setProperty(QStringLiteral("area"), jsArea);

    // State-dependent parameters (clamp splitRatio to valid range for JS safety)
    if (params.state) {
        jsParams.setProperty(QStringLiteral("masterCount"), params.state->masterCount());
        jsParams.setProperty(QStringLiteral("splitRatio"),
                             std::clamp(params.state->splitRatio(), MinSplitRatio, MaxSplitRatio));
    } else {
        jsParams.setProperty(QStringLiteral("masterCount"), DefaultMasterCount);
        jsParams.setProperty(QStringLiteral("splitRatio"), DefaultSplitRatio);
    }

    // Split tree (read-only deep copy for memory-aware scripts)
    if (params.state && params.state->splitTree() && !params.state->splitTree()->isEmpty()) {
        jsParams.setProperty(QStringLiteral("tree"), splitNodeToJSValue(params.state->splitTree()->root()));
    }

    // minSizes array
    // B1: Cap the loop at MaxZones (256) to match the array allocation size
    const int minSizesCap = std::min<int>(params.minSizes.size(), MaxZones);
    QJSValue jsMinSizes = m_engine->newArray(static_cast<uint>(minSizesCap));
    for (int i = 0; i < minSizesCap; ++i) {
        QJSValue entry = m_engine->newObject();
        entry.setProperty(QStringLiteral("w"), params.minSizes[i].width());
        entry.setProperty(QStringLiteral("h"), params.minSizes[i].height());
        jsMinSizes.setProperty(static_cast<quint32>(i), entry);
    }
    jsParams.setProperty(QStringLiteral("minSizes"), jsMinSizes);

    // Watchdog: interrupt JS engine after ScriptWatchdogTimeoutMs from a separate thread.
    // A QTimer cannot fire during synchronous JS execution because the event
    // loop is blocked, so we use a detached std::thread instead.
    //
    // C2: Generation-aware spawn — each call increments the generation counter.
    // Stale watchdog threads compare their captured generation against the current
    // value and become no-ops if they don't match.
    const uint64_t currentGen = ++(m_watchdog->generation);
    auto ctx = m_watchdog; // shared_ptr copy — prevents use-after-free
    std::thread([ctx, gen = currentGen]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ScriptWatchdogTimeoutMs));
        std::lock_guard<std::mutex> lock(ctx->mutex);
        if (ctx->generation.load(std::memory_order_acquire) == gen && ctx->engine) {
            ctx->engine->setInterrupted(true);
        }
    }).detach();

    // Call the JS calculateZones function
    const QJSValue result = m_calculateZonesFn.call({jsParams});

    // C2: Invalidate any in-flight watchdog by advancing generation
    ++(m_watchdog->generation);
    if (m_engine->isInterrupted()) {
        m_engine->setInterrupted(false);
        m_engine->collectGarbage();
        qCWarning(lcAutotile) << "ScriptedAlgorithm: script timed out, script=" << m_scriptId;
        return {};
    }

    if (result.isError()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: calculateZones() error script=" << m_scriptId
                              << "message=" << result.toString();
        return {};
    }

    if (!result.isArray()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: calculateZones() did not return array script=" << m_scriptId;
        return {};
    }

    QVector<QRect> zones = jsArrayToRects(result);

    // EC-1: Bounds-clamp pass — clamp each zone to intersect with params.area
    // to guard against extreme gap*count scenarios producing off-screen zones.
    QVector<QRect> clamped;
    clamped.reserve(zones.size());
    for (const QRect& zone : std::as_const(zones)) {
        const QRect bounded = zone.intersected(area);
        if (bounded.isEmpty()) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: zone falls outside area, skipping"
                                  << "zone=" << zone << "area=" << area << "script=" << m_scriptId;
            continue;
        }
        clamped.append(bounded);
    }

    return clamped;
}

QVector<QRect> ScriptedAlgorithm::jsArrayToRects(const QJSValue& result) const
{
    QVector<QRect> rects;
    const int length = result.property(QStringLiteral("length")).toInt();
    if (length <= 0 || length > MaxZones) {
        if (length > MaxZones)
            qCWarning(lcAutotile) << "ScriptedAlgorithm: zone count exceeds maximum" << MaxZones
                                  << "script=" << m_scriptId;
        return rects;
    }
    rects.reserve(length);

    for (int i = 0; i < length; ++i) {
        const QJSValue elem = result.property(static_cast<quint32>(i));
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

QJSValue ScriptedAlgorithm::splitNodeToJSValue(const SplitNode* node, int depth) const
{
    if (!node || !m_engine || depth > MaxTreeConversionDepth) {
        return QJSValue(QJSValue::UndefinedValue);
    }

    QJSValue jsNode = m_engine->newObject();

    if (node->isLeaf()) {
        jsNode.setProperty(QStringLiteral("windowId"), node->windowId);
    } else {
        jsNode.setProperty(QStringLiteral("ratio"), node->splitRatio);
        jsNode.setProperty(QStringLiteral("horizontal"), node->splitHorizontal);
        jsNode.setProperty(QStringLiteral("first"), splitNodeToJSValue(node->first.get(), depth + 1));
        jsNode.setProperty(QStringLiteral("second"), splitNodeToJSValue(node->second.get(), depth + 1));
    }

    return jsNode;
}

// --- Virtual method overrides ---
// Each checks for a JS function override first, then falls back to parsed metadata,
// then to the base class default.

QString ScriptedAlgorithm::name() const
{
    if (!m_name.isEmpty()) {
        return m_name;
    }
    // Fall back to basename (strip "script:" prefix) with first letter capitalized
    if (!m_scriptId.isEmpty()) {
        QString fallback = m_scriptId;
        if (fallback.startsWith(QLatin1String("script:"))) {
            fallback = fallback.mid(7);
        }
        if (!fallback.isEmpty()) {
            fallback[0] = fallback[0].toUpper();
        }
        return fallback;
    }
    return PzI18n::tr("Scripted");
}

QString ScriptedAlgorithm::description() const
{
    if (!m_description.isEmpty()) {
        return m_description;
    }
    return PzI18n::tr("User-provided scripted tiling algorithm");
}

int ScriptedAlgorithm::masterZoneIndex() const
{
    // H2: Unified three-tier resolution via template helper
    return resolveJsOverride<int>(m_jsMasterZoneIndex, m_cachedMasterZoneIndex, m_masterZoneIndex);
}

bool ScriptedAlgorithm::supportsMasterCount() const
{
    return resolveJsOverride<bool>(m_jsSupportsMasterCount, m_cachedSupportsMasterCount, m_supportsMasterCount);
}

bool ScriptedAlgorithm::supportsSplitRatio() const
{
    return resolveJsOverride<bool>(m_jsSupportsSplitRatio, m_cachedSupportsSplitRatio, m_supportsSplitRatio);
}

qreal ScriptedAlgorithm::defaultSplitRatio() const
{
    // DRY-5: Use resolveJsOverrideClamped to unify clamped resolution
    const qreal fallback = (m_defaultSplitRatio > 0.0) ? m_defaultSplitRatio : TilingAlgorithm::defaultSplitRatio();
    return resolveJsOverrideClamped<qreal>(m_jsDefaultSplitRatio, m_cachedDefaultSplitRatio, fallback, MinSplitRatio,
                                           MaxSplitRatio);
}

int ScriptedAlgorithm::minimumWindows() const
{
    // DRY-5: Use resolveJsOverrideClamped to unify clamped resolution
    const int fallback = (m_minimumWindows > 0) ? m_minimumWindows : TilingAlgorithm::minimumWindows();
    return resolveJsOverrideClamped<int>(m_jsMinimumWindows, m_cachedMinimumWindows, fallback, 1, 100);
}

int ScriptedAlgorithm::defaultMaxWindows() const
{
    // DRY-5: Use resolveJsOverrideClamped to unify clamped resolution
    const int fallback = (m_defaultMaxWindows > 0) ? m_defaultMaxWindows : TilingAlgorithm::defaultMaxWindows();
    return resolveJsOverrideClamped<int>(m_jsDefaultMaxWindows, m_cachedDefaultMaxWindows, fallback, 1, 100);
}

bool ScriptedAlgorithm::producesOverlappingZones() const
{
    return resolveJsOverride<bool>(m_jsProducesOverlappingZones, m_cachedProducesOverlappingZones,
                                   m_producesOverlappingZones);
}

bool ScriptedAlgorithm::supportsMemory() const noexcept
{
    return m_supportsMemory;
}

QString ScriptedAlgorithm::zoneNumberDisplay() const noexcept
{
    if (!m_zoneNumberDisplay.isEmpty()) {
        return m_zoneNumberDisplay;
    }
    return TilingAlgorithm::zoneNumberDisplay();
}

bool ScriptedAlgorithm::centerLayout() const noexcept
{
    return m_centerLayout;
}

bool ScriptedAlgorithm::isScripted() const noexcept
{
    return true;
}

bool ScriptedAlgorithm::isUserScript() const noexcept
{
    return m_isUserScript;
}

} // namespace PlasmaZones
