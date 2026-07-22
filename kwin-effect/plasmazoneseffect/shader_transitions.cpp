// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"
#include "shader_resolve.h"
#include "window_query.h"

#include "compositor/windowanimator.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ProfileTree.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <effect/effecthandler.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <QByteArray>
#include <QChar>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPainter>
#include <QPointer>
#include <QRunnable>
#include <QStringList>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>
#include <QVector4D>

#include <limits>
#include <memory>
#include <unordered_set>
#include <utility>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

using ShaderInternal::kUserTextureSamplerNames;
using ShaderInternal::kUserTextureWrapKeys;
using ShaderInternal::loadUserTextureImage;
using ShaderInternal::shaderClockNowMs;

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
QByteArray ShaderInternal::injectKwinDefineAfterVersion(const QString& source)
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
    const QString eol = useCrlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
    // KWin 6.7's generateCustomShader compiles custom effect shaders at GLSL
    // #version 140 (it rewrites our #version 450 down to the GL context's core
    // version). At 140 the `layout(location = N)` qualifiers our vertex stages
    // declare on in/out attributes are illegal without these ARB extensions, so
    // the vertex shader fails to compile (NVIDIA error C7548). Failed compiles
    // are NOT cached (the compile path returns false without inserting into
    // m_shaderCache), so every transition then re-runs the whole
    // assemble+compile on the compositor thread — the cause of the severe
    // per-command window-movement / mode-change lag. The daemon's Qt-RHI/SPIR-V
    // path (no PLASMAZONES_KWIN) needs the explicit locations for SPIR-V, so we
    // enable the extensions on the KWin path rather than stripping the
    // qualifiers. `: enable` is a harmless no-op on the fragment stage and on
    // drivers that already expose explicit locations in core 140. The
    // directives precede every declaration (only KWin's #defines and the
    // source's leading comments come before them), which is all NVIDIA's
    // compiler requires.
    const QString defineLine = QStringLiteral("#extension GL_ARB_explicit_attrib_location : enable") + eol
        + QStringLiteral("#extension GL_ARB_separate_shader_objects : enable") + eol
        + QStringLiteral("#define PLASMAZONES_KWIN") + eol;

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
        qCWarning(lcEffect) << "Shader source has no #version directive — synthesizing `#version 450`. "
                               "Animation and surface packs MUST declare `#version 450` (the canonical contract); "
                               "the shader-validate CI gate enforces this for the bundled packs.";
        const QString header = QStringLiteral("#version 450") + eol + defineLine;
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

namespace {

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

bool PlasmaZonesEffect::beginShaderTransition(KWin::EffectWindow* window,
                                              const PhosphorAnimationShaders::ShaderProfile& profile, int durationMs,
                                              bool reverse, bool holdCloseGrab, bool holdAddedGrab,
                                              bool animateMinimized,
                                              std::shared_ptr<const PhosphorAnimation::Curve> progressCurve)
{
    const QString effectId = profile.effectiveEffectId();
    if (effectId.isEmpty() || !window)
        return false;

    // A timing curve is meaningless on the animator-driven path (durationMs == 0):
    // that leg reads its progress from the WindowAnimator, whose own profile
    // ALREADY carries the curve, so applying one here would double-ease. The
    // pairing is unrepresentable in the transition (progressCurve is stored only
    // under durationMs > 0, and types.h documents the null-on-that-path invariant
    // as fact), so normalise it away in release instead of letting a caller's
    // curve vanish silently. Assert in debug so the miswiring surfaces at once.
    Q_ASSERT(durationMs > 0 || !progressCurve);
    if (durationMs <= 0 && progressCurve) {
        qCWarning(lcEffect) << "beginShaderTransition: progressCurve supplied with durationMs <= 0 — dropping it; the "
                               "animator-driven path already carries the curve";
        progressCurve.reset();
    }

    // Global animations toggle. Mirrors the daemon's
    // `SurfaceAnimator::beginShow/beginHide` early-out when
    // `setEnabled(false)`. Gating here (rather than only in
    // `tryBeginShaderForEvent`) covers BOTH callsite categories
    // uniformly: window-lifecycle events that flow through
    // `tryBeginShaderForEvent`, and the window.movement.* geometry events that flow through
    // `applyWindowGeometry → beginShaderTransition` directly. Without
    // this gate that path would still install shader transitions
    // even with global animations off.
    if (!m_windowAnimator->isEnabled()) {
        return false;
    }

    // OffscreenEffect's `redirect()` allocates an FBO sized to the
    // window's frame geometry. A window with a genuinely collapsed
    // geometry reports 0×0 (or 1×1) here, and FBO creation aborts
    // with `GL_INVALID_VALUE … <levels>, <width> and <height> must
    // be 1 or greater` followed by `GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT`
    // — the redirect silently leaves the window in a half-broken
    // state that contaminates every subsequent transition until KWin
    // itself reallocates the offscreen data. Skip the install on
    // collapsed surfaces.
    //
    // A MINIMIZED window is rejected UNLESS the caller opted in via
    // animateMinimized (the going-to-minimized leg of
    // window.appearance.minimize is the only opt-in). Minimizing keeps
    // the frame geometry and the last committed buffer intact; painting
    // is merely disabled via PAINT_DISABLED_BY_MINIMIZE, which the
    // EffectWindowVisibleRef installed below lifts for the transition's
    // lifetime (KWin's own Magic Lamp / Squash mechanism). Every OTHER
    // event that can reach here on a minimized window — a snap-commit
    // batch racing the minimize→float bookkeeping, window.focus, a
    // maximizedChanged race — must keep the historical silent no-op:
    // installing (and force-showing) there would animate a window the
    // user believes is minimized. A genuinely 0-sized geometry always
    // bails.
    const QRectF geo = window->frameGeometry();
    if (window->isMinimized() && !animateMinimized) {
        qCDebug(lcEffect) << "beginShaderTransition: skipping minimized window for non-minimize event" << effectId
                          << "window=" << window->windowClass();
        return false;
    }
    if (geo.width() < 1.0 || geo.height() < 1.0) {
        qCDebug(lcEffect) << "beginShaderTransition: skipping collapsed surface" << effectId
                          << "window=" << window->windowClass() << "geo=" << geo
                          << "isMinimized=" << window->isMinimized();
        return false;
    }

    auto eff = m_shaderManager.m_animationShaderRegistry.effect(effectId);
    if (!eff.isValid()) {
        qCWarning(lcEffect) << "beginShaderTransition: registry has no effect" << effectId << "— registry effect count="
                            << m_shaderManager.m_animationShaderRegistry.availableEffects().size();
        return false;
    }
    // Symmetric mirror of the desktop-pass guard (DesktopTransitionManager::
    // begin). This is exclusively the per-window OffscreenEffect path —
    // desktop.switch never routes here — so a desktop-contract pack
    // (appliesTo:["desktop"], two-texture getFromColor/getToColor sampling
    // uFromDesktop/uToDesktop) is always misassigned: on a per-window surface
    // those samplers are unbound and it would paint garbage. It can only reach
    // here via a hand-edited config that assigns a desktop pack at window/global
    // scope (the settings pickers filter desktop packs out of every window
    // event). Refuse it so the window keeps its normal behavior. Guarding on the
    // desktop class specifically — not full class-matching — keeps geometry /
    // appearance / universal shaders untouched. This split is deliberate: a
    // desktop pack on a per-window surface paints GARBAGE (unbound samplers),
    // so it must be refused at this chokepoint; a move/geometry class mismatch
    // paints safely but dead, and is enforced upstream by
    // resolvedShaderAppliesToEvent at every resolution route into here
    // (tryBeginShaderForEvent, applyWindowGeometry). A future direct caller
    // must route through that gate too.
    if (eff.appliesTo.contains(PhosphorAnimation::ProfilePaths::EventClassDesktop)) {
        qCWarning(lcEffect) << "beginShaderTransition: refusing desktop-contract shader" << effectId
                            << "on a per-window event — desktop packs sample unbound uFromDesktop/uToDesktop";
        return false;
    }

    // Everything below THIS point is GL: it compiles the pack's shader (glCreateShader /
    // glLinkProgram), uploads its user textures (glTexImage2D), and runs the LRU eviction,
    // whose victim's destructor is glDeleteTextures. And every caller reaches here OFF the
    // paint cycle — a KWin window signal, a D-Bus reply, a drag ending — where the
    // compositor's context is not current. endShaderTransition, its counterpart, has done
    // this from the start and says why; the begin side never did.
    //
    // Placed here, BELOW the early-outs, not at the top of the function. Making a context
    // current is not free, and the busiest caller by far is a superseded drag geometry
    // update that turns around at one of the guards above without touching GL at all.
    ensureGlContextCurrent();

    // Compile (or fetch the cached) shader program + uniform locations for this
    // effect. Split out to shader_textures.cpp; the GL context made current just
    // above is the one it compiles against.
    const CachedShader* cachedShaderEntry = compileOrLoadAnimationShader(effectId, eff);
    if (!cachedShaderEntry) {
        // Transient failure (missing / empty / unexpandable source) that
        // compileOrLoadAnimationShader deliberately does NOT cache — install
        // nothing and let a later trigger re-attempt.
        return false;
    }
    // A cached null-shader sentinel marks a prior compile failure (see the
    // "Failed to compile" branch in compileOrLoadAnimationShader). Skip the
    // transition without re-attempting the expensive compile on every trigger;
    // the effectsChanged handler clears m_shaderCache on hot-reload, so a
    // corrected shader recompiles then. A successfully compiled entry always
    // holds a non-null shader, so this never false-positives on a live transition.
    if (!cachedShaderEntry->shader) {
        return false;
    }

    // Detect supersession before the teardown so we can skip the
    // redundant unredirect+redirect cycle. KWin's offscreen-effect
    // pipeline reallocates the offscreen render target on every
    // unredirect→redirect, and a back-to-back supersession (e.g. an
    // autotile-reorder drag firing window.move at 60 Hz) would
    // otherwise pay that cost every frame.
    auto* existing = m_shaderManager.findTransition(window);
    const bool isSameWindowSupersession = existing != nullptr;
    // Same-effect short-circuit. KWin fires multiple lifecycle events for
    // a freshly-opened window in quick succession (windowAdded →
    // windowActivated, and windowMaximizedStateChanged if it opens
    // maximized). If the user has the SAME shader bound to several of
    // these events (a common case for window.open + window.focus =
    // fly-in), each event lands here with the same `cacheIt` entry, and
    // without this guard the supersession path erases the in-flight
    // transition and re-inserts a fresh one with startTimeMs = now —
    // restarting the animation from t=0 every time. With a 2 s open
    // duration the user sees multiple staggered copies of the window
    // sliding in (one per restart). Pointer-compare cached entries:
    // m_shaderCache is keyed by effectId, so equal pointers means same
    // effectId. Skip when direction (reverse) and timing mode
    // (durationMs > 0) also match — a true "second trigger of the same
    // already-playing transition" — so a reverse leg or a mode flip
    // still supersedes correctly.
    if (isSameWindowSupersession && existing->cached == cachedShaderEntry && existing->reverse == reverse
        && ((existing->durationMs > 0) == (durationMs > 0))) {
        // Same-effect short-circuit: prior leg is intact and continues
        // running. Caller (`tryBeginShaderForEvent`) MUST NOT schedule a
        // fresh teardown timer — the prior leg's own timer (or animator
        // completion) owns the teardown, and a new timer carrying the
        // SAME generation as that prior leg would fire on a shorter
        // duration and cut its animation short.
        return false;
    }
    // Carry the prior transition's closeGrabHeld through supersession so
    // ref/unref stay balanced. If the prior transition refWindow'd the
    // closing window, the ref must stay held (the new transition takes
    // ownership of the release). Without this, erasing the prior entry
    // would lose track of the ref and leak the EffectWindow forever.
    // Symmetric: if neither prior nor new transition holds the grab, no
    // ref work happens — supersession of two non-close transitions is a
    // no-op for ref accounting.
    // Capture EVERY supersession-carry flag from the prior transition
    // BEFORE the erase below — the `existing` pointer is invalidated by
    // the erase call, and any later read (e.g. for transition.addedGrabHeld)
    // would be UB. closeGrabHeld + addedGrabHeld both need to carry through
    // so ref/unref stay balanced; if EITHER prior or new install holds the
    // grab, the new transition's endShaderTransition will balance.
    const bool existingHeldGrab = isSameWindowSupersession ? existing->closeGrabHeld : false;
    const bool existingAddedHeldGrab = isSameWindowSupersession ? existing->addedGrabHeld : false;
    // Acquire the minimized-window paint lifeline BEFORE the supersession
    // erase below drops the prior transition's ref, so the per-reason
    // visible count never transiently hits zero across a supersession on a
    // still-minimized window. Held in a local so every early-return path
    // between here and the transition stamp releases it automatically
    // (RAII); the stamp copies it into the transition, and KWin's
    // per-reason accounting keeps the copy churn balanced. Reaching here
    // minimized implies animateMinimized — the guard at the top already
    // rejected the other case.
    KWin::EffectWindowVisibleRef minimizedPaintLifeline;
    if (window->isMinimized()) {
        minimizedPaintLifeline = KWin::EffectWindowVisibleRef(window, KWin::EffectWindow::PAINT_DISABLED_BY_MINIMIZE);
    }
    if (isSameWindowSupersession) {
        // Erase the prior bookkeeping but skip the unredirect — we're
        // about to re-shader this same window. setShader() below
        // overwrites the shader pointer; no need to null it first.
        m_shaderManager.eraseTransition(window);
        existing = nullptr;
    }
    // else: window is not currently shaderized; falls through to the
    // redirect() call below (no-op endShaderTransition since the map
    // doesn't have the entry).

    const auto& cachedEntry = *cachedShaderEntry;
    ShaderTransition transition;
    transition.cached = &cachedEntry;
    // Surface-extent shaders (metadata `fboExtent: "surface"`) render past
    // the window bounds; `apply()` and paintWindow read this flag to expand
    // the drawn quad + anchor uniforms to the window's output.
    transition.surfaceExtent =
        (eff.fboExtentKind == PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Surface);
    // Vertex-stage grid deformation (e.g. `flow`): subdivide the surface
    // quad so the vertex shader has interior vertices to displace. Only
    // meaningful for surface-extent shaders, mirroring `apply()`'s guard.
    transition.gridSubdivisions = transition.surfaceExtent ? eff.geometryGridSubdivisions : 0;

    // Soft-body lattice constants (iMoveMesh consumers): read the pack's
    // named params so a mesh pack can tune the physics live in the settings
    // UI without a rebuild. Each falls back to the generic default when the
    // pack doesn't declare it, so a non-mesh pack costs nothing. The same
    // named values also reach the shader as p_<name> customParams, so the
    // pack can expose them as ordinary sliders. Values arrive from
    // user-editable metadata, so they are clamped to keep the explicit spring
    // integrator (mesh_sim.cpp) stable. Each per-node update is
    // v' = drag*v - K*t*x, p += t*moveFactor*v' (t = 10 ms substep, K the
    // spring constant); the 2x2 state matrix has det = drag and
    // trace = 1 - K*t^2*moveFactor + drag, so both eigenvalues stay inside the
    // unit circle iff drag < 1 AND K*t^2*moveFactor < 2*(1+drag). drag < 1
    // alone is NOT sufficient: a stiff pack diverges, `settled` never trips,
    // and the transition drives a full-screen-repaint runaway until the 4 s
    // safety teardown. So clamp drag < 1, keep the stiffnesses non-negative,
    // then cap moveFactor to the product bound below.
    // Materialised once here and reused for the slot translation below.
    const QVariantMap params = profile.effectiveParameters();
    {
        auto readParam = [&](const char* name, qreal& out, qreal lo, qreal hi) {
            const auto it = params.constFind(QLatin1String(name));
            if (it != params.constEnd() && it->canConvert<double>()) {
                out = qBound(lo, it->toDouble(), hi);
            }
        };
        readParam("sheetStiffness", transition.meshParams.stiffness, 0.0, 1.0);
        readParam("gripStiffness", transition.meshParams.gripStiffness, 0.0, 1.0);
        readParam("springiness", transition.meshParams.drag, 0.0, 0.99);
        readParam("moveFactor", transition.meshParams.moveFactor, 0.0, 1.0);

        // Enforce K*t^2*moveFactor < 2*(1+drag) so the lattice always settles.
        // K is the effective spring constant: grip nodes use gripStiffness
        // directly (single DOF), while the free-sheet neighbour springs can
        // reach ~2x sheetStiffness at the highest lattice mode, so take
        // max(gripStiffness, 2*sheetStiffness). Cap moveFactor to 80% of the
        // bound for margin; the shipped KWin preset (k=0.018, gk=0.16,
        // mf=0.16, drag=0.82) sits at ~70% and is left untouched.
        constexpr qreal kMeshSubstepMs = 10.0; // matches mesh_sim.cpp integrator step
        const qreal effectiveK = qMax(transition.meshParams.gripStiffness, 2.0 * transition.meshParams.stiffness);
        if (effectiveK > 0.0) {
            const qreal moveFactorLimit =
                0.8 * 2.0 * (1.0 + transition.meshParams.drag) / (kMeshSubstepMs * kMeshSubstepMs * effectiveK);
            transition.meshParams.moveFactor = qMin(transition.meshParams.moveFactor, moveFactorLimit);
        }
    }

    // Translate the friendly parameter map (e.g. {"direction": 1,
    // "parallax": 0.2}) to slot keys, then pack each
    // `customParams<N>_<x|y|z|w>` set into a vec4 we can blast in one
    // setUniform call per slot. Translation honours the metadata
    // declaration order — same allocation the daemon's
    // SurfaceAnimator::runLeg path uses, so a single ShaderProfile
    // produces identical visuals on either runtime.
    const QVariantMap translated =
        PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(eff, params);
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
    // `m_shaderManager.m_textureCache` (keyed by absolute path so two effects sharing
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
        auto texIt = m_shaderManager.m_textureCache.find(path);
        bool freshlyLoaded = false;
        if (texIt != m_shaderManager.m_textureCache.end()) {
            // Bump the access tick on lookup so the LRU sweep sees this
            // path as "fresh" — keeps frequently-used textures warm
            // even if a flood of unique single-use textures pushes the
            // cache over its bound.
            texIt->second.lastAccessTick = ++m_shaderManager.m_textureCacheAccessTick;
        }
        if (texIt == m_shaderManager.m_textureCache.end()) {
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
            cachedTex.lastAccessTick = ++m_shaderManager.m_textureCacheAccessTick;
            texIt = m_shaderManager.m_textureCache.emplace(path, std::move(cachedTex)).first;
            freshlyLoaded = true;
        }
        // Publish the slot BEFORE any eviction can run. The transition under
        // construction is not in shaderTransitions() yet, so the sweep only knows
        // about the slots we hand it — and the entry whose insertion pushed the cache
        // over the bound would otherwise be the one entry nothing protects, because it
        // is not assigned until after the sweep. It survived on arithmetic (a fresh
        // entry carries the newest access tick, so the LRU scan reaches it last), which
        // holds only while the soft bound stays far above the slot count. That is a
        // property of two constants, neither of which knows it is load-bearing. Assign
        // first, then sweep, and the bound stops mattering.
        transition.userTextures[slot] = &texIt->second;
        if (freshlyLoaded) {
            // std::map only invalidates the erased iterator, so `texIt` survives the
            // sweep — and `transition` now covers every slot it holds, this one included.
            evictLruTextureIfOverBound(&transition);
        }
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
    // unbounded. Two non-install writers share the counter: the drag-start
    // (re-)hold (windowStartUserMovedResized), which fences a prior drag's
    // release timers on a re-grab, and the mesh-drag release handler
    // (windowFinishUserMovedResized), which bumps when it hands the
    // lifetime to the settle gate.
    transition.generation = ++m_shaderManager.m_shaderTransitionGenerationCounter;
    transition.reverse = reverse;
    // Stamp the close-grab flag so endShaderTransition knows to release
    // refWindow + WindowClosedGrabRole on teardown. The new transition
    // inherits the prior transition's grab if supersession was a close-
    // on-close case (so the ref isn't double-incremented or lost). If
    // EITHER the prior or new install wants the grab, we treat it as
    // held — the new transition's endShaderTransition will balance.
    transition.closeGrabHeld = holdCloseGrab || existingHeldGrab;
    transition.addedGrabHeld = holdAddedGrab || existingAddedHeldGrab;
    // Keep a minimized window paintable for the transition's lifetime.
    // The ref was acquired into `minimizedPaintLifeline` BEFORE the
    // supersession erase (see the comment there), so a superseding
    // install on a still-minimized window genuinely holds its own ref
    // before the prior one drops. RAII — released when the transition
    // entry is erased (endShaderTransition / windowDeleted / supersession
    // all destroy the ShaderTransition, whose member dtor unrefs; the
    // copy here refs and the local's scope-end unrefs, balanced by
    // KWin's per-reason accounting).
    transition.visibleRef = minimizedPaintLifeline;
    // Icon target for minimize-to-icon packs. Captured unconditionally —
    // the rect is a stored value on the EffectWindow, and only shaders
    // that declare iIconRect ever read the pushed uniform. A window in no
    // task manager reports an empty rect, which stays a null QRectF here
    // and reaches the shader as (0, 0, 0, 0) = "no icon target".
    {
        const auto icon = window->iconGeometry();
        if (icon.width() >= 1.0 && icon.height() >= 1.0) {
            transition.iconRect = QRectF(icon.x(), icon.y(), icon.width(), icon.height());
        }
    }
    if (durationMs > 0) {
        transition.durationMs = durationMs;
        transition.startTimeMs = shaderClockNowMs();
        // Per-event timing curve (global → "All" → node → rule). paintWindow
        // eases the linear time progress through it. Stored only on the
        // time-driven path; a durationMs == 0 animator-driven transition reads a
        // curve-shaped value from the WindowAnimator, so a curve here would
        // double-ease. Fresh CurveState for a stateful (spring) curve.
        transition.progressCurve = std::move(progressCurve);
        transition.progressCurveState = PhosphorAnimation::CurveState{};
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

    // WindowAddedGrabRole tells every OTHER effect in the chain
    // ("isFadeWindow" style checks in KWin's stock fade / scale / slide /
    // glide built-ins) to ignore this window for the window-added
    // animation. Without it, KWin's stock fade-in renders the window at
    // its natural position while our shader simultaneously animates the
    // same window at the UV-shifted position; both renders end up in
    // the framebuffer and the user sees multiple visible copies. The
    // role only matters for the duration of the install — we clear it
    // in endShaderTransition. No refWindow needed (the window isn't
    // being torn down; it just opened). `existingAddedHeldGrab` is
    // computed at the transition.addedGrabHeld stamp above; we only
    // need to call setData when this install is acquiring the grab
    // fresh (the supersession path inherits via the flag).
    if (holdAddedGrab && !existingAddedHeldGrab) {
        window->setData(KWin::WindowAddedGrabRole, QVariant::fromValue(static_cast<void*>(this)));
    }

    // Emplace the transition entry FIRST, before redirect/setShader. If
    // either of those throws — or if we hit a later failure path — we
    // need a transition entry to tear down so the window doesn't end up
    // redirected with a shader installed but no bookkeeping. RAII guard
    // erases the entry if we don't successfully reach the bottom of the
    // function (either of the two op paths below threw).
    // Snapshot the grab flags BEFORE the move-into-map so the scope-guard
    // rollback path doesn't have to read them back through the inserted
    // pointer (which is null on a contract-violating duplicate-key insert,
    // see insertTransition's docstring). These values came from
    // `holdCloseGrab || existingHeldGrab` / `holdAddedGrab ||
    // existingAddedHeldGrab` computed above.
    const bool transitionHadCloseGrab = transition.closeGrabHeld;
    const bool transitionHadAddedGrab = transition.addedGrabHeld;
    auto* inserted = m_shaderManager.insertTransition(window, std::move(transition));
    if (!inserted) {
        // Contract violation: the supersession branch above did not erase
        // the prior entry, or a concurrent install raced us. Release the
        // grab refs we just acquired so the window doesn't strand in
        // closing/added state, then bail. The new shader/redirect is not
        // installed because we never reach setShader/redirect below.
        // Roll back only what THIS install freshly acquired, and give the
        // INHERITED-grab case (existing*HeldGrab) no release action at all.
        // That asymmetry is deliberate: reaching here with a grab inherited
        // means the supersession erase ran (it is unconditional above) and the
        // insert STILL failed, which leaves two possibilities implying
        // OPPOSITE rollbacks — the erased entry's grab is now unowned (must
        // release), or a racing install already inherited it (releasing
        // double-releases). With no safe action available, leaking on a path
        // the unconditional erase makes unreachable is the right trade.
        // insertTransition pins the duplicate-key invariant itself, asserting
        // in debug and logging a release-build breadcrumb.
        // No `&& window` on either arm: the early-return at the top of
        // beginShaderTransition rejects a null window, and every path between
        // here and there dereferences it unconditionally.
        if (holdAddedGrab && !existingAddedHeldGrab) {
            window->setData(KWin::WindowAddedGrabRole, QVariant());
        }
        if (holdCloseGrab && !existingHeldGrab) {
            window->setData(KWin::WindowClosedGrabRole, QVariant());
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
        return false;
    }
    bool emplaceCommitted = false;
    auto emplaceGuard = qScopeGuard([&]() {
        if (emplaceCommitted) {
            return;
        }
        // The same-window supersession path above erased the prior
        // transition entry directly (no endShaderTransition call →
        // no grab release), and the new transition inherited that
        // grab via `transition.closeGrabHeld = holdCloseGrab ||
        // existingHeldGrab`. If redirect()/setShader() throws after
        // the insert, simply erasing the new entry would leak the
        // inherited (or freshly-acquired) close grab and strand the
        // window in closing state with no release path. Mirror
        // endShaderTransition's grab-release sequence here so the
        // ref + role clear stay balanced on the rollback path.
        //
        // Use the pre-move snapshot rather than reading through
        // `inserted->` — `transitionHadCloseGrab` / `transitionHadAddedGrab`
        // captured the values that landed in the map.
        const bool releaseCloseGrab = transitionHadCloseGrab;
        const bool releaseAddedGrab = transitionHadAddedGrab;
        // No `&& window` here either, for the reason given on the insert-failure
        // rollback above: the early-return at the top of beginShaderTransition
        // rejects a null window, and this point is strictly later in the flow.
        if (releaseAddedGrab) {
            // Symmetric WindowAddedGrabRole rollback. No ref to release.
            window->setData(KWin::WindowAddedGrabRole, QVariant());
        }
        if (releaseCloseGrab) {
            // Clear WindowClosedGrabRole synchronously while the
            // ref we hold guarantees `window` is still alive. The
            // role clear is a courtesy for other effects.
            window->setData(KWin::WindowClosedGrabRole, QVariant());
            // Defer unrefWindow to the next event-loop iteration —
            // matches endShaderTransition's deferred-unref reasoning.
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
        m_shaderManager.eraseTransition(window);
    });

    if (!isSameWindowSupersession) {
        redirect(window);
    }
    // setShader replaces any prior shader pointer (idempotent for the
    // same shader, so same-effect supersession is correct here). Vertex-
    // deform transitions go through KWin's default texture shader so
    // `OffscreenData::paint` samples uTexture0 at the natural quad
    // texcoords; the translate happens at the vertices in `apply()`.
    setShader(window, cachedEntry.shader.get());
    emplaceCommitted = true;

    // Kick the compositor into painting now so paintWindow fires and
    // the transition's iTime starts advancing. Without this, a shader
    // installed on a stable window (e.g. window.focus on a window with
    // no in-flight damage) would sit in m_shaderManager.m_shaderTransitions for its
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
    // A surface-extent transition paints across the whole output. The
    // off-frame band the shader sweeps is covered by the unconditional
    // `effects->addRepaintFull()` at the end of this function —
    // `addLayerRepaint` itself clips its argument back to the window-
    // item's bounding rect via the scene's `mapFromScene` (see
    // paint_pipeline.cpp's commentary), so widening `repaintRect` to
    // `output->geometry()` here only enlarges the layer repaint within
    // the scene-clipped bounds the window already covers — it does NOT
    // by itself reach the off-frame band. The widening still matters
    // for the bounded layer-repaint correctness inside that frame.
    //
    // If `screen()` returns null (transient/popup at install time, monitor
    // unplug mid-attach), the surface-extent contract cannot be honoured
    // with a frame-sized fallback: paint_pipeline.cpp's apply() and the
    // anchor-uniform feed already rely on the same `output` and would also
    // degrade. Fall back to a full repaint so the shader's first paint is
    // not silently clipped to the frame, and log so the missing screen is
    // visible in support traces.
    if (eff.fboExtentKind == PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Surface) {
        if (const auto* output = window->screen()) {
            repaintRect = output->geometry();
        } else {
            // No screen — surface-extent contract cannot be honoured with a
            // frame-sized fallback (paint_pipeline.cpp's apply() and the
            // anchor-uniform feed both depend on the same output). The
            // unconditional `effects->addRepaintFull()` immediately below
            // covers this case — log and fall through.
            qCWarning(lcEffect) << "Surface-extent transition" << effectId
                                << "installed on window with no screen() — relying on the unconditional"
                                << "addRepaintFull below to mark first-frame damage";
        }
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
    return true;
}

void PlasmaZonesEffect::endShaderTransition(KWin::EffectWindow* window)
{
    if (!window)
        return;
    // Hands the redirect back (KWin destroys the window's offscreen texture and
    // framebuffer) and destroys the transition's own snapshot texture. This is the primary
    // teardown for every time-driven animation and it fires from a QTimer between frames,
    // dozens of times a minute, with no current context.
    ensureGlContextCurrent();
    // Drop the expiry-pending guard regardless of whether the
    // transition still exists. If a synchronous teardown beat the
    // queued slot to the punch, the queued slot must not see this
    // window flagged as still-pending or it would skip a future
    // expiry's re-queue.
    m_shaderManager.m_pendingShaderExpiryEnd.remove(window);
    auto* st = m_shaderManager.findTransition(window);
    if (!st) {
        return;
    }
    const bool releaseCloseGrab = st->closeGrabHeld;
    const bool releaseAddedGrab = st->addedGrabHeld;
    // Surface-extent transitions paint across the whole output, far past
    // the window's own geometry. On teardown KWin only damages the
    // window's frame as it unredirects, so the off-frame pixels the
    // shader touched (a fly-in's slide path, a bounce's overshoot) keep
    // the last shader frame until something else repaints them — the
    // "glitch that only clears when you move the window" symptom. Capture
    // the output rect now, while `it` and `window` are valid, and force
    // one output-level repaint once teardown is complete.
    // Guard against teardown on a window that's already been destroyed
    // (windowDeleted may have raced our timer). setShader / unredirect on a
    // deleted EffectWindow is undefined behaviour in KWin's offscreen-effect
    // pipeline; just drop our bookkeeping. The windowDeleted handler at the
    // KWin::effects connection erases m_shaderManager.m_shaderTransitions for the same
    // window, so this is a defence-in-depth against ordering races.
    //
    // The surface-extent post-teardown repaint capture lives INSIDE this
    // guard for the same reason: `window->screen()` on a deleted
    // EffectWindow is the same UB class as `setShader`/`unredirect`. A
    // deleted window can't sweep off-frame pixels anyway — the windowDeleted
    // handler has already erased our bookkeeping and the redirected FBO is
    // gone — so skipping the repaint is correct.
    QRect surfaceExtentRepaint;
    if (!window->isDeleted()) {
        if (st->surfaceExtent) {
            if (const auto* output = window->screen()) {
                surfaceExtentRepaint = output->geometry();
            }
        }
        // Border-vs-animation slot handover: if the window still has a border,
        // the border shader — not "no shader" — is the correct resting state.
        // The animation transition borrowed the OffscreenEffect setShader slot
        // (its begin overrode whatever the border path had set); on teardown,
        // hand the slot BACK to the border shader and KEEP the redirect, rather
        // than unredirecting. Unredirecting here would drop the border outline
        // on every snapped window the moment its open/focus/move animation
        // ends. reconcileDecorationShader re-applies the border shader (setShader +
        // redirect, both idempotent) and stamps WindowDecoration::shaderApplied so
        // the per-frame uniform push and removeWindowDecoration resume owning the
        // slot. We must erase the transition FIRST so reconcileDecorationShader's
        // own findTransition() check sees no live transition and takes the
        // apply branch.
        m_shaderManager.eraseTransition(window);
        st = nullptr;
        const QString wid = getWindowId(window);
        // A CLOSING window's border entry was kept alive so renderSurfaceChain
        // could composite it under the close animation (slotWindowClosed defers
        // the removal); the animation is over and the window is going away, so
        // drop it now rather than re-applying a resting border to a corpse.
        if (releaseCloseGrab) {
            removeWindowDecoration(wid);
        }
        const bool stillBordered = m_windowDecorations.contains(wid);
        if (stillBordered) {
            reconcileDecorationShader(wid, window);
        } else if (!releaseCloseGrab) {
            // First decoration opportunity for a freshly-opened window. Its
            // window.open animation borrowed the OffscreenEffect redirect/shader
            // slot, so creating the border back in slotWindowAdded would fight the
            // in-flight transition (and the open animation visibly broke). Now
            // that the transition is torn down, create it here so the border
            // appears the moment the open animation ends — no focus change needed.
            // updateWindowDecoration self-gates (app-window filter + non-empty chain)
            // and re-applies via reconcileDecorationShader (the transition is already
            // erased, so it takes the apply branch). Skipped for a CLOSING window
            // (releaseCloseGrab) — no point decorating a window on its way out.
            // If the window isn't decoratable, hand the slot back to KWin.
            updateWindowDecoration(wid, window);
            if (!m_windowDecorations.contains(wid)) {
                setShader(window, nullptr);
                unredirect(window);
            }
        } else {
            setShader(window, nullptr);
            unredirect(window);
        }
    } else {
        m_shaderManager.eraseTransition(window);
        st = nullptr;
        // Clear the redirect's bound shader BEFORE the border/multipass
        // teardown below destroys the layer textures its uniforms still
        // reference: the grab release and the Deleted's destruction are not
        // frame-synchronous, so an EXPIRY-FRAME paint of the ref-held corpse
        // would otherwise re-run the animation program with iHasSurfaceLayer
        // stale at 1 and its texture freshly deleted — an unbound sampler
        // reads opaque black across the whole quad. setShader on the ref-held
        // deleted window is the same operation close-begin already performs
        // (KWin's setShader is a keyed no-op only for un-redirected windows;
        // our redirect is live until the unref).
        setShader(window, nullptr);
        // Deleted window: drop the border entry the close path deferred.
        // The rest of removeWindowDecoration's GL side is a no-op here
        // (findWindowById resolves nothing for a deleted id); the
        // windowDeleted handler remains the backstop.
        removeWindowDecoration(getWindowId(window), window);
    }
    if (!surfaceExtentRepaint.isEmpty() && KWin::effects) {
        KWin::effects->addRepaint(KWin::Rect(surfaceExtentRepaint));
    }
    if (releaseAddedGrab && !window->isDeleted()) {
        // Clear WindowAddedGrabRole now that the open transition is
        // done. Symmetric with the install-side setData; no refcount
        // change because we didn't refWindow on the install side
        // (the window opened, it's already live without our ref).
        window->setData(KWin::WindowAddedGrabRole, QVariant());
    }
    if (releaseCloseGrab) {
        // Clear WindowClosedGrabRole while `window` is still alive
        // (the ref we hold via refWindow() guarantees refcount >= 1
        // here). The role clear is a courtesy for other effects;
        // doing it now avoids touching `window` after the deferred
        // unref below.
        window->setData(KWin::WindowClosedGrabRole, QVariant());
        // Defer unrefWindow to the next event-loop iteration. This is
        // CRITICAL because endShaderTransition is reachable from
        // paintWindow's expired-transition fall-through path,
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

} // namespace PlasmaZones
