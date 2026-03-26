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

ScriptedAlgorithm::ScriptedAlgorithm(const QString& filePath, QObject* parent)
    : TilingAlgorithm(parent)
    , m_engine(new QJSEngine(this))
    , m_engineAlive(std::make_shared<std::atomic<bool>>(true))
    , m_engineMutex(std::make_shared<std::mutex>())
{
    loadScript(filePath);
}

ScriptedAlgorithm::~ScriptedAlgorithm()
{
    // Acquire the mutex so no watchdog thread is between the alive-check
    // and the setInterrupted() call while we tear down.
    std::lock_guard<std::mutex> lock(*m_engineMutex);
    m_engineAlive->store(false, std::memory_order_release);
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
    m_scriptId = QFileInfo(filePath).completeBaseName();
    m_valid = false;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: failed to open file=" << filePath;
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
    qCInfo(lcAutotile) << "ScriptedAlgorithm: loaded script=" << m_scriptId << "file=" << filePath;
    return true;
}

void ScriptedAlgorithm::parseMetadata(const QString& source)
{
    static const QRegularExpression metaRe(QStringLiteral(R"(^// @(\w+)\s+(.+)$)"));

    int lineCount = 0;
    const auto lines = QStringView(source).split(QLatin1Char('\n'));

    for (const auto& lineView : lines) {
        if (lineCount >= 50) break;
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

        // Note: @icon is intentionally not parsed — icon() was removed from
        // the TilingAlgorithm interface. Unknown keys are silently ignored.
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
                m_defaultSplitRatio = qBound(0.01, v, 0.99);
            }
        } else if (key == QLatin1String("defaultMaxWindows")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                m_defaultMaxWindows = qBound(1, v, 100);
            }
        } else if (key == QLatin1String("minimumWindows")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                m_minimumWindows = qBound(1, v, 100);
            }
        } else if (key == QLatin1String("masterZoneIndex")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                m_masterZoneIndex = qMax(-1, v);
            }
        } else if (key == QLatin1String("zoneNumberDisplay")) {
            if (value == QLatin1String("all") || value == QLatin1String("last")
                || value == QLatin1String("first") || value == QLatin1String("firstAndLast")
                || value == QLatin1String("none")) {
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
    QJSValue jsMinSizes = m_engine->newArray(static_cast<uint>(params.minSizes.size()));
    for (int i = 0; i < params.minSizes.size(); ++i) {
        QJSValue entry = m_engine->newObject();
        entry.setProperty(QStringLiteral("w"), params.minSizes[i].width());
        entry.setProperty(QStringLiteral("h"), params.minSizes[i].height());
        jsMinSizes.setProperty(static_cast<quint32>(i), entry);
    }
    jsParams.setProperty(QStringLiteral("minSizes"), jsMinSizes);

    // Watchdog: interrupt JS engine after 100ms from a separate thread.
    // A QTimer cannot fire during synchronous JS execution because the event
    // loop is blocked, so we use a detached std::thread instead.
    // Both the finished flag and the alive flag are shared_ptr so the watchdog
    // thread can safely outlive the ScriptedAlgorithm if it is destroyed during
    // the sleep window (e.g., hot-reload unregisters the algorithm).
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto aliveFlag = m_engineAlive;
    auto engineMtx = m_engineMutex;
    auto* engine = m_engine;
    std::thread([engine, finished, aliveFlag, engineMtx]() {
        QThread::msleep(100);
        if (finished->load(std::memory_order_acquire)) {
            return; // Script completed within timeout
        }
        // Hold the mutex to prevent the destructor from invalidating the
        // engine pointer between our alive-check and setInterrupted().
        std::lock_guard<std::mutex> lock(*engineMtx);
        if (!finished->load(std::memory_order_acquire)
            && aliveFlag->load(std::memory_order_acquire)) {
            engine->setInterrupted(true);
        }
    }).detach();

    // Call the JS calculateZones function
    const QJSValue result = m_calculateZonesFn.call({jsParams});

    // Signal the watchdog and reset interrupted state
    finished->store(true, std::memory_order_release);
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
            qCWarning(lcAutotile) << "ScriptedAlgorithm: zone count exceeds maximum" << MaxZones << "script=" << m_scriptId;
        return rects;
    }
    rects.reserve(length);

    for (int i = 0; i < length; ++i) {
        const QJSValue elem = result.property(static_cast<quint32>(i));
        const int x = elem.property(QStringLiteral("x")).toInt();
        const int y = elem.property(QStringLiteral("y")).toInt();
        int w = elem.property(QStringLiteral("width")).toInt();
        int h = elem.property(QStringLiteral("height")).toInt();

        // Validate: non-negative dimensions, clamp to at least 1
        w = std::max(1, w);
        h = std::max(1, h);

        rects.append(QRect(x, y, w, h));
    }

    return rects;
}

QJSValue ScriptedAlgorithm::splitNodeToJSValue(const SplitNode* node) const
{
    if (!node || !m_engine) {
        return QJSValue(QJSValue::UndefinedValue);
    }

    QJSValue jsNode = m_engine->newObject();

    if (node->isLeaf()) {
        jsNode.setProperty(QStringLiteral("windowId"), node->windowId);
    } else {
        jsNode.setProperty(QStringLiteral("ratio"), node->splitRatio);
        jsNode.setProperty(QStringLiteral("horizontal"), node->splitHorizontal);
        jsNode.setProperty(QStringLiteral("first"), splitNodeToJSValue(node->first.get()));
        jsNode.setProperty(QStringLiteral("second"), splitNodeToJSValue(node->second.get()));
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
    // Fall back to script ID with first letter capitalized
    if (!m_scriptId.isEmpty()) {
        QString fallback = m_scriptId;
        fallback[0] = fallback[0].toUpper();
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
    if (m_jsSupportsMasterCount.isCallable()) {
        const QJSValue result = m_jsSupportsMasterCount.call();
        if (!result.isError() && result.isBool())
            return result.toBool();
    }
    return m_supportsMasterCount;
}

bool ScriptedAlgorithm::supportsSplitRatio() const noexcept
{
    if (m_jsSupportsSplitRatio.isCallable()) {
        const QJSValue result = m_jsSupportsSplitRatio.call();
        if (!result.isError() && result.isBool())
            return result.toBool();
    }
    return m_supportsSplitRatio;
}

qreal ScriptedAlgorithm::defaultSplitRatio() const noexcept
{
    if (m_jsDefaultSplitRatio.isCallable()) {
        const QJSValue result = m_jsDefaultSplitRatio.call();
        if (!result.isError() && result.isNumber())
            return result.toNumber();
    }
    if (m_defaultSplitRatio > 0.0) {
        return m_defaultSplitRatio;
    }
    return TilingAlgorithm::defaultSplitRatio();
}

int ScriptedAlgorithm::minimumWindows() const noexcept
{
    if (m_jsMinimumWindows.isCallable()) {
        const QJSValue result = m_jsMinimumWindows.call();
        if (!result.isError() && result.isNumber())
            return result.toInt();
    }
    if (m_minimumWindows > 0) {
        return m_minimumWindows;
    }
    return TilingAlgorithm::minimumWindows();
}

int ScriptedAlgorithm::defaultMaxWindows() const noexcept
{
    if (m_jsDefaultMaxWindows.isCallable()) {
        const QJSValue result = m_jsDefaultMaxWindows.call();
        if (!result.isError() && result.isNumber())
            return result.toInt();
    }
    if (m_defaultMaxWindows > 0) {
        return m_defaultMaxWindows;
    }
    return TilingAlgorithm::defaultMaxWindows();
}

bool ScriptedAlgorithm::producesOverlappingZones() const noexcept
{
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
