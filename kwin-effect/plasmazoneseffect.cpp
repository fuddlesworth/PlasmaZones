// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/StaggerTimer.h>

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/WireTypes.h>
#include <PhosphorIdentity/ScreenId.h>
#include <PhosphorIdentity/WindowId.h>

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <unordered_set>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QTime>
#include <QVector4D>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QRunnable>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QtMath>
#include <QPointer>
#include <QVarLengthArray>
#include <window.h>
#include <workspace.h>
#include <core/output.h> // For Output::name() for multi-monitor support
#include <scene/windowitem.h>
#include <scene/surfaceitem.h>
#include <scene/outlinedborderitem.h>
#include <scene/borderoutline.h>

#include "autotilehandler.h"
#include "compositorclock.h"
#include "kwin_compositor_bridge.h"
#include "screenchangehandler.h"
#include "snapassisthandler.h"
#include "navigationhandler.h"
#include "windowanimator.h"
#include "dragtracker.h"
#include <PhosphorProtocol/ServiceConstants.h>
// QGuiApplication::queryKeyboardModifiers() doesn't work in KWin effects on Wayland
// because the effect runs inside the compositor process. We use mouseChanged instead.

namespace PlasmaZones {

Q_LOGGING_CATEGORY(lcEffect, "plasmazones.effect", QtInfoMsg)

namespace {
// Duplicated from daemon's configkeys.h — effect cannot include daemon headers
constexpr QLatin1String TriggerModifierField("modifier");
constexpr QLatin1String TriggerMouseButtonField("mouseButton");

/// Monotonic milliseconds since steady_clock epoch. Used by time-based shader
/// transitions for elapsed-progress math. We deliberately avoid
/// QDateTime::currentMSecsSinceEpoch — wall-clock isn't monotonic, and an NTP
/// adjustment mid-transition can run elapsed backwards (or jump it past the
/// duration) and either freeze or prematurely tear down the shader leg.
/// std::chrono::steady_clock matches the clock the phosphor-animation-layer
/// SurfaceAnimator already uses for its motion ticks.
inline qint64 shaderClockNowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Upper bound on how long the effect waits for the daemon's endDrag reply.
// If the daemon is blocked (layout recompute, overlay teardown, heavy
// handler), exceeding this budget means the compositor would otherwise
// stall waiting on a reply that may never come. On expiry the window is
// left at its release position and a warning is logged.
constexpr int EndDragTimeoutMs = 500;

/// Splice `#define PLASMAZONES_KWIN` between the shader's `#version`
/// directive and the rest of the source. The macro selects the
/// default-block branch in `data/animations/shared/animation_uniforms.glsl`,
/// which is what KWin's classic-GL `KWin::GLShader` API requires (no UBO
/// bind path). The GLSL spec disallows non-comment, non-whitespace tokens
/// before `#version`, so the define cannot just be prepended verbatim —
/// we find the first newline at or after the `#version` line and inject
/// after it.
///
/// Defensive fallback: if no `#version` is present (a hand-rolled
/// shader that ships without one), synthesize `#version 450` and warn.
/// KWin on Wayland with modern Mesa runs core profile, where the
/// directive is mandatory — a bare `#define` would fail compile with
/// a confusing line-1 error. The bake test on the daemon side already
/// catches any built-in shader that ships without `#version`; the
/// fallback exists to surface third-party packs that violate the
/// contract with a useful journal entry rather than a cryptic GLSL
/// error.
inline QByteArray injectKwinDefineAfterVersion(const QString& source)
{
    // Strip a leading UTF-8 BOM (U+FEFF) before anything else. The BOM
    // is not a Unicode whitespace category, so QString::trimmed() does
    // NOT remove it — without this strip a BOM-prefixed shader's first
    // line is "﻿#version 450", trimmed("...") still leads with the
    // BOM, the `startsWith("#version")` check fails, and we fall into
    // the "no #version" prepend path. That writes
    // `#define PLASMAZONES_KWIN\n﻿#version 450...`, which the
    // GLSL compiler rejects because `#version` must be the first
    // non-comment token of the source.
    QString working = source;
    if (working.startsWith(QChar(0xFEFF))) {
        working.remove(0, 1);
    }
    if (working.isEmpty()) {
        // BOM-only source (or genuinely empty input) — emit a debug
        // breadcrumb so a hot-reload that lands here surfaces the real
        // cause faster than "GLSL compile failed: line 1 unexpected".
        // Debug-level rather than warn because empty input from the
        // bake test or unit fixtures is also legal.
        qCDebug(lcEffect) << "injectKwinDefineAfterVersion: empty source after BOM strip — returning bare define";
    }

    // Detect line ending so the injected define matches the source's
    // convention. GLSL compilers accept mixed CRLF/LF, but mixing
    // produces visually inconsistent diffs and trips lints. If any
    // CRLF appears in the source, emit "\r\n"; otherwise plain "\n".
    const bool useCrlf = working.contains(QStringLiteral("\r\n"));
    const QString defineLine =
        useCrlf ? QStringLiteral("#define PLASMAZONES_KWIN\r\n") : QStringLiteral("#define PLASMAZONES_KWIN\n");

    // Walk the source line-by-line and find the FIRST line whose
    // non-whitespace prefix is `#version`. A naive
    // `source.indexOf("#version")` would match `#version` substrings
    // embedded in `// ...` line comments or `/* ... */` block comments,
    // splicing the define into the comment body — the macro silently
    // disappears and the shader compiles against the wrong UBO ABI.
    //
    // `foundVersion` is tracked separately from `realVersionEnd` because
    // a shader whose `#version` line ends at EOF without a trailing
    // newline (rare but legal: a manual editor save that strips the
    // final LF) hits `lineEnd == -1` on the match, leaving
    // `realVersionEnd == -1`. Without the boolean we can't distinguish
    // "no #version directive at all" (prepend define) from "#version
    // at EOF, no newline" (must append `\n` + define). Conflating the
    // two would emit `#define PLASMAZONES_KWIN\n#version 450`, which
    // the GLSL compiler rejects because `#version` must be the first
    // directive in the translation unit.
    int searchFrom = 0;
    int realVersionEnd = -1;
    bool foundVersion = false;
    bool inBlockComment = false;
    while (searchFrom < working.size()) {
        const int lineEnd = working.indexOf(QLatin1Char('\n'), searchFrom);
        const int lineStop = (lineEnd < 0) ? working.size() : lineEnd;
        QStringView line = QStringView(working).mid(searchFrom, lineStop - searchFrom);
        // Strip block comments (single-line forms only — multi-line
        // detection is handled across iterations via inBlockComment).
        if (inBlockComment) {
            const int closeIdx = line.indexOf(QLatin1String("*/"));
            if (closeIdx < 0) {
                searchFrom = (lineEnd < 0) ? working.size() : lineEnd + 1;
                continue;
            }
            line = line.mid(closeIdx + 2);
            inBlockComment = false;
        }
        // Drop line and same-line block comments before checking.
        QString stripped;
        stripped.reserve(line.size());
        for (int i = 0; i < line.size();) {
            if (i + 1 < line.size() && line[i] == QLatin1Char('/') && line[i + 1] == QLatin1Char('/')) {
                break; // rest of line is comment
            }
            if (i + 1 < line.size() && line[i] == QLatin1Char('/') && line[i + 1] == QLatin1Char('*')) {
                const int closeIdx = line.indexOf(QLatin1String("*/"), i + 2);
                if (closeIdx < 0) {
                    inBlockComment = true;
                    break;
                }
                i = closeIdx + 2;
                continue;
            }
            stripped.append(line[i]);
            ++i;
        }
        const QString trimmed = stripped.trimmed();
        if (trimmed.startsWith(QLatin1String("#version"))) {
            realVersionEnd = lineEnd; // newline AFTER the version line, or -1 if at EOF
            foundVersion = true;
            break;
        }
        if (lineEnd < 0)
            break;
        searchFrom = lineEnd + 1;
    }

    if (!foundVersion) {
        // No #version directive (or it was shadowed by an early break
        // mid block-comment). KWin on Wayland with modern Mesa runs
        // core profile, where `#version` is mandatory and a bare
        // `#define` as the first token would fail compile with a
        // confusing line-1 error. Synthesize a `#version 450` directive
        // matching the rest of the animation suite, prepend the define,
        // and warn so the author sees the contract violation in the
        // journal.
        qCWarning(lcEffect) << "Animation shader source has no #version directive — synthesizing `#version 450`. "
                               "Animation shaders MUST declare `#version 450` (the canonical contract); the bake "
                               "test on the daemon side enforces this.";
        const QString header = useCrlf ? QStringLiteral("#version 450\r\n#define PLASMAZONES_KWIN\r\n")
                                       : QStringLiteral("#version 450\n#define PLASMAZONES_KWIN\n");
        return (header + working).toUtf8();
    }
    if (realVersionEnd < 0) {
        // `#version` line ends at EOF with no trailing newline. The
        // GLSL spec requires `#version` to be the FIRST directive, so
        // we cannot prepend the define; we must append it (with a
        // separator newline) so the compiler still sees `#version`
        // first. Without this branch the `!foundVersion` path above
        // would run instead and emit invalid `#define\n#version`
        // GLSL.
        return (working + QLatin1Char('\n') + defineLine).toUtf8();
    }
    working.insert(realVersionEnd + 1, defineLine);
    return working.toUtf8();
}

/// Load a user-texture file into a QImage. Mirrors the daemon-side path
/// in `PhosphorRendering::ShaderEffect::setShaderParams`: PNG/JPG/etc.
/// load via `QImage`; `.svg` / `.svgz` rasterise via `QSvgRenderer` at
/// the requested max-axis dimension (defaulting to 1024 to match
/// ZoneShaderItem's pre-unification SVG size). The `QImage::Format_RGBA8888`
/// conversion ensures `KWin::GLTexture::upload` always sees a consistent
/// pixel layout regardless of the source file's native format. Returns
/// a null QImage on any failure; the caller logs and skips the slot.
///
/// SVG rasterise target: `Format_ARGB32_Premultiplied` first, then
/// convert to `Format_RGBA8888` for the GPU upload. QPainter's
/// source-over compositing is defined for premultiplied targets;
/// rendering an SVG with semi-transparent strokes/fills directly into
/// `Format_RGBA8888` produces subtly wrong alpha at partial-cover paths.
/// Matches the daemon path's behaviour exactly so the same SVG renders
/// identically on both runtimes.
inline QImage loadUserTextureImage(const QString& path, int svgMaxDim = 1024)
{
    if (path.isEmpty()) {
        return {};
    }
    const bool isSvg = path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive);
    if (isSvg) {
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) {
            return {};
        }
        QSize size = renderer.defaultSize();
        if (!size.isEmpty()) {
            size.scale(svgMaxDim, svgMaxDim, Qt::KeepAspectRatio);
        } else {
            size = QSize(svgMaxDim, svgMaxDim);
        }
        QImage rasterised(size, QImage::Format_ARGB32_Premultiplied);
        rasterised.fill(Qt::transparent);
        QPainter painter(&rasterised);
        renderer.render(&painter);
        painter.end();
        return rasterised.convertToFormat(QImage::Format_RGBA8888);
    }
    return QImage(path).convertToFormat(QImage::Format_RGBA8888);
}

/// Pre-baked uniform / param key strings for the hot paths.
///
/// `customParams[0..7]` and `uTexture1..3` (and their wrap / svgSize
/// variants) are looked up dozens of times per shader transition install
/// and per paintWindow tick. Building those names with `QStringLiteral(...).arg(slot)`
/// allocates a fresh `QString` per call; pre-baking them as `QByteArray`
/// (for `uniformLocation` calls that take `const char*`) and `QLatin1String`
/// (for `QVariantMap::value` / `find` calls keyed by `QString`) eliminates
/// the per-frame allocations entirely. Sized to the contract budgets so a
/// future bump triggers a compile-time mismatch rather than a silent
/// out-of-range read.
constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots>
    kUserTextureSamplerNames = {"uTexture1", "uTexture2", "uTexture3"};
constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots>
    kUserTextureWrapKeys = {"uTexture1_wrap", "uTexture2_wrap", "uTexture3_wrap"};
constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots>
    kITextureResolutionKeys = {"iTextureResolution[0]", "iTextureResolution[1]", "iTextureResolution[2]"};
constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams>
    kCustomParamsElementNames = {"customParams[0]", "customParams[1]", "customParams[2]", "customParams[3]",
                                 "customParams[4]", "customParams[5]", "customParams[6]", "customParams[7]"};
// Worst-case 16 slots — sized to `kMaxCustomColors`. If that ever rises
// the array literal must grow; the static_assert below pins the length.
constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors>
    kCustomColorsElementNames = {"customColors[0]",  "customColors[1]",  "customColors[2]",  "customColors[3]",
                                 "customColors[4]",  "customColors[5]",  "customColors[6]",  "customColors[7]",
                                 "customColors[8]",  "customColors[9]",  "customColors[10]", "customColors[11]",
                                 "customColors[12]", "customColors[13]", "customColors[14]", "customColors[15]"};
static_assert(PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams == 8,
              "kCustomParamsElementNames literal must grow to match kMaxCustomParams");
static_assert(PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors == 16,
              "kCustomColorsElementNames literal must grow to match kMaxCustomColors");
static_assert(PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots == 3,
              "User-texture name arrays must grow to match kMaxUserTextureSlots");

/// Translate a metadata / params wrap-mode string to the GL enum the
/// kwin-effect applies at bind time. Empty / unrecognised values fall
/// through to `GL_CLAMP_TO_EDGE` (the GL default and the daemon's
/// default per `ShaderNodeRhi::setUserTextureWrap`'s normalisation).
inline GLenum wrapStringToEnum(const QString& wrap)
{
    const QString lower = wrap.toLower();
    if (lower == QLatin1String("repeat"))
        return GL_REPEAT;
    if (lower == QLatin1String("mirror") || lower == QLatin1String("mirrored"))
        return GL_MIRRORED_REPEAT;
    return GL_CLAMP_TO_EDGE;
}
} // namespace

// NavigateDirectivePrefix moved to navigationhandler.cpp to avoid redefinition

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus helpers (all async — no QDBusInterface to avoid synchronous introspection)
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Method Implementations
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                                    const QRectF& preCapturedGeometry)
{
    if (!w || windowId.isEmpty()) {
        return;
    }

    if (!isDaemonReady("ensure pre-snap geometry")) {
        return;
    }

    // Use pre-captured geometry if provided, otherwise read from window.
    QRectF geom = preCapturedGeometry.isValid() ? preCapturedGeometry : w->frameGeometry();
    if (geom.width() <= 0 || geom.height() <= 0) {
        return;
    }

    // Use virtual-screen-aware ID — getWindowScreenId() falls back to the physical
    // ID when virtual screen defs haven't loaded yet, so it is safe to call
    // unconditionally. Using it here ensures the stored screen ID always matches
    // the ID used by later lookups.
    const QString screenId = getWindowScreenId(w);

    // Post the store directly with overwrite=false. The daemon's storePreTileGeometry
    // enforces per-windowId idempotency — a second capture for the same runtime
    // instance is a no-op. We deliberately skip the prior async hasPreTileGeometry
    // pre-check: that path matched on appId too, so a stale cross-session entry from
    // a prior window instance (keyed by appId) would block the fresh per-instance
    // capture and freeze float-restore at ancient coordinates.
    PhosphorProtocol::ClientHelpers::fireAndForget(
        this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
        {windowId, static_cast<int>(geom.x()), static_cast<int>(geom.y()), static_cast<int>(geom.width()),
         static_cast<int>(geom.height()), screenId, false},
        QStringLiteral("storePreTileGeometry"));
    qCInfo(lcEffect) << "Stored pre-tile geometry for window" << windowId << "geom=" << geom;
}

QHash<QString, KWin::EffectWindow*> PlasmaZonesEffect::buildWindowMap(bool filterHandleable) const
{
    const auto windows = KWin::effects->stackingOrder();
    QHash<QString, KWin::EffectWindow*> windowMap;
    windowMap.reserve(windows.size());
    for (KWin::EffectWindow* w : windows) {
        if (w && (!filterHandleable || shouldHandleWindow(w))) {
            windowMap[getWindowId(w)] = w;
        }
    }
    return windowMap;
}

KWin::EffectWindow* PlasmaZonesEffect::getValidActiveWindowOrFail(const QString& action)
{
    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (!activeWindow || !shouldHandleWindow(activeWindow)) {
        qCDebug(lcEffect) << "No valid active window for" << action;
        emitNavigationFeedback(false, action, QStringLiteral("no_window"));
        return nullptr;
    }
    return activeWindow;
}

bool PlasmaZonesEffect::isWindowFloating(const QString& windowId) const
{
    return m_navigationHandler->isWindowFloating(windowId);
}

PlasmaZonesEffect::PlasmaZonesEffect()
    : OffscreenEffect()
    , m_autotileHandler(std::make_unique<AutotileHandler>(this))
    , m_navigationHandler(std::make_unique<NavigationHandler>(this))
    , m_screenChangeHandler(std::make_unique<ScreenChangeHandler>(this))
    , m_snapAssistHandler(std::make_unique<SnapAssistHandler>(this))
    // Phase 3: per-output motion clocks drive every AnimatedValue in the
    // controller. One `CompositorClock` per `LogicalOutput` so mixed
    // refresh-rate displays (60 Hz + 144 Hz being the common case)
    // phase-lock independently instead of beating against a shared
    // process-wide clock. Populated below via effects->screens() and
    // maintained via the screenAdded/screenRemoved signals. The
    // fallback unbound clock covers: (a) the bootstrap window before
    // screens() populates, (b) windows whose `screen()` is null
    // mid-migration, (c) test paths that don't drive KWin::effects.
    , m_motionClockFallback(std::make_unique<CompositorClock>(nullptr))
    , m_windowAnimator(std::make_unique<WindowAnimator>())
    , m_dragTracker(std::make_unique<DragTracker>(this))
    , m_compositorBridge(std::make_unique<KWinCompositorBridge>(this))
{
    PhosphorProtocol::registerWireTypes();

    // Single-worker pool for off-loading user-texture loads. See the
    // header docstring for `m_textureLoaderPool` for the rationale —
    // serialised loads keep the dedupe cheap and avoid duplicate GPU
    // uploads if multiple shader transitions reference the same file
    // in quick succession.
    m_textureLoaderPool.setMaxThreadCount(1);

    // Populate per-output clocks from the currently-known output set.
    // Subsequent hotplug events land in onScreenAdded / onScreenRemoved.
    //
    // Order: connect signals FIRST, then iterate the current screens()
    // snapshot. A screen plugged in between those two steps would
    // otherwise be missed — the signal wouldn't have an attached slot
    // yet, and the loop would already have run. With the signals
    // connected first, the worst case is a duplicate `onScreenAdded`
    // call (once via signal, once via loop). `onScreenAdded` is
    // idempotent (re-insertion check against m_motionClocksByOutput)
    // so the duplicate is a no-op.
    if (KWin::effects) {
        connect(KWin::effects, &KWin::EffectsHandler::screenAdded, this, &PlasmaZonesEffect::onScreenAdded);
        connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, this, &PlasmaZonesEffect::onScreenRemoved);
        for (KWin::LogicalOutput* output : KWin::effects->screens()) {
            onScreenAdded(output);
        }
        // Seed the cursor cache with the live position so the first frame
        // after a fresh shader install with iMouse declared sees the real
        // cursor. The default-constructed QPointF(0, 0) would otherwise be
        // misinterpreted as INSIDE any window whose frame contains the
        // origin (i.e. all windows on the primary monitor with origin at
        // (0, 0)) for one frame, producing a false-positive hover spike
        // before prePaintScreen overwrites the cache on the next tick.
        m_cachedCursorGlobal = KWin::effects->cursorPos();
    }

    // Wire the fallback clock as the animator's default. The animator's
    // clockForHandle override resolves the per-output clock at
    // startAnimation time; the default kicks in only when a window has
    // no resolvable output (which is rare but real — XWayland
    // bootstrap, mid-migration with a null screen()).
    m_windowAnimator->setClock(m_motionClockFallback.get());
    m_windowAnimator->setOutputClockResolver([this](KWin::LogicalOutput* output) -> PhosphorAnimation::IMotionClock* {
        return clockForOutput(output);
    });
    m_windowAnimator->setOnAnimationCompleteCallback([this](KWin::EffectWindow* w) {
        // Only tear down ANIMATOR-DRIVEN shader transitions
        // (durationMs == 0; the leg rides m_windowAnimator's timeline).
        // Time-based transitions (durationMs > 0; window.open / close /
        // focus / etc.) have their own QTimer teardown scheduled by
        // tryBeginShaderForEvent — without this guard, a zone.snapIn
        // transition that's been superseded by a window.* event leaves
        // the original animator running its geometry tween, and that
        // animator's eventual completion would prematurely kill the
        // successor (whose own QTimer hasn't fired yet).
        auto it = m_shaderTransitions.find(w);
        if (it == m_shaderTransitions.end() || it->second.durationMs > 0) {
            return;
        }
        endShaderTransition(w);
    });
    connect(&m_animationShaderRegistry, &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this,
            [this]() {
                QVarLengthArray<KWin::EffectWindow*, 8> windows;
                for (auto& [w, _] : m_shaderTransitions)
                    windows.push_back(w);
                for (auto* w : windows)
                    endShaderTransition(w);
                Q_ASSERT(m_shaderTransitions.empty());
                m_shaderCache.clear();
                // Drop the texture cache too — a hot-reload that swaps a
                // texture file behind the same metadata.json path needs
                // a fresh upload to pick up the new contents. The cache
                // is keyed by absolute path; without this clear a
                // file-content change with no path change would never
                // refresh.
                //
                // Bump the cache generation rather than draining the
                // loader pool synchronously. `waitForDone()` on the GL
                // thread would block the compositor for tens of ms when
                // a worker is mid-rasterise of a 1024x1024 SVG (the
                // worst case for `loadUserTextureImage`). Workers
                // already in flight will complete their CPU rasterise,
                // but their queued GL upload checks the generation
                // captured at submission time against
                // `m_textureCacheGeneration` and discards if mismatched
                // — so no stale (pre-reload) bytes can re-populate the
                // cleared cache. Clear immediately so the next
                // `beginShaderTransition` hits the synchronous fallback
                // path and uploads fresh content.
                ++m_textureCacheGeneration;
                m_textureLoadsInFlight.clear();
                m_textureCache.clear();
            });

    // Frame-geometry shadow flush timer. Debounces per-window
    // windowFrameGeometryChanged signals and pushes the latest geometry to
    // the daemon at ~20Hz so daemon-local shortcut handlers (float toggle,
    // etc.) have fresh geometry without a round-trip. Single-shot timer
    // re-armed on each incoming change — the flush fires at most one D-Bus
    // call per window per 50ms window regardless of how many pixels moved.
    m_frameGeometryFlushTimer = new QTimer(this);
    m_frameGeometryFlushTimer->setSingleShot(true);
    m_frameGeometryFlushTimer->setInterval(50);
    connect(m_frameGeometryFlushTimer, &QTimer::timeout, this, &PlasmaZonesEffect::flushPendingFrameGeometry);

    // Connect DragTracker signals
    //
    // Performance optimization: keyboard grab and D-Bus dragMoved calls are deferred
    // until an activation trigger is detected. This eliminates 60Hz D-Bus traffic and
    // keyboard grab/ungrab overhead for non-zone window drags (discussion #167).
    connect(m_dragTracker.get(), &DragTracker::dragStarted, this,
            [this](KWin::EffectWindow* w, const QString& windowId, const QRectF& geometry) {
                qCDebug(lcEffect) << "Window move started -" << w->windowClass()
                                  << "current modifiers:" << static_cast<int>(m_currentModifiers);

                // Note: `cursor.drag` is intentionally NOT wired here. The
                // OffscreenEffect pipeline operates on window content; firing
                // a shader at drag start through it is indistinguishable from
                // `window.move`, and synchronously colliding with the
                // `windowStartUserMovedResized` lambda's `window.move` install
                // means whichever fires second wins (it would be `window.move`
                // here). See `ProfilePaths::CursorDrag` doc comment — the path
                // is reserved for a future cursor-decoration / drag-shadow
                // surface.

                // Fire beginDrag async to get a daemon-authoritative policy.
                // While the reply is pending, we
                // default m_currentDragPolicy to a conservative snap-path so
                // the worst case (stale effect cache would have said autotile
                // but daemon knows better, or vice-versa) is a brief overlay
                // flash rather than a dead drag. The reply handler flips the
                // bypass flag retroactively a few ms later if the daemon says
                // this is an autotile drag.
                //
                // This replaces the previous stale-cache read of
                // m_autotileHandler->isAutotileScreen() as the single source
                // of truth for drag-start routing — root cause of the
                // post-settings-reload dead-drag window found in #310 log
                // forensics.
                m_currentDragPolicy = PhosphorProtocol::DragPolicy{};
                m_currentDragPolicy.streamDragMoved = true;
                m_currentDragPolicy.showOverlay = true;
                m_currentDragPolicy.grabKeyboard = true;
                m_currentDragPolicy.captureGeometry = true;

                const QString startScreenId = getWindowScreenId(w);
                const QRect frame = geometry.toRect();
                QDBusMessage beginMsg = QDBusMessage::createMethodCall(
                    PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                    PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("beginDrag"));
                beginMsg << windowId << frame.x() << frame.y() << frame.width() << frame.height() << startScreenId
                         << static_cast<int>(m_currentMouseButtons);
                QDBusPendingCall beginPending = QDBusConnection::sessionBus().asyncCall(beginMsg);
                auto* beginWatcher = new QDBusPendingCallWatcher(beginPending, this);
                QPointer<KWin::EffectWindow> safeW = w;
                const QString capturedWindowId = windowId;
                const QString capturedScreenId = startScreenId;
                connect(
                    beginWatcher, &QDBusPendingCallWatcher::finished, this,
                    [this, safeW, capturedWindowId, capturedScreenId](QDBusPendingCallWatcher* bw) {
                        bw->deleteLater();
                        QDBusPendingReply<PhosphorProtocol::DragPolicy> reply = *bw;
                        if (!reply.isValid()) {
                            qCWarning(lcEffect) << "beginDrag reply invalid:" << reply.error().message();
                            return;
                        }
                        const PhosphorProtocol::DragPolicy policy = reply.value();
                        if (const QString err = policy.validationError(); !err.isEmpty()) {
                            qCWarning(lcEffect) << "beginDrag reply rejected:" << err
                                                << "— keeping conservative snap-path policy for" << capturedWindowId;
                            return;
                        }
                        m_currentDragPolicy = policy;
                        qCInfo(lcEffect) << "beginDrag reply:" << capturedWindowId
                                         << "bypass=" << m_currentDragPolicy.bypassReason
                                         << "stream=" << m_currentDragPolicy.streamDragMoved
                                         << "immediateFloat=" << m_currentDragPolicy.immediateFloatOnStart;
                        // If the daemon confirms autotile, flip the effect
                        // state to bypass mode. Usually the effect-side
                        // fast path below already did this synchronously;
                        // this catches the stale-cache case where the fast
                        // path missed.
                        if (m_currentDragPolicy.bypassReason == PhosphorProtocol::DragBypassReason::AutotileScreen) {
                            if (!m_dragBypassedForAutotile) {
                                m_dragBypassedForAutotile = true;
                                m_dragBypassScreenId = capturedScreenId;
                                qCInfo(lcEffect) << "beginDrag: retroactive autotile bypass for" << capturedWindowId;
                            }
                            // Apply immediate float transition if the policy
                            // says so and the window wasn't already floated
                            // by the fast path. Using QPointer so we skip
                            // if the window was destroyed between drag-start
                            // and reply.
                            if (safeW && m_currentDragPolicy.immediateFloatOnStart
                                && !isWindowFloating(capturedWindowId)
                                && !m_dragFloatedWindowIds.contains(capturedWindowId)) {
                                m_autotileHandler->handleDragToFloat(safeW, capturedWindowId, capturedScreenId,
                                                                     /*immediate=*/true);
                                m_dragFloatedWindowIds.insert(capturedWindowId);
                            }
                        }
                    });

                // Fast path: the effect-side autotile cache is USUALLY correct.
                // We still consult it synchronously so the common case runs at
                // zero latency. The async beginDrag reply above runs as a
                // correction layer for the cases where the cache is stale
                // (post-settings-reload — the #310 scenario).
                if (m_autotileHandler->isAutotileScreen(startScreenId)) {
                    m_dragBypassedForAutotile = true;
                    m_dragBypassScreenId = startScreenId;
                    // Reorder mode: the daemon owns drag-insert preview for tile
                    // swapping. Skip the synchronous float transition — we want
                    // the tile to stay visually in place while the daemon runs
                    // moveToTiledPosition on each cursor tick. The effect still
                    // flips into bypass state so snap-path logic is suppressed.
                    const bool reorderMode = m_cachedAutotileDragBehavior == EffectAutotileDragBehavior::Reorder;
                    // If the window is currently autotile-tiled, restore its
                    // title bar and pre-autotile size NOW (synchronously, during
                    // the interactive move). This mirrors snap mode, where
                    // dragging a snapped window out of its zone visibly restores
                    // the free-floating size before release — without this, the
                    // user drags a borderless tile-sized window and only sees it
                    // become a floating window after they drop.
                    //
                    // Guarded on isTrackedWindow so we don't touch windows that
                    // are already floating (not in the autotile tree).
                    if (!reorderMode && m_autotileHandler->isTrackedWindow(windowId) && !isWindowFloating(windowId)) {
                        m_autotileHandler->handleDragToFloat(w, windowId, m_dragBypassScreenId, /*immediate=*/true);
                        // Mark as drag-floated so the daemon's pre-tile geometry
                        // restore (applyGeometryForFloat, triggered by the
                        // setWindowFloatingForScreen call at drop) is skipped in
                        // slotApplyGeometryRequested — the window should stay
                        // where the user drops it, not snap back to a stored rect.
                        m_dragFloatedWindowIds.insert(windowId);
                    }
                    return;
                }
                m_dragBypassedForAutotile = false;
                m_dragActivationDetected = false;
                m_dragStartedSent = false;
                m_pendingDragWindowId = windowId;
                m_pendingDragGeometry = geometry;
                m_snapDragStartScreenId = getWindowScreenId(w);

                // beginDrag already initialized daemon-side snap-drag state
                // (called internally from the adaptor). The effect only needs
                // to decide whether to grab the keyboard for local Escape
                // handling.
                detectActivationAndGrab();
                // Grab keyboard to intercept Escape before KWin's MoveResizeFilter.
                // Without this, Escape cancels the interactive move AND the overlay.
                // With the grab, Escape only dismisses the overlay while the drag continues.
                if (!m_keyboardGrabbed) {
                    KWin::effects->grabKeyboard(this);
                    m_keyboardGrabbed = true;
                }
            });
    connect(
        m_dragTracker.get(), &DragTracker::dragMoved, this, [this](const QString& windowId, const QPointF& cursorPos) {
            // Cross-VS flip detection is daemon-owned. The
            // daemon's updateDragCursor handler computes policy at the
            // cursor position and emits dragPolicyChanged when it flips.
            // The effect reacts via slotDragPolicyChanged (see below).
            //
            // Here we only forward the cursor to the daemon as a
            // fire-and-forget call. The daemon-side dispatch handles
            // both the snap-path overlay updates and the cross-VS
            // detection in a single round trip.

            // In autotile bypass — skip snap zone processing locally;
            // the daemon's updateDragCursor still watches for a flip
            // BACK to snap mode.
            const bool bypassed = m_currentDragPolicy.bypassReason == PhosphorProtocol::DragBypassReason::AutotileScreen
                || m_dragBypassedForAutotile;
            if (!bypassed) {
                // Gate D-Bus calls on activation trigger state so a drag
                // without any intent to use zones doesn't flood the bus
                // at 30Hz. This is a local input-event optimization; it
                // isn't policy and doesn't come from the daemon.
                if (!detectActivationAndGrab() && !m_cachedZoneSelectorEnabled && m_triggersLoaded) {
                    return;
                }
            }

            // Forward the cursor to the daemon. For snap drags, this
            // drives overlay/zone detection. For bypass drags, the
            // daemon watches the cursor for a cross-VS flip and emits
            // dragPolicyChanged when the policy changes.
            PhosphorProtocol::ClientHelpers::fireAndForget(
                this, PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("updateDragCursor"),
                {windowId, qRound(cursorPos.x()), qRound(cursorPos.y()), static_cast<int>(m_currentModifiers),
                 static_cast<int>(m_currentMouseButtons)},
                QStringLiteral("updateDragCursor"));
        });
    connect(m_dragTracker.get(), &DragTracker::dragStopped, this,
            [this](KWin::EffectWindow* w, const QString& windowId, bool cancelled) {
                // Release keyboard grab before handling drag end
                if (m_keyboardGrabbed) {
                    KWin::effects->ungrabKeyboard();
                    m_keyboardGrabbed = false;
                }

                // Clear the drag-floated marker on every drag end. Historically
                // this marker was used to suppress a post-drag pre-tile geometry
                // restore (applyGeometryForFloat), but the current daemon-side
                // drag-end path goes through AutotileEngine::setWindowFloat →
                // windowFloatingStateSynced → syncAutotileFloatStatePassive,
                // which never emits applyGeometryForFloat. Leaving the marker
                // set after a drag leaks it into subsequent Meta+F toggles:
                // the next user float is silently skipped, the window's visual
                // position diverges from the daemon's shadow, and then a
                // float→tile toggle overwrites the stored pre-tile rect with
                // the stale tile zone — permanently corrupting the restore
                // target (#bug: zed/firefox/plasmazones-settings resize issues).
                m_dragFloatedWindowIds.remove(windowId);

                // Single entry point for drag-end dispatch. The
                // daemon owns the decision; callEndDrag sends endDrag and
                // the reply handler applies whatever PhosphorProtocol::DragOutcome comes back
                // (ApplySnap / ApplyFloat / RestoreSize / NoOp / etc.).
                //
                // The autotile branch special-casing that used to live here
                // is gone — cross-VS transitions were applied mid-drag by
                // slotDragPolicyChanged, and final drop-time actions are
                // encoded in the PhosphorProtocol::DragOutcome.
                callEndDrag(w, windowId, cancelled);

                // Clear drag state for the next session.
                m_currentDragPolicy = PhosphorProtocol::DragPolicy{};
                m_dragBypassedForAutotile = false;
                m_dragBypassScreenId.clear();
                m_snapDragStartScreenId.clear();
                m_dragActivationDetected = false;
                m_dragStartedSent = false;
                m_pendingDragWindowId.clear();
                m_pendingDragGeometry = QRectF();
            });

    // Connect to window lifecycle signals
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &PlasmaZonesEffect::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, &PlasmaZonesEffect::slotWindowClosed);

    // Belt-and-suspenders: windowClosed removes animations, but if a deferred
    // timer re-adds one between windowClosed and windowDeleted, the Item tree
    // will be torn down while an animation entry still references the window.
    // Purge here to prevent SIGSEGV in animationBounds → expandedGeometry.
    // Also clean up caches that slotWindowClosed may have already cleared —
    // QHash::take/remove on missing keys is a no-op, so this is safe.
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, [this](KWin::EffectWindow* w) {
        endShaderTransition(w);
        m_windowAnimator->removeAnimation(w);
        if (m_windowIdCache.contains(w)) {
            const QString cachedId = m_windowIdCache.take(w);
            m_windowIdReverse.remove(cachedId);
        }
        m_trackedScreenPerWindow.remove(w);
        // Drop per-window shader-event bookkeeping. m_lastFocusShaderWindow is
        // a QPointer that auto-nulls on destroy, so it's already cleaned up;
        // m_lastFullyMaximized is a raw-pointer-keyed QHash so we explicitly
        // erase here to keep it bounded across long sessions.
        m_lastFullyMaximized.remove(w);
    });

    connect(KWin::effects, &KWin::EffectsHandler::windowActivated, this, &PlasmaZonesEffect::slotWindowActivated);

    // Update the daemon's primary screen override when KDE Display Settings change
    if (auto* ws = KWin::Workspace::self()) {
        connect(ws, &KWin::Workspace::outputOrderChanged, this, [this]() {
            auto* workspace = KWin::Workspace::self();
            if (workspace && m_daemonServiceRegistered) {
                const auto outputs = workspace->outputOrder();
                if (!outputs.isEmpty()) {
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        this, PhosphorProtocol::Service::Interface::Screen, QStringLiteral("setPrimaryScreenFromKWin"),
                        {outputs.first()->name()}, QStringLiteral("setPrimaryScreenFromKWin"));
                }
            }
        });
    }

    // mouseChanged is the only reliable way to get modifier state in a KWin effect on Wayland;
    // QGuiApplication::queryKeyboardModifiers() doesn't work since effects run in the compositor.
    connect(KWin::effects, &KWin::EffectsHandler::mouseChanged, this, &PlasmaZonesEffect::slotMouseChanged);

    // Connect to screen geometry changes for keepWindowsInZonesOnResolutionChange feature
    // In KWin 6, use virtualScreenGeometryChanged (not per-screen signal)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, m_screenChangeHandler.get(),
            &ScreenChangeHandler::slotScreenGeometryChanged);
    // Invalidate screen ID cache and refresh virtual screen definitions on screen changes
    // (connector names may be reassigned, physical screen geometry changes invalidate
    // virtual screen absolute geometry)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, this, [this]() {
        m_screenIdCache.clear();
        m_lastEffectiveScreenId.clear();
    });

    // Connect to daemon's settingsChanged D-Bus signal
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::Settings,
                                          QStringLiteral("settingsChanged"), this, SLOT(slotSettingsChanged()));
    qCInfo(lcEffect) << "Connected to daemon settingsChanged D-Bus signal";

    // Connect to virtual screen changes — daemon emits this when a physical screen's
    // virtual subdivisions are added, removed, or modified.
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::Screen,
                                          QStringLiteral("virtualScreensChanged"), this,
                                          SLOT(onVirtualScreensChanged(QString)));

    // Connect to keyboard navigation D-Bus signals
    connectNavigationSignals();

    // Connect to autotile D-Bus signals
    m_autotileHandler->connectSignals();
    m_autotileHandler->loadSettings();

    // Verify daemon availability asynchronously to avoid blocking the compositor.
    // CRITICAL: Do NOT use synchronous isServiceRegistered() here. The daemon
    // registers its D-Bus service name in init() BEFORE start() runs heavy
    // initialization and BEFORE the event loop begins (main.cpp:88→94→102).
    // During that window, isServiceRegistered() returns true but the daemon
    // can't process messages. Any synchronous QDBusInterface creation would
    // trigger Introspect, blocking KWin for up to the D-Bus timeout (~25s).
    //
    // Instead, send an async Introspect — if the daemon responds, it's fully
    // operational and we trigger slotDaemonReady(). If it can't respond (still
    // initializing), the call times out harmlessly and we wait for the
    // daemonReady D-Bus signal instead.
    {
        QDBusMessage introspect = QDBusMessage::createMethodCall(
            PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
            QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(introspect, 3000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QString> reply = *w;
            if (reply.isValid() && !m_daemonServiceRegistered) {
                // Daemon responded — it's fully operational.
                // Trigger the same ready flow as the daemonReady signal.
                slotDaemonReady();
            }
        });
    }

    // Connect to daemon's daemonReady signal — emitted at the end of Daemon::start()
    // after all initialization is complete and the daemon can process D-Bus messages.
    // This is the safe point to set m_daemonServiceRegistered and create QDBusInterfaces.
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::LayoutRegistry,
                                          QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));

    // Watch for daemon D-Bus service registration and unregistration.
    // After a daemon restart, m_lastCursorOutput is still valid in the effect
    // but the daemon's lastCursorScreenName/lastActiveScreenName are empty.
    // Without this, keyboard shortcuts (rotate, etc.) operate on all screens
    // because resolveShortcutScreen returns nullptr.
    //
    // On Wayland, this watcher uses D-Bus monitoring (not X11 selection),
    // which works reliably across both sessions.
    auto* serviceWatcher = new QDBusServiceWatcher(
        PhosphorProtocol::Service::Name, QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon service unregistered";
        m_daemonServiceRegistered = false;
        // Also clear the bridge-registration in-flight gate. Without
        // this, a daemon-restart racing the in-flight registerBridge
        // reply leaves the gate set: the new daemon's `daemonReady`
        // signal arrives, slotDaemonReady sees the gate true and
        // bails, and the gate only clears later when the stale call's
        // error reply arrives — by which time no further signal will
        // re-trigger slotDaemonReady. The effect would sit idle
        // indefinitely. Resetting here keeps the gate authoritative
        // across daemon restarts.
        m_bridgeRegistrationInFlight = false;
        m_daemonReadyRestoresDone = false;
        m_daemonReadyWindowStateProcessed = false;
        m_snapRestoreCache.clear();

        // Restore borderless and monocle-maximized windows — daemon state is gone
        m_autotileHandler->restoreAllBorderless();
        m_autotileHandler->restoreAllMonocleMaximized();
        clearAllBorders();
    });
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon registered: waiting for daemonReady signal";

        // DO NOT set m_daemonServiceRegistered = true here.
        // The daemon registers its D-Bus service name in init(), BEFORE start()
        // runs heavy initialization and BEFORE the event loop begins. Keep the
        // flag false until the daemon's own daemonReady signal fires (end of
        // Daemon::start()), confirming it can handle D-Bus requests.

        // Reconnect daemonReady signal — Qt may cache the old daemon's unique bus
        // name in match rules, so refresh for the new daemon instance.
        // Disconnect first to prevent duplicate match rules (Qt doesn't deduplicate),
        // which would cause slotDaemonReady to fire twice on the same signal.
        QDBusConnection::sessionBus().disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                                 PhosphorProtocol::Service::Interface::LayoutRegistry,
                                                 QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
        QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                              PhosphorProtocol::Service::Interface::LayoutRegistry,
                                              QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
    });

    // NOTE: syncFloatingWindowsFromDaemon() and loadCachedSettings() are NOT
    // called here. m_daemonServiceRegistered is false at this point (set only by
    // slotDaemonReady), so any ensureInterface() call would bail out immediately.
    // All daemon state sync is deferred to slotDaemonReady().

    // Connect to existing windows
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        setupWindowConnections(w);
    }

    // The daemon disables KWin's Quick Tile via kwriteconfig6. We don't reserve electric borders
    // here because that would turn on the edge effect visually; the daemon's config approach
    // is the right way to prevent Quick Tile from activating.

    // Seed m_lastCursorOutput with the compositor's active screen. This ensures
    // the daemon has a valid cursor screen even if no mouse movement occurs after login.
    // slotMouseChanged will overwrite this as soon as the cursor moves.
    //
    // The actual D-Bus push to the daemon happens in slotDaemonReady(), which fires
    // either from the async Introspect callback above (daemon already running) or
    // from the daemonReady D-Bus signal (daemon starts later). We do NOT push here
    // to avoid synchronous QDBusInterface creation on the compositor thread.
    auto* initialScreen = KWin::effects->activeScreen();
    if (initialScreen) {
        m_lastCursorOutput = initialScreen->name();
    }

    qCInfo(lcEffect) << "initialized: C++ effect with D-Bus support and mouseChanged connection";
}

PlasmaZonesEffect::~PlasmaZonesEffect()
{
    // Sever the registry's `effectsChanged` connection BEFORE anything
    // else runs. The slot lambda touches `m_shaderTransitions`,
    // `m_shaderCache`, and `m_textureCache` — all declared AFTER
    // `m_animationShaderRegistry` in the header (h:507 vs h:698+), so
    // they destruct FIRST in C++ reverse-declaration order. The
    // registry destructs LAST, and any signal it (or its underlying
    // file-watcher) emits during its own member teardown would
    // dispatch to the slot AFTER the cache members are gone — UAF.
    // Disconnect now while everything is still alive.
    disconnect(&m_animationShaderRegistry, nullptr, this, nullptr);

    // Drain the texture loader pool before any other teardown. A
    // worker that's mid-rasterise would otherwise post a queued
    // upload via QMetaObject::invokeMethod against `this` AFTER our
    // members start tearing down — UAF on the texture cache.
    // QThreadPool::waitForDone is called here BEFORE we let the
    // member's destructor run (which itself blocks on waitForDone)
    // so the pending invokeMethod posts are flushed against a still-
    // intact `this`.
    //
    // Distinct from the hot-reload path in the `effectsChanged`
    // lambda above: hot-reload bumps `m_textureCacheGeneration` (no
    // wait) so workers in flight can discard their queued upload
    // without blocking the compositor. Shutdown REQUIRES the wait —
    // the queued uploads need to be flushed against a live `this`,
    // not raced against member destruction.
    m_textureLoaderPool.waitForDone();
    m_textureLoadsInFlight.clear();

    // Restore borderless and monocle-maximized windows so they recover properly.
    // Guard against compositor teardown — effects may outlive the stacking order.
    if (KWin::effects) {
        m_autotileHandler->restoreAllBorderless();
        m_autotileHandler->restoreAllMonocleMaximized();
        clearAllBorders();
    }

    if (m_keyboardGrabbed && KWin::effects) {
        // Symmetric with the `if (KWin::effects)` guard above: during
        // compositor teardown KWin::effects can be null, and an
        // unguarded deref here would crash even though we reached the
        // destructor body cleanly. The compositor's own teardown
        // releases the grab when KWin::effects is gone.
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }
    m_screenChangeHandler->stop();
    // We no longer reserve/unreserve edges; the daemon disables KWin snap via config.

    // Explicitly tear down every active shader transition before the
    // member-by-member destruction runs. `endShaderTransition` calls
    // `setShader(window, nullptr)` and `unredirect(window)` to release
    // KWin's offscreen state cleanly; relying on default destruction
    // would let the offscreen FBOs linger until KWin's effect-removed
    // bookkeeping ran. Iterates a snapshot because endShaderTransition
    // erases from `m_shaderTransitions` mid-loop.
    //
    // Guarded by `if (KWin::effects)` matching the clearAllBorders /
    // ungrabKeyboard guards above: during compositor teardown the global
    // is null and `endShaderTransition` dereferences it (setShader,
    // unredirect, refWindow). The compositor's own teardown reclaims
    // the offscreen state when KWin::effects is gone.
    if (KWin::effects) {
        QVarLengthArray<KWin::EffectWindow*, 8> activeWindows;
        for (auto& [w, _] : m_shaderTransitions) {
            activeWindows.push_back(w);
        }
        for (auto* w : activeWindows) {
            endShaderTransition(w);
        }
        // endShaderTransition queues each window's `unrefWindow` via
        // QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) to
        // avoid use-after-free in paintWindow's expiry fall-through. In
        // the destructor path that defer is unsafe in the opposite
        // direction: ~QObject runs after this body returns and discards
        // pending posted MetaCalls targeted at `this`, so the queued
        // unrefs would never fire and KWin's EffectWindow refcount
        // stays incremented for every close-grab transition active at
        // teardown — leaking the close grab. Drain the queue here, while
        // `this` is still fully constructed and the lambdas can safely
        // run. The lambdas only call `KWin::effects->unrefWindow(...)`
        // and don't touch our member state after the unref, so
        // synchronous dispatch from the dtor body is sound.
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
}

bool PlasmaZonesEffect::supported()
{
    // This effect is a compositor plugin that works in KWin on Wayland
    // Note: PlasmaZones daemon requires Wayland with layer-shell support
    return true;
}

bool PlasmaZonesEffect::enabledByDefault()
{
    return true;
}

void PlasmaZonesEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    // Called when KWin wants effects to reload or when daemon notifies of settings change
    qCDebug(lcEffect) << "reconfigure() called";
}

bool PlasmaZonesEffect::isActive() const
{
    // Critical: include `!m_shaderTransitions.empty()` here. KWin calls
    // isActive() before each paint cycle and EXCLUDES the effect from
    // the chain when it returns false — meaning prePaintScreen and
    // paintWindow are never called, so a shader transition installed
    // via beginShaderTransition would never get a frame to advance on.
    //
    // Without this clause, the only paths that wake the chain are
    // (a) interactive drag (`m_dragTracker->isDragging()`) and
    // (b) zone-snap reflow animations (`m_windowAnimator`).
    // window.move works through (a) because the drag holds isActive()
    // true; every other lifecycle event (focus/open/close/minimize/
    // maximize/resize) installs a shader transition only — without this
    // clause those events would resolve cleanly, redirect the window,
    // and then sit unrendered until the timer-driven teardown fired.
    return m_dragTracker->isDragging() || m_windowAnimator->hasActiveAnimations() || !m_shaderTransitions.empty();
}

void PlasmaZonesEffect::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (e->type() == QEvent::KeyPress && e->key() == Qt::Key_Escape && m_dragTracker->isDragging()) {
        // The keyboard grab ensures this runs before KWin's MoveResizeFilter,
        // so Escape never reaches the interactive move handler. The daemon
        // hides the overlay and sets snapCancelled; the drag continues as
        // a plain window move without zone snapping.
        qCInfo(lcEffect) << "Drag escape: overlay hidden, drag continues";
        callCancelSnap();
    }
    // All other keys are silently consumed by the grab. Modifier state is
    // unaffected because mouseChanged reads xkb state directly.
}

void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow* w)
{
    setupWindowConnections(w);
    updateWindowStickyState(w);

    // window.open shader transition: fires once for every newly-mapped
    // window we handle. KWin's stock fade-in handles the *motion* (alpha
    // fade-in over a few frames), so the shader leg layers a transition
    // effect on top. tryBeginShaderForEvent silently no-ops when the user
    // hasn't assigned a shader to window.open in their tree.
    tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowOpen, animationDurationMs());

    // Populate the daemon's WindowRegistry with this window's initial metadata.
    // Runs before any other daemon notification so consumers querying the
    // registry from their windowOpened handlers see a record (sessions 2+).
    pushWindowMetadata(w);

    // Sync floating state for this window from daemon
    // This ensures windows that were floating when closed remain floating when reopened
    // Use full windowId so daemon can do per-instance lookup with appId fallback
    QString windowId = getWindowId(w);
    m_navigationHandler->syncFloatingStateForWindow(windowId);

    bool onAutotileScreen = m_autotileHandler->isAutotileScreen(getWindowScreenId(w));

    // Check if this window is a candidate for snap restore
    // Use stricter filter - only normal application windows, NOT dialogs/utilities
    bool canSnapRestore =
        shouldHandleWindow(w) && isTileableWindow(w) && !w->isMinimized() && !hasOtherWindowOfClassWithDifferentPid(w);

    // Instant snap restore: if we have a cached zone geometry for this app,
    // teleport the window immediately — no D-Bus round-trip, no visible flash.
    // The async callResolveWindowRestore still runs to register the zone assignment
    // in the daemon; this just eliminates the visual lag.
    //
    // The cache is authoritative about the target SCREEN, not the window's current
    // placement. Each entry carries the screenId of the saved zone; the daemon
    // populates the cache only for pending restores whose saved screen is in snap
    // mode, so an entry being present means "this app wants to land on a
    // snap-mode zone". Cross-VS/cross-monitor teleport works because moveResize
    // takes absolute compositor coordinates, so applySnapGeometry moves the
    // window to whichever screen the cached rect lives on. After teleport,
    // re-evaluate onAutotileScreen because KWin updates the window's output
    // assignment.
    //
    // Rare race: the saved screen may have flipped from snap→autotile between
    // when the cache was populated and when the window opens. Re-check the
    // entry's screen mode via the autotile handler before applying.
    if (canSnapRestore && !m_snapRestoreCache.isEmpty()) {
        QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        auto cacheIt = m_snapRestoreCache.find(appId);
        if (cacheIt != m_snapRestoreCache.end()) {
            const CachedSnapRestore& cached = cacheIt.value();
            const bool savedScreenNowAutotile =
                !cached.screenId.isEmpty() && m_autotileHandler->isAutotileScreen(cached.screenId);
            if (cached.geometry.isValid() && !savedScreenNowAutotile) {
                qCInfo(lcEffect) << "Instant snap restore for" << appId << "to:" << cached.geometry
                                 << "screen:" << cached.screenId;
                applySnapGeometry(w, cached.geometry, false, /*skipAnimation=*/true);
                m_snapRestoreCache.erase(cacheIt);
                // Re-evaluate screen after teleport — cross-VS/cross-monitor
                // moveResize updates KWin's output assignment, so the window
                // may no longer be on an autotile screen.
                onAutotileScreen = m_autotileHandler->isAutotileScreen(getWindowScreenId(w));
            } else if (savedScreenNowAutotile) {
                qCDebug(lcEffect) << "Skipping instant snap restore for" << appId
                                  << "- saved screen now autotile:" << cached.screenId;
                m_snapRestoreCache.erase(cacheIt);
            }
        }
    }

    if (onAutotileScreen && canSnapRestore) {
        // Window landed on an autotile screen, but may have a pending snap restore
        // to a non-autotile screen. KWin's session restore places windows at their
        // saved geometry, which may be a pre-snap floating position in the autotile
        // screen's area — even though the window was snapped in the snap screen
        // before logout. Try snap restore FIRST: if it moves the window off the
        // autotile screen, we avoid the autotile add→float→remove→resnap dance
        // that causes visible flickering and repeated resizing.
        QPointer<KWin::EffectWindow> safeW = w;
        callResolveWindowRestore(w, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // Snap restore either moved the window to a snap screen (no-op for
            // autotile) or didn't apply (window genuinely belongs on autotile).
            m_autotileHandler->notifyWindowAdded(safeW);
        });
        return;
    }

    // Standard path: notify autotile first, then try snap restore
    m_autotileHandler->notifyWindowAdded(w);

    if (!onAutotileScreen && canSnapRestore) {
        callResolveWindowRestore(w);
    }
}

void PlasmaZonesEffect::slotWindowClosed(KWin::EffectWindow* w)
{
    // Release keyboard grab if the dragged window was closed
    if (m_keyboardGrabbed && m_dragTracker->draggedWindow() == w) {
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }

    // Delegate to helpers
    m_dragTracker->handleWindowClosed(w);

    // Clear floating state — floating is runtime-only and resets on window close.
    // The daemon clears its side in windowClosed().
    m_navigationHandler->setWindowFloating(getWindowId(w), false);

    // Tear down any in-flight zone.* shader transition first — this window
    // is going away and we don't want a half-faded zone shader fighting the
    // fresh window.close shader. Then layer the close shader on top of
    // whatever fade-out KWin applies as part of the close animation.
    endShaderTransition(w);
    // Close is the reverse of open: same user-assigned shader plays
    // 1→0 so an `appear` shader doubles as a `disappear` shader.
    //
    // holdCloseGrab=true: request KWin::WindowClosedGrabRole so KWin
    // keeps the window alive past its normal unmap-and-delete sequence
    // for the duration of our close shader. Without the grab, KWin
    // proceeds with final destruction as soon as this slot returns;
    // OffscreenEffect's `redirect` is auto-released on deletion (per
    // /usr/include/kwin/effect/offscreeneffect.h:53), so paintWindow
    // never gets a frame to run the close shader on. The grab is
    // released by `endShaderTransition` when the timer-driven teardown
    // fires.
    tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowClose, animationDurationMs(),
                           /*reverse=*/true, /*holdCloseGrab=*/true);
    m_windowAnimator->removeAnimation(w);

    const QString closedWindowId = getWindowId(w);
    const QString closedScreenId = getWindowScreenId(w);

    // Clean up snap-mode minimize tracking
    m_minimizeFloatedWindows.remove(closedWindowId);
    m_dragFloatedWindowIds.remove(closedWindowId);

    // Notify autotile handler for cleanup (tracking sets + autotile D-Bus)
    m_autotileHandler->onWindowClosed(closedWindowId, closedScreenId);

    // Remove the window's border item (parent WindowItem is being destroyed anyway,
    // but clean up our tracking hash to avoid stale entries).
    removeWindowBorder(closedWindowId);

    // Notify general daemon for cleanup
    notifyWindowClosed(w);

    // Clean up caches AFTER all consumers that call getWindowId(w).
    // The windowDeleted handler does final cleanup, but removing here
    // prevents re-insertion by any late calls.
    m_windowIdCache.remove(w);
    m_windowIdReverse.remove(closedWindowId);
    m_trackedScreenPerWindow.remove(w);
}

void PlasmaZonesEffect::slotWindowActivated(KWin::EffectWindow* w)
{
    // Filtering (e.g. shouldHandleWindow) is done inside notifyWindowActivated
    notifyWindowActivated(w);

    // Recreate all borders so the active window gets the active color
    // and inactive windows get the inactive color.  A full recreate is
    // used instead of in-place setOutline() because the latter may not
    // trigger a scene-graph repaint in all KWin versions.
    updateAllBorders();
}

void PlasmaZonesEffect::setupWindowConnections(KWin::EffectWindow* w)
{
    if (!w)
        return;

    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this](KWin::EffectWindow* window) {
        updateWindowStickyState(window);

        // When a window is moved to a different desktop (e.g., "Move to Desktop 2"),
        // treat it as removed from the current desktop's tiling. The normal desktop-
        // switch flow will pick it up when the user switches to the target desktop.
        if (window && !window->isOnCurrentDesktop() && !window->isOnAllDesktops()) {
            const QString windowId = getWindowId(window);
            const QString screenId = getWindowScreenId(window);
            if (m_autotileHandler->isAutotileScreen(screenId)) {
                // Save pre-autotile geometry before onWindowClosed clears it.
                // When the window is re-added on the target desktop, this preserved
                // geometry is used instead of the current (tiled) frame position.
                m_autotileHandler->savePreAutotileForDesktopMove(windowId, screenId);

                // Restore title bar before removing from tiling — onWindowClosed
                // only clears tracking, it doesn't call setNoBorder(false) since
                // it's also used for truly closing windows.
                if (m_autotileHandler->isBorderlessWindow(windowId)) {
                    KWin::Window* kw = window->window();
                    if (kw) {
                        kw->setNoBorder(false);
                    }
                }
                m_autotileHandler->onWindowClosed(windowId, screenId);
                removeWindowBorder(windowId);
                qCInfo(lcEffect) << "Window moved off current desktop, removed from autotile:" << windowId;
            }
        }
    });

    // Detect when a window moves between monitors (e.g., "Move to Screen Right").
    // KWin::Window::outputChanged fires once when the window's output property changes.
    // Transfer the window from the old screen's autotile state to the new screen's state,
    // and unsnap any snapped window that crosses screens.
    KWin::Window* kw = w->window();
    if (kw) {
        QPointer<KWin::EffectWindow> safeW = w;
        // Track the window's screen ID so we can detect cross-screen moves for snapping windows
        // (not tracked by the autotile handler's m_notifiedWindowScreens).
        m_trackedScreenPerWindow[w] = getWindowScreenId(w);
        connect(kw, &KWin::Window::outputChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Delegate autotile handling (autotile→autotile, autotile→snapping, etc.)
            // This must run even during drag so the autotile engine removes the
            // window from the old screen's tiling state immediately.
            m_autotileHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-screen moves: notify the daemon which
            // decides whether to unsnap based on its own state. If the daemon just
            // assigned this window to the new screen (restore/resnap/snap assist),
            // the stored screen matches and no unsnap occurs. If the user moved
            // the window via "Move to Screen" shortcut, the stored screen differs
            // and the daemon unsnaps.
            // Skip during drag: the drag system owns snap state transitions
            // (float, unsnap, size restore, pre-tile cleanup) and handles them
            // in dragStopped() with richer context.
            // Skip when the old screen disappeared (monitor standby/disconnect):
            // KWin reassigns orphaned windows to remaining outputs, firing
            // outputChanged even though the window didn't actually move. The
            // ScreenChangeHandler will resnap windows after the debounce settles.
            // Also skip during an active screen geometry change (debounce in flight).
            bool oldScreenStillConnected = false;
            for (const auto* output : KWin::effects->screens()) {
                if (outputScreenId(output) == oldScreenId) {
                    oldScreenStillConnected = true;
                    break;
                }
            }
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId
                && !m_autotileHandler->isAutotileScreen(oldScreenId)
                && !m_autotileHandler->isAutotileScreen(newScreenId) && !m_dragTracker->isDragging()
                && oldScreenStillConnected && !m_screenChangeHandler->isScreenChangeInProgress()) {
                const QString windowId = getWindowId(safeW);
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                    {windowId, newScreenId}, QStringLiteral("cross-screen move"));
            }
        });
        // Virtual screen boundary detection: KWin's outputChanged only fires when
        // the physical monitor changes. Moving a window between virtual screens on the
        // same physical monitor (e.g., A/vs:0 → A/vs:1) is invisible to outputChanged.
        // Detect these crossings via frameGeometryChanged, using the same trackedScreen
        // state as the outputChanged handler above.
        // (The autotile handler has its own detection in slotWindowFrameGeometryChanged;
        // this covers snapping-mode windows which autotile doesn't track.)
        //
        // VS crossing detection uses PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing()
        // (shared/virtualscreenid.h) — the same predicate used by autotilehandler/tiling.cpp.
        connect(safeW, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted() || m_virtualScreenDefs.isEmpty() || !m_virtualScreensReady) {
                return;
            }
            // Suppress crossing detection while the daemon is moving this window in response
            // to a VS swap/rotate or resnap. The cached m_virtualScreenDefs may still hold
            // pre-rotation regions when the geometry change fires synchronously from
            // applySnapGeometry, so getWindowScreenId would resolve the new position against
            // stale boundaries and report a phantom crossing.
            if (m_inDaemonGeometryApply) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            if (!PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                return;
            }
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Skip during drag — the drag system owns state transitions.
            // Autotile drag handles VS transfers in dragStopped (line 262-285).
            // Snapping drag handles cross-screen unsnap in dragStopped via daemon.
            if (m_dragTracker->isDragging()) {
                return;
            }

            // Skip VS detection for autotile-tracked windows — the autotile
            // handler's slotWindowFrameGeometryChanged owns VS crossing for
            // windows it already tracks (m_notifiedWindows). Only untracked
            // windows (snapping-mode entering an autotile VS) need delegation.
            const QString windowId = getWindowId(safeW);
            if (m_autotileHandler->isTrackedWindow(windowId)) {
                return;
            }

            // Delegate autotile handling for untracked cross-VS transitions
            // (snapping→autotile). The autotile handler's own detection only
            // covers windows it already tracks.
            m_autotileHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-VS moves: notify the daemon
            if (!m_autotileHandler->isAutotileScreen(oldScreenId) && !m_autotileHandler->isAutotileScreen(newScreenId)
                && !m_screenChangeHandler->isScreenChangeInProgress()) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                    {windowId, newScreenId}, QStringLiteral("virtual screen crossing"));
            }
        });

        // Clean up the tracked screen entry when the window is destroyed
        connect(safeW, &QObject::destroyed, this, [this, safeW]() {
            m_trackedScreenPerWindow.remove(safeW);
        });

        // Metadata mutations: KWin fires these when an app swaps its class or
        // desktop file after the surface is already mapped. Electron/CEF apps
        // (Emby, some Discord forks) do this mid-session and silently break any
        // daemon state keyed to the first-seen class. Push the latest metadata
        // to the WindowRegistry so consumers query the current value.
        //
        // Per feedback_class_change_exclusion.md: the registry only updates its
        // record. It does NOT retroactively unsnap, re-snap, or re-evaluate
        // rules — that would surprise users. Committed state stays committed.
        auto pushLatest = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                pushWindowMetadata(safeW);
            }
        };
        connect(kw, &KWin::Window::windowClassChanged, this, pushLatest);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, pushLatest);
        connect(kw, &KWin::Window::captionChanged, this, pushLatest);
    }

    // Detect drag start/end via KWin's per-window signals instead of polling.
    // windowStartUserMovedResized fires once when an interactive move (or resize) begins;
    // windowFinishUserMovedResized fires once when it ends (button release, Escape, etc.).
    // This eliminates the poll timer that previously scanned the full stacking order at
    // 32ms intervals during drag — a significant source of compositor-thread overhead.
    //
    // NOTE: windowFrameGeometryChanged / windowStepUserMovedResized are intentionally NOT
    // connected for drag tracking. They fire on every pixel of movement, which would flood
    // D-Bus. Cursor position updates are handled event-driven via slotMouseChanged →
    // DragTracker::updateCursorPosition(), throttled to ~30Hz.
    connect(w, &KWin::EffectWindow::windowStartUserMovedResized, this, [this](KWin::EffectWindow* window) {
        m_dragTracker->handleWindowStartMoveResize(window);
        // window.move / window.resize shader transitions: KWin's interactive
        // move/resize is its own animation system (Window::moveResize via
        // pointer drag), but we layer an effect-side shader for visual
        // feedback. windowStartUserMovedResized doesn't disambiguate the
        // two; w->isUserResize() does — interactive resize sets it, plain
        // move leaves it false. Each direction can take its own shader
        // assignment. tryBeginShaderForEvent silently no-ops if the user
        // didn't assign a shader to the path.
        if (window) {
            tryBeginShaderForEvent(window,
                                   window->isUserResize() ? PhosphorAnimation::ProfilePaths::WindowResize
                                                          : PhosphorAnimation::ProfilePaths::WindowMove,
                                   animationDurationMs());
        }
    });
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, [this](KWin::EffectWindow* window) {
        m_dragTracker->handleWindowFinishMoveResize(window);
    });

    // Track when user manually unmaximizes a monocle-maximized window
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMaximizedStateChanged);

    // window.maximize / window.unmaximize shader transition. Sibling lambda
    // to the AutotileHandler hookup above (autotile drives the snap-back
    // logic; we drive the shader leg).
    //
    // KWin emits windowMaximizedStateChanged once per axis flip — a
    // user-driven left-half-snap → fully-maximize sequence fires twice
    // (vertical-only first, then fully-maximized). Without an edge filter
    // we'd start the WindowMaximize shader for the intermediate state,
    // then immediately install WindowMaximize on the next emission, with
    // the timer-driven teardown of the first racing the install of the
    // second. Track the last fully-maximized state per window and only
    // fire on actual edge transitions.
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, this,
            [this](KWin::EffectWindow* window, bool horizontal, bool vertical) {
                if (!window) {
                    return;
                }
                const bool fullyMaximized = horizontal && vertical;
                const bool wasFullyMaximized = m_lastFullyMaximized.value(window, false);
                if (fullyMaximized == wasFullyMaximized) {
                    return; // intermediate axis-only flip, no shader
                }
                m_lastFullyMaximized.insert(window, fullyMaximized);
                // Going-to-maximized is "appear" (forward 0→1);
                // returning to floating is "disappear" (reverse 1→0).
                tryBeginShaderForEvent(window, PhosphorAnimation::ProfilePaths::WindowMaximize, animationDurationMs(),
                                       /*reverse=*/!fullyMaximized);
            });

    // Track when a monocle-maximized window goes fullscreen
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFullScreenChanged);

    // Autotile: center undersized Wayland windows as soon as they commit constrained size
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFrameGeometryChanged);

    // Frame-geometry shadow: push the latest geometry to the daemon so
    // daemon-local shortcut handlers (float toggle, etc.) can read fresh
    // geometry without round-tripping. Debounced at ~50ms per window via
    // m_frameGeometryFlushTimer so rapid move/resize sequences collapse
    // into at most one D-Bus push.
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
            [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
                if (!safeW || !shouldHandleWindow(safeW)) {
                    return;
                }
                const QString windowId = getWindowId(safeW);
                if (windowId.isEmpty()) {
                    return;
                }
                const QRect geo = safeW->frameGeometry().toRect();
                if (geo.width() <= 0 || geo.height() <= 0) {
                    return;
                }
                m_pendingFrameGeometry[windowId] = geo;
                if (!m_frameGeometryFlushTimer->isActive()) {
                    m_frameGeometryFlushTimer->start();
                }
            });

    // Autotile: track minimize/unminimize to remove/re-add windows from tiling
    connect(w, &KWin::EffectWindow::minimizedChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMinimizedChanged);

    // Snap mode: track minimize/unminimize to float/unfloat snapped windows
    connect(w, &KWin::EffectWindow::minimizedChanged, this, &PlasmaZonesEffect::slotWindowMinimizedChanged);
}

void PlasmaZonesEffect::slotMouseChanged(const QPointF& pos, const QPointF& oldpos, Qt::MouseButtons buttons,
                                         Qt::MouseButtons oldbuttons, Qt::KeyboardModifiers modifiers,
                                         Qt::KeyboardModifiers oldmodifiers)
{
    Q_UNUSED(oldpos)
    Q_UNUSED(oldmodifiers)

    const bool modifiersChanged = (m_currentModifiers != modifiers);
    const bool buttonsChanged = (oldbuttons != buttons);

    if (buttonsChanged && m_dragTracker->isDragging()) {
        qCInfo(lcEffect) << "mouseChanged buttons:" << static_cast<int>(oldbuttons) << "->"
                         << static_cast<int>(buttons);
    }

    if (modifiersChanged) {
        m_currentModifiers = modifiers;
        qCDebug(lcEffect) << "Modifiers changed to" << static_cast<int>(modifiers);
    }
    m_currentMouseButtons = buttons;

    if (m_dragTracker->isDragging()) {
        if ((oldbuttons & Qt::LeftButton) && !(buttons & Qt::LeftButton)) {
            // Primary button released = drag is over. Force-end regardless of whether
            // other buttons (e.g. right-click for zone activation) are still held.
            //
            // KWin keeps isUserMove() true while any button is held, so
            // windowFinishUserMovedResized wouldn't fire until ALL buttons are
            // released. forceEnd() gives immediate snap response on LMB release.
            //
            // After forceEnd, applySnapGeometry will defer (retry every 100 ms)
            // until isUserMove() clears when the remaining buttons are released.
            m_dragTracker->forceEnd(pos);
        } else if (modifiersChanged || buttonsChanged) {
            // Push modifier/button changes to daemon during drag immediately.
            // This includes activation button press/release — the daemon's
            // lazy snap-drag activation uses these modifiers to decide when
            // to promote a pending drag to active (first tick with trigger
            // held) and when to hide the overlay (trigger released).
            //
            // For bypass (autotile) drags, modifier changes must also flow
            // so the daemon's autotile drag-insert rising-edge detection
            // (hold and toggle modes) can fire without requiring cursor
            // motion. Without this, tapping the trigger while stationary
            // was silently dropped.
            //
            // The daemon's updateDragCursor is cheap for pending drags
            // (returns early without running dragMoved), so the rapid fire
            // of modifier-change events during a drag no longer causes the
            // overlay destroy/create churn that prompted discussion #310's
            // sibling regression.
            const bool bypassed = m_currentDragPolicy.bypassReason == PhosphorProtocol::DragBypassReason::AutotileScreen
                || m_dragBypassedForAutotile;
            const bool shouldForward =
                bypassed || detectActivationAndGrab() || m_cachedZoneSelectorEnabled || !m_triggersLoaded;
            if (shouldForward) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("updateDragCursor"),
                    {m_dragTracker->draggedWindowId(), qRound(pos.x()), qRound(pos.y()),
                     static_cast<int>(m_currentModifiers), static_cast<int>(m_currentMouseButtons)},
                    QStringLiteral("updateDragCursor - modifier/button change"));
            }
        } else {
            // Position-only change: drive cursor tracking through DragTracker's
            // event-driven path. This eliminates QTimer jitter from the compositor
            // frame path — updates arrive at input-device cadence (throttled to
            // ~30Hz inside DragTracker to avoid D-Bus flooding).
            m_dragTracker->updateCursorPosition(pos);
        }
    }

    // Track which screen the cursor is on for shortcut screen detection.
    // Only send a D-Bus call when the cursor actually crosses to a different monitor
    // (or virtual screen), not on every pixel move. This gives the daemon accurate
    // cursor-based screen info on Wayland where QCursor::pos() is unreliable for
    // background processes.
    const QPoint roundedPos(qRound(pos.x()), qRound(pos.y()));
    auto* output = KWin::effects->screenAt(roundedPos);
    QString connectorName;
    QString effectiveScreenId;
    if (output) {
        connectorName = output->name();
        // Resolve to virtual screen ID if subdivisions exist
        effectiveScreenId = resolveEffectiveScreenId(roundedPos, output);
        if (effectiveScreenId != m_lastEffectiveScreenId) {
            m_lastEffectiveScreenId = effectiveScreenId;
            m_lastCursorOutput = connectorName;
            if (m_daemonServiceRegistered) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"),
                    {effectiveScreenId});
            }
        }
    }

    // Focus follows mouse: activate autotile window under cursor when not dragging.
    // Reuse effectiveScreenId computed above to avoid redundant resolveEffectiveScreenId call.
    if (!m_dragTracker->isDragging() && output) {
        m_autotileHandler->handleCursorMoved(pos, effectiveScreenId);
    }
}

void PlasmaZonesEffect::applyStaggeredOrImmediate(int count, const std::function<void(int)>& applyFn,
                                                  const std::function<void()>& onComplete)
{
    // Convert the D-Bus-sourced int to the typed enum at this boundary;
    // the library API only accepts SequenceMode. Unknown ints fall back
    // to AllAtOnce — same behaviour as Profile::fromJson.
    const PhosphorAnimation::SequenceMode mode =
        (m_cachedAnimationSequenceMode == static_cast<int>(PhosphorAnimation::SequenceMode::Cascade))
        ? PhosphorAnimation::SequenceMode::Cascade
        : PhosphorAnimation::SequenceMode::AllAtOnce;
    PhosphorAnimation::applyStaggeredOrImmediate(this, count, mode, m_cachedAnimationStaggerInterval, applyFn,
                                                 onComplete);
}

void PlasmaZonesEffect::slotDaemonReady()
{
    if (m_daemonServiceRegistered) {
        return; // Already ready — idempotent guard
    }
    if (m_bridgeRegistrationInFlight) {
        // A registerBridge async call is already pending. The Introspect-
        // probe path at line ~782 and the daemonReady D-Bus signal can
        // both fire slotDaemonReady before the FIRST registerBridge reply
        // sets m_daemonServiceRegistered. Without this gate, a daemon
        // racing its own readiness signal against an Introspect probe
        // would receive TWO registerBridge calls in flight, then both
        // replies would call continueDaemonReadySetup() — duplicate state
        // re-push. Idempotent? Mostly. Worth the fragility? No.
        return;
    }
    m_bridgeRegistrationInFlight = true;

    qCInfo(lcEffect) << "daemon ready: registering bridge before re-pushing state";

    // Register the compositor bridge with the daemon, passing our protocol
    // version so the daemon can reject us if we're too old. The daemon returns
    // its own API version and a session ID; "REJECTED" means version mismatch.
    //
    // All post-registration work (state re-push, virtual screen fetch, etc.) is
    // deferred into the reply callback so that on REJECTED / protocol mismatch
    // we never send a single stateful call to an incompatible daemon. Any such
    // call would either fail noisily or risk silent marshalling mismatches —
    // the very failure mode this PR is designed to prevent.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::CompositorBridge, QStringLiteral("registerBridge"));
    msg << QStringLiteral("kwin") << QString::number(PhosphorProtocol::Service::ApiVersion)
        << QStringList{QStringLiteral("borderless"), QStringLiteral("animation")};
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        // Clear the in-flight flag on EVERY return path (success, error,
        // rejection, version mismatch) so a subsequent slotDaemonReady
        // can retry. m_daemonServiceRegistered remains the long-lived
        // success gate; m_bridgeRegistrationInFlight only covers the
        // narrow window between the call leaving and its reply arriving.
        m_bridgeRegistrationInFlight = false;
        QDBusPendingReply<PhosphorProtocol::BridgeRegistrationResult> reply = *w;
        if (reply.isError()) {
            qCWarning(lcEffect) << "registerBridge call failed:" << reply.error().message()
                                << "— effect remains idle until the daemon signals ready again.";
            return;
        }
        PhosphorProtocol::BridgeRegistrationResult result = reply.value();
        if (const QString err = result.validationError(); !err.isEmpty()) {
            qCWarning(lcEffect) << "registerBridge reply rejected:" << err
                                << "— effect remains idle until the daemon signals ready again.";
            return;
        }
        if (result.sessionId == QLatin1String("REJECTED")) {
            qCCritical(lcEffect) << "Daemon REJECTED this effect: daemon apiVersion=" << result.apiVersion
                                 << "but this effect speaks" << PhosphorProtocol::Service::ApiVersion
                                 << "— update the effect to match the daemon.";
            return;
        }
        int daemonVersion = result.apiVersion.toInt();
        if (daemonVersion < PhosphorProtocol::Service::MinPeerApiVersion) {
            qCCritical(lcEffect) << "Daemon apiVersion" << daemonVersion << "is below this effect's minimum"
                                 << PhosphorProtocol::Service::MinPeerApiVersion
                                 << "— update the daemon to match the effect.";
            return;
        }
        qCInfo(lcEffect) << "Bridge registered: daemon apiVersion=" << result.apiVersion
                         << "session=" << result.sessionId;
        m_daemonServiceRegistered = true;
        continueDaemonReadySetup();
    });
}

void PlasmaZonesEffect::continueDaemonReadySetup()
{
    // All D-Bus calls use QDBusMessage::createMethodCall + asyncCall (no QDBusInterface)
    // to avoid synchronous D-Bus introspection that blocks the compositor thread.

    // Drop the snap-assist capture's "we recently posted this handle" set —
    // the daemon's bounded LRU is empty after a fresh registration (whether
    // first-start or restart), so any handle the kwin-effect would otherwise
    // skip on assumption-of-residence must be re-captured. Without this
    // reset, the first ~24 windows the user snap-assists toward after a
    // daemon restart silently fall back to icons.
    if (m_snapAssistHandler) {
        m_snapAssistHandler->resetRecentlyPostedThumbnails();
    }

    // Push KWin's output-order primary screen to the daemon so getPrimaryScreen()
    // reflects KDE Display Settings rather than QGuiApplication::primaryScreen().
    auto* ws = KWin::Workspace::self();
    if (ws) {
        const auto outputs = ws->outputOrder();
        if (!outputs.isEmpty()) {
            PhosphorProtocol::ClientHelpers::fireAndForget(
                this, PhosphorProtocol::Service::Interface::Screen, QStringLiteral("setPrimaryScreenFromKWin"),
                {outputs.first()->name()}, QStringLiteral("setPrimaryScreenFromKWin"));
        }
    }

    // Re-push cursor screen — use the cached effective screen ID (which includes
    // virtual screen IDs like "A/vs:0") so the daemon's shortcut handler resolves
    // to the correct virtual screen, not the physical monitor.
    // m_lastEffectiveScreenId was set during the last processCursorPosition() call
    // via resolveEffectiveScreenId(), so it already has the correct virtual ID.
    if (!m_lastEffectiveScreenId.isEmpty()) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("cursorScreenChanged"), {m_lastEffectiveScreenId},
                                                       QStringLiteral("cursorScreenChanged"));
        qCDebug(lcEffect) << "Re-sent cursor screen:" << m_lastEffectiveScreenId;
    } else if (!m_lastCursorOutput.isEmpty()) {
        // Fallback: no effective ID cached yet (cursor hasn't moved since startup).
        // Resolve physical ID from connector name.
        QString cursorScreenId;
        for (const auto* output : KWin::effects->screens()) {
            if (output->name() == m_lastCursorOutput) {
                cursorScreenId = outputScreenId(output);
                break;
            }
        }
        if (cursorScreenId.isEmpty()) {
            cursorScreenId = m_lastCursorOutput;
        }
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("cursorScreenChanged"), {cursorScreenId},
                                                       QStringLiteral("cursorScreenChanged"));
        qCDebug(lcEffect) << "Re-sent cursor screen (physical fallback):" << cursorScreenId;
    }

    // Re-notify active window (gives daemon lastActiveScreenName).
    // Use notifyWindowActivated which bypasses user exclusion lists — the daemon
    // must always know which window is active for correct shortcut handling.
    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (activeWindow) {
        notifyWindowActivated(activeWindow);
    }

    // Fetch virtual screen definitions from daemon — needed before any screen ID
    // resolution so that getWindowScreenId() and cursor tracking return virtual
    // screen IDs when subdivisions are configured.
    // Clear ready flag immediately to close the race window where stale virtual
    // screen state from the previous daemon cycle is used before the new fetch
    // completes.
    m_virtualScreensReady = false;
    fetchAllVirtualScreenConfigs();

    // Re-sync floating windows (async, no QDBusInterface needed).
    // MUST clear the local set first — after daemon restart, the daemon's float state
    // is empty (ephemeral). Without clearing, stale entries from the previous daemon
    // session would persist in the effect, causing isWindowFloating() to return true
    // for windows that are no longer floating.
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
            PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getFloatingWindows"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QStringList> reply = *w;
            if (reply.isValid()) {
                m_navigationHandler->clearAllFloatingState();
                QStringList floatingIds = reply.value();
                for (const QString& id : floatingIds) {
                    m_navigationHandler->setWindowFloating(id, true);
                }
                qCDebug(lcEffect) << "Synced" << floatingIds.size() << "floating windows from daemon";
            }
        });
    }

    // These already use QDBusMessage::createMethodCall (no QDBusInterface)
    loadCachedSettings();
    // Note: connectNavigationSignals() is NOT called here — it's already called
    // once in the constructor. D-Bus signal subscriptions persist across daemon
    // restarts. Calling it again would create duplicate connections, causing
    // handlers (e.g., toggleWindowFloat) to fire twice per signal.

    // Window state processing (autotile init, snap restore, etc.) depends on
    // virtual screen definitions being loaded for correct screen ID resolution.
    // Deferred to processDaemonReadyWindowState(), called by fetchAllVirtualScreenConfigs
    // once all async D-Bus replies have arrived.
}

void PlasmaZonesEffect::processDaemonReadyWindowState()
{
    if (m_daemonReadyWindowStateProcessed) {
        return;
    }
    m_daemonReadyWindowStateProcessed = true;

    // Delegate autotile re-initialization to handler.
    // Snapshot the active window so the autotile raise loop can re-activate it
    // after putting all tiled windows on top (which would bury non-tiled windows
    // like the KCM settings panel). Only set if the active window is NOT on an
    // autotile screen — autotile screens handle their own focus via
    // m_pendingAutotileFocusWindowId in the onComplete callback.
    KWin::EffectWindow* activeWin = KWin::effects->activeWindow();
    if (activeWin && !m_autotileHandler->isAutotileScreen(getWindowScreenId(activeWin))) {
        m_autotileHandler->setPendingReactivateWindow(activeWin);
    }
    m_autotileHandler->onDaemonReady();

    // Re-announce all existing windows on autotile screens in one batch D-Bus
    // call instead of per-window windowOpened round-trips.
    const auto windows = KWin::effects->stackingOrder();
    m_autotileHandler->notifyWindowsAddedBatch(windows);

    // Report all live window IDs to the daemon so it can prune stale
    // entries from KConfig (windows that were snapped but no longer exist).
    {
        QStringList aliveWindowIds;
        for (KWin::EffectWindow* w : windows) {
            if (w && shouldHandleWindow(w)) {
                aliveWindowIds.append(getWindowId(w));
            }
        }
        PhosphorProtocol::ClientHelpers::fireAndForget(
            this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("pruneStaleWindows"),
            {QVariant::fromValue(aliveWindowIds)}, QStringLiteral("pruneStaleWindows"));
    }

    // Fetch pre-computed pending restore geometries so slotWindowAdded can
    // teleport windows to their zone immediately (no D-Bus round-trip flash).
    // Fire-and-forget: the cache is populated asynchronously. Windows that open
    // before the reply arrives fall back to the normal async restore path.
    {
        QDBusMessage geoMsg = QDBusMessage::createMethodCall(
            PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
            PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getPendingRestoreGeometries"));
        auto* geoWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(geoMsg), this);
        connect(geoWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QString> reply = *w;
            if (!reply.isValid()) {
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
            if (!doc.isObject()) {
                return;
            }
            QJsonObject obj = doc.object();
            m_snapRestoreCache.clear();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                QJsonObject geo = it.value().toObject();
                int x = geo[QLatin1String("x")].toInt();
                int y = geo[QLatin1String("y")].toInt();
                int w = geo[QLatin1String("width")].toInt();
                int h = geo[QLatin1String("height")].toInt();
                QString savedScreen = geo[QLatin1String("screenId")].toString();
                if (w > 0 && h > 0) {
                    m_snapRestoreCache.insert(it.key(), CachedSnapRestore{QRect(x, y, w, h), savedScreen});
                }
            }
            qCDebug(lcEffect) << "Cached" << m_snapRestoreCache.size() << "pending restore geometries";
        });
    }

    // Restore snap state for all untracked windows.
    // pendingRestoresAvailable may have fired BEFORE daemonReady, causing
    // slotPendingRestoresAvailable to bail out (m_daemonServiceRegistered was false).
    // Now that the daemon is confirmed ready, retry the restore flow using raw
    // QDBusMessage (no QDBusInterface) to avoid synchronous introspection.
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
            PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            // Guard: prevent slotPendingRestoresAvailable from double-processing
            // the same windows. Set inside the callback so that if this D-Bus call
            // fails, the flag stays false and slotPendingRestoresAvailable can
            // still function as a fallback.
            m_daemonReadyRestoresDone = true;

            QDBusPendingReply<QStringList> reply = *w;
            QSet<QString> trackedAppIds;
            if (reply.isValid()) {
                const QStringList trackedWindows = reply.value();
                for (const QString& windowId : trackedWindows) {
                    QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
                    if (!appId.isEmpty()) {
                        trackedAppIds.insert(appId);
                    }
                }
            }

            // Snapshot the current stacking order before snap restores.
            // moveResize() on KWin 6 / Wayland implicitly raises the target
            // window. After all restores complete, we re-raise windows in
            // their original order — same pattern as the autotile handler's
            // onComplete raise loop in tiling.cpp.
            const auto allWindows = KWin::effects->stackingOrder();
            QVector<QPointer<KWin::EffectWindow>> savedStackingOrder;
            for (KWin::EffectWindow* w : allWindows) {
                savedStackingOrder.append(QPointer<KWin::EffectWindow>(w));
            }

            // Collect windows that need snap restoration (untracked).
            // Don't skip windows on autotile screens: KWin session restore may
            // place a window in the autotile screen's area even though it was
            // snapped in the snap screen before logout. The daemon's pending
            // restore entry knows the correct screen; if it returns a snap
            // geometry, the window moves off the autotile screen and the
            // autotile handler detects the departure via VS crossing detection.
            // Use QPointer for lifetime safety in case a window is destroyed
            // between collection and the dispatch loop below.
            QVector<QPointer<KWin::EffectWindow>> toRestore;
            for (KWin::EffectWindow* window : allWindows) {
                if (!window || !shouldHandleWindow(window)) {
                    continue;
                }
                if (window->isMinimized()) {
                    continue;
                }
                QString appId = ::PhosphorIdentity::WindowId::extractAppId(getWindowId(window));
                if (trackedAppIds.contains(appId)) {
                    continue;
                }
                toRestore.append(QPointer<KWin::EffectWindow>(window));
            }

            if (toRestore.isEmpty()) {
                qCDebug(lcEffect) << "No untracked windows need snap restore after daemon ready";
                return;
            }

            qCInfo(lcEffect) << "Triggered snap restore for" << toRestore.size()
                             << "untracked windows after daemon ready";

            // Track how many windows actually moved (moveResize was called).
            // If none moved, skip the stacking restoration — no disruption occurred.
            auto pending = std::make_shared<int>(toRestore.size());
            auto movedCount = std::make_shared<int>(0);

            for (const auto& safeWindow : toRestore) {
                if (!safeWindow || safeWindow->isDeleted()) {
                    // Window destroyed between collection and dispatch — count
                    // it as done so the pending counter still reaches zero.
                    if (--(*pending) == 0) {
                        qCDebug(lcEffect) << "Stacking restore: all targets gone, skipping";
                    }
                    continue;
                }
                // Snapshot geometry before the async call; if it changes after
                // applySnapGeometry, we know a moveResize happened.
                QRectF geoBefore = safeWindow->frameGeometry();

                callResolveWindowRestore(
                    safeWindow.data(), [pending, movedCount, safeWindow, geoBefore, savedStackingOrder]() {
                        // Detect whether moveResize actually fired by comparing geometry.
                        if (safeWindow && !safeWindow->isDeleted() && safeWindow->frameGeometry() != geoBefore) {
                            ++(*movedCount);
                        }

                        if (--(*pending) > 0) {
                            return;
                        }

                        // All snap restores done.
                        if (*movedCount == 0) {
                            qCDebug(lcEffect) << "Stacking restore: all windows at target geometry, skipping";
                            return;
                        }

                        // Re-raise windows in original order (bottom-to-top).
                        auto* ws = KWin::Workspace::self();
                        if (!ws) {
                            return;
                        }
                        for (const auto& wPtr : savedStackingOrder) {
                            if (wPtr && !wPtr->isDeleted()) {
                                KWin::Window* kw = wPtr->window();
                                if (kw) {
                                    ws->raiseWindow(kw);
                                }
                            }
                        }
                    });
            }
        });
    }
}

void PlasmaZonesEffect::slotSettingsChanged()
{
    qCInfo(lcEffect) << "settingsChanged: reloading settings";
    loadCachedSettings();
    // Note: loadAutotileSettings() is intentionally NOT called here.
    // Autotile screen changes are tracked via the dedicated autotileScreensChanged
    // D-Bus signal (→ slotAutotileScreensChanged), which is authoritative.
    // Calling loadAutotileSettings on every settingsChanged causes redundant
    // full window re-notification (N D-Bus windowOpened calls + retile round)
    // on every algorithm/gap/setting change — the daemon already retiles and
    // emits windowsTiled directly for those changes.
}

QString PlasmaZonesEffect::getWindowId(KWin::EffectWindow* w) const
{
    // windowId IS the instance id. The daemon's runtime primary key is this
    // opaque, compositor-supplied string. It's stable for the window's
    // lifetime regardless of class mutations, so every map/set keyed by
    // windowId inside the daemon is immune to Electron/CEF apps swapping
    // their WM_CLASS after the surface is mapped.
    //
    // App class is looked up separately — via getWindowAppId() here in the
    // effect, and via WindowRegistry in the daemon after pushWindowMetadata
    // updates the registry on KWin's class-change signals. Both read the live
    // value rather than trusting a frozen first-seen string.
    if (!w) {
        return QString();
    }

    // Cache hit: the composite is frozen at first observation for the
    // window's lifetime so daemon maps keyed by windowId stay stable even
    // when an Electron/CEF app mutates its class mid-session.
    auto cacheIt = m_windowIdCache.constFind(w);
    if (cacheIt != m_windowIdCache.constEnd()) {
        return cacheIt.value();
    }

    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    const QString instanceId = window->internalId().toString(QUuid::WithoutBraces);
    const QString appId = getWindowAppId(w);
    const QString result = ::PhosphorIdentity::WindowId::buildCompositeId(appId, instanceId);
    m_windowIdCache.insert(w, result);
    m_windowIdReverse.insert(result, const_cast<KWin::EffectWindow*>(w));
    return result;
}

QString PlasmaZonesEffect::getWindowInstanceId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    return window->internalId().toString(QUuid::WithoutBraces);
}

QString PlasmaZonesEffect::getWindowAppId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    // Prefer desktopFileName (stable cross-session identifier when available).
    QString appId = window->desktopFileName();
    if (appId.isEmpty()) {
        // Fallback: normalize windowClass
        //   X11: "resourceName resourceClass" → extract resourceClass
        //   Wayland: app_id as-is
        QString wc = w->windowClass();
        int spaceIdx = wc.indexOf(QLatin1Char(' '));
        appId = (spaceIdx > 0) ? wc.mid(spaceIdx + 1) : wc;
    }
    return appId.toLower();
}

void PlasmaZonesEffect::pushWindowMetadata(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    const QString instanceId = getWindowInstanceId(w);
    if (instanceId.isEmpty()) {
        return;
    }

    const QString appId = getWindowAppId(w);
    KWin::Window* window = w->window();
    const QString desktopFile = window ? window->desktopFileName() : QString();
    const QString title = w->caption();

    // Fire-and-forget — the daemon side is idempotent.
    PhosphorProtocol::ClientHelpers::fireAndForget(
        this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setWindowMetadata"),
        {instanceId, appId, desktopFile, title}, QStringLiteral("setWindowMetadata"));
}

void PlasmaZonesEffect::flushPendingFrameGeometry()
{
    if (m_pendingFrameGeometry.isEmpty()) {
        return;
    }
    // Move into a local so reentrancy from D-Bus (or later pushes) can't
    // disturb the iteration.
    const auto batch = std::exchange(m_pendingFrameGeometry, {});
    for (auto it = batch.constBegin(); it != batch.constEnd(); ++it) {
        const QRect& geo = it.value();
        PhosphorProtocol::ClientHelpers::fireAndForget(
            this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setFrameGeometry"),
            {it.key(), geo.x(), geo.y(), geo.width(), geo.height()}, QStringLiteral("setFrameGeometry"));
    }
}

bool PlasmaZonesEffect::isPlasmaShellSurface(const QString& windowClass)
{
    // Substring match on "plasmashell" already subsumes "org.kde.plasmashell".
    // Listed classes are the layer-shell surfaces that leak into autotile
    // tracking on Wayland: notification containers, system tray popups, the
    // OSD, the emoji picker, and krunner. Case-insensitive because Wayland
    // appIds and X11 class names differ in casing conventions.
    return windowClass.contains(QLatin1String("plasmashell"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("org.kde.plasma.emojier"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("org.kde.plasma.notifications"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("org.kde.krunner"), Qt::CaseInsensitive);
}

bool PlasmaZonesEffect::shouldHandleWindow(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    // Never snap our own overlay/editor windows (but allow the settings app)
    const QString windowClass = w->windowClass();
    if (windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive)) {
        return false;
    }

    // Exclude XDG desktop portal windows (file dialogs, color pickers, etc.)
    if (windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive)) {
        return false;
    }

    // Plasma shell layer-shell surfaces — see isPlasmaShellSurface() for rationale.
    if (isPlasmaShellSurface(windowClass)) {
        return false;
    }

    // Check user-configured exclusion lists (needed for drag gating — daemon also enforces
    // for keyboard nav, but the effect must filter for drag operations and lifecycle reporting)
    if (!m_excludedApplications.isEmpty() || !m_excludedWindowClasses.isEmpty()) {
        KWin::Window* kw = w->window();
        const QString appName = kw ? kw->desktopFileName() : QString();
        for (const QString& excluded : m_excludedApplications) {
            if (!excluded.isEmpty() && appName.contains(excluded, Qt::CaseInsensitive)) {
                return false;
            }
        }
        for (const QString& excluded : m_excludedWindowClasses) {
            if (!excluded.isEmpty() && windowClass.contains(excluded, Qt::CaseInsensitive)) {
                return false;
            }
        }
    }

    // Skip special / non-manageable window types (inherently effect-side — KWin metadata)
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()) {
        return false;
    }

    // Skip transient/dialog windows unconditionally. Dialogs, utilities, tooltips,
    // notifications, etc. should never be zone-managed. User-configured exclusion
    // lists and minimum size checks are handled by the daemon.
    if (w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isOnScreenDisplay()
        || w->isModal() || w->isPopupWindow()) {
        return false;
    }

    return true;
}

bool PlasmaZonesEffect::isTileableWindow(KWin::EffectWindow* w) const
{
    // Reject menus, popups, tooltips, modals, and transient children.
    // Electron apps (Vesktop, VS Code, Discord) create separate windows
    // for context menus and dropdowns that pass shouldHandleWindow() but
    // must never enter the autotile tree.
    if (!w->isNormalWindow() || w->isModal() || w->isPopupWindow() || w->isDropdownMenu() || w->isPopupMenu()
        || w->isTooltip() || w->isMenu() || w->transientFor()) {
        return false;
    }
    // Reject keep-above windows — overlay/utility tools (Spectacle, color
    // pickers, screen rulers, etc.) set keep-above and should not enter the
    // autotile tree or receive auto-focus. Without this guard, opening
    // Spectacle while focusNewWindows is enabled disrupts the tiled layout.
    if (w->keepAbove()) {
        return false;
    }
    return true;
}

// shouldAutoSnapWindow removed — equivalent to shouldHandleWindow + isTileableWindow.
// Call sites use isTileableWindow directly (stricter than shouldHandleWindow alone).

bool PlasmaZonesEffect::hasOtherWindowOfClassWithDifferentPid(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    QString windowClass = w->windowClass();
    pid_t windowPid = w->pid();

    // Check all existing windows for same class but different PID
    // This detects when another app (e.g., Cachy Update) spawns a window
    // of a class that the user has previously snapped (e.g., Ghostty)
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* other : windows) {
        if (other == w) {
            continue; // Skip self
        }
        if (!shouldHandleWindow(other)) {
            continue; // Skip non-managed windows
        }
        if (other->windowClass() == windowClass && other->pid() != windowPid) {
            // Found another window of the same class with different PID
            // This means the new window was likely spawned by a different app
            return true;
        }
    }

    return false;
}

bool PlasmaZonesEffect::isDaemonReady(const char* methodName) const
{
    if (!m_daemonServiceRegistered) {
        qCDebug(lcEffect) << "Cannot" << methodName << "- daemon not ready";
        return false;
    }
    return true;
}

void PlasmaZonesEffect::syncFloatingWindowsFromDaemon()
{
    // Delegate to NavigationHandler
    m_navigationHandler->syncFloatingWindowsFromDaemon();
}

// Template implementation for loadSettingAsync — delegates to shared helper.
template<typename Fn>
void PlasmaZonesEffect::loadSettingAsync(const QString& name, Fn&& onValue)
{
    PhosphorProtocol::ClientHelpers::loadSettingAsync(this, name, std::forward<Fn>(onValue));
}

void PlasmaZonesEffect::loadCachedSettings()
{
    // Uses raw QDBusMessage (not QDBusInterface) to avoid synchronous introspection
    // that would block the compositor during login (see discussion #158).
    //
    // Transient exclusion and min-size are handled by the daemon. Exclusion lists are
    // cached here for drag-operation gating (shouldHandleWindow).
    m_triggersLoaded = false; // Permissive until new triggers arrive (#175)

    loadSettingAsync(QStringLiteral("excludedApplications"), [this](const QVariant& v) {
        m_excludedApplications = v.toStringList();
    });
    loadSettingAsync(QStringLiteral("excludedWindowClasses"), [this](const QVariant& v) {
        m_excludedWindowClasses = v.toStringList();
    });
    loadSettingAsync(QStringLiteral("minimumWindowWidth"), [this](const QVariant& v) {
        m_cachedMinWindowWidth = v.toInt();
    });
    loadSettingAsync(QStringLiteral("minimumWindowHeight"), [this](const QVariant& v) {
        m_cachedMinWindowHeight = v.toInt();
    });
    loadSettingAsync(QStringLiteral("snapAssistEnabled"), [this](const QVariant& v) {
        m_snapAssistHandler->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationsEnabled"), [this](const QVariant& v) {
        m_windowAnimator->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationDuration"), [this](const QVariant& v) {
        // Clamp against the canonical settings-UI bounds. The earlier
        // local 500ms cap silently clamped a 2000ms user setting down
        // to 500ms, making shader transitions like matrix run far
        // faster than the daemon path's identical setting (the daemon
        // honours the full 2000ms range via the same constants).
        const int d = qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, v.toInt(),
                             PhosphorAnimation::Limits::MaxAnimationDurationMs);
        m_windowAnimator->setDuration(d);
        m_cachedAnimationDuration = d;
    });
    loadSettingAsync(QStringLiteral("animationEasingCurve"), [this](const QVariant& v) {
        // Polymorphic curve parse — handles bare bezier, named easing,
        // and "spring:..." in one path so Spring can drive snap motion
        // end-to-end without a settings-side branch.
        m_windowAnimator->setCurve(m_curveRegistry.create(v.toString()));
    });
    loadSettingAsync(QStringLiteral("animationMinDistance"), [this](const QVariant& v) {
        m_windowAnimator->setMinDistance(qBound(0, v.toInt(), 200));
    });
    loadSettingAsync(QStringLiteral("animationSequenceMode"), [this](const QVariant& v) {
        m_cachedAnimationSequenceMode = qBound(0, v.toInt(), 1);
    });
    loadSettingAsync(QStringLiteral("animationStaggerInterval"), [this](const QVariant& v) {
        m_cachedAnimationStaggerInterval = qBound(PhosphorAnimation::Limits::MinAnimationStaggerIntervalMs, v.toInt(),
                                                  PhosphorAnimation::Limits::MaxAnimationStaggerIntervalMs);
    });
    loadShaderProfileFromDbus();
    loadShaderRegistryFromDbus();
    loadSettingAsync(QStringLiteral("toggleActivation"), [this](const QVariant& v) {
        m_cachedToggleActivation = v.toBool();
    });
    loadSettingAsync(QStringLiteral("autotileDragInsertToggle"), [this](const QVariant& v) {
        m_cachedAutotileDragInsertToggle = v.toBool();
    });
    loadSettingAsync(QStringLiteral("autotileDragBehavior"), [this](const QVariant& v) {
        // Clamp unknown values to the safe default (Float) rather than the
        // highest known value — an older effect build against a newer daemon
        // must not silently map e.g. a future `ReorderAcrossScreens=2` onto
        // the nearest mode it happens to recognize.
        const int raw = v.toInt();
        switch (raw) {
        case static_cast<int>(EffectAutotileDragBehavior::Float):
            m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Float;
            break;
        case static_cast<int>(EffectAutotileDragBehavior::Reorder):
            m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Reorder;
            break;
        default:
            m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Float;
            break;
        }
    });
    loadSettingAsync(QStringLiteral("zoneSelectorEnabled"), [this](const QVariant& v) {
        m_cachedZoneSelectorEnabled = v.toBool();
    });

    // autotileHideTitleBars needs extra logic when toggled off — delegate to handler
    loadSettingAsync(QStringLiteral("autotileHideTitleBars"), [this](const QVariant& v) {
        m_autotileHandler->updateHideTitleBarsSetting(v.toBool());
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileShowBorder"), [this](const QVariant& v) {
        m_autotileHandler->updateShowBorderSetting(v.toBool());
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileBorderWidth"), [this](const QVariant& v) {
        int bw = qBound(0, v.toInt(), 10);
        if (m_autotileHandler->borderWidth() != bw) {
            m_autotileHandler->setBorderWidth(bw);
            // Invalidate pending stagger timers that would use the old border width
            m_autotileHandler->invalidateStaggerGeneration();
            PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Autotile,
                                                           QStringLiteral("retileAllScreens"), {},
                                                           QStringLiteral("border width change retile"));
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderRadius"), [this](const QVariant& v) {
        int br = qBound(0, v.toInt(), 20);
        if (m_autotileHandler->borderRadius() != br) {
            m_autotileHandler->setBorderRadius(br);
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderColor"), [this](const QVariant& v) {
        m_autotileHandler->setBorderColor(QColor(v.toString()));
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileInactiveBorderColor"), [this](const QVariant& v) {
        m_autotileHandler->setInactiveBorderColor(QColor(v.toString()));
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileFocusFollowsMouse"), [this](const QVariant& v) {
        m_autotileHandler->setFocusFollowsMouse(v.toBool());
    });

    // dragActivationTriggers — uses shared TriggerParser for QDBusArgument deserialization
    {
        PhosphorProtocol::ClientHelpers::loadSettingAsync(
            this, QStringLiteral("dragActivationTriggers"), [this](const QVariant& v) {
                m_parsedTriggers = TriggerParser::parseTriggers(v, TriggerModifierField, TriggerMouseButtonField);

                qCDebug(lcEffect) << "Loaded dragActivationTriggers:" << m_parsedTriggers.size() << "triggers";
                bool anyValid =
                    std::any_of(m_parsedTriggers.cbegin(), m_parsedTriggers.cend(), [](const ParsedTrigger& pt) {
                        return pt.modifier != 0 || pt.mouseButton != 0;
                    });
                if (!m_parsedTriggers.isEmpty() && !anyValid) {
                    qCWarning(lcEffect) << "All triggers have modifier=0 mouseButton=0"
                                        << "- possible deserialization issue";
                }
                m_triggersLoaded = true;
            });
    }

    qCDebug(lcEffect) << "Loading cached settings asynchronously, using defaults until loaded";
}

bool PlasmaZonesEffect::anyLocalTriggerHeld() const
{
    return TriggerParser::anyTriggerHeld(m_parsedTriggers, m_currentModifiers, m_currentMouseButtons);
}

bool PlasmaZonesEffect::detectActivationAndGrab()
{
    if (m_dragActivationDetected) {
        return true;
    }
    // Autotile drag-insert toggle mode also forces activation so the daemon
    // receives dragMoved ticks for rising-edge detection even when the drag
    // started on a non-autotile screen and the user hasn't held any snap
    // trigger. Without this, the cross-to-autotile policy flip never fires
    // because the gate below (drag lambda, slotMouseChanged) swallows ticks.
    if (anyLocalTriggerHeld() || m_cachedToggleActivation || m_cachedAutotileDragInsertToggle) {
        m_dragActivationDetected = true;
        if (!m_keyboardGrabbed) {
            KWin::effects->grabKeyboard(this);
            m_keyboardGrabbed = true;
        }
        return true;
    }
    return false;
}

// beginDrag is called unconditionally at drag-start; there's no deferred
// "only send dragStarted when zones activate" path because the daemon
// always knows about the drag from the moment it begins.

void PlasmaZonesEffect::connectNavigationSignals()
{
    // Daemon-driven navigation: daemon computes geometry and emits applyGeometryRequested directly
    QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("applyGeometryRequested"), this,
        SLOT(slotApplyGeometryRequested(QString, int, int, int, int, QString, QString, bool)));

    // Daemon-driven focus/cycle: daemon resolves target window and emits activateWindowRequested
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("activateWindowRequested"), this,
                                          SLOT(slotActivateWindowRequested(QString)));

    // Float toggle is entirely daemon-local: the daemon reads the active
    // window from its own shadow, calls toggleFloatForWindow internally, and
    // emits applyGeometryRequested to paint the outcome. The effect no longer
    // participates in the decision.

    // Daemon-driven batch operations (rotate, resnap emit applyGeometriesBatch)
    QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("applyGeometriesBatch"), this,
        SLOT(slotApplyGeometriesBatch(PhosphorProtocol::WindowGeometryList, QString)));

    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("raiseWindowsRequested"), this,
                                          SLOT(slotRaiseWindowsRequested(QStringList)));

    // Snap-all: daemon triggers effect to collect candidates
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("snapAllWindowsRequested"), this,
                                          SLOT(slotSnapAllWindowsRequested(QString)));

    // Move specific window (Snap Assist selection)
    QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("moveSpecificWindowToZoneRequested"), this,
        SLOT(slotMoveSpecificWindowToZoneRequested(QString, QString, int, int, int, int)));

    // Pending restores on daemon startup
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("pendingRestoresAvailable"), this,
                                          SLOT(slotPendingRestoresAvailable()));

    // Screen geometry reapply
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("reapplyWindowGeometriesRequested"),
                                          m_screenChangeHandler.get(), SLOT(slotReapplyWindowGeometriesRequested()));

    // Floating state sync
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("windowFloatingChanged"), this,
                                          SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    // Settings: window picker for KCM exclusion list
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::Settings,
                                          QStringLiteral("runningWindowsRequested"), this,
                                          SLOT(slotRunningWindowsRequested()));

    // WindowDrag: during-drag size restore
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowDrag,
                                          QStringLiteral("restoreSizeDuringDragChanged"), this,
                                          SLOT(slotRestoreSizeDuringDrag(QString, int, int)));

    // WindowDrag: cross-VS policy flip. Daemon detects the cursor crossing
    // a virtual-screen boundary that changes autotile↔snap routing and
    // emits this signal so the effect can apply the transition locally
    // (handleDragToFloat, onWindowClosed, overlay cancel, etc.).
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowDrag,
                                          QStringLiteral("dragPolicyChanged"), this,
                                          SLOT(slotDragPolicyChanged(QString, PhosphorProtocol::DragPolicy)));

    // WindowDrag: snap assist (delivered asynchronously, separate from the
    // fast endDrag reply). The daemon schedules the empty-zone-list compute
    // after endDrag returns, so the compositor is unblocked first.
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowDrag,
                                          QStringLiteral("snapAssistReady"), this,
                                          SLOT(slotSnapAssistReady(QString, QString, PhosphorProtocol::EmptyZoneList)));

    qCInfo(lcEffect) << "Connected to navigation D-Bus signals";
}

KWin::EffectWindow* PlasmaZonesEffect::getActiveWindow() const
{
    // Prefer KWin's active (focused) window when it is manageable and on current desktop
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    if (active && active->isOnCurrentActivity() && active->isOnCurrentDesktop() && !active->isMinimized()
        && shouldHandleWindow(active)) {
        return active;
    }
    // Fallback: topmost manageable window on current desktop (e.g. when activeWindow() is
    // null or refers to a dialog/utility we don't handle)
    const auto windows = KWin::effects->stackingOrder();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        KWin::EffectWindow* w = *it;
        if (w && w->isOnCurrentActivity() && w->isOnCurrentDesktop() && !w->isMinimized() && shouldHandleWindow(w)) {
            return w;
        }
    }
    return nullptr;
}

QString PlasmaZonesEffect::outputScreenId(const KWin::LogicalOutput* output) const
{
    if (!output) {
        return QString();
    }
    const QString connectorName = output->name();

    // Cache: screen IDs are stable for the lifetime of an output. Caching avoids
    // repeated QGuiApplication::screens() iteration and sysfs reads (~30Hz during drag).
    // Invalidated on screen add/remove (m_screenIdCache cleared by screen change handler).
    auto it = m_screenIdCache.constFind(connectorName);
    if (it != m_screenIdCache.constEnd()) {
        return *it;
    }

    // Build a screen ID that exactly matches the daemon's Phosphor::Screens::ScreenIdentity::identifierFor().
    // Uses shared ScreenIdUtils (compositor-common) for hex normalization and sysfs EDID
    // fallback, ensuring byte-identical output across daemon and compositor processes.
    //
    // Try QScreen::serialNumber() first (same source as daemon), then sysfs fallback.
    QString serialNumber;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == connectorName) {
            serialNumber = screen->serialNumber();
            break;
        }
    }

    const QString baseId = PhosphorIdentity::ScreenId::buildScreenBaseId(output->manufacturer(), output->model(),
                                                                         serialNumber, connectorName);

    // Disambiguate identical monitors: if another screen produces the same base ID,
    // append "/ConnectorName" to make each unique. Mirrors daemon's screenIdentifier().
    bool hasDuplicate = false;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() != connectorName
            && PhosphorIdentity::ScreenId::buildScreenBaseId(screen->manufacturer(), screen->model(),
                                                             screen->serialNumber(), screen->name())
                == baseId) {
            hasDuplicate = true;
            break;
        }
    }

    QString result = hasDuplicate ? baseId + QLatin1Char('/') + connectorName : baseId;
    m_screenIdCache.insert(connectorName, result);
    return result;
}

QString PlasmaZonesEffect::getWindowScreenId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    const QPointF c = w->frameGeometry().center();
    return resolveEffectiveScreenId(QPoint(qRound(c.x()), qRound(c.y())), w->screen());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual Screen Support
// ═══════════════════════════════════════════════════════════════════════════════

QString PlasmaZonesEffect::resolveEffectiveScreenId(const QPoint& pos, const KWin::LogicalOutput* output) const
{
    const QString physId = outputScreenId(output);
    if (physId.isEmpty()) {
        return physId;
    }

    // Check if this physical screen has virtual subdivisions
    auto it = m_virtualScreenDefs.constFind(physId);
    if (it == m_virtualScreenDefs.constEnd() || it->isEmpty()) {
        return physId; // No subdivisions, return physical ID
    }

    // Find which virtual screen contains the point.
    // Use exclusive-right/bottom semantics to match the daemon's containment check.
    // QRect::contains() uses inclusive-right, which causes boundary-pixel mismatches
    // between effect and daemon for abutting virtual screens.
    for (const auto& vs : *it) {
        const QRect& r = vs.geometry;
        if (pos.x() >= r.x() && pos.x() < r.x() + r.width() && pos.y() >= r.y() && pos.y() < r.y() + r.height()) {
            return vs.id;
        }
    }

    // Fallback: pick nearest virtual screen (covers rounding gaps)
    QString nearestVsId;
    int minDist = INT_MAX;
    for (const auto& vs : *it) {
        // Manhattan distance from point to nearest edge of the rect
        int dx = 0;
        int dy = 0;
        // Use exclusive-right/bottom (x + width, y + height) to match the
        // primary containment check above.  QRect::right()/bottom() return
        // inclusive values (x + width - 1), which would be off by 1px.
        const int exRight = vs.geometry.x() + vs.geometry.width();
        const int exBottom = vs.geometry.y() + vs.geometry.height();
        if (pos.x() < vs.geometry.left()) {
            dx = vs.geometry.left() - pos.x();
        } else if (pos.x() >= exRight) {
            dx = pos.x() - exRight;
        }
        if (pos.y() < vs.geometry.top()) {
            dy = vs.geometry.top() - pos.y();
        } else if (pos.y() >= exBottom) {
            dy = pos.y() - exBottom;
        }
        int dist = dx + dy;
        if (dist < minDist) {
            minDist = dist;
            nearestVsId = vs.id;
        }
    }
    if (!nearestVsId.isEmpty()) {
        return nearestVsId;
    }
    // Ultimate fallback (should never reach here)
    qCWarning(lcEffect) << "resolveEffectiveScreenId: no virtual screens found for" << physId;
    return physId;
}

void PlasmaZonesEffect::fetchVirtualScreenConfig(const QString& physicalScreenId, uint64_t generation)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Screen, QStringLiteral("getVirtualScreenConfig"));
    msg << physicalScreenId;

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    QPointer<PlasmaZonesEffect> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [self, physicalScreenId, generation](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (!self)
                    return;
                // Helper lambda: decrement pending counter and fire deferred processing when all done.
                // Only participates in the startup gate if generation != 0 (issued by fetchAllVirtualScreenConfigs)
                // and the generation matches the current one (not stale from a prior fetch cycle).
                // Captures self by value (QPointer copy) to avoid dangling reference.
                auto countdownVsGate = [self, generation]() {
                    if (generation == 0 || !self || self->m_vsConfigGeneration != generation) {
                        return;
                    }
                    if (self->m_pendingVsConfigReplies > 0 && --self->m_pendingVsConfigReplies == 0) {
                        self->m_virtualScreensReady = true;
                        if (self->m_daemonServiceRegistered) {
                            self->processDaemonReadyWindowState();
                        }
                    }
                };

                QDBusPendingReply<QString> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << "fetchVirtualScreenConfig: no virtual screens for" << physicalScreenId
                                      << reply.error().message();
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                    countdownVsGate();
                    return;
                }

                const QString json = reply.value();
                QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
                if (!doc.isObject()) {
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                    countdownVsGate();
                    return;
                }

                QJsonArray screens = doc.object().value(QLatin1String("screens")).toArray();

                // Look up the physical output geometry ONCE rather than per VS definition (O(N) vs O(N*M))
                QRect physGeom;
                const auto outputs = KWin::effects->screens();
                for (const auto* out : outputs) {
                    if (self->outputScreenId(out) == physicalScreenId) {
                        physGeom = out->geometry();
                        break;
                    }
                }

                if (!physGeom.isValid()) {
                    qCWarning(lcEffect) << "Physical output" << physicalScreenId
                                        << "not found (hot-unplug?) — skipping VS config update;"
                                        << "will re-fetch on reconnect";
                }

                QVector<EffectVirtualScreenDef> defs;
                for (const QJsonValue& val : screens) {
                    QJsonObject obj = val.toObject();
                    QJsonObject region = obj.value(QLatin1String("region")).toObject();

                    EffectVirtualScreenDef def;
                    def.id = obj.value(QLatin1String("id")).toString();

                    // Compute absolute geometry from fractional region within physical screen
                    if (physGeom.isValid()) {
                        qreal rx = region.value(QLatin1String("x")).toDouble();
                        qreal ry = region.value(QLatin1String("y")).toDouble();
                        qreal rw = region.value(QLatin1String("width")).toDouble();
                        qreal rh = region.value(QLatin1String("height")).toDouble();
                        // Edge-consistent rounding: compute edges then derive width/height
                        // to avoid 1px gaps between abutting virtual screens
                        int left = physGeom.x() + qRound(rx * physGeom.width());
                        int top = physGeom.y() + qRound(ry * physGeom.height());
                        int right = physGeom.x() + qRound((rx + rw) * physGeom.width());
                        int bottom = physGeom.y() + qRound((ry + rh) * physGeom.height());
                        def.geometry = QRect(left, top, right - left, bottom - top);
                    }

                    if (def.geometry.isValid() && !def.id.isEmpty()) {
                        defs.append(def);
                    }
                }

                if (defs.isEmpty()) {
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                } else {
                    qCInfo(lcEffect) << "Loaded" << defs.size() << "virtual screens for" << physicalScreenId;
                    self->m_virtualScreenDefs.insert(physicalScreenId, defs);
                }

                // Re-resolve tracked screen IDs so stale virtual screen IDs
                // are replaced with IDs from the updated boundaries.
                for (auto it = self->m_trackedScreenPerWindow.begin(); it != self->m_trackedScreenPerWindow.end();
                     ++it) {
                    auto* window = it.key();
                    if (!window || window->isDeleted()) {
                        continue;
                    }
                    {
                        const QPointF cf = window->frameGeometry().center();
                        const QPoint center(qRound(cf.x()), qRound(cf.y()));
                        const QString newScreenId = self->resolveEffectiveScreenId(center, window->screen());
                        if (!newScreenId.isEmpty()) {
                            it.value() = newScreenId;
                            // Also update the autotile handler's notified screen map
                            // so slotWindowFrameGeometryChanged does not compare against
                            // the stale pre-config-change screen ID.
                            const QString windowId = self->getWindowId(window);
                            self->m_autotileHandler->updateNotifiedScreen(windowId, newScreenId);
                        }
                    }
                }

                countdownVsGate();

                // For live VS config changes (generation=0), re-enable VS crossing
                // detection now that boundary definitions are updated.
                // countdownVsGate skips for generation=0, so m_virtualScreensReady
                // must be restored here. For startup fetches (generation>0),
                // countdownVsGate already sets it when all screens are processed.
                if (generation == 0) {
                    self->m_virtualScreensReady = true;
                }
            });
}

void PlasmaZonesEffect::fetchAllVirtualScreenConfigs()
{
    const auto outputs = KWin::effects->screens();

    // Collect physical screen IDs in a single pass to avoid count/iterate race
    // (a screen removed between two loops would cause count and calls to diverge)
    QStringList physIds;
    for (const auto* output : outputs) {
        const QString physId = outputScreenId(output);
        if (!physId.isEmpty()) {
            physIds.append(physId);
        }
    }

    physIds.removeDuplicates();

    // Prune stale m_virtualScreenDefs entries for physical screens that are no
    // longer connected. Without this, resolveEffectiveScreenId could match against
    // geometry from a disconnected monitor.
    const QSet<QString> currentPhysIds(physIds.begin(), physIds.end());
    for (auto it = m_virtualScreenDefs.begin(); it != m_virtualScreenDefs.end();) {
        if (!currentPhysIds.contains(it.key()))
            it = m_virtualScreenDefs.erase(it);
        else
            ++it;
    }

    if (physIds.isEmpty()) {
        // No physical screens to query — gate opens immediately
        m_virtualScreensReady = true;
        m_pendingVsConfigReplies = 0;
        if (m_daemonServiceRegistered) {
            processDaemonReadyWindowState();
        }
        return;
    }

    // Bump generation so stale callbacks from prior fetches are ignored
    const uint64_t generation = ++m_vsConfigGeneration;
    m_pendingVsConfigReplies = physIds.size();
    m_virtualScreensReady = false;

    for (const QString& physId : physIds) {
        fetchVirtualScreenConfig(physId, generation);
    }
}

void PlasmaZonesEffect::onVirtualScreensChanged(const QString& physicalScreenId)
{
    qCInfo(lcEffect) << "Virtual screens changed for" << physicalScreenId;
    m_screenIdCache.clear();
    m_lastEffectiveScreenId.clear();
    // Temporarily disable VS-aware crossing detection while the async fetch is in-flight.
    // Without this, slotWindowFrameGeometryChanged uses stale boundary definitions from the
    // old config, potentially causing spurious VS crossing events during the D-Bus round-trip.
    m_virtualScreensReady = false;
    fetchVirtualScreenConfig(physicalScreenId); // generation=0, won't participate in startup gate
}

void PlasmaZonesEffect::emitNavigationFeedback(bool success, const QString& action, const QString& reason,
                                               const QString& sourceZoneId, const QString& targetZoneId,
                                               const QString& screenId)
{
    // Call D-Bus method on daemon to report navigation feedback (can't emit signals on another service's interface)
    if (!isDaemonReady("report navigation feedback")) {
        return;
    }
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("reportNavigationFeedback"),
                                                   {success, action, reason, sourceZoneId, targetZoneId, screenId});
}

void PlasmaZonesEffect::slotActivateWindowRequested(const QString& windowId)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (w) {
        KWin::effects->activateWindow(w);
    } else {
        qCDebug(lcEffect) << "slotActivateWindowRequested: window not found" << windowId;
    }
}

void PlasmaZonesEffect::slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, int x,
                                                              int y, int width, int height)
{
    QRect geometry(x, y, width, height);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: invalid geometry" << geometry;
        return;
    }

    // Match by exact full window ID (appId|uuid) to distinguish
    // multiple windows of the same application. Fall back to appId only if
    // the exact match fails (e.g. window was recreated between candidate build
    // and selection).
    KWin::EffectWindow* targetWindow = nullptr;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w && shouldHandleWindow(w) && getWindowId(w) == windowId) {
            targetWindow = w;
            break;
        }
    }
    if (!targetWindow) {
        QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        for (KWin::EffectWindow* w : windows) {
            if (w && shouldHandleWindow(w) && ::PhosphorIdentity::WindowId::extractAppId(getWindowId(w)) == appId) {
                targetWindow = w;
                break;
            }
        }
    }

    if (!targetWindow) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: window not found" << windowId;
        emitNavigationFeedback(false, QStringLiteral("snap_assist"), QStringLiteral("window_not_found"));
        return;
    }

    // Capture geometry BEFORE applySnapGeometry resizes the window. The async D-Bus
    // callback in ensurePreSnapGeometryStored would read frameGeometry() after the
    // resize, corrupting the pre-tile entry with zone dimensions.
    ensurePreSnapGeometryStored(targetWindow, getWindowId(targetWindow), targetWindow->frameGeometry());
    applySnapGeometry(targetWindow, geometry);

    // Derive screen from the applied geometry center. Use resolveEffectiveScreenId
    // to get the virtual screen ID (not just the physical output).
    QPoint geoCenter = geometry.center();
    const auto* output = KWin::effects->screenAt(geoCenter);
    QString screenId = output ? resolveEffectiveScreenId(geoCenter, output) : getWindowScreenId(targetWindow);

    if (isDaemonReady("snap assist windowSnapped")) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Snap,
                                                       QStringLiteral("windowSnapped"),
                                                       {getWindowId(targetWindow), zoneId, screenId});
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Snap,
                                                       QStringLiteral("recordSnapIntent"),
                                                       {getWindowId(targetWindow), true});

        // Snap Assist continuation: only for manual-mode screens.
        // Autotile screens manage their own window placement; showing snap assist
        // after an autotile resnap is incorrect (the daemon silently ignores the
        // selection anyway via the isAutotileScreen guard in signals.cpp).
        if (!m_autotileHandler->isAutotileScreen(screenId)) {
            m_snapAssistHandler->showContinuationIfNeeded(screenId);
        }
    }
}

// slotToggleWindowFloatRequested removed — the daemon now handles float-toggle
// locally against its active-window + frame-geometry shadow and emits
// applyGeometryRequested directly. See WindowTrackingAdaptor::toggleWindowFloat.

void PlasmaZonesEffect::slotApplyGeometryRequested(const QString& windowId, int x, int y, int width, int height,
                                                   const QString& zoneId, const QString& screenId, bool sizeOnly)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: window not found" << windowId;
        return;
    }

    // Check for size-only restore (drag-out unsnap without activation trigger).
    // The daemon sets sizeOnly=true to restore pre-snap width/height while keeping
    // the window at its current drop position.
    if (sizeOnly) {
        if (width > 0 && height > 0) {
            QRectF currentFrame = w->frameGeometry();
            QRect sizeOnlyGeo(qRound(currentFrame.x()), qRound(currentFrame.y()), width, height);
            qCInfo(lcEffect) << "slotApplyGeometryRequested: size-only restore for" << windowId << width << "x"
                             << height;
            // Drag-out unsnap: the daemon kept us at the drop position but restored pre-snap
            // dimensions. Logically a snap-out (the window is leaving zone-managed sizing),
            // not an in-zone resize.
            applySnapGeometry(w, sizeOnlyGeo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                              PhosphorAnimation::ProfilePaths::ZoneSnapOut);
        }
        return;
    }

    QRect geometry(x, y, width, height);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotApplyGeometryRequested: invalid geometry" << geometry;
        return;
    }
    // Skip float-restore geometry on minimized windows: when a snapped window is minimized
    // we float it (to free the zone slot), but applying the pre-tile geometry while minimized
    // would poison what KWin restores to on unminimize, causing a visible flash of the
    // pre-snap geometry before the unfloat re-snaps to the zone.
    if (w->isMinimized() && zoneId.isEmpty()) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: skipping float-restore geometry on minimized window:"
                          << windowId;
        return;
    }
    // Skip float-restore geometry for drag-to-float: when the user drags a window
    // off the autotile layout, the daemon restores pre-autotile geometry. But the
    // user expects the window to stay where they dropped it, not snap back.
    if (zoneId.isEmpty() && m_dragFloatedWindowIds.remove(windowId)) {
        qCInfo(lcEffect) << "slotApplyGeometryRequested: skipping float-restore for drag-floated window:" << windowId;
        return;
    }
    qCInfo(lcEffect) << "slotApplyGeometryRequested:" << windowId << "geo:" << geometry << "zoneId:" << zoneId
                     << "screen:" << screenId << "floating:" << isWindowFloating(windowId)
                     << "currentFrame:" << w->frameGeometry();
    // Store pre-snap geometry before first snap (idempotent — skips if already stored).
    // The daemon handles windowSnapped/recordSnapIntent internally, but only the effect
    // knows the window's current frame geometry for pre-tile storage.
    if (!zoneId.isEmpty()) {
        // Capture frame geometry synchronously BEFORE applySnapGeometry moves the window.
        // ensurePreSnapGeometryStored is async (D-Bus hasPreTileGeometry check) — without
        // pre-capturing, the callback would read the post-move geometry instead of the
        // original free-floating position.
        ensurePreSnapGeometryStored(w, getWindowId(w), w->frameGeometry());
    }

    // Empty zoneId = float-restore (daemon placing the window back at its pre-snap geometry, e.g.
    // autotile drag-to-float, drag-out unsnap). Non-empty zoneId = snap into a target zone. The
    // shader-tree path differs accordingly so users can give snap-in and snap-out distinct effects.
    applySnapGeometry(w, geometry, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                      zoneId.isEmpty() ? PhosphorAnimation::ProfilePaths::ZoneSnapOut
                                       : PhosphorAnimation::ProfilePaths::ZoneSnapIn);
    // Note: windowSnapped/recordSnapIntent are NOT called here. For daemon-driven
    // navigation, the daemon handles zone bookkeeping internally before emitting
    // applyGeometryRequested. For legacy callers (autotile float restore via
    // applyGeometryForFloat), zoneId is empty so no snap confirmation is needed.
}

void PlasmaZonesEffect::slotApplyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries,
                                                 const QString& action)
{
    qCInfo(lcEffect) << "applyGeometriesBatch:" << action;

    if (geometries.isEmpty()) {
        return;
    }

    QHash<QString, KWin::EffectWindow*> windowMap = buildWindowMap();

    struct PendingApply
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
        QString screenId; ///< daemon-authoritative target screen (empty = no override)
    };
    QVector<PendingApply> pending;

    for (const auto& entry : geometries) {
        if (entry.windowId.isEmpty() || entry.width <= 0 || entry.height <= 0) {
            continue;
        }

        // Exact match first, appId fallback for single-instance apps
        KWin::EffectWindow* window = windowMap.value(entry.windowId);
        if (!window) {
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(entry.windowId);
            KWin::EffectWindow* candidate = nullptr;
            int matchCount = 0;
            for (auto it = windowMap.constBegin(); it != windowMap.constEnd(); ++it) {
                if (::PhosphorIdentity::WindowId::extractAppId(it.key()) == appId) {
                    candidate = it.value();
                    if (++matchCount > 1)
                        break;
                }
            }
            if (matchCount == 1) {
                window = candidate;
            }
        }

        if (!window) {
            continue;
        }

        PendingApply p;
        p.window = QPointer<KWin::EffectWindow>(window);
        p.geometry = entry.toRect();
        p.screenId = entry.screenId;
        pending.append(p);
    }

    if (pending.isEmpty()) {
        return;
    }

    // Note: ensurePreSnapGeometryStored is NOT called here. Batch operations (rotate, resnap)
    // move windows between zones — their pre-tile geometry is already stored from the original
    // snap. The daemon's processBatchEntries calls clearPreTileGeometry only for __restore__
    // entries (overflow windows). Calling ensurePreSnapGeometryStored here would race with
    // the daemon's clearPreTileGeometry and store the zone geometry as pre-tile, corrupting
    // the restore path on subsequent mode transitions.

    // Capture stacking order before applying geometries (moveResize raises on Wayland)
    const auto allWindows = KWin::effects->stackingOrder();
    QVector<QPointer<KWin::EffectWindow>> savedStack;
    for (KWin::EffectWindow* w : allWindows) {
        savedStack.append(QPointer<KWin::EffectWindow>(w));
    }

    // Map the daemon's action string to a shader-tree ProfilePath. "resnap" / "retile" are layout
    // changes (different layout or autotile recompute) — semantically a layout switch. "rotate"
    // moves windows between existing zones in the same layout — a snap-in. Default to ZoneSnapIn
    // for unknown actions (forward-compat with future daemon-emitted strings).
    const QString batchProfilePath = (action == QLatin1String("resnap") || action == QLatin1String("retile"))
        ? PhosphorAnimation::ProfilePaths::ZoneLayoutSwitchIn
        : PhosphorAnimation::ProfilePaths::ZoneSnapIn;

    applyStaggeredOrImmediate(
        pending.size(),
        [this, pending, batchProfilePath](int i) {
            const auto& p = pending[i];
            if (!p.window) {
                return;
            }
            // Seed the tracked-screen cache from the daemon's authoritative answer for
            // this batch BEFORE applySnapGeometry, not after. Empty screenId means the
            // daemon didn't supply an authoritative answer (e.g. autotile float-restore
            // path) — fall through to the existing geometry-based behavior in that case.
            // The pre-seed handles async follow-up frame changes; m_inDaemonGeometryApply
            // (set below) handles the synchronous frame change emitted from inside
            // applySnapGeometry, which would otherwise resolve the new position against
            // pre-rotation m_virtualScreenDefs and report a phantom cross-VS unsnap.
            if (!p.screenId.isEmpty()) {
                m_trackedScreenPerWindow[p.window] = p.screenId;
                m_autotileHandler->updateNotifiedScreen(getWindowId(p.window), p.screenId);
            }
            m_inDaemonGeometryApply = true;
            const auto guard = qScopeGuard([this] {
                m_inDaemonGeometryApply = false;
            });
            applySnapGeometry(p.window, p.geometry, /*allowDuringDrag=*/false,
                              /*skipAnimation=*/false, batchProfilePath);
        },
        [this, savedStack, action]() {
            // Restore z-order after all geometries applied
            auto* ws = KWin::Workspace::self();
            if (ws) {
                for (const auto& wPtr : savedStack) {
                    if (wPtr && !wPtr->isDeleted()) {
                        KWin::Window* kw = wPtr->window();
                        if (kw) {
                            ws->raiseWindow(kw);
                        }
                    }
                }
            }
            // Show snap assist after resnap if applicable
            if (action == QLatin1String("resnap") && m_snapAssistHandler->isEnabled()) {
                KWin::EffectWindow* activeWin = getActiveWindow();
                QString activeScreenId = activeWin ? getWindowScreenId(activeWin) : QString();
                if (!activeScreenId.isEmpty() && !m_autotileHandler->isAutotileScreen(activeScreenId)) {
                    m_snapAssistHandler->showContinuationIfNeeded(activeScreenId);
                }
            }
        });
}

void PlasmaZonesEffect::slotRaiseWindowsRequested(const QStringList& windowIds)
{
    auto* ws = KWin::Workspace::self();
    if (!ws) {
        return;
    }

    for (const QString& windowId : windowIds) {
        KWin::EffectWindow* w = findWindowById(windowId);
        if (w && !w->isDeleted()) {
            KWin::Window* kw = w->window();
            if (kw) {
                ws->raiseWindow(kw);
            }
        }
    }
}

void PlasmaZonesEffect::slotSnapAllWindowsRequested(const QString& screenId)
{
    qCInfo(lcEffect) << "Snap all windows requested for screen:" << screenId;

    if (!isDaemonReady("snap all windows")) {
        return;
    }

    // Async fetch all snapped windows to filter already-snapped ones locally
    QDBusPendingCall snapCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);

    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* sw) {
        sw->deleteLater();

        QDBusPendingReply<QStringList> snapReply = *sw;
        QSet<QString> snappedFullIds;
        QSet<QString> snappedAppIds;
        if (snapReply.isValid()) {
            for (const QString& id : snapReply.value()) {
                snappedFullIds.insert(id);
                snappedAppIds.insert(::PhosphorIdentity::WindowId::extractAppId(id));
            }
        }

        // Collect unsnapped, non-floating windows on this screen in stacking order
        // (bottom-to-top) so lower windows get lower-numbered zones deterministically
        QStringList unsnappedWindowIds;
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : windows) {
            if (!w || !shouldHandleWindow(w)) {
                continue;
            }

            QString windowId = getWindowId(w);
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);

            // User-initiated snap commands override floating state.
            // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

            // Always use EDID-based screen ID for comparison
            QString winScreen = getWindowScreenId(w);
            if (winScreen != screenId) {
                qCDebug(lcEffect) << "snap-all: skipping window on different screen" << appId;
                continue;
            }

            if (w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                qCDebug(lcEffect) << "snap-all: skipping minimized/other-desktop window" << appId;
                continue;
            }

            // Full ID match first (distinguishes multi-instance apps),
            // appId fallback for single-instance apps
            if (snappedFullIds.contains(windowId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window" << appId;
                continue;
            }
            if (!hasOtherWindowOfClassWithDifferentPid(w) && snappedAppIds.contains(appId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window (appId match)" << appId;
                continue;
            }

            unsnappedWindowIds.append(windowId);
        }

        qCDebug(lcEffect) << "snap-all: found" << unsnappedWindowIds.size() << "unsnapped windows to snap";

        if (unsnappedWindowIds.isEmpty()) {
            qCDebug(lcEffect) << "No unsnapped windows to snap on screen" << screenId;
            emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"), QString(),
                                   QString(), screenId);
            return;
        }

        if (!isDaemonReady("snap all windows calculation")) {
            return;
        }

        // Ask daemon to calculate zone assignments
        QDBusPendingCall calcCall = PhosphorProtocol::ClientHelpers::asyncCall(
            PhosphorProtocol::Service::Interface::Snap, QStringLiteral("calculateSnapAllWindows"),
            {QVariant::fromValue(unsnappedWindowIds), screenId});
        auto* calcWatcher = new QDBusPendingCallWatcher(calcCall, this);

        connect(calcWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* cw) {
            cw->deleteLater();

            QDBusPendingReply<PhosphorProtocol::SnapAllResultList> calcReply = *cw;
            if (calcReply.isError()) {
                qCWarning(lcEffect) << "calculateSnapAllWindows failed:" << calcReply.error().message();
                emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("calculation_error"),
                                       QString(), QString(), screenId);
                return;
            }

            PhosphorProtocol::SnapAllResultList snapResults = calcReply.value();

            // Build WindowGeometryList for the batch geometry path
            PhosphorProtocol::WindowGeometryList snapGeometries;
            snapGeometries.reserve(snapResults.size());
            for (const auto& r : snapResults) {
                snapGeometries.append(r.toGeometryEntry());
            }
            slotApplyGeometriesBatch(snapGeometries, QStringLiteral("snap_all"));

            // Confirm snap assignments with daemon
            if (isDaemonReady("snap-all confirmation")) {
                PhosphorProtocol::SnapConfirmationList confirmEntries;
                for (const auto& r : snapResults) {
                    PhosphorProtocol::SnapConfirmationEntry entry;
                    entry.windowId = r.windowId;
                    entry.zoneId = r.targetZoneId;
                    entry.screenId = screenId;
                    entry.isRestore = false;
                    confirmEntries.append(entry);
                }
                if (!confirmEntries.isEmpty()) {
                    QDBusMessage msg = QDBusMessage::createMethodCall(
                        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                        PhosphorProtocol::Service::Interface::Snap, QStringLiteral("windowsSnappedBatch"));
                    msg << QVariant::fromValue(confirmEntries);
                    auto* batchWatcher =
                        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
                    connect(batchWatcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher* bw) {
                        if (bw->isError()) {
                            qCWarning(lcEffect) << "windowsSnappedBatch D-Bus call failed:" << bw->error().message();
                        }
                        bw->deleteLater();
                    });
                }
            }
        });
    });
}

void PlasmaZonesEffect::slotPendingRestoresAvailable()
{
    // If slotDaemonReady already dispatched snap restores for this daemon
    // session, skip — both signals fire during restart, and the second round
    // of moveResize() calls would disrupt the stacking order that the first
    // round carefully preserves via activateWindow(previouslyActive).
    if (m_daemonReadyRestoresDone) {
        qCInfo(lcEffect) << "Pending restores: already handled by slotDaemonReady, skipping";
        return;
    }

    qCInfo(lcEffect) << "Pending restores: retrying restoration for all visible windows";

    if (!isDaemonReady("pending restores")) {
        return;
    }

    // Use ASYNC batch call to get all tracked windows at once
    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        QSet<QString> trackedAppIds;

        if (reply.isValid()) {
            // Extract app IDs from tracked windows for comparison
            const QStringList trackedWindows = reply.value();
            for (const QString& windowId : trackedWindows) {
                QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
                if (!appId.isEmpty()) {
                    trackedAppIds.insert(appId);
                }
            }
            qCDebug(lcEffect) << "Got" << trackedAppIds.size() << "tracked windows from daemon";
        } else {
            qCWarning(lcEffect) << "Failed to get tracked windows:" << reply.error().message();
            // Continue anyway - will try to restore all windows (daemon will handle duplicates)
        }

        // Now iterate through all visible windows and restore untracked ones
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* window : windows) {
            if (!window || !shouldHandleWindow(window)) {
                continue;
            }

            // Skip minimized or invisible windows
            if (window->isMinimized() || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
                continue;
            }

            // Check if this window is already tracked using local set lookup (O(1))
            QString windowId = getWindowId(window);
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
            if (trackedAppIds.contains(appId)) {
                continue; // Already tracked
            }

            // Window is not tracked - try to restore it
            qCDebug(lcEffect) << "Retrying restoration for untracked window:" << windowId;
            callResolveWindowRestore(window);
        }
    });
}

void PlasmaZonesEffect::slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId)
{
    Q_UNUSED(screenId)
    // Update local floating cache when daemon notifies us of state changes
    // This keeps the effect's cache in sync with the daemon, preventing
    // inverted toggle behavior when a floating window is drag-snapped.
    // Uses full windowId for per-instance tracking (appId fallback in isWindowFloating).
    qCInfo(lcEffect) << "Floating state changed for" << windowId << "- isFloating:" << isFloating;
    m_navigationHandler->setWindowFloating(windowId, isFloating);
    // When a window is unfloated (tiled/snapped), clear the drag-float skip flag.
    // Without this, a subsequent float toggle's geometry restore would be skipped
    // because m_dragFloatedWindowIds still has the entry from the original drag.
    if (!isFloating) {
        m_dragFloatedWindowIds.remove(windowId);
    }
}

void PlasmaZonesEffect::slotWindowMinimizedChanged(KWin::EffectWindow* w)
{
    if (!w || !shouldHandleWindow(w) || !isTileableWindow(w)) {
        return;
    }
    const QString windowId = getWindowId(w);
    const QString screenId = getWindowScreenId(w);

    // Autotile handler handles its own screens — only handle snap-mode here
    if (m_autotileHandler->isAutotileScreen(screenId)) {
        return;
    }

    const bool minimized = w->isMinimized();

    // window.minimize shader transition. We only fire on UN-minimize
    // (forward 0→1, "appear"). The going-to-minimized direction is
    // intentionally not a shader event on the kwin-effect path: KWin
    // pulls the surface (collapses frame geometry to 0×0 / sets
    // isMinimized=true) BEFORE this signal fires, and
    // beginShaderTransition's collapsed-surface guard rejects the
    // install — the FBO allocation aborts on a 0×0 redirect target.
    // A genuine "going away" minimise animation would need an
    // unredirect-time hook that captures the last live frame before
    // KWin tears the surface down; that's out of scope for this layer.
    if (!minimized) {
        tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowMinimize, animationDurationMs(),
                               /*reverse=*/false);
    }

    if (minimized) {
        if (isWindowFloating(windowId)) {
            qCDebug(lcEffect) << "Snap: minimized already-floating window, skipping float:" << windowId;
            return;
        }
        m_minimizeFloatedWindows.insert(windowId);
    } else {
        if (!m_minimizeFloatedWindows.remove(windowId)) {
            qCDebug(lcEffect) << "Snap: unminimized window was not minimize-floated, skipping unfloat:" << windowId;
            return;
        }
    }

    qCInfo(lcEffect) << "Snap: window" << (minimized ? "minimized, floating:" : "unminimized, unfloating:") << windowId
                     << "on" << screenId;

    if (m_daemonServiceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(
            this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setWindowFloatingForScreen"),
            {windowId, screenId, minimized}, QStringLiteral("setWindowFloatingForScreen"));
    }
}

void PlasmaZonesEffect::slotRunningWindowsRequested()
{
    qCInfo(lcEffect) << "Running windows requested by KCM";

    QJsonArray windowArray;
    QSet<QString> seenClasses;

    // Iterate in reverse (top-to-bottom) so deduplication keeps the topmost
    // window's caption per class, which is more useful to the user
    const auto windows = KWin::effects->stackingOrder();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        KWin::EffectWindow* w = *it;
        if (!w) {
            continue;
        }

        // Include all normal, non-special windows (relaxed filter for the picker)
        if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isSkipSwitcher() || w->isNotification()
            || w->isOnScreenDisplay() || w->isPopupWindow()) {
            continue;
        }

        QString windowClass = w->windowClass();
        if (windowClass.isEmpty()) {
            continue;
        }

        // Normalize X11 "resourceName resourceClass" to just resourceClass,
        // matching the format used by getWindowId() for app rule matching.
        int spaceIdx = windowClass.indexOf(QLatin1Char(' '));
        if (spaceIdx > 0) {
            windowClass = windowClass.mid(spaceIdx + 1);
        }

        // Deduplicate by windowClass (first seen = topmost due to reverse iteration)
        if (seenClasses.contains(windowClass)) {
            continue;
        }
        seenClasses.insert(windowClass);

        QString appName = ::PhosphorIdentity::WindowId::deriveShortName(windowClass);
        if (appName.isEmpty()) {
            appName = windowClass;
        }

        QJsonObject obj;
        obj[QLatin1String("windowClass")] = windowClass;
        obj[QLatin1String("appName")] = appName;
        obj[QLatin1String("caption")] = w->caption();
        windowArray.append(obj);
    }

    QString jsonString = QString::fromUtf8(QJsonDocument(windowArray).toJson(QJsonDocument::Compact));
    qCDebug(lcEffect) << "Providing" << windowArray.size() << "running windows to daemon";

    // Send result back to daemon via D-Bus
    if (m_daemonServiceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Settings,
                                                       QStringLiteral("provideRunningWindows"), {jsonString},
                                                       QStringLiteral("provideRunningWindows"));
    } else {
        qCWarning(lcEffect) << "provideRunningWindows: daemon not ready";
    }
}

bool PlasmaZonesEffect::borderActivated(KWin::ElectricBorder border)
{
    Q_UNUSED(border)
    // We no longer reserve edges, so this callback won't be triggered by our effect.
    // The daemon handles disabling Quick Tile via KWin config.
    return false;
}

void PlasmaZonesEffect::callResolveWindowRestore(KWin::EffectWindow* window, std::function<void()> onComplete)
{
    if (!window) {
        if (onComplete)
            onComplete();
        return;
    }

    if (!isDaemonReady("resolve window restore")) {
        if (onComplete)
            onComplete();
        return;
    }

    QString windowId = getWindowId(window);
    QString screenId = getWindowScreenId(window);
    bool sticky = isWindowSticky(window);

    QPointer<KWin::EffectWindow> safeWindow = window;

    // Single D-Bus call — daemon runs the full appRule → persisted → emptyZone → lastZone chain
    // skipAnimation=true: window is being restored to its snap position on startup/reopen,
    // so teleport directly instead of sliding from KWin's saved position.
    // storePreSnap=false: the window is already at its snap/zone position (from before
    // daemon restart or from KWin session restore), so its current frameGeometry is the
    // zone geometry — NOT the free-floating geometry. Storing it as pre-tile would cause
    // float toggle to restore to the zone geometry instead of the original free-floating position.
    tryAsyncSnapCall(PhosphorProtocol::Service::Interface::Snap, QStringLiteral("resolveWindowRestore"),
                     {windowId, screenId, sticky}, safeWindow, windowId, false, nullptr, nullptr,
                     /*skipAnimation=*/true, onComplete);
}

// The kwin-effect no longer calls the legacy dragStarted D-Bus method;
// beginDrag sets up snap-path state internally on the daemon side, so
// there's only one code path into the drag state machine.
bool PlasmaZonesEffect::isWindowSticky(KWin::EffectWindow* w) const
{
    return w && w->isOnAllDesktops();
}

void PlasmaZonesEffect::updateWindowStickyState(KWin::EffectWindow* w)
{
    if (!w || !m_daemonServiceRegistered) {
        return;
    }

    QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return;
    }

    bool sticky = isWindowSticky(w);
    // Use fire-and-forget instead of QDBusInterface to avoid synchronous D-Bus
    // introspection. slotWindowAdded → updateWindowStickyState fires for every
    // window during login; QDBusInterface creation blocks the compositor thread
    // for ~25s if the daemon hasn't entered app.exec() yet (daemonReady is
    // emitted before the event loop starts).
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("setWindowSticky"), {windowId, sticky},
                                                   QStringLiteral("setWindowSticky"));
}

// The dragMoved lambda sends updateDragCursor directly via
// ClientHelpers::fireAndForget. Single entry point for hot-path cursor updates.

void PlasmaZonesEffect::callEndDrag(KWin::EffectWindow* window, const QString& windowId, bool cancelled)
{
    // Single entry point for drag-end dispatch.
    // Sends endDrag, receives a PhosphorProtocol::DragOutcome, and applies exactly the
    // action the daemon decided. Replaces callDragStopped (whose reply
    // shape was a 9-tuple of out-params) with a typed struct.
    QPointF cursorAtRelease = m_dragTracker->lastCursorPos();

    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("endDrag"));
    msg << windowId << static_cast<int>(cursorAtRelease.x()) << static_cast<int>(cursorAtRelease.y())
        << static_cast<int>(m_currentModifiers) << static_cast<int>(m_currentMouseButtons) << cancelled;
    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(msg);

    QPointer<KWin::EffectWindow> safeWindow = window;
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    // Pair the watcher with a timeout. If the daemon is blocked (layout
    // recompute, overlay teardown, heavy handler), the compositor would
    // otherwise wait indefinitely for a reply that may never come. The
    // shared `handled` flag guarantees exactly-once handling: whichever
    // fires first (reply or timeout) takes the transition, the other path
    // is a no-op. Deleting the watcher does NOT cancel the underlying
    // QDBusPendingCall — any late reply is silently discarded by Qt.
    auto handled = std::make_shared<bool>(false);
    QTimer* timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [this, windowId, handled, watcher, timeoutTimer]() {
        if (*handled) {
            return;
        }
        *handled = true;
        qCWarning(lcEffect) << "endDrag timed out after" << EndDragTimeoutMs
                            << "ms; daemon unresponsive. Leaving window" << windowId << "at release position.";
        watcher->deleteLater();
        timeoutTimer->deleteLater();
    });
    timeoutTimer->start(EndDragTimeoutMs);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, handled, timeoutTimer](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (*handled) {
                    // Timeout already fired; this is a late reply — discard.
                    return;
                }
                *handled = true;
                timeoutTimer->stop();
                timeoutTimer->deleteLater();

                QDBusPendingReply<PhosphorProtocol::DragOutcome> reply = *w;
                if (reply.isError()) {
                    qCWarning(lcEffect) << "endDrag call failed:" << reply.error().message();
                    return;
                }
                const PhosphorProtocol::DragOutcome outcome = reply.value();
                if (const QString err = outcome.validationError(); !err.isEmpty()) {
                    // Garbled outcome — refuse to apply any window transform.
                    // Better to leave the window where it is than to float/snap
                    // based on a corrupted payload.
                    qCWarning(lcEffect) << "endDrag outcome rejected:" << err
                                        << "— dropping without applying any action for" << windowId;
                    return;
                }
                qCInfo(lcEffect) << "endDrag outcome:" << windowId << "action=" << outcome.action
                                 << "screen=" << outcome.targetScreenId << "geo=" << outcome.toRect()
                                 << "snapAssist=" << outcome.requestSnapAssist;

                switch (outcome.action) {
                case PhosphorProtocol::DragOutcome::NoOp:
                case PhosphorProtocol::DragOutcome::CancelSnap:
                case PhosphorProtocol::DragOutcome::NotifyDragOutUnsnap:
                    // Daemon handled any internal cleanup. Nothing for the
                    // effect to paint.
                    break;

                case PhosphorProtocol::DragOutcome::ApplyFloat: {
                    // Autotile bypass drag ended — float the window at its
                    // current screen. The plugin-side compositor work
                    // (handleDragToFloat, setWindowFloatingForScreen) was
                    // previously inlined in the dragStopped lambda; now it
                    // fires here off the daemon's authoritative answer.
                    //
                    // Cross-VS transitions that happened mid-drag were
                    // applied by slotDragPolicyChanged at the moment of
                    // crossing, so by the time we get here the autotile
                    // handler has the right tracking state.
                    if (!safeWindow) {
                        break;
                    }
                    const QString dropScreenId = getWindowScreenId(safeWindow);
                    if (dropScreenId.isEmpty()) {
                        break;
                    }
                    m_autotileHandler->handleDragToFloat(safeWindow, windowId, dropScreenId);
                    // Note: m_dragFloatedWindowIds is intentionally NOT re-set here.
                    // See dragStopped handler — the marker is cleared at drag end
                    // because the daemon's drag-end float path (setWindowFloat →
                    // windowFloatingStateSynced) never emits applyGeometryForFloat,
                    // so there's nothing for the marker to suppress.
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        this, PhosphorProtocol::Service::Interface::WindowTracking,
                        QStringLiteral("setWindowFloatingForScreen"), {windowId, dropScreenId, true},
                        QStringLiteral("setWindowFloatingForScreen - endDrag ApplyFloat"));
                    qCInfo(lcEffect) << "endDrag ApplyFloat:" << windowId << "on" << dropScreenId;
                    break;
                }

                case PhosphorProtocol::DragOutcome::ApplySnap: {
                    if (!safeWindow || safeWindow->isFullScreen()) {
                        break;
                    }
                    const QRect snapGeometry = outcome.toRect();
                    // If the window is still in user-move state because only
                    // the activation mouse button is held (LMB already
                    // released), cancel KWin's interactive move so we can
                    // snap immediately. Without this, applySnapGeometry
                    // defers (100ms retry) until ALL buttons are released —
                    // noticeable delay when using a mouse button (RMB) for
                    // zone activation.
                    if (safeWindow->isUserMove() && !(m_currentMouseButtons & Qt::LeftButton)) {
                        if (KWin::Window* kw = safeWindow->window()) {
                            kw->cancelInteractiveMoveResize();
                        }
                    }
                    applySnapGeometry(safeWindow, snapGeometry);
                    break;
                }

                case PhosphorProtocol::DragOutcome::RestoreSize: {
                    if (!safeWindow || safeWindow->isFullScreen()) {
                        break;
                    }
                    // Drag-to-unsnap: apply pre-snap width/height at current
                    // position. Skip if slotRestoreSizeDuringDrag already
                    // applied during the drag (size within 1px).
                    QRectF frame = safeWindow->frameGeometry();
                    const QRect geo(static_cast<int>(frame.x()), static_cast<int>(frame.y()), outcome.width,
                                    outcome.height);
                    if (qAbs(frame.width() - outcome.width) <= 1 && qAbs(frame.height() - outcome.height) <= 1) {
                        qCDebug(lcEffect) << "endDrag RestoreSize: already at correct size, skipping";
                        break;
                    }
                    if (safeWindow->isUserMove() && !(m_currentMouseButtons & Qt::LeftButton)) {
                        if (KWin::Window* kw = safeWindow->window()) {
                            kw->cancelInteractiveMoveResize();
                        }
                    }
                    // Drag-to-unsnap: window leaves zone-managed sizing, restore pre-snap dimensions.
                    applySnapGeometry(safeWindow, geo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                      PhosphorAnimation::ProfilePaths::ZoneSnapOut);
                    break;
                }
                }

                // Auto-fill: if window was dropped without snapping to a
                // zone and wasn't floated, try the first empty zone on the
                // release screen. Daemon-provided targetScreenId wins over
                // window's current screen (cross-screen drags).
                const bool applied = outcome.action == PhosphorProtocol::DragOutcome::ApplySnap
                    || outcome.action == PhosphorProtocol::DragOutcome::ApplyFloat;
                if (!applied && safeWindow && !outcome.targetScreenId.isEmpty() && isDaemonReady("auto-fill on drop")) {
                    const bool sticky = isWindowSticky(safeWindow);
                    auto onSnapSuccess = [this](const QString&, const QString& snappedScreenId) {
                        m_snapAssistHandler->showContinuationIfNeeded(snappedScreenId);
                    };
                    tryAsyncSnapCall(PhosphorProtocol::Service::Interface::Snap, QStringLiteral("snapToEmptyZone"),
                                     {windowId, outcome.targetScreenId, sticky}, safeWindow, windowId, true, nullptr,
                                     onSnapSuccess);
                }

                // Snap Assist: show the window picker if the daemon
                // requested it. asyncShow is non-blocking.
                if (outcome.requestSnapAssist && !outcome.emptyZones.isEmpty() && !outcome.targetScreenId.isEmpty()) {
                    m_snapAssistHandler->asyncShow(windowId, outcome.targetScreenId, outcome.emptyZones);
                }
            });
}

void PlasmaZonesEffect::callCancelSnap()
{
    qCInfo(lcEffect) << "Calling cancelSnap (drag cancelled by Escape or external event)";
    // QDBusMessage::createMethodCall — purely local, no D-Bus introspection.
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("cancelSnap"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PlasmaZonesEffect::tryAsyncSnapCall(const QString& interface, const QString& method, const QList<QVariant>& args,
                                         QPointer<KWin::EffectWindow> window, const QString& windowId,
                                         bool storePreSnap, std::function<void()> fallback,
                                         std::function<void(const QString&, const QString&)> onSnapSuccess,
                                         bool skipAnimation, std::function<void()> onComplete)
{
    QDBusPendingCall call = PhosphorProtocol::ClientHelpers::asyncCall(interface, method, args);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, window, windowId, storePreSnap, method, fallback, onSnapSuccess, args, skipAnimation,
             onComplete](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<int, int, int, int, bool> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << method << "error:" << reply.error().message();
                    if (fallback)
                        fallback();
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (reply.argumentAt<4>() && window) {
                    QRect geo(reply.argumentAt<0>(), reply.argumentAt<1>(), reply.argumentAt<2>(),
                              reply.argumentAt<3>());
                    qCInfo(lcEffect) << method << "snapping" << windowId << "to:" << geo;
                    if (storePreSnap)
                        ensurePreSnapGeometryStored(window, windowId, window ? window->frameGeometry() : QRectF());
                    applySnapGeometry(window, geo, false, skipAnimation);
                    // args[1] is screenId (e.g. for snapToEmptyZone, snapToLastZone)
                    if (onSnapSuccess && args.size() >= 2) {
                        onSnapSuccess(windowId, args[1].toString());
                    }
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (fallback)
                    fallback();
                if (onComplete)
                    onComplete();
            });
}

void PlasmaZonesEffect::repaintSnapRegions(KWin::EffectWindow* window, const QRectF& oldFrame, const QRect& newGeo)
{
    window->addRepaintFull();
    // Guard the global compositor repaint requests: this method can run
    // from late D-Bus reply callbacks (callEndDrag → applySnap → here)
    // that may dispatch during compositor teardown, when KWin::effects
    // has been torn down. The window-local addRepaintFull above is
    // safe because the EffectWindow itself is alive (we hold a
    // QPointer-checked reference at the call site).
    if (KWin::effects) {
        if (oldFrame.isValid()) {
            KWin::effects->addRepaint(oldFrame.toAlignedRect());
        }
        KWin::effects->addRepaint(newGeo);
    }
}

void PlasmaZonesEffect::applySnapGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag,
                                          bool skipAnimation, const QString& profilePath)
{
    if (!window) {
        qCWarning(lcEffect) << "applyGeometry: window is null";
        return;
    }

    // Normalize so width/height are non-negative; reject invalid rects
    QRect geo = geometry.normalized();
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) {
        qCWarning(lcEffect) << "applyGeometry: invalid or empty geometry:" << geometry;
        return;
    }

    // Don't call moveResize() on fullscreen windows, it can crash KWin.
    // See KDE bugs #429752, #301529, #489546.
    if (window->isFullScreen()) {
        qCDebug(lcEffect) << "applyGeometry: window is fullscreen, skipping";
        return;
    }

    // For X11/XWayland windows, KWin constrains the frame size to align with
    // WM_SIZE_HINTS (size increments for terminals like Ghostty, Kitty, etc.).
    // Pre-compute the constrained size and center the window in its zone so the
    // gap is distributed evenly instead of all at the bottom-right.
    // This applies to all snap operations (zone snap, autotile, resnap, etc.).
    // Wayland-native clients negotiate size async (constrainFrameSize only
    // checks min/max, not char-cell grid), so they're handled by the deferred
    // check in slotWindowFrameGeometryChanged().
    if (window->isX11Client()) {
        KWin::Window* kw = window->window();
        if (kw) {
            const QSizeF constrained = kw->constrainFrameSize(QSizeF(geo.size()));
            const int cw = qRound(constrained.width());
            const int ch = qRound(constrained.height());
            if (cw < geo.width() || ch < geo.height()) {
                // Clamp to non-negative: if min-size exceeds the zone in one
                // dimension, don't shift the window beyond the zone's edge.
                const int dx = qMax(0, geo.width() - cw) / 2;
                const int dy = qMax(0, geo.height() - ch) / 2;
                geo = QRect(geo.x() + dx, geo.y() + dy, cw, ch);
                qCDebug(lcEffect) << "Pre-centered X11 window with size constraints:"
                                  << "zone=" << geometry.size() << "constrained=" << constrained << "adjusted=" << geo;
            }
        }
    }

    // Skip no-op: if window is already at the target geometry AND there is
    // no in-flight animation, calling moveResize() is redundant and can have
    // subtle stacking side effects on some KWin versions (e.g. during daemon
    // restart double-processing).
    //
    // When an animation IS in flight, frameGeometry() already reflects the
    // committed target from the previous applySnapGeometry's moveResize —
    // but the visual position is still mid-transition. A rapid reversal
    // (float → unfloat, rotate → rotate back) legitimately targets the same
    // committed geometry and must NOT be skipped, because the animation needs
    // to play from the current visual position to that target.
    if (QRectF(geo) == window->frameGeometry() && !m_windowAnimator->hasAnimation(window)) {
        qCDebug(lcEffect) << "moveResize: window already at target geometry, skipping:" << geo;
        return;
    }

    qCDebug(lcEffect) << "Setting window geometry from" << window->frameGeometry() << "to" << geo;

    // Capture old frame before moveResize for repaint region
    const QRectF oldFrame = window->frameGeometry();

    // In KWin 6, we use the window's moveResize methods
    // When allowDuringDrag is false: defer if window is in user move/resize (snap on release)
    // When allowDuringDrag is true: apply immediately (snap-on-hover during drag)
    if (!allowDuringDrag && (window->isUserMove() || window->isUserResize())) {
        qCDebug(lcEffect) << "Window in user move/resize, deferring geometry via windowFinishUserMovedResized";
        QPointer<KWin::EffectWindow> safeWindow = window;
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(window, &KWin::EffectWindow::windowFinishUserMovedResized, this,
                        [this, safeWindow, geo, skipAnimation, profilePath, conn](KWin::EffectWindow*) {
                            disconnect(*conn);
                            if (safeWindow && !safeWindow->isDeleted() && !safeWindow->isFullScreen()) {
                                applySnapGeometry(safeWindow, geo, false, skipAnimation, profilePath);
                            }
                        });
        return;
    }

    // Animation: moveResize to the final geometry immediately, then morph
    // the window visually from its old position/size to the new one using
    // translate + scale in paintWindow(). This follows the standard KDE
    // effect pattern — effects are visual overlays, never per-frame moveResize.
    if (!skipAnimation && !allowDuringDrag && m_windowAnimator->isEnabled()) {
        const QRectF targetFrame(geo);

        // Apply final geometry immediately — client starts re-rendering at new size.
        // Do this before touching the animator so the controller's
        // downstream bounds / padding queries see the updated
        // expandedGeometry for this frame.
        KWin::Window* kw = window->window();
        if (kw) {
            kw->moveResize(targetFrame);
        }

        if (m_windowAnimator->hasAnimation(window)) {
            if (m_windowAnimator->isAnimatingToTarget(window, targetFrame)) {
                return; // Already animating to this target
            }
            // Capture the displaced animation's endpoints before retarget
            // modifies or deletes the entry. On a rapid reversal where
            // advance() hasn't ticked, m_current still equals m_from
            // (the animation's start point), so retarget(newTarget) sees
            // current ≈ newTarget when the reversal goes back to the
            // original zone — degenerate. Use the displaced animation's
            // TARGET as the visual origin for the replacement: that's
            // where the window was visually heading (and where moveResize
            // just committed to), so animating from there to the new
            // target matches the user's expectation.
            const QRectF displacedTarget = m_windowAnimator->animationFor(window)->to();
            const QRectF visualPos = m_windowAnimator->currentValue(window, QRectF(oldFrame));
            const auto result = m_windowAnimator->retargetWithResult(
                window, targetFrame, PhosphorAnimation::RetargetPolicy::PreserveVelocity);
            if (result == PhosphorAnimation::RetargetResult::DegenerateReap) {
                // Retarget collapsed (current visual ≈ new target).
                // Start a fresh animation from the displaced target
                // (where the window was heading) to the new target.
                // If that's also degenerate (same point), startAnimation
                // returns false and no animation plays — correct, since
                // there's no visual distance to cover.
                const QRectF animFrom = (displacedTarget != targetFrame) ? displacedTarget : visualPos;
                m_windowAnimator->startAnimation(window, animFrom, targetFrame);
            }
        } else {
            m_windowAnimator->startAnimation(window, QRectF(oldFrame), targetFrame);
        }

        if (m_windowAnimator->hasAnimation(window)) {
            const auto shaderProfile = m_shaderProfileTree.resolve(profilePath);
            if (!shaderProfile.effectiveEffectId().isEmpty()) {
                beginShaderTransition(window, shaderProfile);
            }
        }

        repaintSnapRegions(window, oldFrame, geo);
        return;
    }

    // No animation path (disabled, during drag, etc.): apply moveResize directly.
    if (m_windowAnimator->hasAnimation(window)) {
        m_windowAnimator->removeAnimation(window);
    }

    KWin::Window* kwinWindow = window->window();
    if (kwinWindow) {
        qCInfo(lcEffect) << "moveResize: QRect=" << geo << "-> QRectF=" << QRectF(geo);
        kwinWindow->moveResize(QRectF(geo));

        repaintSnapRegions(window, oldFrame, geo);
    } else {
        qCWarning(lcEffect) << "Cannot get underlying Window from EffectWindow";
    }
}

void PlasmaZonesEffect::slotRestoreSizeDuringDrag(const QString& windowId, int width, int height)
{
    // Restore pre-snap size when cursor leaves zone during drag. The window may have been
    // snapped when the drag started (at zone size); when the user drags out of all zones,
    // we restore to floated state immediately so they see the window return to original size.
    // This complements the release path (dragStopped) which also handles restore.
    if (!m_dragTracker->isDragging() || m_dragTracker->draggedWindowId() != windowId) {
        return;
    }

    KWin::EffectWindow* window = m_dragTracker->draggedWindow();
    if (!window || !shouldHandleWindow(window)) {
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    // Restore-size-only: keep current position, apply pre-snap width/height
    QRectF frame = window->frameGeometry();
    QRect geometry(static_cast<int>(frame.x()), static_cast<int>(frame.y()), width, height);

    qCDebug(lcEffect) << "Restoring size during drag:" << windowId << geometry;
    // Live drag-out unsnap: restoring pre-snap dimensions while the user is still dragging.
    // Logically a snap-out (the window is leaving zone-managed sizing).
    applySnapGeometry(window, geometry, /*allowDuringDrag=*/true, /*skipAnimation=*/false,
                      PhosphorAnimation::ProfilePaths::ZoneSnapOut);
}

void PlasmaZonesEffect::slotSnapAssistReady(const QString& windowId, const QString& releaseScreenId,
                                            const PhosphorProtocol::EmptyZoneList& emptyZones)
{
    // Discard if a new drag has already started — this signal was from a
    // prior drop. The daemon defers the compute to after endDrag returns,
    // so by the time this slot fires the user may already be dragging again.
    if (m_dragTracker->isDragging()) {
        qCDebug(lcEffect) << "Discarding snapAssistReady: new drag in progress";
        return;
    }
    if (emptyZones.isEmpty() || releaseScreenId.isEmpty()) {
        return;
    }
    m_snapAssistHandler->asyncShow(windowId, releaseScreenId, emptyZones);
}

void PlasmaZonesEffect::slotDragPolicyChanged(const QString& windowId, const PhosphorProtocol::DragPolicy& newPolicy)
{
    // Daemon-owned cross-VS flip. The daemon's updateDragCursor
    // handler computed policy at the current cursor position and found it
    // different from the policy in force — tell us so we can apply the
    // compositor-level transition. Replaces the effect-side cross-VS flip
    // loop in the dragMoved lambda that walked KWin::effects->screens()
    // with a stale m_autotileScreens cache.
    //
    // Guards: this slot only acts if we're actively tracking the drag for
    // this windowId. Stray signals (daemon restart, out-of-order delivery)
    // are ignored.
    if (!m_dragTracker->isDragging() || m_dragTracker->draggedWindowId() != windowId) {
        qCDebug(lcEffect) << "slotDragPolicyChanged: drag no longer active for" << windowId;
        return;
    }

    if (const QString err = newPolicy.validationError(); !err.isEmpty()) {
        // Garbled policy change — keep current state rather than transitioning
        // to a corrupted one. The daemon will re-emit on the next cursor tick
        // if this was transient.
        qCWarning(lcEffect) << "slotDragPolicyChanged rejected:" << err << "for" << windowId;
        return;
    }

    const PhosphorProtocol::DragBypassReason oldReason = m_currentDragPolicy.bypassReason;
    const PhosphorProtocol::DragBypassReason newReason = newPolicy.bypassReason;
    if (oldReason == newReason) {
        // Same reason but different screenId (autotile→autotile cross-VS):
        // update the captured screen so endDrag's ApplyFloat uses the right one.
        m_currentDragPolicy = newPolicy;
        if (newReason == PhosphorProtocol::DragBypassReason::AutotileScreen) {
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    qCInfo(lcEffect) << "slotDragPolicyChanged:" << windowId << oldReason << "->" << newReason
                     << "screen=" << newPolicy.screenId;

    m_currentDragPolicy = newPolicy;

    KWin::EffectWindow* dragW = m_dragTracker->draggedWindow();

    if (newReason == PhosphorProtocol::DragBypassReason::AutotileScreen) {
        // Snap → autotile (or context-disabled → autotile). Cancel any
        // active snap overlay, enter bypass mode. Mirrors the old
        // effect-side flip block's "snap→autotile" branch, but driven by
        // daemon truth rather than an effect-cached screen set.
        if (!m_dragBypassedForAutotile) {
            callCancelSnap();
            m_dragBypassedForAutotile = true;
            m_dragBypassScreenId = newPolicy.screenId;
            m_dragStartedSent = false;
            m_pendingDragWindowId.clear();
            m_pendingDragGeometry = QRectF();
            m_snapDragStartScreenId.clear();
        } else {
            // Already in bypass but on a different autotile screen — just
            // update the captured screen id.
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    if (oldReason == PhosphorProtocol::DragBypassReason::AutotileScreen) {
        // Autotile → snap (or autotile → context-disabled). Drop the
        // bypass flag and initialize snap-drag state as if the drag just
        // started on this snap screen. Remove the window from autotile
        // tracking so slotWindowFrameGeometryChanged doesn't fight the
        // snap geometry on subsequent geometry changes.
        //
        // Do NOT call handleDragToFloat here: the mid-drag schedule would
        // race against the zone snap at drop, making the window jump after
        // the user lets go. onWindowClosed alone clears the tracking state.
        if (dragW) {
            m_autotileHandler->onWindowClosed(windowId, m_dragBypassScreenId);
        }
        m_dragBypassedForAutotile = false;
        m_dragActivationDetected = false;
        m_dragStartedSent = false;
        m_pendingDragWindowId = windowId;
        m_pendingDragGeometry = dragW ? dragW->frameGeometry() : QRectF();
        m_snapDragStartScreenId = newPolicy.screenId;
        if (!m_keyboardGrabbed) {
            KWin::effects->grabKeyboard(this);
            m_keyboardGrabbed = true;
        }
        return;
    }

    // Other transitions (snap ↔ context_disabled / snapping_disabled):
    // no compositor-level work needed. The daemon will return a NoOp at
    // endDrag for disabled paths.
}

void PlasmaZonesEffect::notifyWindowClosed(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    const QString windowId = getWindowId(w);

    if (!isDaemonReady("notify windowClosed")) {
        return;
    }

    qCInfo(lcEffect) << "Notifying daemon: windowClosed" << windowId;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("windowClosed"), {windowId});
}

void PlasmaZonesEffect::notifyWindowActivated(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    // Skip non-manageable window types but NOT user-excluded apps — the daemon
    // must always know which window is active so that keyboard shortcuts can
    // correctly skip excluded windows instead of operating on a stale
    // m_lastActiveWindowId.
    const QString windowClass = w->windowClass();
    if (windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive)) {
        return;
    }
    if (windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive)) {
        return;
    }
    // Plasma shell surfaces — independent filter chain from shouldHandleWindow()
    // because notifyWindowActivated() intentionally skips user-exclusion lists
    // (the daemon still needs focus updates for excluded apps). The plasmashell
    // rejection must apply in both chains; see isPlasmaShellSurface().
    if (isPlasmaShellSurface(windowClass)) {
        return;
    }
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()
        || w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isOnScreenDisplay()
        || w->isModal() || w->isPopupWindow()) {
        return;
    }

    // window.focus shader transition. Fires after the rejection-filter cascade
    // so we don't shader plasmashell surfaces, dialogs, etc. — only "real"
    // app windows the user expects to see focus feedback on. Independent of
    // daemon-readiness gating below; the shader runs locally.
    //
    // Gate on a same-window check because KWin's windowActivated also fires
    // on virtual-desktop and activity switches, on re-stacking, and on
    // Wayland focus-stealing arbitration even when the focused window didn't
    // actually change. Without this gate the shader spams every desktop /
    // activity switch. m_lastFocusShaderWindow is a QPointer that auto-nulls
    // on window destroy, so a fresh window reusing the address can't
    // false-match.
    if (m_lastFocusShaderWindow.data() != w) {
        m_lastFocusShaderWindow = w;
        tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowFocus, animationDurationMs());
    }

    if (!isDaemonReady("notify windowActivated")) {
        return;
    }

    QString windowId = getWindowId(w);
    QString screenId = getWindowScreenId(w);

    qCDebug(lcEffect) << "Notifying daemon: windowActivated" << windowId << "on screen" << screenId;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("windowActivated"), {windowId, screenId});

    // Notify autotile engine of focus change so m_windowToScreen is updated
    if (m_autotileHandler->isAutotileScreen(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Autotile,
                                                       QStringLiteral("notifyWindowFocused"), {windowId, screenId},
                                                       QStringLiteral("notifyWindowFocused"));
    }
}

KWin::EffectWindow* PlasmaZonesEffect::findWindowById(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }

    // O(1) exact match via reverse cache
    auto it = m_windowIdReverse.constFind(windowId);
    if (it != m_windowIdReverse.constEnd() && it.value() && !it.value()->isDeleted()) {
        return it.value();
    }

    // Fallback: appId-based fuzzy match (for cross-session restore where
    // the UUID portion changed but the appId is the same)
    const QString targetAppId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
    KWin::EffectWindow* appMatch = nullptr;
    int matchCount = 0;

    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        const QString wId = getWindowId(w);
        if (::PhosphorIdentity::WindowId::extractAppId(wId) == targetAppId) {
            appMatch = w;
            ++matchCount;
        }
    }
    // Only return the fuzzy match if it's unambiguous — two Firefox windows
    // with different UUIDs would otherwise pick an arbitrary one and silently
    // misroute daemon requests.
    return matchCount == 1 ? appMatch : nullptr;
}

QVector<KWin::EffectWindow*> PlasmaZonesEffect::findAllWindowsById(const QString& windowId) const
{
    // Instance ids are unique — "all windows for a given id" is at most one
    // window. findAllWindowsById exists as an API seam for the (historical)
    // case where callers wanted every instance of an app class matching a
    // given composite; that semantic now lives on the daemon's
    // WindowRegistry::instancesWithAppId() + per-instance lookups. The
    // single-instance behavior here is the only case that remains.
    QVector<KWin::EffectWindow*> out;
    if (windowId.isEmpty()) {
        return out;
    }
    const QString targetAppId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        const QString wId = getWindowId(w);
        if (wId == windowId) {
            // Exact match — discard any appId matches accumulated from earlier
            // windows in the stacking order. Without this clear, a second instance
            // of the same app (same appId) triggers the disambiguation path in
            // slotWindowsTileRequested, which can assign the wrong EffectWindow to
            // the tile entry — leaving the new window untiled.
            return {w};
        }
        if (::PhosphorIdentity::WindowId::extractAppId(wId) == targetAppId) {
            out.append(w);
        }
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-window borders (native OutlinedBorderItem)
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::removeWindowBorder(const QString& windowId)
{
    auto it = m_windowBorders.find(windowId);
    if (it == m_windowBorders.end()) {
        return;
    }
    WindowBorder& wb = it.value();
    if (wb.clippedContainer) {
        wb.clippedContainer->setBorderRadius(wb.savedContainerRadius);
    }
    // QPointer: item may already be null if Qt parent-child ownership destroyed it.
    // Use deleteLater() rather than raw delete — OutlinedBorderItem is a QObject
    // parented into the scene graph and may have queued signals / pending paints
    // mid-cycle. CLAUDE.md: never manual-delete QObjects.
    //
    // Hide-then-deleteLater: updateWindowBorder calls removeWindowBorder and then
    // immediately allocates a new OutlinedBorderItem under the same windowItem
    // parent. Without setVisible(false) here, both the old and the new item live
    // in the scene graph for one event-loop iteration (until deleteLater fires)
    // and the user sees a one-frame flicker / Z-fight on every active-window
    // swap. Hiding first short-circuits the old item's render path while the
    // QObject deletion is still deferred per the CLAUDE.md no-manual-delete rule.
    if (wb.item) {
        wb.item->setVisible(false);
        wb.item->deleteLater();
    }
    QObject::disconnect(wb.geometryConnection);
    m_windowBorders.erase(it);
}

void PlasmaZonesEffect::clearAllBorders()
{
    while (!m_windowBorders.isEmpty()) {
        removeWindowBorder(m_windowBorders.begin().key());
    }
}

void PlasmaZonesEffect::updateWindowBorder(const QString& windowId, KWin::EffectWindow* w)
{
    // Remove existing border for this window first
    removeWindowBorder(windowId);

    const int bw = m_autotileHandler->borderWidth();
    if (bw <= 0) {
        return;
    }

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    if (!m_autotileHandler->shouldShowBorderForWindow(windowId)) {
        return;
    }

    // Choose color: active for focused window, inactive for others
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    const bool isFocused = (w == active);
    const QColor bc = isFocused ? m_autotileHandler->borderColor() : m_autotileHandler->inactiveBorderColor();
    if (!bc.isValid() || bc.alpha() == 0) {
        return;
    }

    // The OutlinedBorderItem draws the border OUTSIDE the innerRect, but the
    // parent WindowItem clips children to the window frame.  Inset the innerRect
    // by borderWidth so the border draws fully inside the frame (no clipping).
    const QRectF frame = w->frameGeometry();
    const KWin::RectF innerRect(bw, bw, frame.width() - 2.0 * bw, frame.height() - 2.0 * bw);
    const int br = m_autotileHandler->borderRadius();
    const KWin::BorderOutline outline(bw, bc, KWin::BorderRadius(br));

    KWin::WindowItem* windowItem = w->windowItem();
    if (!windowItem) {
        return;
    }

    WindowBorder wb;
    wb.item = new KWin::OutlinedBorderItem(innerRect, outline, windowItem);

    // Clip the window contents so they don't poke past the rounded outline
    // at the corners (dark pixels leaking past the border).
    //
    // Geometry: KWin's BorderOutline takes `radius` as the INNER curve
    // radius (verified against src/scene/outlinedborderitem.cpp:buildQuads —
    // the corner quad is sized `thickness + radius`, with the arc going
    // from the inner straight-edge meeting points at distance `radius` from
    // the corner-quad center). The outer curve is concentric and has
    // radius `radius + thickness`.
    //
    // We pass `br` as BorderOutline.radius and `bw` as thickness, so:
    //   - Outline's INNER curve: radius `br`, located at innerRect edges
    //     `(bw, bw)–(w-bw, h-bw)`.
    //   - Outline's OUTER curve: radius `br + bw`, at the frame edges
    //     `(0, 0)–(w, h)`.
    //
    // Clip on `windowContainer()`, NOT on the SurfaceItem directly:
    //   - WindowItem::m_windowContainer is the parent Item that holds the
    //     surface + decoration. Its rect is the FULL frame (0, 0, w, h) —
    //     identical to the outline's outer rect.
    //   - SurfaceItem::rect() is the client buffer extent, which can be
    //     SMALLER than the frame for SSD windows (decoration adds margin)
    //     or have a non-zero offset within the windowContainer.
    //   - Item::setBorderRadius rounds the item's OWN rect corners, so a
    //     clip on the surface anchors at surface-local origin — wrong for
    //     SSD windows where surface != frame.
    //   - The borderRadius propagates via cornerStack to descendants, so
    //     clipping the windowContainer applies the same RoundedCorners
    //     shader trait to the SurfaceItem render branch but anchored at
    //     the frame corners (where the outline lives), regardless of
    //     surface buffer size or offset.
    //
    // Don't go through Window::setBorderRadius — that triggers KDecoration3
    // active-state outline machinery on focused windows, drawing an extra
    // inset outline that looks visually different from the inactive border.
    //
    // Apply universally when bw > 0: SSD windows we made borderless (their
    // surface IS the content area), CSD windows we left alone (GTK/Electron
    // — hasDecoration returned false so the borderless path skipped them),
    // and any other tiled window whose squared corners would peek past the
    // rounded outline.
    if (bw > 0) {
        KWin::Item* container = windowItem->windowContainer();
        if (container) {
            const int containerRadius = br + bw;
            wb.savedContainerRadius = container->borderRadius();
            container->setBorderRadius(KWin::BorderRadius(containerRadius));
            wb.clippedContainer = container;
        }
    }

    // Keep the border in sync when the window resizes or moves.
    const QString wid = windowId; // capture by value
    wb.geometryConnection =
        connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
                [this, wid, bw](KWin::EffectWindow* ew, const QRectF& /*oldGeo*/) {
                    auto it = m_windowBorders.find(wid);
                    if (it != m_windowBorders.end() && it->item) {
                        const QRectF f = ew->frameGeometry();
                        it->item->setInnerRect(KWin::RectF(bw, bw, f.width() - 2.0 * bw, f.height() - 2.0 * bw));
                    }
                });

    m_windowBorders.insert(windowId, wb);
}

void PlasmaZonesEffect::updateAllBorders()
{
    clearAllBorders();

    const int bw = m_autotileHandler->borderWidth();
    if (bw <= 0) {
        return;
    }

    // Iterate all effect windows and create borders for tiled ones
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted() || !w->isOnCurrentDesktop()) {
            continue;
        }
        const QString wid = getWindowId(w);
        if (m_autotileHandler->shouldShowBorderForWindow(wid)) {
            updateWindowBorder(wid, w);
        }
    }
}

PhosphorAnimation::IMotionClock* PlasmaZonesEffect::clockForOutput(KWin::LogicalOutput* output) const
{
    if (output) {
        auto it = m_motionClocksByOutput.find(output);
        if (it != m_motionClocksByOutput.end()) {
            return it->second.get();
        }
    }
    return m_motionClockFallback.get();
}

void PlasmaZonesEffect::onScreenAdded(KWin::LogicalOutput* output)
{
    if (!output) {
        return;
    }
    // Construct a bound clock for this output. Idempotent: if the same
    // output arrives twice (rare, but possible on some compositors'
    // hotplug sequences) we keep the existing clock rather than
    // replacing it — the old clock's latched presentTime would be
    // lost and any in-flight animations bound to it would see a dt
    // jump.
    if (m_motionClocksByOutput.find(output) != m_motionClocksByOutput.end()) {
        return;
    }
    m_motionClocksByOutput.emplace(output, std::make_unique<CompositorClock>(output));
}

void PlasmaZonesEffect::onScreenRemoved(KWin::LogicalOutput* output)
{
    if (!output) {
        return;
    }
    // Any in-flight AnimatedValue whose MotionSpec captured this clock's
    // pointer would UAF on its next advance() if we just dropped the
    // unique_ptr. Reap only the animations bound to THIS output's clock
    // — other outputs' animations keep ticking uninterrupted. Uses the
    // controller's reapAnimationsForClock() helper which iterates
    // m_animations and filters on spec().clock pointer equality.
    auto it = m_motionClocksByOutput.find(output);
    if (it == m_motionClocksByOutput.end()) {
        return;
    }
    // m_windowAnimator is a unique_ptr initialized in the ctor and
    // never reset except during ~PlasmaZonesEffect; any screenRemoved
    // signal posted after our destruction is auto-disconnected by
    // QObject's teardown, so a nullptr guard here would be dead
    // code rather than defensive. Assert the invariant instead.
    Q_ASSERT(m_windowAnimator);

    // Ordering matters: extract the unique_ptr and erase the map
    // entry BEFORE calling reap. A re-entrant `onAnimationReaped` hook
    // that starts a new animation on a handle whose `screen()` still
    // returns the dying output would otherwise route through
    // `clockForOutput(output)` → find this clock in the map → bind
    // the new animation to it. The subsequent destructor run would
    // then UAF on the next advanceAnimations. By erasing first, the
    // lookup falls through to the fallback clock — new animations
    // started during reap are born bound to the fallback, never the
    // dying clock. The `dyingClock` unique_ptr keeps the clock alive
    // for the reap iteration itself (the captured raw pointer remains
    // valid through the function's scope).
    std::unique_ptr<CompositorClock> dyingClock = std::move(it->second);
    m_motionClocksByOutput.erase(it);
    m_windowAnimator->reapAnimationsForClock(dyingClock.get());
    // dyingClock destroyed at scope exit — at this point reap has
    // cleared every animation that captured the pointer, so the
    // destruction cannot strand a dangling MotionSpec::clock.
}

void PlasmaZonesEffect::prePaintScreen(KWin::ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
    // Feed presentTime to the clock for THIS output so animations
    // bound to other outputs' clocks read stale `now` on their
    // AnimatedValue::advance() calls this tick and step with dt=0
    // (correct: they tick when their own output paints, not when any
    // output paints).
    //
    // The fallback clock is intentionally NOT fed per-output presentTime
    // here. It self-drives from std::chrono::steady_clock — on an
    // N-output desktop, prePaintScreen fires N× per vsync, and pushing
    // presentTime into the fallback every call would step fallback-bound
    // animations N× per frame. Fallback's now() reads steady_clock
    // directly so it advances once per wall-clock moment regardless of
    // how many outputs painted. See CompositorClock::now()/updatePresentTime
    // for the fallback branch; epoch identity is shared (both rooted at
    // steady_clock) so rebinds between per-output and fallback remain
    // compatible.
    if (data.screen) {
        auto it = m_motionClocksByOutput.find(data.screen);
        if (it != m_motionClocksByOutput.end()) {
            // Pass `data.screen` so the clock can cross-check in debug
            // builds that it is being fed presentTime only for the
            // output it was constructed against. The map lookup above
            // already guarantees this by construction, but the extra
            // argument makes the invariant explicit at the call site —
            // a future refactor that stops keying by output will fire
            // the assertion instead of silently latching another
            // output's timestamps.
            it->second->updatePresentTime(presentTime, data.screen);
        }
    }

    // advanceAnimations iterates all animations regardless of which
    // clock was just updated; each animation reads its own clock's
    // `now()` in AnimatedValue::advance and steps with its own dt.
    // Cost is O(#animations) per prePaintScreen — typical paths see
    // single-digit counts.
    m_windowAnimator->advanceAnimations();

    if (m_windowAnimator->hasActiveAnimations() || !m_shaderTransitions.empty()) {
        // Windows have translation transforms that move them outside their
        // frame geometry bounds — force full compositing mode. Shader
        // transitions also need this: without
        // PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS the compositor skips
        // our paintWindow override on stable, undamaged windows (focus,
        // open after the fade settles, minimise, etc.), which means
        // the shader installs and silently expires unrendered.
        data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }

    // Cache cursor pos once per frame for shader-transition iMouse uniform.
    // paintWindow runs once per active transition (and may run multiple
    // times across outputs); reading KWin::effects->cursorPos() at every
    // call multiplies up. Caching here also guarantees every transition
    // this frame reads an identical iMouse, eliminating sub-frame jitter.
    if (KWin::effects && !m_shaderTransitions.empty()) {
        m_cachedCursorGlobal = KWin::effects->cursorPos();
    }

    KWin::effects->prePaintScreen(data, presentTime);
}

void PlasmaZonesEffect::postPaintScreen()
{
    // Schedule targeted repaints for active animations instead of full-screen
    m_windowAnimator->scheduleRepaints();
    // Time-based shader transitions (window.*) ride a steady-clock
    // timer, not m_windowAnimator, so paintWindow would only fire on
    // surface damage and iTime would stall. Mirror KWin's own
    // `AnimationEffect::postPaintScreen`: while a time-based transition
    // is live, inject expanded-geometry layer repaint per active
    // window so the next vsync runs our paint chain. Animator-driven
    // transitions (durationMs == 0) are kept alive by
    // m_windowAnimator->scheduleRepaints above.
    if (!m_shaderTransitions.empty()) {
        const qint64 now = shaderClockNowMs();
        for (const auto& [w, transition] : m_shaderTransitions) {
            if (transition.durationMs > 0 && (now - transition.startTimeMs) <= transition.durationMs) {
                if (w && !w->isDeleted()) {
                    // Fall back to frameGeometry when expanded is empty
                    // — a window with no shadow / decoration extents
                    // reports an empty expanded rect, and `addLayerRepaint`
                    // on an empty rect is a silent no-op that would stall
                    // the time-based shader's iTime advance.
                    QRect repaintRect = w->expandedGeometry().toAlignedRect();
                    if (repaintRect.isEmpty()) {
                        repaintRect = w->frameGeometry().toAlignedRect();
                    }
                    w->addLayerRepaint(repaintRect);
                }
            }
        }
    }
    KWin::effects->postPaintScreen();
}

void PlasmaZonesEffect::prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data,
                                       std::chrono::milliseconds presentTime)
{
    if (w && (m_windowAnimator->hasAnimation(w) || m_shaderTransitions.find(w) != m_shaderTransitions.end())) {
        // Mark as transformed so paintWindow applies our translate+scale, and
        // so the OffscreenEffect redirect drives full-window repaints for the
        // shader leg's iTime advance even when the underlying window content
        // hasn't changed (lifecycle-event shaders need this — without the
        // transformed flag, paintWindow only fires on actual window damage).
        data.setTransformed();
    }

    OffscreenEffect::prePaintWindow(view, w, data, presentTime);
}

void PlasmaZonesEffect::paintWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                    KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                    KWin::WindowPaintData& data)
{
    m_windowAnimator->applyTransform(w, data);

    auto sit = m_shaderTransitions.find(w);
    if (sit != m_shaderTransitions.end() && sit->second.cached && sit->second.cached->shader) {
        // Non-const reference because the per-frame book-keeping (`frameCount`,
        // `lastPaintTimeMs`) advances on every paintWindow tick that feeds
        // the transition. Without the mutation, `iFrame` would stay at 0 and
        // `iTimeDelta` would always read 0.
        auto& transition = sit->second;
        // Two progress sources, picked by the transition's mode (see
        // ShaderTransition's docstring). Lifecycle events (window.*)
        // started via tryBeginShaderForEvent set durationMs > 0 and drive
        // progress from monotonic steady-clock elapsed; zone.* events flowed
        // through applySnapGeometry leave durationMs = 0 and ride the
        // m_windowAnimator timeline so the shader matches the geometry
        // animation.
        qreal progress = 0.0;
        bool active = false;
        if (transition.durationMs > 0) {
            const qint64 now = shaderClockNowMs();
            const qint64 elapsed = now - transition.startTimeMs;
            if (elapsed >= 0 && elapsed <= transition.durationMs) {
                progress = qreal(elapsed) / qreal(transition.durationMs);
                active = true;
            }
        } else {
            const auto* anim = m_windowAnimator->animationFor(w);
            if (anim && anim->isAnimating()) {
                progress = qBound(0.0, anim->state().value, 1.0);
                active = true;
            }
        }
        // Flip the timeline for `going-away` events so a single user-
        // assigned shader covers both directions of a paired event:
        // window.open plays 0→1, window.close plays the same shader 1→0;
        // going-to-minimized plays 1→0 while unminimize plays 0→1; same
        // for maximize/unmaximize. Both progress sources are guaranteed
        // to be in [0, 1] above, so the flip is bound-preserving.
        if (transition.reverse) {
            progress = 1.0 - progress;
        }
        if (active) {
            const CachedShader* cached = transition.cached;
            KWin::GLShader* shader = cached->shader.get();

            // Animation-shader contract — see
            // `PhosphorAnimationShaders::AnimationShaderContract`. iTime
            // is 0..1 progress, iResolution is the surface size, and
            // per-effect declared parameters land in `customParams[N]`
            // slots populated at transition begin time by
            // `translateAnimationParams`. iTimeDelta / iFrame / iDate /
            // iMouse mirror the daemon's SurfaceAnimator semantics so a
            // single shader source observes equivalent state on either
            // runtime. Audio / multipass / texture uniforms are still
            // unpopulated on the kwin path — those need C++ wiring
            // (CAVA subscription, FBO chain, texture cache) that is out
            // of scope for this commit.
            //
            // setUniform must run with the shader bound: KWin's
            // `GLShader::setUniform` calls `glUniform*` directly, which
            // writes into whichever program is active. KWin's
            // `OffscreenEffect::drawWindow` only binds our shader later
            // inside `OffscreenData::paint`'s ShaderBinder, so without
            // this push the writes either hit GL_INVALID_OPERATION or
            // land on the prior effect's program. Uniform values are
            // stored in the program object, so push → set → pop leaves
            // them in place for OffscreenEffect's subsequent bind.
            //
            // -1 from a uniformLocation lookup means the linker dropped
            // the uniform (unreferenced in GLSL). GL silently ignores
            // glUniform on -1, but the explicit `loc < 0` guard at every
            // call site makes the intent ("only push uniforms the shader
            // actually declared") explicit and survives a future GL
            // backend that doesn't honour the -1-is-noop convention.
            // iTimeDelta + iFrame are book-keeping that must advance every
            // tick regardless of whether the shader declares the
            // uniforms — they're inputs to the per-leg state machine.
            const qint64 nowMs = shaderClockNowMs();
            const float iTimeDelta = (transition.lastPaintTimeMs < 0)
                ? 0.0f
                : static_cast<float>(nowMs - transition.lastPaintTimeMs) / 1000.0f;
            transition.lastPaintTimeMs = nowMs;
            // Pin the GLSL contract: `iFrame` is declared `uniform int`
            // in animation_uniforms.glsl; bumping iFrameValue's type to
            // unsigned without updating the shader (or vice versa) would
            // silently reinterpret bit patterns at the SRB boundary.
            static_assert(std::is_same_v<decltype(transition.frameCount), int>,
                          "transition.frameCount must stay `int` to match GLSL `uniform int iFrame;`");
            const int iFrameValue = transition.frameCount++;
            const QRectF geo = w->frameGeometry();
            {
                KWin::ShaderBinder binder(shader);
                if (cached->iTimeLoc >= 0) {
                    shader->setUniform(cached->iTimeLoc, static_cast<float>(progress));
                }
                if (cached->iResolutionLoc >= 0) {
                    shader->setUniform(cached->iResolutionLoc, QVector2D(geo.width(), geo.height()));
                }
                if (cached->iTimeDeltaLoc >= 0) {
                    shader->setUniform(cached->iTimeDeltaLoc, iTimeDelta);
                }
                if (cached->iFrameLoc >= 0) {
                    shader->setUniform(cached->iFrameLoc, iFrameValue);
                }
                if (cached->iDateLoc >= 0) {
                    // iDate: local-time (year, month, day, seconds-since-
                    // midnight). Hoisted behind the loc>=0 guard so a
                    // shader that doesn't read iDate doesn't pay the
                    // QDateTime + QDate + QTime build cost on every
                    // paint (a 144 Hz display × multiple in-flight
                    // transitions would otherwise pay it dozens of
                    // times per frame).
                    //
                    // iDate.w decomposes seconds-since-midnight from
                    // hour/minute/second/msec rather than dividing
                    // `msecsSinceStartOfDay()` by 1000 — at 12:00 the
                    // raw msec count is ~43.2M, and a single-precision
                    // float divide there only resolves to ~4 ms steps
                    // (vs the ~1 µs steps produced by the decomposed
                    // form). Matches `shadernoderhiuniforms.cpp:51-52`
                    // exactly so a shader that reads iDate.w sees the
                    // same value on both runtimes.
                    // 1Hz cache: re-decompose the QDateTime only when at
                    // least 1000 ms have elapsed since the last refresh
                    // (or this is the first paint to read iDate). Mirrors
                    // shadernoderhiuniforms.cpp:42-53 — sub-second iDate
                    // variation is invisible for typical shader use
                    // (clocks, time-of-day tints, etc.), and multiple
                    // in-flight transitions on a high-Hz display would
                    // otherwise pay the QDateTime / QDate / QTime build
                    // cost per transition per frame.
                    //
                    // Use shaderClockNowMs() (steady_clock) for the
                    // staleness gate even though the cached value itself
                    // is wall-clock-derived: a backwards NTP correction
                    // on the wall clock would push `nowMs` below
                    // `m_lastIDateRefreshMs`, the diff would go negative,
                    // and the cache would never refresh again until the
                    // wall clock caught back up. steady_clock guarantees
                    // monotonic increase, matching the rest of the
                    // shader-timing path. Reuses the outer-scope `nowMs`
                    // captured for iTimeDelta / iFrame above so all of
                    // this paint tick's monotonic readings come from the
                    // same clock sample.
                    if (nowMs - m_lastIDateRefreshMs >= 1000) {
                        const QDateTime nowDateTime = QDateTime::currentDateTime();
                        const QDate date = nowDateTime.date();
                        const QTime t = nowDateTime.time();
                        const float seconds = static_cast<float>(t.hour() * 3600 + t.minute() * 60 + t.second())
                            + static_cast<float>(t.msec()) / 1000.0f;
                        m_cachedIDate = QVector4D(static_cast<float>(date.year()), static_cast<float>(date.month()),
                                                  static_cast<float>(date.day()), seconds);
                        m_lastIDateRefreshMs = nowMs;
                    }
                    shader->setUniform(cached->iDateLoc, m_cachedIDate);
                }
                if (cached->iMouseLoc >= 0) {
                    // iMouse: cursor position in window-local logical
                    // pixels (.xy), with `(-1, -1)` sentinel when the
                    // cursor is outside the window's frame. .zw carry
                    // the same position normalised to surface size,
                    // computed unconditionally from the same xy values
                    // — matches the daemon's animation-shader iMouse
                    // semantics: the `(-1, -1)` sentinel is applied at
                    // the higher layer by `SurfaceAnimator` via its
                    // `QQuickHoverHandler` (see
                    // `surfaceanimator.cpp::seedShaderUniformsAtAttach`
                    // — when the handler reports `!isHovered()` the
                    // animator pushes `setIMouse(QPointF(-1, -1))`).
                    // The rendering layer's
                    // `ShaderNodeRhi::syncBaseUniforms` itself always
                    // writes the live position; the sentinel is
                    // applied at the higher layer. We synthesise the
                    // same sentinel here directly because there is no
                    // hover-handler equivalent in the KWin-effect
                    // pipeline. Hoisted behind the loc>=0 guard for
                    // the same reason as iDate above.
                    //
                    // Edge inclusivity: exclusive on right/bottom edges
                    // — matches `QRectF::contains` parity with the
                    // daemon's `ShaderNodeRhi::setMousePosition`. With
                    // inclusive edges, a cursor parked exactly on the
                    // boundary between two abutting outputs would
                    // register as inside both windows simultaneously,
                    // and the resulting iMouse uniform would diverge
                    // from the daemon's. Spell out the exclusive check
                    // so the sentinel synthesis matches QRectF.
                    // Cursor pos cached once per prePaintScreen — paintWindow
                    // is called per active transition (and per output), so
                    // re-reading KWin::effects->cursorPos() per call would
                    // multiply up across high-Hz multi-output setups.
                    const QPointF cursorGlobal = m_cachedCursorGlobal;
                    float localX = -1.0f;
                    float localY = -1.0f;
                    const bool inside = cursorGlobal.x() >= geo.x() && cursorGlobal.x() < geo.x() + geo.width()
                        && cursorGlobal.y() >= geo.y() && cursorGlobal.y() < geo.y() + geo.height();
                    if (inside) {
                        localX = static_cast<float>(cursorGlobal.x() - geo.x());
                        localY = static_cast<float>(cursorGlobal.y() - geo.y());
                    }
                    QVector4D iMouseValue(localX, localY, 0.0f, 0.0f);
                    if (geo.width() > 0.0)
                        iMouseValue.setZ(localX / static_cast<float>(geo.width()));
                    if (geo.height() > 0.0)
                        iMouseValue.setW(localY / static_cast<float>(geo.height()));
                    shader->setUniform(cached->iMouseLoc, iMouseValue);
                }
                if (cached->iIsReversedLoc >= 0) {
                    shader->setUniform(cached->iIsReversedLoc, transition.reverse ? 1 : 0);
                }
                for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
                    const int loc = cached->customParamsLoc[slot];
                    if (loc < 0)
                        continue; // shader didn't declare / reference this slot
                    shader->setUniform(loc, transition.customParamsValues[slot]);
                }
                for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
                    const int loc = cached->customColorsLoc[slot];
                    if (loc < 0)
                        continue;
                    shader->setUniform(loc, transition.customColorsValues[slot]);
                }
                // User textures: bind each cached GLTexture to texture
                // unit (1 + slot) — TEXTURE0 holds the redirected window
                // texture KWin's OffscreenData::paint binds during
                // drawWindow. Push the matching sampler uniform so the
                // shader knows which unit to read; populate
                // iTextureResolution[slot] so shaders that key on texture
                // size (e.g. tile-grid shaders like Matrix's glyph atlas)
                // can compute their own UV math without authors hard-
                // coding bitmap dimensions.
                //
                // Order matters: setUniform addresses program-object
                // state, glActiveTexture+bind addresses GL state. Both
                // need to be active when KWin's drawWindow issues its
                // glDraw* calls; ShaderBinder keeps the program bound,
                // and texture-unit binds outlive the program switch
                // OffscreenData::paint performs internally (program
                // switches don't reset texture-unit bindings).
                for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
                     ++slot) {
                    CachedTexture* entry = transition.userTextures[slot];
                    if (!entry || !entry->texture) {
                        continue;
                    }
                    KWin::GLTexture* tex = entry->texture.get();
                    if (cached->userTextureLoc[slot] >= 0) {
                        shader->setUniform(cached->userTextureLoc[slot], 1 + slot);
                    }
                    if (cached->iTextureResolutionLoc[slot] >= 0) {
                        const QSize sz = tex->size();
                        shader->setUniform(cached->iTextureResolutionLoc[slot],
                                           QVector4D(sz.width(), sz.height(), 0.0f, 0.0f));
                    }
                    glActiveTexture(GL_TEXTURE1 + slot);
                    tex->bind();
                    // Wrap mode lives on the cached `GLTexture`'s GL
                    // state — apply the per-leg value so two transitions
                    // sharing the same path can run with different wrap
                    // modes without invalidating each other. Skip the
                    // setWrapMode call when the cache entry's last-
                    // applied wrap matches the transition's target;
                    // setWrapMode issues two `glTexParameteri`s and
                    // would otherwise fire on every paintWindow tick.
                    const GLenum wantWrap = transition.userTextureWrap[slot];
                    if (entry->lastAppliedWrap != wantWrap) {
                        tex->setWrapMode(wantWrap);
                        entry->lastAppliedWrap = wantWrap;
                    }
                }
                // Restore TEXTURE0 as the active unit so KWin's
                // OffscreenData::paint binds the redirected surface
                // to the unit it expects (its `m_texture->bind()` runs
                // without a preceding glActiveTexture).
                glActiveTexture(GL_TEXTURE0);
            }
            OffscreenEffect::drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
            // Hygiene: unbind our user textures from TEXTURE1+. Each
            // effect in the chain assumes TEXTURE0 is the only active
            // unit; leaving stale binds risks the next effect inheriting
            // a sampler that points at our matrix glyph atlas (or
            // whatever) when it expects a default-empty unit.
            for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
                if (!transition.userTextures[slot]) {
                    continue;
                }
                glActiveTexture(GL_TEXTURE1 + slot);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            glActiveTexture(GL_TEXTURE0);
            return;
        }
        // Expiry fall-through: the transition is past its duration but
        // still installed. Tearing it down synchronously here would
        // unredirect the window mid-paint; the current frame would
        // then render UN-redirected and the user would see a
        // one-frame surface flash before the next frame stabilises.
        // Defer the teardown to a queued slot so the current paint
        // cycle still consumes the redirected (final-progress) state,
        // and the unredirect runs before the next paint. The pending-
        // set guard prevents the next frame (which lands before the
        // queued slot runs) from re-queuing a duplicate end and
        // double-tearing-down the same transition.
        if (!m_pendingShaderExpiryEnd.contains(w)) {
            m_pendingShaderExpiryEnd.insert(w);
            QPointer<KWin::EffectWindow> safeWindow(w);
            // Stash the raw pointer too — used as the set key for
            // membership cleanup. We MUST NOT remove the entry if the
            // QPointer has been cleared: the EffectWindow at that
            // address may have been destroyed by KWin and a fresh
            // window allocated at the same address before this queued
            // slot runs. The windowDeleted handler already calls
            // endShaderTransition for the dying window (which removes
            // the matching pending-expiry entry), so a null QPointer
            // means cleanup already happened — removing again would
            // wipe the new window's freshly-inserted entry. The raw
            // pointer is only used for set membership cleanup (never
            // dereferenced unless the QPointer is still live).
            KWin::EffectWindow* rawWindow = w;
            // Capture the EXPIRING transition's generation at queue-time
            // so the queued lambda can confirm the transition it sees on
            // dispatch is still the same one we observed as expired.
            // Race window: between this queue and the lambda firing, a
            // fresh `beginShaderTransition` may install a SUCCESSOR at
            // the same EffectWindow* (e.g. window.focus retriggers while
            // window.maximize was on its expiry frame). Without the
            // generation check the lambda would call
            // `endShaderTransition` on the successor and kill it before
            // it ever paints. Mirrors the timer-driven teardown pattern
            // in `tryBeginShaderForEvent` (~line 5744).
            // Iterator `sit` was obtained earlier in this function and no
            // intervening code mutates m_shaderTransitions, so the read is
            // safe. The assertion documents that contract for future edits.
            Q_ASSERT(sit != m_shaderTransitions.end());
            const quint64 expiringGeneration = sit->second.generation;
            QMetaObject::invokeMethod(
                this,
                [this, safeWindow, rawWindow, expiringGeneration]() {
                    if (!safeWindow) {
                        // Window died; windowDeleted already removed
                        // the entry. Don't risk wiping an entry that
                        // belongs to a successor sharing this address.
                        return;
                    }
                    // Remove unconditionally so the pending-set entry
                    // doesn't leak when the transition was already
                    // ended via a different path (synchronous teardown,
                    // windowDeleted, generation-mismatch successor).
                    m_pendingShaderExpiryEnd.remove(rawWindow);
                    auto liveIt = m_shaderTransitions.find(safeWindow.data());
                    if (liveIt != m_shaderTransitions.end() && liveIt->second.generation == expiringGeneration) {
                        endShaderTransition(safeWindow.data());
                    }
                    // else: a successor replaced us (last-event-wins) and
                    // owns its own teardown — leave it alone.
                },
                Qt::QueuedConnection);
        }
    }

    OffscreenEffect::drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Async texture pre-warm.
//
// Pattern: a `QRunnable` posted to `m_textureLoaderPool` performs the
// CPU-bound load (`loadUserTextureImage` — QImage decode for raster
// formats, QSvgRenderer rasterise for SVG/SVGZ) on a worker thread,
// producing a `QImage` in `Format_RGBA8888`. The worker then dispatches
// a queued slot back to `this` via `QMetaObject::invokeMethod(...,
// Qt::QueuedConnection)` so the GL upload (`KWin::GLTexture::upload`)
// runs on the GL-context thread (the compositor thread). The cache
// insert and in-flight set bookkeeping happen entirely on the
// compositor thread, so no locking is needed against
// `m_textureCache` or `m_textureLoadsInFlight`.
//
// Thread-safety notes:
//   • The worker only reads the captured path string (`m_path`) and
//     `m_svgMaxDim` — both POD captured-by-value at submission time.
//     It NEVER touches `m_textureCache` or `m_textureLoadsInFlight`;
//     all access to those members happens on the compositor thread,
//     either at submission time or inside the queued upload lambda.
//     The submission-time generation captured into the worker lets
//     the queued lambda detect a hot-reload that cleared the cache
//     and discard the upload before touching `m_textureLoadsInFlight`.
//   • `QSvgRenderer` is NOT thread-safe across instances (it owns
//     mutable rasteriser state during `render()` per Qt docs).
//     `loadUserTextureImage` constructs a fresh `QSvgRenderer` per
//     call, so each worker invocation gets its own renderer — safe.
//   • `QImage(path)` (PNG/JPG/etc. decode) is thread-safe across
//     instances per Qt docs.
//   • `KWin::GLTexture::upload` MUST run on the GL thread; that's
//     why the upload is dispatched back via QueuedConnection rather
//     than completed inline on the worker.
// ─────────────────────────────────────────────────────────────────────────────
void PlasmaZonesEffect::evictLruTextureIfOverBound()
{
    while (m_textureCache.size() > kTextureCacheSoftBound) {
        // Build the set of cache pointers currently referenced by any
        // active transition's userTextures slots. Eviction must skip
        // every one of these — the transition holds a raw non-owning
        // pointer that would dangle if we erased the entry.
        std::unordered_set<const CachedTexture*> inFlight;
        for (const auto& [_, transition] : m_shaderTransitions) {
            for (CachedTexture* tex : transition.userTextures) {
                if (tex) {
                    inFlight.insert(tex);
                }
            }
        }
        // Find the cache entry with the smallest lastAccessTick that is
        // NOT in-flight. If every entry is in flight (pathological;
        // would require >kTextureCacheSoftBound concurrent transitions
        // each referencing a unique texture), break — the cache
        // transiently exceeds the bound rather than tearing a live
        // pointer. Self-heals on the next eviction once a transition
        // ends.
        auto evictIt = m_textureCache.end();
        quint64 oldestTick = std::numeric_limits<quint64>::max();
        for (auto it = m_textureCache.begin(); it != m_textureCache.end(); ++it) {
            if (inFlight.count(&it->second) > 0) {
                continue;
            }
            if (it->second.lastAccessTick < oldestTick) {
                oldestTick = it->second.lastAccessTick;
                evictIt = it;
            }
        }
        if (evictIt == m_textureCache.end()) {
            return; // every entry is in flight; no safe eviction this pass
        }
        qCDebug(lcEffect) << "evictLruTextureIfOverBound: evicting" << evictIt->first
                          << "(lastAccessTick=" << evictIt->second.lastAccessTick
                          << ", cache size=" << m_textureCache.size() << ")";
        m_textureCache.erase(evictIt);
    }
}

void PlasmaZonesEffect::warmUserTextureAsync(const QString& absolutePath)
{
    if (absolutePath.isEmpty()) {
        return;
    }
    // Already warm — fast path, no allocation.
    if (m_textureCache.find(absolutePath) != m_textureCache.end()) {
        return;
    }
    // Already in flight — a worker is mid-load; deduplicate to avoid
    // duplicate GPU uploads when several transitions request the
    // same path before the first one completes.
    if (m_textureLoadsInFlight.contains(absolutePath)) {
        return;
    }
    m_textureLoadsInFlight.insert(absolutePath);

    // SVG default size matches `loadUserTextureImage`'s 1024 max-axis.
    // Captured by value into the worker. The cache is path-keyed; if
    // we ever need per-asset size variants the cache key must include
    // the rasterised dimension, otherwise two callers requesting the
    // same SVG at different sizes would race on whichever one wins.
    constexpr int svgMaxDim = 1024;

    // Capture the cache generation at submission time. The queued
    // upload lambda compares this against the live
    // `m_textureCacheGeneration` and discards if mismatched — i.e. a
    // hot-reload (`effectsChanged`) bumped the generation between
    // submission and upload, so this worker's bytes are stale and
    // must not re-populate the cleared cache.
    const quint64 submissionGeneration = m_textureCacheGeneration;

    class Loader : public QRunnable
    {
    public:
        Loader(QPointer<PlasmaZonesEffect> effect, QString path, int svgMaxDim, quint64 submissionGeneration)
            : m_effect(std::move(effect))
            , m_path(std::move(path))
            , m_svgMaxDim(svgMaxDim)
            , m_submissionGeneration(submissionGeneration)
        {
        }
        void run() override
        {
            QImage img = loadUserTextureImage(m_path, m_svgMaxDim);
            QPointer<PlasmaZonesEffect> effect = m_effect;
            QString path = m_path;
            const quint64 submissionGeneration = m_submissionGeneration;
            // Bounce back to the compositor thread for the GL upload.
            // The QPointer guards against the effect being destroyed
            // while the worker was running — destructor's
            // `m_textureLoaderPool.waitForDone()` already protects
            // against this for the in-process teardown case, but the
            // QPointer is defence-in-depth for any future caller that
            // schedules this without owning the pool's lifetime.
            QMetaObject::invokeMethod(
                effect.data(),
                [effect, path, img = std::move(img), submissionGeneration]() mutable {
                    if (!effect) {
                        return;
                    }
                    // Generation check FIRST — before touching
                    // `m_textureCache` or `m_textureLoadsInFlight`. If
                    // the cache was cleared underneath us by a hot-
                    // reload (`effectsChanged`) the in-flight set was
                    // already cleared too; touching it now would mean
                    // racing with state the lambda has no business
                    // mutating. Discard cleanly.
                    if (submissionGeneration != effect->m_textureCacheGeneration) {
                        qCDebug(lcEffect) << "warmUserTextureAsync: discarding stale upload for" << path
                                          << "(generation mismatch — cache cleared during load)";
                        return;
                    }
                    effect->m_textureLoadsInFlight.remove(path);
                    if (img.isNull()) {
                        qCWarning(lcEffect) << "warmUserTextureAsync: load failed for" << path;
                        return;
                    }
                    // Re-check the cache: another transition may have
                    // synchronously loaded this path while we were on
                    // the worker. Honour the existing entry; dropping
                    // ours avoids a redundant GPU upload.
                    if (effect->m_textureCache.find(path) != effect->m_textureCache.end()) {
                        return;
                    }
                    std::unique_ptr<KWin::GLTexture> gpuTex = KWin::GLTexture::upload(img);
                    if (!gpuTex) {
                        qCWarning(lcEffect) << "warmUserTextureAsync: GLTexture::upload failed for" << path;
                        return;
                    }
                    gpuTex->setFilter(GL_LINEAR);
                    gpuTex->setWrapMode(GL_CLAMP_TO_EDGE);
                    CachedTexture cachedTex;
                    cachedTex.texture = std::move(gpuTex);
                    cachedTex.lastAppliedWrap = GL_CLAMP_TO_EDGE;
                    cachedTex.lastAccessTick = ++effect->m_textureCacheAccessTick;
                    effect->m_textureCache.emplace(path, std::move(cachedTex));
                    effect->evictLruTextureIfOverBound();
                    qCDebug(lcEffect) << "warmUserTextureAsync: cached" << path;
                },
                Qt::QueuedConnection);
        }

    private:
        QPointer<PlasmaZonesEffect> m_effect;
        QString m_path;
        int m_svgMaxDim;
        quint64 m_submissionGeneration;
    };

    // Pass `this` as a QPointer so the conversion happens at the call
    // site (where `this` is known live), not inside the ctor body where
    // a freed `effect` mid-construction would silently degrade to a raw
    // pointer that never registers with QPointer's tracker.
    auto* loader = new Loader(QPointer<PlasmaZonesEffect>(this), absolutePath, svgMaxDim, submissionGeneration);
    loader->setAutoDelete(true);
    m_textureLoaderPool.start(loader);
}

void PlasmaZonesEffect::beginShaderTransition(KWin::EffectWindow* window,
                                              const PhosphorAnimationShaders::ShaderProfile& profile, int durationMs,
                                              bool reverse, bool holdCloseGrab)
{
    const QString effectId = profile.effectiveEffectId();
    if (effectId.isEmpty() || !window)
        return;

    // Global animations toggle. Mirrors the daemon's
    // `SurfaceAnimator::beginShow/beginHide` early-out when
    // `setEnabled(false)`. Gating here (rather than only in
    // `tryBeginShaderForEvent`) covers BOTH callsite categories
    // uniformly: window-lifecycle events that flow through
    // `tryBeginShaderForEvent`, and zone.* events that flow through
    // `applySnapGeometry → beginShaderTransition` directly. Without
    // this gate the zone.* path would still install shader transitions
    // even with global animations off.
    if (m_windowAnimator && !m_windowAnimator->isEnabled()) {
        return;
    }

    // OffscreenEffect's `redirect()` allocates an FBO sized to the
    // window's frame geometry. A window that's already minimised /
    // unmapped reports 0×0 (or 1×1) here, and FBO creation aborts
    // with `GL_INVALID_VALUE … <levels>, <width> and <height> must
    // be 1 or greater` followed by `GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT`
    // — the redirect silently leaves the window in a half-broken
    // state that contaminates every subsequent transition until KWin
    // itself reallocates the offscreen data. Skip the install on
    // collapsed surfaces. The minimize lifecycle event in particular
    // fires AFTER KWin has already pulled the surface, so its shader
    // assignment is intrinsically a no-op on this code path; users
    // who want a minimise animation need an unredirect-time hook,
    // which is out of scope here.
    const QRectF geo = window->frameGeometry();
    if (window->isMinimized() || geo.width() < 1.0 || geo.height() < 1.0) {
        qCDebug(lcEffect) << "beginShaderTransition: skipping collapsed surface" << effectId
                          << "window=" << window->windowClass() << "geo=" << geo
                          << "isMinimized=" << window->isMinimized();
        return;
    }

    auto eff = m_animationShaderRegistry.effect(effectId);
    if (!eff.isValid()) {
        qCWarning(lcEffect) << "beginShaderTransition: registry has no effect" << effectId
                            << "— registry effect count=" << m_animationShaderRegistry.availableEffects().size();
        return;
    }

    // KWin-specific default vertex stage. Hardcoded here rather than
    // loaded from `data/animations/shared/animation.vert` because that
    // file is shared with the daemon's RHI surface pipeline, which
    // requires all uniforms to live in UBOs (default-block uniforms
    // aren't supported under Qt-RHI/SPIR-V) and supplies vertex
    // positions already in clip space — exactly the opposite of what
    // KWin's classic-GL OffscreenEffect needs:
    //
    //   • Positions arrive in screen-pixel space (KWin's
    //     `OffscreenData::paint` writes window-rect pixel coords into
    //     the streaming buffer at `GLVertex2DLayout`'s position slot).
    //   • The pixel→NDC projection lives in the
    //     `modelViewProjectionMatrix` default-block uniform, which KWin
    //     sets via `Mat4Uniform::ModelViewProjectionMatrix` (mapped to
    //     uniform name `modelViewProjectionMatrix` per
    //     `KWin::GLShader::resolveLocations` in
    //     `<opengl/glshader.cpp>`). Skipping the multiplication leaves
    //     the redirected quad at coords like (1920, 1080) — entirely
    //     outside the [-1, 1] viewport — and the transition shader
    //     runs but paints to nothing. Manifest: `tryBeginShader`
    //     resolves, `beginShaderTransition` installs cleanly, no
    //     compile warnings, but the user sees no visible animation.
    //   • Attribute slot indices match `KWin::VA_Position` (0) and
    //     `KWin::VA_TexCoord` (1) per `<opengl/glvertexbuffer.h>`;
    //     explicit `layout(location = N)` decorations bypass KWin's
    //     `bindAttributeLocation("position", ...)` lookup (which is
    //     name-only and would mismatch our `texCoord` vs KWin's
    //     `texcoord`).
    //
    // Authors that ship a per-shader vertex stage via metadata's
    // `vertexShader` field still flow through the file-load path
    // below. They opt out of this default and own the matrix /
    // attribute contract themselves — INCLUDING the Y-flip described
    // immediately below. A custom vertex stage that emits
    // `vTexCoord = texCoord` (the obvious-looking pass-through) will
    // render upside-down on the kwin path, because KWin's
    // `OffscreenData::paint` populates texCoord with Y-up FBO sampling
    // coordinates while animation shaders are authored against the
    // daemon's Y=0-at-top convention. See the canonical GLSL header
    // (`data/animations/shared/animation_uniforms.glsl`) for the full
    // contract; the rule of thumb is "if you supply your own vertex
    // shader for kwin, replicate the `1.0 - texCoord.y` flip".
    // Y-flip the texCoord on the way out so animation shader math sees
    // the same Y=0-at-top convention the daemon's Qt-RHI pipeline
    // delivers. KWin's `OffscreenData::paint` populates the texCoord
    // attribute with OpenGL FBO sampling coordinates (Y-up: Y=0 maps
    // to the bottom row of the FBO; Y=1 to the top). Our animation
    // shaders are authored against the daemon's Y=0-at-top convention
    // (matrix's rain falls top-to-bottom via `fragCoord.y` math;
    // dissolve's noise sweep, slidefade's leading edge, etc. all read
    // vTexCoord.y as "0 at the top of the surface"). Without this
    // flip the y-axis math runs upside-down on KWin and the rain
    // appears to fall from the bottom up.
    //
    // The flip ALSO corrects the texture sample: with KWin's Y-up
    // FBO storage, the window's top row is stored at FBO-Y=1.
    // Sampling `uTexture0(unflipped vTexCoord)` at vTexCoord.y=0
    // would return the bottom of the FBO (= bottom of the window).
    // After the flip, `texture(uTexture0, vTexCoord)` at vTexCoord.y=0
    // samples FBO-Y=1, which is the top of the window — matching the
    // daemon's behaviour.
    static const QByteArray kKwinDefaultVertexSource = QByteArrayLiteral(
        "#version 450\n"
        "\n"
        "layout(location = 0) in vec2 position;\n"
        "layout(location = 1) in vec2 texCoord;\n"
        "\n"
        "layout(location = 0) out vec2 vTexCoord;\n"
        "\n"
        "uniform mat4 modelViewProjectionMatrix;\n"
        "\n"
        "void main() {\n"
        "    vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);\n"
        "    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);\n"
        "}\n");

    auto cacheIt = m_shaderCache.find(effectId);
    if (cacheIt == m_shaderCache.end()) {
        // Diagnostic-once-per-compile: log multipass degradation when the
        // shader is first compiled for this session, not on every transition
        // install. Lifecycle events (window.move on a drag, window.focus on
        // alt-tab) can fire beginShaderTransition many times in quick
        // succession against an already-cached effect; a per-install log
        // would flood the journal. Cache invalidation (effectsChanged →
        // m_shaderCache.clear) re-fires the log at the next install, which
        // is the right semantic for hot-reload.
        if (eff.isMultipass) {
            qCInfo(lcEffect) << "Animation effect" << effectId
                             << "is multipass — compositor path runs single-pass only (buffer passes skipped)";
        }

        QFile shaderFile(eff.fragmentShaderPath);
        if (!shaderFile.open(QIODevice::ReadOnly)) {
            qCWarning(lcEffect) << "Failed to open shader file" << eff.fragmentShaderPath;
            return;
        }
        const QString rawSource = QString::fromUtf8(shaderFile.readAll());
        if (rawSource.isEmpty()) {
            qCWarning(lcEffect) << "Shader file is empty" << eff.fragmentShaderPath;
            return;
        }
        QStringList animIncludePaths;
        for (const QString& sp : m_animationShaderRegistry.searchPaths()) {
            const QString sharedDir = sp + QStringLiteral("/shared");
            if (QDir(sharedDir).exists()) {
                animIncludePaths.append(sharedDir);
            }
        }
        QString includeError;
        const QString currentDir = QFileInfo(eff.fragmentShaderPath).absolutePath();
        const QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
            rawSource, currentDir, animIncludePaths, &includeError);
        if (expanded.isEmpty()) {
            qCWarning(lcEffect) << "Failed to expand shader includes for" << effectId << ":" << includeError;
            return;
        }

        // Selects the default-block branch in `animation_uniforms.glsl`.
        // KWin's `KWin::GLShader` API addresses default-block uniforms only
        // (no UBO bind path), so the canonical header's `#ifdef
        // PLASMAZONES_KWIN` branch emits plain `uniform float iTime;`-style
        // declarations instead of the daemon's `layout(std140, binding=0)
        // uniform AnimationUniforms { ... };`. The macro must land AFTER the
        // shader's `#version` line — the GLSL spec disallows tokens before
        // `#version` other than whitespace and comments — so the helper
        // below splices it between the version directive and the rest of
        // the source.
        const QByteArray fragWithKwinDefine = injectKwinDefineAfterVersion(expanded);

        QByteArray vertWithKwinDefine = kKwinDefaultVertexSource;
        if (!eff.vertexShaderPath.isEmpty()) {
            QFile vertFile(eff.vertexShaderPath);
            if (!vertFile.open(QIODevice::ReadOnly)) {
                qCWarning(lcEffect) << "Failed to open vertex shader" << eff.vertexShaderPath << "for effect"
                                    << effectId << "— falling back to KWin default vertex stage";
            } else {
                const QString rawVert = QString::fromUtf8(vertFile.readAll());
                if (rawVert.isEmpty()) {
                    qCWarning(lcEffect) << "Vertex shader file is empty" << eff.vertexShaderPath << "for effect"
                                        << effectId << "— falling back to KWin default vertex stage";
                } else {
                    const QString vertDir = QFileInfo(eff.vertexShaderPath).absolutePath();
                    QString vertIncErr;
                    const QString expandedVert = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
                        rawVert, vertDir, animIncludePaths, &vertIncErr);
                    if (expandedVert.isEmpty()) {
                        qCWarning(lcEffect) << "Failed to expand vertex shader includes for" << effectId << ":"
                                            << vertIncErr << "— falling back to KWin default vertex stage";
                    } else {
                        vertWithKwinDefine = injectKwinDefineAfterVersion(expandedVert);
                    }
                }
            }
        }

        auto shader = KWin::ShaderManager::instance()->generateCustomShader(KWin::ShaderTrait::MapTexture,
                                                                            vertWithKwinDefine, fragWithKwinDefine);
        if (!shader || !shader->isValid()) {
            qCWarning(lcEffect) << "Failed to compile shader transition" << effectId;
            return;
        }

        CachedShader cached;
        // Animation-shader contract — names sourced from
        // `PhosphorAnimationShaders::AnimationShaderContract`. Both the
        // daemon overlay-surface execution site and this compositor
        // window-content execution site resolve the same names.
        cached.iTimeLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kITime);
        cached.iResolutionLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIResolution);
        cached.iTimeDeltaLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kITimeDelta);
        cached.iFrameLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIFrame);
        cached.iDateLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIDate);
        cached.iMouseLoc = shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIMouse);
        cached.iIsReversedLoc =
            shader->uniformLocation(PhosphorAnimationShaders::AnimationShaderContract::kIIsReversed);
        // Cache element locations for the per-effect declared parameter
        // slots: `customParams[0..kMaxCustomParams-1]` for float / int /
        // bool params, and `customColors[0..kMaxCustomColors-1]` for color
        // params. Each declared parameter lands in one of these slots —
        // see `AnimationShaderRegistry::translateAnimationParams` for
        // the exact mapping. `glGetUniformLocation` returns -1 for slots
        // the shader didn't reference (e.g. a one-param effect that the
        // GLSL compiler optimises away the unused tail of either array);
        // the per-frame push loop in paintWindow guards against -1 to
        // skip the setUniform call.
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
            // Pre-baked element-name table — no per-slot QByteArray
            // alloc. Sized + static_asserted against the contract budget
            // at the namespace-level definition.
            cached.customParamsLoc[slot] = shader->uniformLocation(kCustomParamsElementNames[slot]);
        }
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
            cached.customColorsLoc[slot] = shader->uniformLocation(kCustomColorsElementNames[slot]);
        }
        // User textures: resolve sampler + iTextureResolution[N] uniform
        // locations only. The actual texture upload happens per-leg
        // inside `beginShaderTransition`'s body below — keyed by the
        // resolved path in `m_textureCache` so two legs with different
        // override paths don't collide on the per-effect cache.
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
            // GLSL sampler name: uTexture1..3 (slot+1 because uTexture0 is
            // the redirected surface, not user-declared). Matches the
            // overlay shader convention in data/shaders/shared/textures.glsl.
            // Pre-baked from the file-scope `kUserTextureSamplerNames` /
            // `kITextureResolutionKeys` arrays — no per-slot QByteArray
            // alloc per shader install.
            cached.userTextureLoc[slot] = shader->uniformLocation(kUserTextureSamplerNames[slot]);
            cached.iTextureResolutionLoc[slot] = shader->uniformLocation(kITextureResolutionKeys[slot]);
        }
        cached.shader = std::move(shader);
        cacheIt = m_shaderCache.emplace(effectId, std::move(cached)).first;
    }

    // Detect supersession before the teardown so we can skip the
    // redundant unredirect+redirect cycle. KWin's offscreen-effect
    // pipeline reallocates the offscreen render target on every
    // unredirect→redirect, and a back-to-back supersession (e.g. an
    // autotile-reorder drag firing window.move at 60 Hz) would
    // otherwise pay that cost every frame.
    const auto existingIt = m_shaderTransitions.find(window);
    const bool isSameWindowSupersession = existingIt != m_shaderTransitions.end();
    // Carry the prior transition's closeGrabHeld through supersession so
    // ref/unref stay balanced. If the prior transition refWindow'd the
    // closing window, the ref must stay held (the new transition takes
    // ownership of the release). Without this, erasing the prior entry
    // would lose track of the ref and leak the EffectWindow forever.
    // Symmetric: if neither prior nor new transition holds the grab, no
    // ref work happens — supersession of two non-close transitions is a
    // no-op for ref accounting.
    const bool existingHeldGrab = isSameWindowSupersession ? existingIt->second.closeGrabHeld : false;
    if (isSameWindowSupersession) {
        // Erase the prior bookkeeping but skip the unredirect — we're
        // about to re-shader this same window. setShader() below
        // overwrites the shader pointer; no need to null it first.
        m_shaderTransitions.erase(existingIt);
    }
    // else: window is not currently shaderized; falls through to the
    // redirect() call below (no-op endShaderTransition since the map
    // doesn't have the entry).

    const auto& cachedEntry = cacheIt->second;
    ShaderTransition transition;
    transition.cached = &cachedEntry;

    // Translate the friendly parameter map (e.g. {"direction": 1,
    // "parallax": 0.2}) to slot keys, then pack each
    // `customParams<N>_<x|y|z|w>` set into a vec4 we can blast in one
    // setUniform call per slot. Translation honours the metadata
    // declaration order — same allocation the daemon's
    // SurfaceAnimator::runLeg path uses, so a single ShaderProfile
    // produces identical visuals on either runtime.
    const QVariantMap translated =
        PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(eff, profile.effectiveParameters());
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
        auto pull = [&](char component) -> float {
            const QString key = PhosphorAnimationShaders::AnimationShaderContract::slotKey(slot, component);
            const auto it = translated.constFind(key);
            if (it == translated.constEnd())
                return 0.0f;
            bool ok = false;
            const float v = it->toFloat(&ok);
            return ok ? v : 0.0f;
        };
        transition.customParamsValues[slot] = QVector4D(pull('x'), pull('y'), pull('z'), pull('w'));
    }
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
        const QString key = PhosphorAnimationShaders::AnimationShaderContract::colorKey(slot);
        const auto it = translated.constFind(key);
        if (it == translated.constEnd()) {
            continue;
        }
        // Registry-side `translateAnimationParams` coerces every color
        // to a valid QColor — unparseable inputs fall through to the
        // declared default and finally to `Qt::transparent`, which
        // `isValid()` reports as true. So under the documented contract
        // this guard never fires. It exists purely as defence-in-depth
        // against a future caller that bypasses the registry encoder
        // (e.g. injects a raw QString into a profile's
        // effectiveParameters() pass-through) — `redF/greenF/blueF/alphaF`
        // on an invalid QColor are undefined per Qt docs. Falling through
        // to the default-init (0,0,0,0) keeps the slot at transparent
        // black, matching the registry's documented Qt::transparent
        // fallback.
        const QColor c = it->value<QColor>();
        if (!c.isValid()) {
            continue;
        }
        transition.customColorsValues[slot] = QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF());
    }
    // User textures: resolve per-leg paths from translated params.
    // translateAnimationParams enriches the map with `uTextureN` /
    // `uTextureN_wrap` keys (pack defaults from `eff.textures` merged
    // with any `friendlyParams` runtime overrides), with relative paths
    // already resolved against `eff.sourceDir`. We look each path up in
    // `m_textureCache` (keyed by absolute path so two effects sharing
    // the same texture file share one upload) and stash a non-owning
    // pointer in the transition. Wrap mode is stored per-transition so
    // two legs sharing a path can run with different wrap modes
    // without invalidating each other's cache entry.
    //
    // Pre-warm: kick an async load for every declared texture path
    // BEFORE the synchronous fallback loop below. On the very first
    // transition for a given path the cache is cold and the
    // synchronous loader still runs (so the first frame is correct);
    // every subsequent transition for that path either hits the warm
    // cache (worker completed) or hits the in-flight dedupe (worker
    // still running, synchronous loader picks up the slack one more
    // time). The warmup itself is cheap — early-out on cache hit and
    // on in-flight membership — so a second pass over the same path
    // costs only a hash lookup.
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
        // Path key shares the GLSL sampler name (`uTexture<N>`) — see
        // the metadata enrichment in `translateAnimationParams`.
        const QString path = translated.value(QLatin1String(kUserTextureSamplerNames[slot])).toString();
        if (!path.isEmpty()) {
            warmUserTextureAsync(path);
        }
    }
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
        const int glslSlot = slot + 1; // uTexture0 is the surface; only used for the freshly-loaded log
        const QString path = translated.value(QLatin1String(kUserTextureSamplerNames[slot])).toString();
        const QString wrap = translated.value(QLatin1String(kUserTextureWrapKeys[slot])).toString();
        transition.userTextureWrap[slot] = wrapStringToEnum(wrap);
        if (path.isEmpty()) {
            transition.userTextures[slot] = nullptr;
            continue;
        }
        auto texIt = m_textureCache.find(path);
        bool freshlyLoaded = false;
        if (texIt != m_textureCache.end()) {
            // Bump the access tick on lookup so the LRU sweep sees this
            // path as "fresh" — keeps frequently-used textures warm
            // even if a flood of unique single-use textures pushes the
            // cache over its bound.
            texIt->second.lastAccessTick = ++m_textureCacheAccessTick;
        }
        if (texIt == m_textureCache.end()) {
            // Synchronous fallback — the warm path didn't promote in
            // time (or this is the very first transition for this
            // path). Subsequent transitions for the same path will
            // hit the cache and pay zero load cost.
            //
            // This load runs on the compositor thread (PNG decode +
            // GLTexture::upload, or QSvgRenderer rasterise + upload).
            // Surface the cost in the journal so cold-install stutter
            // is attributable rather than mysterious. Don't skip the
            // load — the first transition still needs a rendered
            // texture or the slot would sample transparent black.
            qCInfo(lcEffect) << "synchronous texture load on compositor thread:" << path
                             << "(cache miss; first transition for this effect)";
            const QImage img = loadUserTextureImage(path);
            if (img.isNull()) {
                qCWarning(lcEffect) << "User texture failed to load:" << path << "for effect" << effectId << "slot"
                                    << slot;
                transition.userTextures[slot] = nullptr;
                continue;
            }
            std::unique_ptr<KWin::GLTexture> gpuTex = KWin::GLTexture::upload(img);
            if (!gpuTex) {
                qCWarning(lcEffect) << "GLTexture::upload failed:" << path << "for effect" << effectId;
                transition.userTextures[slot] = nullptr;
                continue;
            }
            gpuTex->setFilter(GL_LINEAR);
            // Force the GL state and the tracker into the same starting
            // condition. A freshly-uploaded GLTexture has GL_REPEAT on
            // GL_TEXTURE_WRAP_S/T per GL spec; without an explicit
            // setWrapMode here the tracker would default to
            // GL_CLAMP_TO_EDGE and the first bind requesting clamp
            // would skip the setWrapMode call (tracker comparison
            // matches), leaving the texture at REPEAT — silently wrong
            // sampling on the first frame.
            gpuTex->setWrapMode(GL_CLAMP_TO_EDGE);
            CachedTexture cachedTex;
            cachedTex.texture = std::move(gpuTex);
            cachedTex.lastAppliedWrap = GL_CLAMP_TO_EDGE;
            cachedTex.lastAccessTick = ++m_textureCacheAccessTick;
            texIt = m_textureCache.emplace(path, std::move(cachedTex)).first;
            // Eviction sweep is safe here: std::map only invalidates the
            // erased iterator, so `texIt` (the entry we just inserted)
            // stays valid. The freshly-inserted entry's lastAccessTick
            // is the global maximum, so it can never be the eviction
            // victim.
            evictLruTextureIfOverBound();
            freshlyLoaded = true;
        }
        transition.userTextures[slot] = &texIt->second;
        // One-shot diagnostic per (effectId, slot, path) tuple — fires
        // on first upload only, so a leg that re-uses an already-cached
        // texture stays silent. Lets a journal scan answer "did matrix's
        // glyph atlas actually load on the kwin path?" without per-paint
        // spam.
        if (freshlyLoaded) {
            const QSize sz = texIt->second.texture->size();
            qCInfo(lcEffect) << "User texture loaded:" << path << "size=" << sz << "for effect" << effectId << "slot"
                             << slot << "(uTexture" << glslSlot << ")";
        }
    }
    // Bump generation for every install so the timer-driven teardown in
    // tryBeginShaderForEvent can detect supersession (a fresh transition
    // installed before the prior timer fires) and bail without killing the
    // successor. Counter is monotonic per-process; 64-bit so practically
    // unbounded.
    transition.generation = ++m_shaderTransitionGenerationCounter;
    transition.reverse = reverse;
    // Stamp the close-grab flag so endShaderTransition knows to release
    // refWindow + WindowClosedGrabRole on teardown. The new transition
    // inherits the prior transition's grab if supersession was a close-
    // on-close case (so the ref isn't double-incremented or lost). If
    // EITHER the prior or new install wants the grab, we treat it as
    // held — the new transition's endShaderTransition will balance.
    transition.closeGrabHeld = holdCloseGrab || existingHeldGrab;
    if (durationMs > 0) {
        transition.durationMs = durationMs;
        transition.startTimeMs = shaderClockNowMs();
    }

    // Claim the closing window for our shader animation. Done HERE — after
    // every early-return path (effectId empty, collapsed surface, registry
    // miss, shader compile fail, supersession dedup) has been cleared and
    // we're committed to installing the transition. Setting the grab
    // earlier would leak it on any of those skip paths, leaving the window
    // stranded in closing state with no transition to release it.
    //
    // Without WindowClosedGrabRole, KWin's normal teardown destroys the
    // closing window as soon as `slotWindowClosed` returns — OffscreenEffect's
    // `redirect` is auto-released on deletion (per the docstring at
    // /usr/include/kwin/effect/offscreeneffect.h:53), so paintWindow never
    // gets a frame to run the close shader on. Setting the grab here,
    // while the window is still in the closing-but-not-yet-deleted window
    // of validity, blocks final destruction until endShaderTransition
    // releases it. The data role's value is the Effect's `this` pointer
    // per KWin convention so other effects can detect the grab.
    // refWindow() is the actual lifeline — KWin's docs at
    // effecthandler.h:835 explicitly say "An effect which wants to
    // animate the window closing should connect to this signal and
    // reference the window by using refWindow". Without it, the
    // EffectWindow is destroyed as soon as slotWindowClosed returns
    // regardless of WindowClosedGrabRole — the grab role only tells
    // OTHER effects to skip the window, it does NOT keep the window
    // alive. paintWindow needs both the ref (for the EffectWindow* to
    // remain valid across paint cycles) and the redirect (for the
    // offscreen FBO snapshot).
    //
    // WindowClosedGrabRole is set in addition so KWin's built-in close
    // animations (fade, glide, etc.) skip this window — their
    // isFadeWindow check tests `effect.isGrabbed(w, WindowClosedGrabRole)`
    // (see /usr/share/kwin-wayland/effects/fade/contents/code/main.js)
    // and bails when grabbed. Without the grab, the built-in fade
    // would race our shader.
    //
    // Only ref/grab when the caller is asking for the grab AND the prior
    // transition didn't already hold one — otherwise we'd double-
    // increment the refcount and leak the EffectWindow on the single
    // unrefWindow in endShaderTransition.
    if (holdCloseGrab && !existingHeldGrab) {
        window->refWindow();
        window->setData(KWin::WindowClosedGrabRole, QVariant::fromValue(static_cast<void*>(this)));
    }

    // Emplace the transition entry FIRST, before redirect/setShader. If
    // either of those throws — or if we hit a later failure path — we
    // need a transition entry to tear down so the window doesn't end up
    // redirected with a shader installed but no bookkeeping. RAII guard
    // erases the entry if we don't successfully reach the bottom of the
    // function (either of the two op paths below threw).
    auto emplaceResult = m_shaderTransitions.emplace(window, std::move(transition));
    bool emplaceCommitted = false;
    auto emplaceGuard = qScopeGuard([&]() {
        if (emplaceCommitted) {
            return;
        }
        // The supersession path at line ~5509 erased the prior
        // transition entry directly (no endShaderTransition call →
        // no grab release), and the new transition inherited that
        // grab via `transition.closeGrabHeld = holdCloseGrab ||
        // existingHeldGrab`. If redirect()/setShader() throws after
        // the emplace, simply erasing the new entry would leak the
        // inherited (or freshly-acquired) close grab and strand the
        // window in closing state with no release path. Mirror
        // endShaderTransition's grab-release sequence here so the
        // ref + role clear stay balanced on the rollback path.
        //
        // Read the held flag from the emplaced entry — the local
        // `transition` was moved-from into the map and is no longer
        // safe to inspect. The entry is guaranteed to be present
        // (emplaceResult.second is true on the new-key path; the
        // map contains no prior entry for this window because the
        // supersession branch above erased it).
        const bool releaseCloseGrab = emplaceResult.first->second.closeGrabHeld;
        if (releaseCloseGrab && window) {
            // Clear WindowClosedGrabRole synchronously while the
            // ref we hold guarantees `window` is still alive. The
            // role clear is a courtesy for other effects.
            window->setData(KWin::WindowClosedGrabRole, QVariant());
            // Defer unrefWindow to the next event-loop iteration —
            // matches endShaderTransition's reasoning at line ~5806.
            // beginShaderTransition is reachable from paintWindow
            // via tryBeginShaderForEvent → animator callbacks, and
            // a synchronous unref here could destroy the
            // EffectWindow while a paint cycle still holds it.
            QPointer<PlasmaZonesEffect> selfGuard(this);
            KWin::EffectWindow* heldWindow = window;
            QMetaObject::invokeMethod(
                this,
                [selfGuard, heldWindow]() {
                    if (!selfGuard) {
                        return;
                    }
                    heldWindow->unrefWindow();
                },
                Qt::QueuedConnection);
        }
        m_shaderTransitions.erase(emplaceResult.first);
    });

    if (!isSameWindowSupersession) {
        redirect(window);
    }
    // setShader is unconditional — it replaces any prior shader pointer
    // (idempotent for the same shader, so even a same-effect
    // supersession is correct here).
    setShader(window, cachedEntry.shader.get());
    emplaceCommitted = true;

    // Kick the compositor into painting now so paintWindow fires and
    // the transition's iTime starts advancing. Without this, a shader
    // installed on a stable window (e.g. window.focus on a window with
    // no in-flight damage) would sit in m_shaderTransitions for its
    // full duration without ever reaching paintWindow. Interactive
    // events (window.move) don't need this because the drag is its own
    // continuous repaint source. postPaintScreen drives subsequent
    // frames via per-window expanded-geometry layer repaints.
    //
    // Fall back to frameGeometry when expanded is empty — a window
    // with no shadow / decoration extents reports an empty expanded
    // rect, and `addLayerRepaint` on an empty rect is a silent no-op
    // that would deny the transition its first paint.
    QRect repaintRect = window->expandedGeometry().toAlignedRect();
    if (repaintRect.isEmpty()) {
        repaintRect = window->frameGeometry().toAlignedRect();
    }
    window->addLayerRepaint(repaintRect);
    if (KWin::effects) {
        // Match the null-guard the constructor and destructor use for
        // KWin::effects access — this method is callable from public
        // entry points (animator-completion callback, programmatic
        // shader installs from the future plugin API), and a future
        // caller during compositor teardown could land here with
        // KWin::effects null.
        KWin::effects->addRepaintFull();
    }
}

void PlasmaZonesEffect::endShaderTransition(KWin::EffectWindow* window)
{
    if (!window)
        return;
    // Drop the expiry-pending guard regardless of whether the
    // transition still exists. If a synchronous teardown beat the
    // queued slot to the punch, the queued slot must not see this
    // window flagged as still-pending or it would skip a future
    // expiry's re-queue.
    m_pendingShaderExpiryEnd.remove(window);
    auto it = m_shaderTransitions.find(window);
    if (it == m_shaderTransitions.end()) {
        return;
    }
    const bool releaseCloseGrab = it->second.closeGrabHeld;
    // Guard against teardown on a window that's already been destroyed
    // (windowDeleted may have raced our timer). setShader / unredirect on a
    // deleted EffectWindow is undefined behaviour in KWin's offscreen-effect
    // pipeline; just drop our bookkeeping. The windowDeleted handler at the
    // KWin::effects connection erases m_shaderTransitions for the same
    // window, so this is a defence-in-depth against ordering races.
    if (!window->isDeleted()) {
        setShader(window, nullptr);
        unredirect(window);
    }
    m_shaderTransitions.erase(it);
    if (releaseCloseGrab) {
        // Clear WindowClosedGrabRole while `window` is still alive
        // (the ref we hold via refWindow() guarantees refcount >= 1
        // here). The role clear is a courtesy for other effects;
        // doing it now avoids touching `window` after the deferred
        // unref below.
        window->setData(KWin::WindowClosedGrabRole, QVariant());
        // Defer unrefWindow to the next event-loop iteration. This is
        // CRITICAL because endShaderTransition is reachable from
        // paintWindow's expired-transition fall-through (line ~4782),
        // and a synchronous unrefWindow there would destroy the
        // EffectWindow while paintWindow still holds it as `w`. The
        // caller would then deref a freed pointer when it falls
        // through to OffscreenEffect::drawWindow — exactly the crash
        // backtrace observed: paintWindow → drawWindow → finalDrawWindow
        // → windowItem() on a destroyed EffectWindow. By queueing the
        // unref through the event loop, KWin's destruction happens
        // AFTER the current paint cycle has finished using `w`.
        //
        // The QPointer guards the queued lambda against `this`
        // destruction across the queue boundary; the raw `window*`
        // capture is fine because the ref we hold ensures the
        // EffectWindow stays alive until the lambda runs (the lambda
        // is what releases it).
        QPointer<PlasmaZonesEffect> selfGuard(this);
        KWin::EffectWindow* heldWindow = window;
        QMetaObject::invokeMethod(
            this,
            [selfGuard, heldWindow]() {
                if (!selfGuard) {
                    return;
                }
                heldWindow->unrefWindow();
            },
            Qt::QueuedConnection);
    }
}

void PlasmaZonesEffect::tryBeginShaderForEvent(KWin::EffectWindow* window, const QString& profilePath, int durationMs,
                                               bool reverse, bool holdCloseGrab)
{
    if (!window || durationMs <= 0) {
        return;
    }
    // Fast-path early-out on the global animations toggle. The
    // authoritative gate also lives in `beginShaderTransition` (so
    // zone.* callers via `applySnapGeometry` are gated too), but
    // dispatching there would still pay the shader-tree resolve cost
    // — this skips it entirely when the global toggle is off.
    if (m_windowAnimator && !m_windowAnimator->isEnabled()) {
        return;
    }
    const auto profile = m_shaderProfileTree.resolve(profilePath);
    if (profile.effectiveEffectId().isEmpty()) {
        // Default-state path: a fresh user with no shader overrides
        // anywhere in the tree resolves every event to empty effectId,
        // which is correct ("no shader assigned"). Logging at WARNING
        // for that floods the journal with bogus failures every time a
        // window opens, closes, or moves. Only WARN when the tree has
        // overrides (so an empty resolve here is genuinely surprising —
        // the documented prune / D-Bus-race scenarios), otherwise
        // demote to DEBUG.
        if (m_shaderProfileTree.overriddenPaths().isEmpty()) {
            qCDebug(lcEffect) << "tryBeginShader[" << profilePath
                              << "]: no shader assigned (tree empty — default state)";
        } else {
            qCWarning(lcEffect) << "tryBeginShader[" << profilePath
                                << "]: no shader assigned (tree-resolve returned empty effectId, tree size="
                                << m_shaderProfileTree.overriddenPaths().size() << ")";
        }
        return;
    }
    beginShaderTransition(window, profile, durationMs, reverse, holdCloseGrab);
    // Capture the just-installed transition's generation so the deferred
    // teardown bails if a successor has replaced us by the time the timer
    // fires. Without this, two events overlapping on the same window
    // (window.move during zone.snapIn, window.focus interrupting
    // window.maximize) leave a stale timer that tears down the SUCCESSOR
    // when its own timer hasn't fired yet.
    auto it = m_shaderTransitions.find(window);
    if (it == m_shaderTransitions.end()) {
        return; // beginShaderTransition no-op'd (compile fail / invalid id)
    }
    const quint64 myGeneration = it->second.generation;
    QPointer<KWin::EffectWindow> safeWindow(window);
    QTimer::singleShot(durationMs, this, [this, safeWindow, myGeneration]() {
        // Two-tier guard: QPointer catches QObject destruction,
        // endShaderTransition's isDeleted() catches KWin's deletion-animation phase
        if (!safeWindow) {
            return;
        }
        auto it = m_shaderTransitions.find(safeWindow);
        if (it != m_shaderTransitions.end() && it->second.generation == myGeneration) {
            endShaderTransition(safeWindow);
        }
        // else: a newer transition replaced us (last-event-wins) and owns
        // its own timer — leave it alone.
    });
}

void PlasmaZonesEffect::loadShaderProfileFromDbus()
{
    loadSettingAsync(QStringLiteral("shaderProfileTree"), [this](const QVariant& v) {
        const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (doc.isObject()) {
            m_shaderProfileTree = PhosphorAnimationShaders::ShaderProfileTree::fromJson(doc.object());
            qCDebug(lcEffect) << "loadShaderProfileFromDbus: tree loaded with"
                              << m_shaderProfileTree.overriddenPaths().size()
                              << "overrides — paths=" << m_shaderProfileTree.overriddenPaths();
        } else {
            qCWarning(lcEffect) << "Failed to parse shaderProfileTree from D-Bus — not a JSON object";
        }
    });
}

void PlasmaZonesEffect::loadShaderRegistryFromDbus()
{
    loadSettingAsync(QStringLiteral("animationShaderSearchPaths"), [this](const QVariant& v) {
        const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (!doc.isArray())
            return;
        QStringList paths;
        for (const auto& entry : doc.array()) {
            if (entry.isString())
                paths.append(entry.toString());
        }
        if (!paths.isEmpty()) {
            m_animationShaderRegistry.addSearchPaths(paths);
        }
        qCDebug(lcEffect) << "loadShaderRegistryFromDbus: added" << paths.size()
                          << "search paths — registry effect count="
                          << m_animationShaderRegistry.availableEffects().size();
    });
}

} // namespace PlasmaZones

// KWin Effect Factory - creates the plugin
#include <effect/effect.h>

namespace KWin {

KWIN_EFFECT_FACTORY_SUPPORTED(PlasmaZones::PlasmaZonesEffect, "metadata.json",
                              return PlasmaZones::PlasmaZonesEffect::supported();)

} // namespace KWin

// MOC include - REQUIRED for the Q_OBJECT in KWIN_EFFECT_FACTORY_SUPPORTED
#include "plasmazoneseffect.moc"
