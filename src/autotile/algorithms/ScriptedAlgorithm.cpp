// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithm.h"
#include "ScriptedAlgorithmHelpers.h"
#include "ScriptedAlgorithmJsBuiltins.h"
#include "ScriptedAlgorithmSandbox.h"
#include "../SplitTree.h"
#include "../TilingState.h"
#include "core/constants.h"
#include "core/logging.h"
#include "core/utils.h"
#include "pz_i18n.h"
#include <QCoreApplication>
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

namespace {

/// RAII guard to prevent re-entrant calls into ScriptedAlgorithm::calculateZones.
/// QJSEngine is not re-entrant; a signal handler calling back into calculateZones
/// while an evaluation is in flight would corrupt engine state.
///
/// memory_order_relaxed is sufficient: this guards single-thread re-entrancy
/// (e.g. signal handler calling calculateZones during JS evaluation), not
/// cross-thread synchronization. QJSEngine is main-thread-only.
struct ReentrancyGuard
{
    std::atomic<bool>& flag;
    explicit ReentrancyGuard(std::atomic<bool>& f)
        : flag(f)
    {
        flag.store(true, std::memory_order_relaxed);
    }
    ~ReentrancyGuard()
    {
        flag.store(false, std::memory_order_relaxed);
    }
    Q_DISABLE_COPY_MOVE(ReentrancyGuard)
};

} // namespace

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
    uint64_t generation{0}; ///< Plain counter — all accesses are under mutex
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
    // ScriptedAlgorithmJsBuiltins uses static lazy initialization (function-local
    // statics) that is thread-safe for first-call but not for concurrent QJSEngine
    // evaluation. Enforce the single-thread contract.
    Q_ASSERT(!QCoreApplication::instance() || QThread::currentThread() == QCoreApplication::instance()->thread());
    // Runtime guard — Q_ASSERT vanishes in release builds but QJSEngine corruption
    // from off-thread construction is silent and catastrophic.
    if (QCoreApplication::instance() && QThread::currentThread() != QCoreApplication::instance()->thread()) {
        qCCritical(lcAutotile) << "ScriptedAlgorithm must be constructed on the main thread";
        return;
    }
    m_watchdog->engine = m_engine;
    m_watchdog->watchdogThread = std::thread([ctx = m_watchdog]() {
        while (true) {
            std::unique_lock<std::mutex> lock(ctx->mutex);
            // Wait for an arm signal (pending == true) or shutdown
            ctx->cv.wait(lock, [&ctx]() {
                return ctx->pending || ctx->shutdown;
            });
            if (ctx->shutdown) {
                return;
            }
            ctx->pending = false;
            const uint64_t gen = ctx->generation;

            // Use cv.wait_for instead of sleep_for so that disarming
            // (generation change) or shutdown wakes us immediately rather than
            // sleeping the full timeout duration unconditionally.
            const bool expired =
                !ctx->cv.wait_for(lock, std::chrono::milliseconds(ScriptWatchdogTimeoutMs), [&ctx, gen]() {
                    return ctx->generation != gen || ctx->shutdown;
                });

            if (ctx->shutdown) {
                return;
            }
            // Only interrupt if we actually timed out (generation unchanged)
            if (expired && ctx->engine) {
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

// Extracted watchdog arm-evaluate-disarm-check pattern into a reusable helper.
// Arms the watchdog, calls fn(), disarms the watchdog, checks for timeout.
// Returns the QJSValue from fn(), or a synthetic error QJSValue on timeout.
QJSValue ScriptedAlgorithm::guardedCall(const std::function<QJSValue()>& fn) const
{
    // Arm the watchdog
    {
        std::lock_guard<std::mutex> lock(m_watchdog->mutex);
        // Clear any stale interrupt from a previous watchdog race where the main
        // thread armed+evaluated+disarmed before the watchdog thread woke,
        // causing a spurious timeout that poisoned the engine's interrupt flag.
        m_engine->setInterrupted(false);
        ++(m_watchdog->generation);
        m_watchdog->pending = true;
    }
    m_watchdog->cv.notify_one();

    // Execute the guarded operation
    const QJSValue result = fn();

    // Disarm the watchdog by advancing generation and atomically check for timeout.
    // setInterrupted(false) MUST be inside the lock to prevent the watchdog
    // thread from firing between unlock and the clear.
    bool wasInterrupted;
    {
        std::lock_guard<std::mutex> lock(m_watchdog->mutex);
        ++(m_watchdog->generation);
        wasInterrupted = m_engine->isInterrupted();
        if (wasInterrupted) {
            m_engine->setInterrupted(false);
        }
    }
    // collectGarbage() moved outside the mutex — generation is already advanced
    // and the interrupt flag is cleared, so the watchdog cannot fire on stale state.
    if (wasInterrupted) {
        m_engine->collectGarbage();
    }
    // Notify the watchdog so it wakes from wait_for immediately on disarm
    // instead of sleeping until the full timeout expires.
    m_watchdog->cv.notify_one();

    if (wasInterrupted) {
        // Signal timeout via flag instead of evaluating JS on a just-interrupted engine
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
    // Include "script:" prefix for consistency with ScriptedAlgorithmLoader's registry key
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
    // Reset consolidated metadata struct
    m_metadata = ScriptedHelpers::ScriptMetadata{};

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: failed to open file=" << filePath;
        return false;
    }

    // Reject scripts larger than 1 MB to prevent resource exhaustion
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

    // Parse metadata via helper — single struct assignment
    m_metadata = ScriptedHelpers::parseMetadata(source, filePath);

    // Apply sandbox hardening BEFORE helper injection so that the sandbox
    // restrictions (frozen prototypes, disabled eval/Function) are in place before
    // any user-visible globals are defined. hardenSandbox() also freezes the helper
    // globals via freezeGlobal() — those calls are no-ops until the helpers exist,
    // so we re-freeze them after injection below.
    if (!hardenSandbox(m_engine)) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: sandbox hardening failed, file=" << filePath;
        return false;
    }

    // Inject frozen constants so JS helpers and user scripts can reference them.
    // Check return values — if the engine is broken these would silently be
    // undefined and every helper using them would produce NaN.
    const QJSValue constResult1 = m_engine->evaluate(QStringLiteral("var PZ_MIN_ZONE_SIZE = %1;").arg(MinZoneSizePx),
                                                     QStringLiteral("builtin:constants"));
    const QJSValue constResult2 = m_engine->evaluate(QStringLiteral("var PZ_MIN_SPLIT = %1;").arg(MinSplitRatio),
                                                     QStringLiteral("builtin:constants"));
    const QJSValue constResult3 = m_engine->evaluate(QStringLiteral("var PZ_MAX_SPLIT = %1;").arg(MaxSplitRatio),
                                                     QStringLiteral("builtin:constants"));
    const QJSValue constResult4 = m_engine->evaluate(
        QStringLiteral("var MAX_TREE_DEPTH = %1;").arg(MaxRuntimeTreeDepth), QStringLiteral("builtin:constants"));
    if (constResult1.isError() || constResult2.isError() || constResult3.isError() || constResult4.isError()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: constant injection failed, file=" << filePath;
        return false;
    }

    // Freeze constants immediately so builtin helper scripts cannot reassign them.
    // The frozenGlobals loop below will re-freeze them (harmless no-op on already-frozen properties).
    static const QLatin1String earlyFreezeConstants[] = {
        QLatin1String("PZ_MIN_ZONE_SIZE"),
        QLatin1String("PZ_MIN_SPLIT"),
        QLatin1String("PZ_MAX_SPLIT"),
        QLatin1String("MAX_TREE_DEPTH"),
    };
    for (const auto& name : earlyFreezeConstants) {
        QJSValue r = m_engine->evaluate(
            QStringLiteral("Object.defineProperty(this, '%1', {writable: false, configurable: false});").arg(name));
        if (r.isError()) {
            qCWarning(lcAutotile) << "Failed to early-freeze constant:" << name << r.toString();
            return false;
        }
    }

    // Inject built-in helpers from ScriptedAlgorithmHelpers and JsBuiltins.
    // NOTE: Each helper's exported global name(s) must also appear in the
    // frozenGlobals[] array below — a missing entry is a sandbox escape.
    auto injectBuiltin = [this, &filePath](const QString& source, const QString& label) -> bool {
        if (source.isEmpty()) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: empty builtin source for" << label << "file=" << filePath;
            return false;
        }
        const QJSValue result = m_engine->evaluate(source, label);
        if (result.isError()) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: builtin injection failed for" << label
                                  << "error=" << result.toString() << "file=" << filePath;
            return false;
        }
        return true;
    };

    // Injection order matters: each helper must be injected after its dependencies.
    // Dependency graph (arrows mean "depends on"):
    //   applyTreeGeometry  (standalone)
    //   lShapeLayout       (standalone)
    //   deckLayout          → fillArea
    //   distributeEvenly    (standalone)
    //   distributeWithGaps  (standalone)
    //   distributeWithMinSizes → distributeWithGaps
    //   distributeWithOptionalMins → distributeWithGaps, distributeWithMinSizes
    //   solveTwoPart        (standalone)
    //   solveThreeColumn    (standalone, uses PZ_* constants)
    //   computeCumulativeMinDims (standalone)
    //   appendGracefulDegradation → distributeWithGaps
    //   dwindleLayout       → computeCumulativeMinDims, appendGracefulDegradation
    //   extractMinDims      (standalone)
    //   interleaveStacks    (standalone)
    //   applyPerWindowMinSize (standalone)
    //   extractRegionMaxMin  (standalone)
    //   fillRegion           (standalone)
    //   fillArea             → fillRegion
    //   masterStackLayout   → fillArea, extractRegionMaxMin, solveTwoPart,
    //                         extractMinDims, distributeWithOptionalMins
    //   equalColumnsLayout  → extractMinDims, distributeWithOptionalMins
    //   threeColumnLayout   → fillArea, extractRegionMaxMin, solveThreeColumn,
    //                         extractMinDims, interleaveStacks, distributeWithOptionalMins
    if (!injectBuiltin(ScriptedHelpers::clampSplitRatioJs(), QStringLiteral("builtin:clampSplitRatio"))
        || !injectBuiltin(ScriptedHelpers::applyTreeGeometryJs(), QStringLiteral("builtin:applyTreeGeometry"))
        || !injectBuiltin(ScriptedHelpers::lShapeLayoutJs(), QStringLiteral("builtin:lShapeLayout"))
        || !injectBuiltin(ScriptedHelpers::distributeEvenlyJs(), QStringLiteral("builtin:distributeEvenly"))
        || !injectBuiltin(ScriptedHelpers::distributeWithGapsJs(), QStringLiteral("builtin:distributeWithGaps"))
        || !injectBuiltin(ScriptedHelpers::distributeWithMinSizesJs(), QStringLiteral("builtin:distributeWithMinSizes"))
        || !injectBuiltin(ScriptedHelpers::distributeWithOptionalMinsJs(),
                          QStringLiteral("builtin:distributeWithOptionalMins"))
        || !injectBuiltin(ScriptedHelpers::solveTwoPartJs(), QStringLiteral("builtin:solveTwoPart"))
        || !injectBuiltin(ScriptedHelpers::solveThreeColumnJs(), QStringLiteral("builtin:solveThreeColumn"))
        || !injectBuiltin(ScriptedHelpers::cumulativeMinDimsJs(), QStringLiteral("builtin:computeCumulativeMinDims"))
        || !injectBuiltin(ScriptedHelpers::gracefulDegradationJs(), QStringLiteral("builtin:appendGracefulDegradation"))
        || !injectBuiltin(ScriptedHelpers::dwindleLayoutJs(), QStringLiteral("builtin:dwindleLayout"))
        || !injectBuiltin(ScriptedHelpers::extractMinDimsJs(), QStringLiteral("builtin:extractMinDims"))
        || !injectBuiltin(ScriptedHelpers::interleaveStacksJs(), QStringLiteral("builtin:interleaveStacks"))
        || !injectBuiltin(ScriptedHelpers::applyPerWindowMinSizeJs(), QStringLiteral("builtin:applyPerWindowMinSize"))
        || !injectBuiltin(ScriptedHelpers::extractRegionMaxMinJs(), QStringLiteral("builtin:extractRegionMaxMin"))
        || !injectBuiltin(ScriptedHelpers::fillRegionJs(), QStringLiteral("builtin:fillRegion"))
        || !injectBuiltin(ScriptedHelpers::fillAreaJs(), QStringLiteral("builtin:fillArea"))
        || !injectBuiltin(ScriptedHelpers::deckLayoutJs(), QStringLiteral("builtin:deckLayout"))
        || !injectBuiltin(ScriptedHelpers::masterStackLayoutJs(), QStringLiteral("builtin:masterStackLayout"))
        || !injectBuiltin(ScriptedHelpers::equalColumnsLayoutJs(), QStringLiteral("builtin:equalColumnsLayout"))
        || !injectBuiltin(ScriptedHelpers::threeColumnLayoutJs(), QStringLiteral("builtin:threeColumnLayout"))) {
        return false;
    }

    // Freeze helper globals and constants so user scripts cannot overwrite them.
    // This must happen after injection since hardenSandbox's freezeGlobal calls
    // ran before the helpers existed.
    //
    // IMPORTANT: Every global name injected by builtinHelpers must appear here.
    // A missing entry is a sandbox escape — user scripts could overwrite the helper.
    // Update the static_assert count below when adding/removing builtins.
    // User-exported functions (calculateZones, etc.) are intentionally
    // NOT frozen. When adding a new builtin helper, add its exported
    // global name(s) here.
    static const QLatin1String frozenGlobals[] = {
        // Injected constants (from C++ AutotileDefaults)
        QLatin1String("PZ_MIN_ZONE_SIZE"),
        QLatin1String("PZ_MIN_SPLIT"),
        QLatin1String("PZ_MAX_SPLIT"),
        QLatin1String("MAX_TREE_DEPTH"),
        // Helpers from ScriptedAlgorithmJsBuiltins
        QLatin1String("applyTreeGeometry"),
        QLatin1String("lShapeLayout"),
        QLatin1String("deckLayout"),
        QLatin1String("distributeEvenly"),
        QLatin1String("distributeWithGaps"),
        QLatin1String("distributeWithMinSizes"),
        QLatin1String("distributeWithOptionalMins"),
        QLatin1String("solveTwoPart"),
        QLatin1String("solveThreeColumn"),
        QLatin1String("computeCumulativeMinDims"),
        QLatin1String("appendGracefulDegradation"),
        QLatin1String("dwindleLayout"),
        QLatin1String("extractMinWidths"), // from extractMinDims
        QLatin1String("extractMinHeights"), // from extractMinDims
        QLatin1String("buildStackIsLeft"), // from interleaveStacks
        QLatin1String("interleaveMinWidths"), // from interleaveStacks
        QLatin1String("interleaveMinHeights"), // from interleaveStacks
        QLatin1String("assignInterleavedStacks"), // from interleaveStacks
        QLatin1String("applyPerWindowMinSize"),
        QLatin1String("extractRegionMaxMin"),
        QLatin1String("fillArea"),
        QLatin1String("masterStackLayout"),
        QLatin1String("equalColumnsLayout"),
        QLatin1String("fillRegion"),
        QLatin1String("threeColumnLayout"),
        QLatin1String("_extractMinDims"), // internal helper from extractMinDims (used by extractMinWidths/Heights)
        QLatin1String("clampSplitRatio"),
    };
    static_assert(std::size(frozenGlobals) == 31, "frozenGlobals count mismatch — did you add a new builtin?");
    bool freezeFailed = false;
    for (const auto& name : frozenGlobals) {
        QJSValue freezeResult = m_engine->evaluate(
            QStringLiteral("Object.defineProperty(this, '%1', {writable: false, configurable: false});").arg(name));
        if (freezeResult.isError()) {
            qCWarning(lcAutotile) << "Failed to freeze global:" << name << freezeResult.toString();
            freezeFailed = true;
        }
    }
    if (freezeFailed) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm: aborting load — global freeze failed, file=" << filePath;
        return false;
    }

    // Use guardedCall helper for watchdog arm-evaluate-disarm-check pattern.
    // Wrap user script in an IIFE that shadows eval/Function with undefined via
    // parameter binding. This is defense-in-depth: QJSEngine V4 treats direct
    // eval() as a language-level built-in that bypasses Object.defineProperty on
    // the global, so property-level lockdown alone is insufficient. The IIFE
    // scoping ensures eval/Function resolve to undefined in the user script scope.
    // Exported globals (calculateZones, optional overrides) are re-attached to
    // the global object after the IIFE body executes.
    static const QString wrapPrefix = QStringLiteral("(function(eval, Function) {");
    static const QString wrapSuffix = QStringLiteral(
        "\nif (typeof calculateZones === 'function') this.calculateZones = calculateZones;"
        "if (typeof masterZoneIndex === 'function') this.masterZoneIndex = masterZoneIndex;"
        "if (typeof supportsMasterCount === 'function') this.supportsMasterCount = supportsMasterCount;"
        "if (typeof supportsSplitRatio === 'function') this.supportsSplitRatio = supportsSplitRatio;"
        "if (typeof defaultSplitRatio === 'function') this.defaultSplitRatio = defaultSplitRatio;"
        "if (typeof minimumWindows === 'function') this.minimumWindows = minimumWindows;"
        "if (typeof defaultMaxWindows === 'function') this.defaultMaxWindows = defaultMaxWindows;"
        "if (typeof producesOverlappingZones === 'function') this.producesOverlappingZones = "
        "producesOverlappingZones;"
        "if (typeof centerLayout === 'function') this.centerLayout = centerLayout;"
        "}).call(this, void 0, void 0);\n");
    const QString wrappedSource = wrapPrefix + source + wrapSuffix;
    const QJSValue result = guardedCall([this, &wrappedSource, &filePath]() {
        return m_engine->evaluate(wrappedSource, filePath);
    });

    // Check structured timeout flag instead of parsing error message strings
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
    m_jsCenterLayout = m_engine->globalObject().property(QStringLiteral("centerLayout"));

    // Look up optional lifecycle hook functions
    m_jsOnWindowAdded = m_engine->globalObject().property(QStringLiteral("onWindowAdded"));
    m_jsOnWindowRemoved = m_engine->globalObject().property(QStringLiteral("onWindowRemoved"));
    m_hasLifecycleHooks = m_jsOnWindowAdded.isCallable() || m_jsOnWindowRemoved.isCallable();

    m_valid = true;
    // Cache JS override values through guardedCall so that a malicious function
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
    m_cachedCenterLayout = guardedCacheJsValue(m_jsCenterLayout, m_cachedCenterLayout);
    m_cachedValuesLoaded = true;

    qCInfo(lcAutotile) << "ScriptedAlgorithm: loaded script=" << m_scriptId << "file=" << filePath;
    return true;
}

// calculateZones() is NOT reentrant — it mutates mutable QJSEngine state
// (params object, watchdog generation, interrupt flag). All calls must be serialized
// on the owning thread.
QVector<QRect> ScriptedAlgorithm::calculateZones(const TilingParams& params) const
{
    // Runtime thread guard — QJSEngine is not thread-safe
    if (QThread::currentThread() != thread()) {
        qCWarning(lcAutotile) << "ScriptedAlgorithm::calculateZones called from wrong thread";
        return {};
    }

    // Re-entrancy guard — QJSEngine state is not re-entrant
    if (m_evaluating) {
        qCWarning(lcAutotile) << "Re-entrant calculateZones call on" << m_scriptId << "— returning empty";
        return {};
    }
    ReentrancyGuard guard(m_evaluating);

    if (!m_valid || params.windowCount <= 0 || !params.screenGeometry.isValid()) {
        return {};
    }

    // Compute the usable area after outer gaps
    const QRect area = innerRect(params.screenGeometry, params.outerGaps);

    // Early return for empty area (e.g., gaps exceed screen geometry)
    if (area.isEmpty()) {
        return {};
    }

    // Degenerate screen: area too small for meaningful tiling — fill with stacked zones
    if (area.width() < MinZoneSizePx || area.height() < MinZoneSizePx) {
        QVector<QRect> zones;
        zones.reserve(params.windowCount);
        for (int i = 0; i < params.windowCount; ++i) {
            zones.append(area);
        }
        return zones;
    }

    // Single-window case always fills the area. Scripts cannot customize
    // single-window behavior — this is intentional to avoid unnecessary JS calls.
    if (params.windowCount == 1) {
        return {area};
    }

    // Build the JS params object
    // Normalize innerGap to >= 0 here so every JS script doesn't need to repeat
    // Math.max(0, params.innerGap || 0).
    QJSValue jsParams = m_engine->newObject();
    jsParams.setProperty(QStringLiteral("windowCount"), params.windowCount);
    jsParams.setProperty(QStringLiteral("innerGap"), std::max(0, params.innerGap));

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
        QJSValue jsTree = splitNodeToJSValue(params.state->splitTree()->root());
        // Expose leafCount on the root so JS scripts can check tree validity
        // without traversing (matches C++ SplitTree::leafCount() API)
        jsTree.setProperty(QStringLiteral("leafCount"), params.state->splitTree()->leafCount());
        jsParams.setProperty(QStringLiteral("tree"), jsTree);
    }

    // minSizes array
    // Cap the loop at MaxZones (256) to match the array allocation size
    const int minSizesCap = std::min<int>(params.minSizes.size(), MaxZones);
    QJSValue jsMinSizes = m_engine->newArray(static_cast<uint>(minSizesCap));
    for (int i = 0; i < minSizesCap; ++i) {
        QJSValue entry = m_engine->newObject();
        entry.setProperty(QStringLiteral("w"), params.minSizes[i].width());
        entry.setProperty(QStringLiteral("h"), params.minSizes[i].height());
        jsMinSizes.setProperty(static_cast<quint32>(i), entry);
    }
    jsParams.setProperty(QStringLiteral("minSizes"), jsMinSizes);

    // Per-window metadata: params.windows = [{appId, focused}, ...]
    if (!params.windowInfos.isEmpty()) {
        const int winInfoCap = std::min<int>(params.windowInfos.size(), MaxZones);
        jsParams.setProperty(QStringLiteral("windows"), buildJsWindowArray(params.windowInfos, winInfoCap));
    }

    // Focused window index (-1 if unknown)
    jsParams.setProperty(QStringLiteral("focusedIndex"), params.focusedIndex);

    // Screen metadata: params.screen = {id, portrait, aspectRatio}
    if (!params.screenInfo.id.isEmpty()) {
        QJSValue jsScreen = m_engine->newObject();
        jsScreen.setProperty(QStringLiteral("id"), params.screenInfo.id);
        jsScreen.setProperty(QStringLiteral("portrait"), params.screenInfo.portrait);
        jsScreen.setProperty(QStringLiteral("aspectRatio"), params.screenInfo.aspectRatio);
        jsParams.setProperty(QStringLiteral("screen"), jsScreen);
    }

    // Custom algorithm parameters: params.custom = {paramName: value, ...}
    if (!params.customParams.isEmpty()) {
        QJSValue jsCustom = m_engine->newObject();
        for (auto it = params.customParams.constBegin(); it != params.customParams.constEnd(); ++it) {
            const QVariant& val = it.value();
            if (val.typeId() == QMetaType::Bool) {
                jsCustom.setProperty(it.key(), val.toBool());
            } else if (val.typeId() == QMetaType::Double || val.typeId() == QMetaType::Float
                       || val.typeId() == QMetaType::Int) {
                jsCustom.setProperty(it.key(), val.toDouble());
            } else {
                jsCustom.setProperty(it.key(), val.toString());
            }
        }
        jsParams.setProperty(QStringLiteral("custom"), jsCustom);
    }

    // Use guardedCall helper for watchdog arm-evaluate-disarm-check pattern
    const QJSValue result = guardedCall([this, &jsParams]() {
        return m_calculateZonesFn.call({jsParams});
    });

    // Check structured timeout flag instead of parsing error message strings
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

    // Delegate to helpers for array conversion and bounds clamping
    const QVector<QRect> zones = ScriptedHelpers::jsArrayToRects(result, m_scriptId, MaxZones);
    const QVector<QRect> clamped = ScriptedHelpers::clampZonesToArea(zones, area, m_scriptId);

    // Collect garbage periodically to limit script memory accumulation.
    // Every-call GC is expensive during frequent retiles (e.g. window drag);
    // every 8th call balances memory hygiene against jank.
    if (++m_gcCounter % GcInterval == 0) {
        m_engine->collectGarbage();
    }

    return clamped;
}

QJSValue ScriptedAlgorithm::splitNodeToJSValue(const SplitNode* node, int depth) const
{
    if (!node || !m_engine || depth > MaxTreeConversionDepth) {
        return QJSValue();
    }

    // Cache Object.freeze once at the top of the recursion instead of looking it up per node
    const QJSValue freezeFn =
        m_engine->globalObject().property(QStringLiteral("Object")).property(QStringLiteral("freeze"));
    if (!freezeFn.isCallable()) {
        qCWarning(lcAutotile) << "Object.freeze not callable — tree will be mutable to JS scripts";
    }
    int nodeCount = 0;
    return splitNodeToJSValueImpl(node, freezeFn, depth, nodeCount);
}

QJSValue ScriptedAlgorithm::splitNodeToJSValueImpl(const SplitNode* node, const QJSValue& freezeFn, int depth,
                                                   int& nodeCount) const
{
    if (!node || !m_engine || depth > MaxTreeConversionDepth) {
        return QJSValue();
    }

    static constexpr int MaxNodeConversionCount = 512;
    if (++nodeCount > MaxNodeConversionCount) {
        return QJSValue();
    }

    QJSValue jsNode = m_engine->newObject();

    if (node->isLeaf()) {
        jsNode.setProperty(QStringLiteral("windowId"), node->windowId);
    } else {
        const qreal ratio = std::isnan(node->splitRatio) ? 0.5 : std::clamp(node->splitRatio, 0.1, 0.9);
        jsNode.setProperty(QStringLiteral("ratio"), ratio);
        jsNode.setProperty(QStringLiteral("horizontal"), node->splitHorizontal);
        jsNode.setProperty(QStringLiteral("first"),
                           splitNodeToJSValueImpl(node->first.get(), freezeFn, depth + 1, nodeCount));
        jsNode.setProperty(QStringLiteral("second"),
                           splitNodeToJSValueImpl(node->second.get(), freezeFn, depth + 1, nodeCount));
    }

    // Freeze the node so scripts cannot mutate the tree representation
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
    // Unified three-tier resolution via template helper
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
    // Use resolveJsOverrideClamped to unify clamped resolution
    const qreal fallback =
        (m_metadata.defaultSplitRatio > 0.0) ? m_metadata.defaultSplitRatio : TilingAlgorithm::defaultSplitRatio();
    return resolveJsOverrideClamped<qreal>(m_jsDefaultSplitRatio, m_cachedDefaultSplitRatio, fallback, MinSplitRatio,
                                           MaxSplitRatio);
}

int ScriptedAlgorithm::minimumWindows() const
{
    // Use resolveJsOverrideClamped to unify clamped resolution
    const int fallback =
        (m_metadata.minimumWindows > 0) ? m_metadata.minimumWindows : TilingAlgorithm::minimumWindows();
    return resolveJsOverrideClamped<int>(m_jsMinimumWindows, m_cachedMinimumWindows, fallback, MinMetadataWindows,
                                         MaxMetadataWindows);
}

int ScriptedAlgorithm::defaultMaxWindows() const
{
    // Use resolveJsOverrideClamped to unify clamped resolution
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

bool ScriptedAlgorithm::supportsMinSizes() const noexcept
{
    return m_metadata.supportsMinSizes;
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

bool ScriptedAlgorithm::centerLayout() const
{
    return resolveJsOverride<bool>(m_jsCenterLayout, m_cachedCenterLayout, m_metadata.centerLayout);
}

bool ScriptedAlgorithm::isScripted() const noexcept
{
    return true;
}

bool ScriptedAlgorithm::isUserScript() const noexcept
{
    return m_isUserScript;
}

QString ScriptedAlgorithm::builtinId() const
{
    return m_metadata.builtinId;
}

void ScriptedAlgorithm::prepareTilingState(TilingState* state) const
{
    if (!m_metadata.supportsMemory) {
        return; // Only memory-aware scripts need tree preparation
    }

    if (!state || state->splitTree()) {
        return; // Already has a tree (or no state)
    }

    // Only reset the split ratio to our default (0.5) if it still holds a
    // value from a different algorithm (e.g., MasterStack's 0.6).
    const qreal currentRatio = state->splitRatio();
    const qreal defRatio = defaultSplitRatio();
    // Reset split ratio to our default when it still holds a value from a
    // different algorithm (e.g. MasterStack's 0.6). Small differences within
    // the hysteresis band are kept so user fine-tuning is not discarded.
    if (currentRatio > defRatio + AutotileDefaults::SplitRatioHysteresis
        || currentRatio < defRatio - AutotileDefaults::SplitRatioHysteresis) {
        state->setSplitRatio(defRatio);
    }

    const QStringList tiledWindows = state->tiledWindows();
    if (tiledWindows.size() <= 1) {
        return; // No tree needed for 0-1 windows
    }

    // Cap window count to prevent unbounded tree growth (MaxZones = 256)
    const int maxWindows = qMin(static_cast<int>(tiledWindows.size()), AutotileDefaults::MaxZones);

    const qreal ratio = state->splitRatio();
    auto newTree = std::make_unique<SplitTree>();
    for (int i = 0; i < maxWindows; ++i) {
        newTree->insertAtEnd(tiledWindows[i], ratio);
    }
    state->setSplitTree(std::move(newTree));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle Hooks (v2)
// ═══════════════════════════════════════════════════════════════════════════════

bool ScriptedAlgorithm::supportsLifecycleHooks() const noexcept
{
    return m_hasLifecycleHooks;
}

QJSValue ScriptedAlgorithm::buildJsWindowArray(const QVector<WindowInfo>& infos, int cap) const
{
    QJSValue jsWindows = m_engine->newArray(static_cast<uint>(cap));
    for (int i = 0; i < cap; ++i) {
        QJSValue entry = m_engine->newObject();
        entry.setProperty(QStringLiteral("appId"), infos[i].appId);
        entry.setProperty(QStringLiteral("focused"), infos[i].focused);
        jsWindows.setProperty(static_cast<quint32>(i), entry);
    }
    return jsWindows;
}

QJSValue ScriptedAlgorithm::buildJsState(const TilingState* state) const
{
    QJSValue jsState = m_engine->newObject();
    jsState.setProperty(QStringLiteral("windowCount"), state->tiledWindowCount());
    jsState.setProperty(QStringLiteral("masterCount"), state->masterCount());
    jsState.setProperty(QStringLiteral("splitRatio"), std::clamp(state->splitRatio(), MinSplitRatio, MaxSplitRatio));

    const QStringList windows = state->tiledWindows();
    const QString focusedWin = state->focusedWindow();
    const int winCount = windows.size();
    QVector<WindowInfo> infos;
    infos.reserve(winCount);
    int focusedIdx = -1;
    for (int i = 0; i < winCount; ++i) {
        WindowInfo info;
        info.appId = Utils::extractAppId(windows[i]);
        info.focused = (windows[i] == focusedWin);
        if (info.focused) {
            focusedIdx = i;
        }
        infos.append(info);
    }

    jsState.setProperty(QStringLiteral("windows"), buildJsWindowArray(infos, winCount));
    jsState.setProperty(QStringLiteral("focusedIndex"), focusedIdx);

    return jsState;
}

void ScriptedAlgorithm::onWindowAdded(TilingState* state, int windowIndex)
{
    if (!m_jsOnWindowAdded.isCallable() || !state) {
        return;
    }
    QJSValue jsState = buildJsState(state);
    guardedCall([this, &jsState, windowIndex]() {
        return m_jsOnWindowAdded.call({jsState, QJSValue(windowIndex)});
    });
}

void ScriptedAlgorithm::onWindowRemoved(TilingState* state, int windowIndex)
{
    if (!m_jsOnWindowRemoved.isCallable() || !state) {
        return;
    }
    QJSValue jsState = buildJsState(state);
    // Expose countAfterRemoval so hook authors don't need to subtract 1 from windowCount
    jsState.setProperty(QStringLiteral("countAfterRemoval"), state->tiledWindowCount() - 1);
    guardedCall([this, &jsState, windowIndex]() {
        return m_jsOnWindowRemoved.call({jsState, QJSValue(windowIndex)});
    });
}

const QVector<ScriptedHelpers::CustomParamDef>& ScriptedAlgorithm::customParamDefs() const
{
    return m_metadata.customParams;
}

} // namespace PlasmaZones
