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

// 100ms watchdog — generous enough for ARM / slow systems where JS evaluation takes longer
static constexpr int ScriptWatchdogTimeoutMs = 100;

/**
 * @brief Consolidated watchdog thread state shared between the main thread and the persistent watchdog thread
 *
 * All members are mutex-protected so that the destructor can safely
 * signal shutdown to the watchdog thread.
 */
struct WatchdogContext
{
    std::mutex mutex; ///< Guards engine pointer access and condition variable
    std::condition_variable cv; ///< Used to wake the persistent watchdog thread
    uint64_t generation{0}; ///< H3: Plain counter — all accesses are under mutex
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
    // After loadScript() sets m_cachedValuesLoaded, all calls use the cached path above.
    // Before that, fall back to metadata — never call JS without the watchdog.
    return metadataFallback;
}

template<typename T>
T ScriptedAlgorithm::resolveJsOverrideClamped(const QJSValue& jsFn, T cachedValue, T metadataFallback, T minVal,
                                              T maxVal) const
{
    return std::clamp(resolveJsOverride<T>(jsFn, cachedValue, metadataFallback), minVal, maxVal);
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
            const uint64_t gen = ctx->generation;
            lock.unlock();

            std::this_thread::sleep_for(std::chrono::milliseconds(ScriptWatchdogTimeoutMs));

            std::lock_guard<std::mutex> guard(ctx->mutex);
            if (ctx->shutdown) {
                return;
            }
            if (ctx->generation == gen && ctx->engine) {
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

    // Disarm the watchdog by advancing generation and atomically check for timeout.
    // C2: setInterrupted(false) and collectGarbage() MUST be inside the lock to prevent
    // the watchdog thread from firing between unlock and the clear.
    bool wasInterrupted;
    {
        std::lock_guard<std::mutex> lock(m_watchdog->mutex);
        ++(m_watchdog->generation);
        wasInterrupted = m_engine->isInterrupted();
        if (wasInterrupted) {
            m_engine->setInterrupted(false);
            m_engine->collectGarbage();
        }
    }

    if (wasInterrupted) {
        // M2: Signal timeout via flag instead of evaluating JS on a just-interrupted engine
        m_lastCallTimedOut = true;
        return QJSValue(QStringLiteral("Script execution timed out"));
    }

    m_lastCallTimedOut = false;
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

    // M3: Apply sandbox hardening BEFORE helper injection so that the sandbox
    // restrictions (frozen prototypes, disabled eval/Function) are in place before
    // any user-visible globals are defined. hardenSandbox() also freezes the helper
    // globals via freezeGlobal() — those calls are no-ops until the helpers exist,
    // so we re-freeze them after injection below.
    if (!hardenSandbox(m_engine)) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: sandbox hardening failed, file=" << filePath;
        return false;
    }

    // M3: Inject built-in helpers from ScriptedAlgorithmHelpers
    m_engine->evaluate(ScriptedHelpers::treeHelperJs(), QStringLiteral("builtin:applyTreeGeometry"));
    m_engine->evaluate(ScriptedHelpers::lShapeHelperJs(), QStringLiteral("builtin:lShapeLayout"));
    m_engine->evaluate(ScriptedHelpers::deckHelperJs(), QStringLiteral("builtin:deckLayout"));
    m_engine->evaluate(ScriptedHelpers::distributeEvenlyHelperJs(), QStringLiteral("builtin:distributeEvenly"));

    // M3: Freeze helper globals so user scripts cannot overwrite them.
    // This must happen after injection since hardenSandbox's freezeGlobal calls
    // ran before the helpers existed.
    for (const auto& helperName : {QLatin1String("applyTreeGeometry"), QLatin1String("lShapeLayout"),
                                   QLatin1String("deckLayout"), QLatin1String("distributeEvenly")}) {
        m_engine->evaluate(QStringLiteral("Object.defineProperty(this, '%1', {writable: false, configurable: false});")
                               .arg(QLatin1String(helperName)));
    }

    // D2: Use guardedCall helper for watchdog arm-evaluate-disarm-check pattern.
    // Wrap user script in an IIFE that shadows eval/Function with undefined via
    // parameter binding. This is defense-in-depth: QJSEngine V4 treats direct
    // eval() as a language-level built-in that bypasses Object.defineProperty on
    // the global, so property-level lockdown alone is insufficient. The IIFE
    // scoping ensures eval/Function resolve to undefined in the user script scope.
    // Exported globals (calculateZones, optional overrides) are re-attached to
    // the global object after the IIFE body executes.
    static const QString wrapPrefix = QStringLiteral("(function(eval, Function) {");
    static const QString wrapSuffix = QStringLiteral(
        "\nvar __pz_g = this;"
        "if (typeof calculateZones === 'function') __pz_g.calculateZones = calculateZones;"
        "if (typeof masterZoneIndex === 'function') __pz_g.masterZoneIndex = masterZoneIndex;"
        "if (typeof supportsMasterCount === 'function') __pz_g.supportsMasterCount = supportsMasterCount;"
        "if (typeof supportsSplitRatio === 'function') __pz_g.supportsSplitRatio = supportsSplitRatio;"
        "if (typeof defaultSplitRatio === 'function') __pz_g.defaultSplitRatio = defaultSplitRatio;"
        "if (typeof minimumWindows === 'function') __pz_g.minimumWindows = minimumWindows;"
        "if (typeof defaultMaxWindows === 'function') __pz_g.defaultMaxWindows = defaultMaxWindows;"
        "if (typeof producesOverlappingZones === 'function') __pz_g.producesOverlappingZones = "
        "producesOverlappingZones;"
        "}).call(this, void 0, void 0);\n");
    const QString wrappedSource = wrapPrefix + source + wrapSuffix;
    const QJSValue result = guardedCall([this, &wrappedSource, &filePath]() {
        return m_engine->evaluate(wrappedSource, filePath);
    });

    // M2: Check structured timeout flag instead of parsing error message strings
    if (m_lastCallTimedOut) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: script timed out during evaluate, file=" << filePath;
        return false;
    }
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
    // C-1: Cache JS override values through guardedCall so that a malicious function
    // (e.g. `function masterZoneIndex() { while(true){} }`) cannot hang forever.
    // If the guarded call times out, we keep the existing default cached value.
    auto guardedCacheJsValue = [this]<typename T>(const QJSValue& jsFn, T fallback) -> T {
        if (!jsFn.isCallable())
            return fallback;
        const QJSValue r = guardedCall([&jsFn]() {
            return jsFn.call();
        });
        if (m_lastCallTimedOut) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: JS override timed out, using default";
            return fallback;
        }
        if (!r.isError() && detail::jsValueHasType<T>(r))
            return detail::jsValueTo<T>(r);
        return fallback;
    };
    m_cachedMasterZoneIndex = guardedCacheJsValue(m_jsMasterZoneIndex, m_cachedMasterZoneIndex);
    m_cachedSupportsMasterCount = guardedCacheJsValue(m_jsSupportsMasterCount, m_cachedSupportsMasterCount);
    m_cachedSupportsSplitRatio = guardedCacheJsValue(m_jsSupportsSplitRatio, m_cachedSupportsSplitRatio);
    m_cachedMinimumWindows = std::clamp(guardedCacheJsValue(m_jsMinimumWindows, m_cachedMinimumWindows), 1, 100);
    m_cachedDefaultMaxWindows =
        std::clamp(guardedCacheJsValue(m_jsDefaultMaxWindows, m_cachedDefaultMaxWindows), 1, 100);
    m_cachedDefaultSplitRatio =
        std::clamp(guardedCacheJsValue(m_jsDefaultSplitRatio, m_cachedDefaultSplitRatio), MinSplitRatio, MaxSplitRatio);
    m_cachedProducesOverlappingZones =
        guardedCacheJsValue(m_jsProducesOverlappingZones, m_cachedProducesOverlappingZones);
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

    // M2: Check structured timeout flag instead of parsing error message strings
    if (m_lastCallTimedOut) {
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

    // M3: Delegate to helpers for array conversion and bounds clamping
    const QVector<QRect> zones = ScriptedHelpers::jsArrayToRects(result, m_scriptId, MaxZones);
    const QVector<QRect> clamped = ScriptedHelpers::clampZonesToArea(zones, area, m_scriptId);

    // S2: Collect garbage every invocation to limit script memory accumulation
    m_engine->collectGarbage();

    return clamped;
}

QJSValue ScriptedAlgorithm::splitNodeToJSValue(const SplitNode* node, int depth) const
{
    if (!node || !m_engine) {
        return QJSValue();
    }

    // m-7: Cache Object.freeze once at the top of the recursion instead of looking it up per node
    const QJSValue freezeFn =
        m_engine->globalObject().property(QStringLiteral("Object")).property(QStringLiteral("freeze"));
    return splitNodeToJSValueImpl(node, freezeFn, depth);
}

QJSValue ScriptedAlgorithm::splitNodeToJSValueImpl(const SplitNode* node, const QJSValue& freezeFn, int depth) const
{
    if (!node || !m_engine || depth > MaxTreeConversionDepth) {
        return QJSValue();
    }

    QJSValue jsNode = m_engine->newObject();

    if (node->isLeaf()) {
        jsNode.setProperty(QStringLiteral("windowId"), node->windowId);
    } else {
        const qreal ratio = std::isnan(node->splitRatio) ? 0.5 : std::clamp(node->splitRatio, 0.1, 0.9);
        jsNode.setProperty(QStringLiteral("ratio"), ratio);
        jsNode.setProperty(QStringLiteral("horizontal"), node->splitHorizontal);
        jsNode.setProperty(QStringLiteral("first"), splitNodeToJSValueImpl(node->first.get(), freezeFn, depth + 1));
        jsNode.setProperty(QStringLiteral("second"), splitNodeToJSValueImpl(node->second.get(), freezeFn, depth + 1));
    }

    // L-1: Freeze the node so scripts cannot mutate the tree representation
    if (freezeFn.isCallable()) {
        freezeFn.call({jsNode});
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
