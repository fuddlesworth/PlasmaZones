// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithm.h"
#include "../SplitTree.h"
#include "../TilingState.h"
#include "core/constants.h"
#include "core/logging.h"
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QRegularExpression>
#include <QStringView>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace PlasmaZones {

using namespace AutotileDefaults;

// L14: Named constant for watchdog timeout (was magic number 100)
static constexpr int ScriptWatchdogTimeoutMs = 100;

ScriptedAlgorithm::ScriptedAlgorithm(const QString& filePath, QObject* parent)
    : TilingAlgorithm(parent)
    , m_engine(new QJSEngine(this))
    , m_engineAlive(std::make_shared<std::atomic<bool>>(true))
    , m_engineMutex(std::make_shared<std::mutex>())
    , m_watchdogGeneration(std::make_shared<std::atomic<uint64_t>>(0))
    , m_watchdogEnginePtr(std::make_shared<QJSEngine*>(m_engine))
{
    loadScript(filePath);
}

ScriptedAlgorithm::~ScriptedAlgorithm()
{
    // Acquire the mutex so no watchdog thread is between the alive-check
    // and the setInterrupted() call while we tear down.
    std::lock_guard<std::mutex> lock(*m_engineMutex);
    m_engineAlive->store(false, std::memory_order_release);
    *m_watchdogEnginePtr = nullptr;
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
    if (m_jsMasterZoneIndex.isCallable()) {
        const QJSValue r = m_jsMasterZoneIndex.call();
        if (!r.isError() && r.isNumber())
            m_cachedMasterZoneIndex = r.toInt();
    }
    if (m_jsSupportsMasterCount.isCallable()) {
        const QJSValue r = m_jsSupportsMasterCount.call();
        if (!r.isError() && r.isBool())
            m_cachedSupportsMasterCount = r.toBool();
    }
    if (m_jsSupportsSplitRatio.isCallable()) {
        const QJSValue r = m_jsSupportsSplitRatio.call();
        if (!r.isError() && r.isBool())
            m_cachedSupportsSplitRatio = r.toBool();
    }
    if (m_jsMinimumWindows.isCallable()) {
        const QJSValue r = m_jsMinimumWindows.call();
        if (!r.isError() && r.isNumber())
            m_cachedMinimumWindows = std::clamp(r.toInt(), 1, 100);
    }
    if (m_jsDefaultMaxWindows.isCallable()) {
        const QJSValue r = m_jsDefaultMaxWindows.call();
        if (!r.isError() && r.isNumber())
            m_cachedDefaultMaxWindows = std::clamp(r.toInt(), 1, 100);
    }
    if (m_jsDefaultSplitRatio.isCallable()) {
        const QJSValue r = m_jsDefaultSplitRatio.call();
        if (!r.isError() && r.isNumber())
            m_cachedDefaultSplitRatio = std::clamp(r.toNumber(), MinSplitRatio, MaxSplitRatio);
    }
    if (m_jsProducesOverlappingZones.isCallable()) {
        const QJSValue r = m_jsProducesOverlappingZones.call();
        if (!r.isError() && r.isBool())
            m_cachedProducesOverlappingZones = r.toBool();
    }
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
                m_masterZoneIndex = qMax(-1, v);
            }
        } else if (key == QLatin1String("zoneNumberDisplay")) {
            if (value == QLatin1String("all") || value == QLatin1String("last") || value == QLatin1String("first")
                || value == QLatin1String("firstAndLast") || value == QLatin1String("none")) {
                m_zoneNumberDisplay = value;
            }
        }
    }
}

QVector<QRect> ScriptedAlgorithm::calculateZones(const TilingParams& params) const
{
    if (!m_valid || params.windowCount <= 0 || !params.screenGeometry.isValid()) {
        return {};
    }

    // Compute the usable area after outer gaps
    const QRect area = innerRect(params.screenGeometry, params.outerGaps);

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
    QJSValue jsMinSizes = m_engine->newArray(static_cast<uint>(std::min<qsizetype>(params.minSizes.size(), 256)));
    for (int i = 0; i < params.minSizes.size(); ++i) {
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
    // H1: We use a generation counter to prevent stale watchdog threads from
    // interrupting a newer calculateZones() call. Each call increments the
    // generation and the watchdog only interrupts if the generation still matches.
    const uint64_t myGen = ++(*m_watchdogGeneration);

    auto aliveFlag = m_engineAlive;
    auto engineMtx = m_engineMutex;
    auto enginePtr = m_watchdogEnginePtr;
    auto genPtr = m_watchdogGeneration; // shared_ptr copy — prevents use-after-free
    std::thread([enginePtr, aliveFlag, engineMtx, genPtr, myGen]() {
        QThread::msleep(ScriptWatchdogTimeoutMs);
        // Only interrupt if this is still the active generation
        if (genPtr->load(std::memory_order_acquire) != myGen) {
            return; // A newer call has started or the call completed
        }
        // Hold the mutex to prevent the destructor from invalidating the
        // engine pointer between our alive-check and setInterrupted().
        std::lock_guard<std::mutex> lock(*engineMtx);
        if (genPtr->load(std::memory_order_acquire) == myGen && aliveFlag->load(std::memory_order_acquire)) {
            // C1: Check for null before calling setInterrupted
            auto engine = *enginePtr;
            if (engine) {
                engine->setInterrupted(true);
            }
        }
    }).detach();

    // Call the JS calculateZones function
    const QJSValue result = m_calculateZonesFn.call({jsParams});

    // Advance generation so any in-flight watchdog becomes a no-op, and reset interrupted state
    ++(*m_watchdogGeneration);
    m_engine->setInterrupted(false);

    if (result.isError()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: calculateZones() error script=" << m_scriptId
                              << "message=" << result.toString();
        return {};
    }

    if (!result.isArray()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: calculateZones() did not return array script=" << m_scriptId;
        return {};
    }

    return jsArrayToRects(result);
}

QVector<QRect> ScriptedAlgorithm::jsArrayToRects(const QJSValue& result) const
{
    QVector<QRect> rects;
    const int length = result.property(QStringLiteral("length")).toInt();
    constexpr int MaxZones = 256;
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
    return QStringLiteral("Scripted");
}

QString ScriptedAlgorithm::description() const
{
    if (!m_description.isEmpty()) {
        return m_description;
    }
    return QStringLiteral("User-provided scripted tiling algorithm");
}

int ScriptedAlgorithm::masterZoneIndex() const noexcept
{
    // H5: Return cached value if available
    if (m_cachedValuesLoaded && m_jsMasterZoneIndex.isCallable()) {
        return m_cachedMasterZoneIndex;
    }
    // QJSValue API is noexcept-safe (no C++ exceptions); the isError() check
    // guards against JS-level errors.  Build uses -fno-exceptions.
    if (m_jsMasterZoneIndex.isCallable()) {
        const QJSValue result = m_jsMasterZoneIndex.call();
        if (!result.isError() && result.isNumber())
            return result.toInt();
    }
    return m_masterZoneIndex;
}

bool ScriptedAlgorithm::supportsMasterCount() const noexcept
{
    // H5: Return cached value if available
    if (m_cachedValuesLoaded && m_jsSupportsMasterCount.isCallable()) {
        return m_cachedSupportsMasterCount;
    }
    if (m_jsSupportsMasterCount.isCallable()) {
        const QJSValue result = m_jsSupportsMasterCount.call();
        if (!result.isError() && result.isBool())
            return result.toBool();
    }
    return m_supportsMasterCount;
}

bool ScriptedAlgorithm::supportsSplitRatio() const noexcept
{
    // H5: Return cached value if available
    if (m_cachedValuesLoaded && m_jsSupportsSplitRatio.isCallable()) {
        return m_cachedSupportsSplitRatio;
    }
    if (m_jsSupportsSplitRatio.isCallable()) {
        const QJSValue result = m_jsSupportsSplitRatio.call();
        if (!result.isError() && result.isBool())
            return result.toBool();
    }
    return m_supportsSplitRatio;
}

qreal ScriptedAlgorithm::defaultSplitRatio() const noexcept
{
    // H5: Return cached value if available
    if (m_cachedValuesLoaded && m_jsDefaultSplitRatio.isCallable()) {
        return m_cachedDefaultSplitRatio;
    }
    if (m_jsDefaultSplitRatio.isCallable()) {
        const QJSValue result = m_jsDefaultSplitRatio.call();
        if (!result.isError() && result.isNumber())
            return std::clamp(result.toNumber(), MinSplitRatio, MaxSplitRatio);
    }
    if (m_defaultSplitRatio > 0.0) {
        return m_defaultSplitRatio;
    }
    return TilingAlgorithm::defaultSplitRatio();
}

int ScriptedAlgorithm::minimumWindows() const noexcept
{
    // H5: Return cached value if available
    if (m_cachedValuesLoaded && m_jsMinimumWindows.isCallable()) {
        return m_cachedMinimumWindows;
    }
    if (m_jsMinimumWindows.isCallable()) {
        const QJSValue result = m_jsMinimumWindows.call();
        if (!result.isError() && result.isNumber())
            // M11: Clamp JS override return values to same range as metadata parser
            return std::clamp(result.toInt(), 1, 100);
    }
    if (m_minimumWindows > 0) {
        return m_minimumWindows;
    }
    return TilingAlgorithm::minimumWindows();
}

int ScriptedAlgorithm::defaultMaxWindows() const noexcept
{
    // H5: Return cached value if available
    if (m_cachedValuesLoaded && m_jsDefaultMaxWindows.isCallable()) {
        return m_cachedDefaultMaxWindows;
    }
    if (m_jsDefaultMaxWindows.isCallable()) {
        const QJSValue result = m_jsDefaultMaxWindows.call();
        if (!result.isError() && result.isNumber())
            // M11: Clamp JS override return values to same range as metadata parser
            return std::clamp(result.toInt(), 1, 100);
    }
    if (m_defaultMaxWindows > 0) {
        return m_defaultMaxWindows;
    }
    return TilingAlgorithm::defaultMaxWindows();
}

bool ScriptedAlgorithm::producesOverlappingZones() const noexcept
{
    // H5: Return cached value if available
    if (m_cachedValuesLoaded && m_jsProducesOverlappingZones.isCallable()) {
        return m_cachedProducesOverlappingZones;
    }
    if (m_jsProducesOverlappingZones.isCallable()) {
        const QJSValue result = m_jsProducesOverlappingZones.call();
        if (!result.isError() && result.isBool())
            return result.toBool();
    }
    return m_producesOverlappingZones;
}

QString ScriptedAlgorithm::zoneNumberDisplay() const noexcept
{
    if (!m_zoneNumberDisplay.isEmpty()) {
        return m_zoneNumberDisplay;
    }
    return TilingAlgorithm::zoneNumberDisplay();
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
