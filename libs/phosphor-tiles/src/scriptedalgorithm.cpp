// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/ScriptedAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmHelpers.h>
#include <PhosphorTiles/ScriptedAlgorithmJsBuiltins.h>
#include <PhosphorTiles/ScriptedAlgorithmSandbox.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/ScriptedAlgorithmWatchdog.h>
#include "tileslogging.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <functional>

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

namespace PhosphorTiles {

using namespace AutotileDefaults;

// ScriptWatchdogTimeoutMs is defined in AutotileConstants.h (inside the
// AutotileDefaults namespace). The `using namespace AutotileDefaults;`
// above brings it into scope; no local duplicate needed.

// resolveJsOverride<T> / resolveJsOverrideClamped<T> are defined inline in
// the header so the _hooks.cpp and _tree.cpp TUs can instantiate them.

ScriptedAlgorithm::ScriptedAlgorithm(const QString& filePath, std::shared_ptr<ScriptedAlgorithmWatchdog> watchdog,
                                     QObject* parent)
    : TilingAlgorithm(parent)
    , m_engine(new QJSEngine(this))
    , m_watchdog(std::move(watchdog))
{
    // ScriptedAlgorithmJsBuiltins uses static lazy initialization (function-local
    // statics) that is thread-safe for first-call but not for concurrent QJSEngine
    // evaluation. Enforce the single-thread contract.
    Q_ASSERT(!QCoreApplication::instance() || QThread::currentThread() == QCoreApplication::instance()->thread());
    // Runtime guard — Q_ASSERT vanishes in release builds but QJSEngine corruption
    // from off-thread construction is silent and catastrophic.
    if (QCoreApplication::instance() && QThread::currentThread() != QCoreApplication::instance()->thread()) {
        qCCritical(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm must be constructed on the main thread";
        return;
    }

    // Watchdog is borrowed from the loader. arm()/disarm() happen per
    // guardedCall(). m_watchdog == nullptr disables the safety net (used
    // by direct-construction unit tests where adding a thread-creation
    // cost per test case would dwarf the script's own execution time).
    // Production paths (the loader) always pass a non-null watchdog.

    loadScript(filePath);
}

ScriptedAlgorithm::~ScriptedAlgorithm()
{
    // Remove ourselves from the borrowed watchdog's tracking map before
    // destruction so the watchdog thread cannot interrupt an engine that is
    // about to disappear. unregister() takes the watchdog mutex, so it
    // serialises with an in-flight interrupt attempt.
    if (m_watchdog) {
        m_watchdog->unregister(this);
    }
}

void ScriptedAlgorithm::interruptEngine()
{
    // Called from the shared watchdog thread when a guarded JS call exceeds
    // its deadline. QJSEngine::setInterrupted is documented thread-safe
    // relative to the main-thread evaluation it targets.
    if (m_engine) {
        m_engine->setInterrupted(true);
    }
}

// Extracted watchdog arm-evaluate-disarm-check pattern into a reusable helper.
// Arms the watchdog, calls fn(), disarms the watchdog, checks for timeout.
// Returns the QJSValue from fn(), or a synthetic error QJSValue on timeout.
QJSValue ScriptedAlgorithm::guardedCall(const std::function<QJSValue()>& fn) const
{
    // Clear any stale interrupt from a previous race where the main thread
    // armed+evaluated+disarmed before the watchdog thread checked the deadline,
    // leaving a spurious interrupt flag set. Safe to clear here because the
    // watchdog is disarmed for this instance — no thread is about to set it.
    m_engine->setInterrupted(false);

    // Arm the loader-owned watchdog. After this call returns, the watchdog
    // thread may interrupt our engine at any moment if the deadline passes
    // before disarm() runs. m_watchdog == nullptr (headless test fixtures)
    // skips the safety net entirely; the JS code runs unsupervised.
    if (m_watchdog) {
        m_watchdog->arm(const_cast<ScriptedAlgorithm*>(this), ScriptWatchdogTimeoutMs);
    }

    // Execute the guarded operation
    const QJSValue result = fn();

    // Disarm the watchdog first so the watchdog thread can no longer fire a
    // late interrupt. The disarm() call holds the watchdog mutex while it
    // bumps our generation; any interrupt the watchdog was about to issue for
    // the previous generation is therefore suppressed. After this returns the
    // only interrupt flag we could observe is one the watchdog already set
    // before disarm() took the mutex, so checking/clearing below is race-free.
    if (m_watchdog) {
        m_watchdog->disarm(const_cast<ScriptedAlgorithm*>(this));
    }

    const bool wasInterrupted = m_engine->isInterrupted();
    if (wasInterrupted) {
        m_engine->setInterrupted(false);
        // Running GC here is safe: the watchdog is disarmed (no interrupt
        // can land while we GC), and collectGarbage tidies up any
        // half-evaluated state left by the interrupted script.
        m_engine->collectGarbage();
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
    m_cachedDefaultSplitRatio = DefaultSplitRatio;
    m_cachedMinimumWindows = 1;
    m_cachedSupportsMasterCount = false;
    m_cachedSupportsSplitRatio = false;
    m_cachedProducesOverlappingZones = false;
    // Reset consolidated metadata struct
    m_metadata = ScriptedHelpers::ScriptMetadata{};

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: failed to open file=" << filePath;
        return false;
    }

    // Reject scripts larger than 1 MB to prevent resource exhaustion
    static constexpr qint64 MaxScriptSizeBytes = 1024 * 1024; // 1 MB
    if (file.size() > MaxScriptSizeBytes) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "Script file too large:" << filePath << file.size() << "bytes (max" << MaxScriptSizeBytes << ")";
        return false;
    }

    const QString source = QTextStream(&file).readAll();
    file.close();

    if (source.isEmpty()) {
        qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: empty script file=" << filePath;
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
        qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: sandbox hardening failed, file=" << filePath;
        return false;
    }

    // Shared freeze helper — locks a global so subsequent code (later
    // builtins, the user script) cannot redefine or reassign it. This is
    // the sandbox's core integrity guarantee: any name left writable is a
    // potential escape vector.
    auto freezeGlobal = [this, &filePath](QLatin1String name, const QString& context) -> bool {
        QJSValue r = m_engine->evaluate(
            QStringLiteral("Object.defineProperty(this, '%1', {writable: false, configurable: false});").arg(name));
        if (r.isError()) {
            qCWarning(PhosphorTiles::lcTilesLib)
                << "Failed to freeze global:" << name << "context=" << context << "file=" << filePath << r.toString();
            return false;
        }
        return true;
    };

    // Inject frozen constants so JS helpers and user scripts can reference them.
    //
    // Constants are set directly as properties on the global object via
    // QJSValue::setProperty rather than stitched into JS source. The
    // stitched-source approach was locale-dependent (qreal formatting could
    // emit "0,5" under de_DE) and fragile to type changes on the C++ side;
    // binding via setProperty bypasses the JS parser entirely and
    // round-trips the native numeric value.
    //
    // Each constant is frozen immediately after being set so a later helper
    // or user script cannot shadow it via `this.PZ_MIN_SPLIT = ...`.
    QJSValue globalObj = m_engine->globalObject();
    struct Constant
    {
        QLatin1String name;
        QJSValue value;
    };
    const Constant constants[] = {
        {QLatin1String("PZ_MIN_ZONE_SIZE"), QJSValue(static_cast<int>(MinZoneSizePx))},
        {QLatin1String("PZ_MIN_SPLIT"), QJSValue(static_cast<double>(MinSplitRatio))},
        {QLatin1String("PZ_MAX_SPLIT"), QJSValue(static_cast<double>(MaxSplitRatio))},
        {QLatin1String("MAX_TREE_DEPTH"), QJSValue(static_cast<int>(MaxRuntimeTreeDepth))},
    };
    for (const auto& c : constants) {
        globalObj.setProperty(QString(c.name), c.value);
        if (!freezeGlobal(c.name, QStringLiteral("constant"))) {
            return false;
        }
    }

    // Inject each builtin helper and immediately freeze its exported
    // global names before the next helper runs. Freezing in one batch at
    // the end would leave a window where an already-injected helper could
    // be silently shadowed by a later one via plain `this.X = ...`
    // assignment — the trusted builtin set makes that a latent hazard
    // today, but the per-step freeze closes the gap so adding a new helper
    // can't regress the invariant.
    //
    // NOTE: Each entry's `exports` list must name every global the helper
    // defines. A missing name is a sandbox escape — user scripts could
    // overwrite the helper. User-exported functions (calculateZones, etc.)
    // are intentionally NOT frozen.
    auto injectAndFreeze = [this, &filePath, &freezeGlobal](const QString& source, QLatin1String label,
                                                            std::initializer_list<QLatin1String> exports) -> bool {
        if (source.isEmpty()) {
            qCWarning(PhosphorTiles::lcTilesLib)
                << "ScriptedAlgorithm: empty builtin source for" << label << "file=" << filePath;
            return false;
        }
        const QJSValue result = m_engine->evaluate(source, QString(label));
        if (result.isError()) {
            qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: builtin injection failed for" << label
                                                 << "error=" << result.toString() << "file=" << filePath;
            return false;
        }
        for (QLatin1String exp : exports) {
            if (!freezeGlobal(exp, QString(label))) {
                return false;
            }
        }
        return true;
    };

    // Injection order matters: each helper must be injected after its dependencies.
    // Dependency graph (arrows mean "depends on"):
    //   clampSplitRatio    (standalone, utility)
    //   applyTreeGeometry  (standalone)
    //   lShapeLayout       (standalone)
    //   distributeEvenly   (standalone)
    //   distributeWithGaps (standalone)
    //   distributeWithMinSizes → distributeWithGaps
    //   distributeWithOptionalMins → distributeWithGaps, distributeWithMinSizes
    //   solveTwoPart       (standalone)
    //   solveThreeColumn   (standalone, uses PZ_* constants)
    //   computeCumulativeMinDims (standalone)
    //   appendGracefulDegradation → distributeWithGaps
    //   dwindleLayout      → computeCumulativeMinDims, appendGracefulDegradation
    //   extractMinDims     (standalone) — exports extractMinWidths/Heights/_extractMinDims
    //   interleaveStacks   (standalone) — exports 4 helpers
    //   applyPerWindowMinSize (standalone)
    //   extractRegionMaxMin (standalone)
    //   fillRegion         (standalone)
    //   fillArea           → fillRegion
    //   deckLayout         → fillArea
    //   masterStackLayout  → fillArea, extractRegionMaxMin, solveTwoPart, extractMinDims, distributeWithOptionalMins
    //   equalColumnsLayout → extractMinDims, distributeWithOptionalMins
    //   threeColumnLayout  → fillArea, extractRegionMaxMin, solveThreeColumn, extractMinDims, interleaveStacks,
    //                        distributeWithOptionalMins
    const bool ok = injectAndFreeze(ScriptedHelpers::clampSplitRatioJs(), QLatin1String("builtin:clampSplitRatio"),
                                    {QLatin1String("clampSplitRatio")})
        && injectAndFreeze(ScriptedHelpers::applyTreeGeometryJs(), QLatin1String("builtin:applyTreeGeometry"),
                           {QLatin1String("applyTreeGeometry")})
        && injectAndFreeze(ScriptedHelpers::lShapeLayoutJs(), QLatin1String("builtin:lShapeLayout"),
                           {QLatin1String("lShapeLayout")})
        && injectAndFreeze(ScriptedHelpers::distributeEvenlyJs(), QLatin1String("builtin:distributeEvenly"),
                           {QLatin1String("distributeEvenly")})
        && injectAndFreeze(ScriptedHelpers::distributeWithGapsJs(), QLatin1String("builtin:distributeWithGaps"),
                           {QLatin1String("distributeWithGaps")})
        && injectAndFreeze(ScriptedHelpers::distributeWithMinSizesJs(), QLatin1String("builtin:distributeWithMinSizes"),
                           {QLatin1String("distributeWithMinSizes")})
        && injectAndFreeze(ScriptedHelpers::distributeWithOptionalMinsJs(),
                           QLatin1String("builtin:distributeWithOptionalMins"),
                           {QLatin1String("distributeWithOptionalMins")})
        && injectAndFreeze(ScriptedHelpers::solveTwoPartJs(), QLatin1String("builtin:solveTwoPart"),
                           {QLatin1String("solveTwoPart")})
        && injectAndFreeze(ScriptedHelpers::solveThreeColumnJs(), QLatin1String("builtin:solveThreeColumn"),
                           {QLatin1String("solveThreeColumn")})
        && injectAndFreeze(ScriptedHelpers::cumulativeMinDimsJs(), QLatin1String("builtin:computeCumulativeMinDims"),
                           {QLatin1String("computeCumulativeMinDims")})
        && injectAndFreeze(ScriptedHelpers::gracefulDegradationJs(), QLatin1String("builtin:appendGracefulDegradation"),
                           {QLatin1String("appendGracefulDegradation")})
        && injectAndFreeze(ScriptedHelpers::dwindleLayoutJs(), QLatin1String("builtin:dwindleLayout"),
                           {QLatin1String("dwindleLayout")})
        && injectAndFreeze(ScriptedHelpers::extractMinDimsJs(), QLatin1String("builtin:extractMinDims"),
                           {QLatin1String("extractMinWidths"), QLatin1String("extractMinHeights"),
                            QLatin1String("_extractMinDims")})
        && injectAndFreeze(ScriptedHelpers::interleaveStacksJs(), QLatin1String("builtin:interleaveStacks"),
                           {QLatin1String("buildStackIsLeft"), QLatin1String("interleaveMinWidths"),
                            QLatin1String("interleaveMinHeights"), QLatin1String("assignInterleavedStacks")})
        && injectAndFreeze(ScriptedHelpers::applyPerWindowMinSizeJs(), QLatin1String("builtin:applyPerWindowMinSize"),
                           {QLatin1String("applyPerWindowMinSize")})
        && injectAndFreeze(ScriptedHelpers::extractRegionMaxMinJs(), QLatin1String("builtin:extractRegionMaxMin"),
                           {QLatin1String("extractRegionMaxMin")})
        && injectAndFreeze(ScriptedHelpers::fillRegionJs(), QLatin1String("builtin:fillRegion"),
                           {QLatin1String("fillRegion")})
        && injectAndFreeze(ScriptedHelpers::fillAreaJs(), QLatin1String("builtin:fillArea"),
                           {QLatin1String("fillArea")})
        && injectAndFreeze(ScriptedHelpers::deckLayoutJs(), QLatin1String("builtin:deckLayout"),
                           {QLatin1String("deckLayout")})
        && injectAndFreeze(ScriptedHelpers::masterStackLayoutJs(), QLatin1String("builtin:masterStackLayout"),
                           {QLatin1String("masterStackLayout")})
        && injectAndFreeze(ScriptedHelpers::equalColumnsLayoutJs(), QLatin1String("builtin:equalColumnsLayout"),
                           {QLatin1String("equalColumnsLayout")})
        && injectAndFreeze(ScriptedHelpers::threeColumnLayoutJs(), QLatin1String("builtin:threeColumnLayout"),
                           {QLatin1String("threeColumnLayout")});
    if (!ok) {
        return false;
    }

    // Use guardedCall helper for watchdog arm-evaluate-disarm-check pattern.
    // Wrap user script in an IIFE that shadows eval/Function (and the three
    // generator/async constructors) with undefined via parameter binding. This
    // is defense-in-depth: QJSEngine V4 treats direct eval() as a language-level
    // built-in that bypasses Object.defineProperty on the global, so
    // property-level lockdown alone is insufficient. The IIFE scoping ensures
    // these names resolve to undefined (bound to `void 0`) in the user script
    // scope — attempting to call any of them throws "undefined is not a
    // function", defeating prototype-chain re-acquisition of the constructors.
    // Exported globals (calculateZones, optional overrides) are re-attached to
    // the global object after the IIFE body executes.
    static const QString wrapPrefix =
        QStringLiteral("(function(eval, Function, AsyncFunction, GeneratorFunction, AsyncGeneratorFunction) {");
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
        "if (typeof customParams !== 'undefined') this.customParams = customParams;"
        "}).call(this, void 0, void 0, void 0, void 0, void 0);\n");
    const QString wrappedSource = wrapPrefix + source + wrapSuffix;
    const QJSValue result = guardedCall([this, &wrappedSource, &filePath]() {
        return m_engine->evaluate(wrappedSource, filePath);
    });

    // Check structured timeout flag instead of parsing error message strings
    if (m_lastCallTimedOut) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "ScriptedAlgorithm: script timed out during evaluate, file=" << filePath;
        return false;
    }
    if (result.isError()) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "ScriptedAlgorithm: evaluation error file=" << filePath
            << "line=" << result.property(QStringLiteral("lineNumber")).toInt() << "message=" << result.toString();
        return false;
    }

    // Look up the required calculateZones function
    m_calculateZonesFn = m_engine->globalObject().property(QStringLiteral("calculateZones"));
    if (!m_calculateZonesFn.isCallable()) {
        qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: no callable calculateZones() file=" << filePath;
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

    // Read JS-exported customParams array. Supplements (and takes priority over)
    // the comment-parsed @param metadata — scripts can declare custom parameters
    // as a plain JS array instead of fragile regex-parsed comments.
    const QJSValue jsCustomParams = m_engine->globalObject().property(QStringLiteral("customParams"));
    if (jsCustomParams.isArray()) {
        QVector<ScriptedHelpers::CustomParamDef> jsParams;
        const int len = jsCustomParams.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < len; ++i) {
            const QJSValue entry = jsCustomParams.property(static_cast<quint32>(i));
            if (!entry.isObject()) {
                continue;
            }
            ScriptedHelpers::CustomParamDef def;
            def.name = entry.property(QStringLiteral("name")).toString().left(64);
            def.type = entry.property(QStringLiteral("type")).toString();
            def.description = entry.property(QStringLiteral("description")).toString().left(200);
            if (def.name.isEmpty() || def.type.isEmpty()) {
                continue;
            }
            if (def.type == QLatin1String("number")) {
                const QJSValue defVal = entry.property(QStringLiteral("default"));
                def.defaultValue = defVal.isNumber() ? defVal.toNumber() : 0.0;
                const QJSValue minVal = entry.property(QStringLiteral("min"));
                def.minValue = minVal.isNumber() ? minVal.toNumber() : 0.0;
                const QJSValue maxVal = entry.property(QStringLiteral("max"));
                def.maxValue = maxVal.isNumber() ? maxVal.toNumber() : 1.0;
                if (def.minValue > def.maxValue) {
                    std::swap(def.minValue, def.maxValue);
                }
            } else if (def.type == QLatin1String("bool")) {
                def.defaultValue = entry.property(QStringLiteral("default")).toBool();
            } else if (def.type == QLatin1String("enum")) {
                def.defaultValue = entry.property(QStringLiteral("default")).toString();
                const QJSValue opts = entry.property(QStringLiteral("options"));
                if (opts.isArray()) {
                    const int optLen = opts.property(QStringLiteral("length")).toInt();
                    for (int j = 0; j < optLen; ++j) {
                        const QString opt = opts.property(static_cast<quint32>(j)).toString().left(64);
                        if (!opt.isEmpty()) {
                            def.enumOptions.append(opt);
                        }
                    }
                }
            } else {
                continue;
            }
            jsParams.append(def);
        }
        if (!jsParams.isEmpty()) {
            m_metadata.customParams = jsParams;
        }
    }

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
            qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: JS override timed out, using default";
            return fallback;
        }
        if (!r.isError() && detail::jsValueHasType<T>(r))
            return detail::jsValueTo<T>(r);
        return fallback;
    };
    // Seed caches from parsed metadata so scripts that only declare values via
    // `// @defaultSplitRatio 0.6`-style comments (no JS override function) are
    // honoured. Without this, guardedCacheJsValue() falls back to the raw C++
    // default and metadata silently has no effect at cache time.
    if (m_metadata.defaultSplitRatio > 0.0) {
        m_cachedDefaultSplitRatio = m_metadata.defaultSplitRatio;
    }
    if (m_metadata.minimumWindows > 0) {
        m_cachedMinimumWindows = m_metadata.minimumWindows;
    }
    if (m_metadata.defaultMaxWindows > 0) {
        m_cachedDefaultMaxWindows = m_metadata.defaultMaxWindows;
    }
    if (m_metadata.masterZoneIndex >= 0) {
        m_cachedMasterZoneIndex = m_metadata.masterZoneIndex;
    }
    m_cachedSupportsMasterCount = m_metadata.supportsMasterCount;
    m_cachedSupportsSplitRatio = m_metadata.supportsSplitRatio;
    m_cachedProducesOverlappingZones = m_metadata.producesOverlappingZones;
    m_cachedCenterLayout = m_metadata.centerLayout;

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

    qCInfo(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: loaded script=" << m_scriptId << "file=" << filePath;
    return true;
}

// calculateZones() is NOT reentrant — it mutates mutable QJSEngine state
// (params object, watchdog generation, interrupt flag). All calls must be serialized
// on the owning thread.
QVector<QRect> ScriptedAlgorithm::calculateZones(const TilingParams& params) const
{
    // Runtime thread guard — QJSEngine is not thread-safe
    if (QThread::currentThread() != thread()) {
        qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm::calculateZones called from wrong thread";
        return {};
    }

    // Re-entrancy guard — QJSEngine state is not re-entrant
    if (m_evaluating) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "Re-entrant calculateZones call on" << m_scriptId << "— returning empty";
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
            } else if (AutotileDefaults::isNumericMetaType(val.typeId())) {
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
        qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithm: script timed out, script=" << m_scriptId;
        return {};
    }
    if (result.isError()) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "ScriptedAlgorithm: calculateZones() error script=" << m_scriptId << "message=" << result.toString();
        return {};
    }

    if (!result.isArray()) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "ScriptedAlgorithm: calculateZones() did not return array script=" << m_scriptId;
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

// splitNodeToJSValue + splitNodeToJSValueImpl live in scriptedalgorithm_tree.cpp.
// Virtual accessors + prepareTilingState + lifecycle-hook (v2) + custom-parameter
// surface live in scriptedalgorithm_hooks.cpp. Keeps this TU under the 800-line cap.

} // namespace PhosphorTiles
