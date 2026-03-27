// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithm.h"
#include "ScriptedAlgorithmHelpers.h"
#include "ScriptedAlgorithmSandbox.h"
#include "../SplitTree.h"
#include "../TilingState.h"
#include "core/constants.h"
#include "core/logging.h"
#include "pz_i18n.h"
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Generous enough for ARM / slow systems where JS evaluation takes longer
static constexpr int ScriptWatchdogTimeoutMs = 250;

/**
 * @brief Consolidated watchdog thread state shared between the main thread and the persistent watchdog thread
 *
 * All members are atomic or mutex-protected so that the destructor can safely
 * signal shutdown to the watchdog thread.
 */
struct WatchdogContext
{
    std::mutex mutex; ///< Guards engine pointer access and condition variable
    std::condition_variable cv; ///< Used to wake the persistent watchdog thread
    std::atomic<uint64_t> generation{0}; ///< Generation counter to prevent stale watchdog interrupts
    bool pending{false}; ///< True when a new watchdog check is requested
    bool shutdown{false}; ///< True when the watchdog thread should exit
    QJSEngine* engine = nullptr; ///< Stable engine pointer shared with watchdog thread
    std::thread watchdogThread; ///< Persistent watchdog thread (joined on destruction)
};

template<typename T>
T ScriptedAlgorithm::resolveJsOverride(const QJSValue& jsFn, T cachedValue, T metadataFallback) const
{
    if (m_cachedValuesLoaded && jsFn.isCallable()) {
        return cachedValue;
    }
    if (m_valid && QThread::currentThread() != thread()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm::resolveJsOverride called from wrong thread";
        return metadataFallback;
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
    m_watchdog->watchdogThread = std::thread([ctx = m_watchdog]() {
        while (true) {
            std::unique_lock<std::mutex> lock(ctx->mutex);
            ctx->cv.wait(lock, [&ctx]() {
                return ctx->pending || ctx->shutdown;
            });
            if (ctx->shutdown) {
                return;
            }
            ctx->pending = false;
            const uint64_t gen = ctx->generation.load(std::memory_order_acquire);
            lock.unlock();

            std::this_thread::sleep_for(std::chrono::milliseconds(ScriptWatchdogTimeoutMs));

            std::lock_guard<std::mutex> guard(ctx->mutex);
            if (ctx->shutdown) {
                return;
            }
            if (ctx->generation.load(std::memory_order_acquire) == gen && ctx->engine) {
                ctx->engine->setInterrupted(true);
            }
        }
    });

    loadScript(filePath);
}

ScriptedAlgorithm::~ScriptedAlgorithm()
{
    {
        std::lock_guard<std::mutex> lock(m_watchdog->mutex);
        m_watchdog->shutdown = true;
        m_watchdog->engine = nullptr;
    }
    m_watchdog->cv.notify_one();
    if (m_watchdog->watchdogThread.joinable()) {
        m_watchdog->watchdogThread.join();
    }
}

// D2: Extracted watchdog arm-evaluate-disarm-check pattern into a reusable helper.
// Arms the watchdog, calls fn(), disarms the watchdog, checks for timeout.
// Returns the QJSValue from fn(), or a synthetic error QJSValue on timeout.
QJSValue ScriptedAlgorithm::guardedCall(const std::function<QJSValue()>& fn) const
{
    // Arm the watchdog
    {
        std::lock_guard<std::mutex> lock(m_watchdog->mutex);
        ++(m_watchdog->generation);
        m_watchdog->pending = true;
    }
    m_watchdog->cv.notify_one();

    // Execute the guarded operation
    const QJSValue result = fn();

    // Disarm the watchdog by advancing generation
    {
        std::lock_guard<std::mutex> lock(m_watchdog->mutex);
        ++(m_watchdog->generation);
    }

    // Check for timeout
    if (m_engine->isInterrupted()) {
        m_engine->setInterrupted(false);
        m_engine->collectGarbage();
        // Return a synthetic error so callers can detect the timeout
        return m_engine->evaluate(QStringLiteral("new Error('Script execution timed out')"));
    }

    return result;
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
    m_cachedValuesLoaded = false;
    m_cachedMasterZoneIndex = -1;
    m_cachedDefaultMaxWindows = 6;
    m_cachedDefaultSplitRatio = AutotileDefaults::DefaultSplitRatio;
    m_cachedMinimumWindows = 1;
    m_cachedSupportsMasterCount = false;
    m_cachedSupportsSplitRatio = false;
    m_cachedProducesOverlappingZones = false;
    // D3: Reset consolidated metadata struct
    m_metadata = ScriptedHelpers::ScriptMetadata{};

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

    // M3: Parse metadata via helper — D3: single struct assignment
    m_metadata = ScriptedHelpers::parseMetadata(source, filePath);

    // M3: Inject built-in helpers from ScriptedAlgorithmHelpers
    m_engine->evaluate(ScriptedHelpers::treeHelperJs(), QStringLiteral("builtin:applyTreeGeometry"));
    m_engine->evaluate(ScriptedHelpers::lShapeHelperJs(), QStringLiteral("builtin:lShapeLayout"));
    m_engine->evaluate(ScriptedHelpers::deckHelperJs(), QStringLiteral("builtin:deckLayout"));
    m_engine->evaluate(ScriptedHelpers::distributeEvenlyHelperJs(), QStringLiteral("builtin:distributeEvenly"));

    // C1: Apply sandbox hardening (extracted to ScriptedAlgorithmSandbox.cpp)
    if (!hardenSandbox(m_engine)) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: sandbox hardening failed, file=" << filePath;
        return false;
    }

    // D2: Use guardedCall helper for watchdog arm-evaluate-disarm-check pattern
    const QJSValue result = guardedCall([this, &source, &filePath]() {
        return m_engine->evaluate(source, filePath);
    });

    // guardedCall returns a synthetic error on timeout; detect via message
    if (result.isError()) {
        const QString msg = result.toString();
        if (msg.contains(QLatin1String("timed out"))) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: script timed out during evaluate, file=" << filePath;
        } else {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: evaluation error file=" << filePath
                                  << "line=" << result.property(QStringLiteral("lineNumber")).toInt()
                                  << "message=" << msg;
        }
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

// L1: calculateZones() is NOT reentrant — it mutates mutable QJSEngine state
// (params object, watchdog generation, interrupt flag). All calls must be serialized
// on the owning thread.
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

    // DRY-3: Single-window case always fills the area. Scripts cannot customize
    // single-window behavior — this is intentional to avoid unnecessary JS calls.
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

    // D2: Use guardedCall helper for watchdog arm-evaluate-disarm-check pattern
    const QJSValue result = guardedCall([this, &jsParams]() {
        return m_calculateZonesFn.call({jsParams});
    });

    // guardedCall handles timeout detection — check for it
    if (result.isError()) {
        const QString msg = result.toString();
        if (msg.contains(QLatin1String("timed out"))) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: script timed out, script=" << m_scriptId;
        } else {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: calculateZones() error script=" << m_scriptId
                                  << "message=" << msg;
        }
        return {};
    }

    if (!result.isArray()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: calculateZones() did not return array script=" << m_scriptId;
        return {};
    }

    // M3: Delegate to helpers for array conversion and bounds clamping
    const QVector<QRect> zones = ScriptedHelpers::jsArrayToRects(result, m_scriptId, MaxZones);
    const QVector<QRect> clamped = ScriptedHelpers::clampZonesToArea(zones, area, m_scriptId);

    // S2: Collect garbage every invocation to limit script memory accumulation
    m_engine->collectGarbage();

    return clamped;
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
        const qreal ratio = std::isnan(node->splitRatio) ? 0.5 : std::clamp(node->splitRatio, 0.1, 0.9);
        jsNode.setProperty(QStringLiteral("ratio"), ratio);
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
    if (!m_metadata.name.isEmpty()) {
        return m_metadata.name;
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
    if (!m_metadata.description.isEmpty()) {
        return m_metadata.description;
    }
    return PzI18n::tr("User-provided scripted tiling algorithm");
}

int ScriptedAlgorithm::masterZoneIndex() const
{
    // H2: Unified three-tier resolution via template helper
    return resolveJsOverride<int>(m_jsMasterZoneIndex, m_cachedMasterZoneIndex, m_metadata.masterZoneIndex);
}

bool ScriptedAlgorithm::supportsMasterCount() const
{
    return resolveJsOverride<bool>(m_jsSupportsMasterCount, m_cachedSupportsMasterCount,
                                   m_metadata.supportsMasterCount);
}

bool ScriptedAlgorithm::supportsSplitRatio() const
{
    return resolveJsOverride<bool>(m_jsSupportsSplitRatio, m_cachedSupportsSplitRatio, m_metadata.supportsSplitRatio);
}

qreal ScriptedAlgorithm::defaultSplitRatio() const
{
    // DRY-5: Use resolveJsOverrideClamped to unify clamped resolution
    const qreal fallback =
        (m_metadata.defaultSplitRatio > 0.0) ? m_metadata.defaultSplitRatio : TilingAlgorithm::defaultSplitRatio();
    return resolveJsOverrideClamped<qreal>(m_jsDefaultSplitRatio, m_cachedDefaultSplitRatio, fallback, MinSplitRatio,
                                           MaxSplitRatio);
}

int ScriptedAlgorithm::minimumWindows() const
{
    // DRY-5: Use resolveJsOverrideClamped to unify clamped resolution
    const int fallback =
        (m_metadata.minimumWindows > 0) ? m_metadata.minimumWindows : TilingAlgorithm::minimumWindows();
    return resolveJsOverrideClamped<int>(m_jsMinimumWindows, m_cachedMinimumWindows, fallback, MinMetadataWindows,
                                         MaxMetadataWindows);
}

int ScriptedAlgorithm::defaultMaxWindows() const
{
    // DRY-5: Use resolveJsOverrideClamped to unify clamped resolution
    const int fallback =
        (m_metadata.defaultMaxWindows > 0) ? m_metadata.defaultMaxWindows : TilingAlgorithm::defaultMaxWindows();
    return resolveJsOverrideClamped<int>(m_jsDefaultMaxWindows, m_cachedDefaultMaxWindows, fallback, MinMetadataWindows,
                                         MaxMetadataWindows);
}

bool ScriptedAlgorithm::producesOverlappingZones() const
{
    return resolveJsOverride<bool>(m_jsProducesOverlappingZones, m_cachedProducesOverlappingZones,
                                   m_metadata.producesOverlappingZones);
}

bool ScriptedAlgorithm::supportsMemory() const noexcept
{
    return m_metadata.supportsMemory;
}

QString ScriptedAlgorithm::zoneNumberDisplay() const noexcept
{
    if (!m_metadata.zoneNumberDisplay.isEmpty()) {
        return m_metadata.zoneNumberDisplay;
    }
    return TilingAlgorithm::zoneNumberDisplay();
}

bool ScriptedAlgorithm::centerLayout() const noexcept
{
    return m_metadata.centerLayout;
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
