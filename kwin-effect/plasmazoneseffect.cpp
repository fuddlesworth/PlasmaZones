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
