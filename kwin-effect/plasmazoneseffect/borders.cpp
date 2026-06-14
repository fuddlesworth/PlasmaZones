// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>

#include "../autotilehandler.h"
#include "../snaphandler.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorCompositor/AutotileState.h>

#include <QByteArray>
#include <QVector2D>
#include <QVector4D>

#include <optional>

namespace PlasmaZones {

void PlasmaZonesEffect::setupDecorationManager()
{
    // The drain-time veto is the authoritative re-check for deferred
    // title-bar restores: a vetoed restore stays QUEUED (the manager
    // re-arms its fallback timer and bounds the retries), so the veto must
    // hold ONLY while a re-acquire is genuinely expected — the window's
    // screen re-entered autotile mid-drain, the mode's hide-title-bars is
    // still on, and the window is not floating (a retile never re-acquires
    // floated windows). Without the latter two conditions, a hide-toggle-off
    // drain or a floated window's restore would be vetoed until the
    // manager's retry bound overrides it.
    m_decorationManager->setRestoreVeto([this](const QString& windowId) {
        KWin::EffectWindow* w = findWindowById(windowId);
        // Exact-id re-check: findWindowById's appId fuzzy fallback can
        // resolve a same-app SIBLING when the exact id misses, and a
        // sibling's screen/floating state must never decide the veto for
        // the window the queue entry tracks (same hazard guard as
        // SnapHandler::markWindowSnapped and the manager's own
        // resolveExact for physical toggles).
        if (!w || getWindowId(w) != windowId || !m_autotileHandler->isAutotileScreen(getWindowScreenId(w))) {
            return false;
        }
        return m_autotileHandler->borderState().hideTitleBars && !isWindowFloating(windowId);
    });
    connect(m_decorationManager.get(), &DecorationManager::windowDecorationRestored, this,
            [this](const QString& windowId) {
                // A veto-driven restore leaves the window mode-owned and
                // still border-eligible — rebuild its overlay instead of
                // dropping it. updateWindowBorder self-gates on the merged
                // appearance (it removes first and re-creates only when
                // something should show). Border overlays are visual-only,
                // so off-desktop windows just get their stale item dropped —
                // the desktopChanged → updateAllBorders refresh rebuilds
                // theirs when they become visible (same policy as
                // updateAllBorders and markWindowSnapped). Exact-id re-check:
                // findWindowById's fuzzy appId fallback could resolve a
                // same-app sibling for a dead id, and creating a border item
                // keyed under the dead id against the sibling would linger
                // until the next full rebuild.
                KWin::EffectWindow* w = findWindowById(windowId);
                if (w && getWindowId(w) == windowId && w->isOnCurrentDesktop()) {
                    updateWindowBorder(windowId, w);
                } else {
                    removeWindowBorder(windowId);
                }
            });
    connect(m_decorationManager.get(), &DecorationManager::drainFinished, this, [this]() {
        updateAllBorders();
    });
}

void PlasmaZonesEffect::removeWindowBorder(const QString& windowId)
{
    auto it = m_windowBorders.find(windowId);
    if (it == m_windowBorders.end()) {
        return;
    }
    WindowBorder& wb = it.value();
    // Release the offscreen redirect + border shader slot IF this border owns
    // it. When an animation transition currently owns the slot (shaderApplied
    // is false — the transition's begin took it over), we must NOT touch
    // setShader / unredirect: the transition lifecycle owns the handover and a
    // stray unredirect here would tear down the animation mid-flight. The
    // transition-end path re-checks m_windowBorders and only re-applies the
    // border shader when the border still exists, so dropping the entry here
    // (after this guarded release) is the correct teardown.
    if (wb.shaderApplied) {
        if (KWin::EffectWindow* w = findWindowById(windowId)) {
            // Only clear when no transition raced in to own the slot between
            // this border being applied and now — reconcileBorderShader would
            // have cleared shaderApplied in that case, but guard defensively.
            if (!m_shaderManager.findTransition(w)) {
                setShader(w, nullptr);
                unredirect(w);
            }
        }
    }
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

    // Base appearance from the owning mode (autotile / snap). nullptr → no mode
    // currently draws a border for this window (floating, or its mode's border
    // is off) — a per-window rule may still force one on below.
    const PhosphorCompositor::BorderState* state = resolveBorderStateFor(windowId);

    // Per-window rule override — applies to ANY matched window, snapped or
    // floating (mirrors SetOpacity). Resolved against the same evaluator the
    // opacity / animation rules use; gated on a non-empty rule set so windows
    // with no rules pay nothing.
    std::optional<ResolvedWindowAppearance> ovr;
    if (w && !m_shaderManager.animationRuleSet().isEmpty()) {
        ovr = resolveWindowAppearance(m_shaderManager.animationRuleEvaluator(),
                                      windowRuleQueryFor(w, getWindowScreenId(w)), windowId);
    }

    // Merge: a rule field wins; otherwise fall back to the owning mode's value
    // (or "no border" when the window has no owning border state).
    // resolveBorderStateFor only returns non-null when that mode SHOWS a
    // border, so a non-null `state` means baseShows == true.
    const bool show = (ovr && ovr->showBorder) ? *ovr->showBorder : (state != nullptr);
    if (!show) {
        return;
    }

    const int bw = (ovr && ovr->borderWidth) ? *ovr->borderWidth : (state ? state->width : 0);
    if (bw <= 0) {
        return;
    }

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    // Choose color. The owning mode (autotile / snap) carries separate active
    // and inactive border colours — global appearance settings, not rules — so
    // pick the one matching the window's current focus state. A per-window
    // SetBorderColor rule, when matched, overrides it. Focus-dependence of the
    // RULE colour is expressed in the rule itself via the IsFocused match
    // condition: `windowRuleQueryFor` set the query's isFocused flag, so a
    // focus-scoped rule (`WHEN focused`/`WHEN NOT focused`) only fills the
    // border-colour slot in its matching state, while a focus-agnostic rule
    // applies in both. Either way `ovr->borderColor` already holds the colour
    // appropriate to this window's current focus — no post-resolution switch.
    // A floating window (no owning mode) whose only rule is focus-scoped thus
    // correctly shows no border in the unmatched state; author a focus-agnostic
    // rule to keep a border in both states.
    const bool isFocused = (w == KWin::effects->activeWindow());
    const QColor modeColor = state ? (isFocused ? state->color : state->inactiveColor) : QColor();
    const QColor bc = (ovr && ovr->borderColor) ? *ovr->borderColor : modeColor;
    if (!bc.isValid() || bc.alpha() == 0) {
        return;
    }

    const int br = (ovr && ovr->borderRadius) ? *ovr->borderRadius : (state ? state->radius : 0);

    // Store the resolved appearance (LOGICAL pixels). pushBorderUniforms scales
    // these by viewport.scale() per-frame to reach device px for the shader,
    // and reads live frameGeometry()/expandedGeometry() so a resize/move needs
    // no geometry-sync bookkeeping — the OffscreenEffect redirect already drives
    // a fresh paint on every geometry change.
    WindowBorder wb;
    wb.width = bw;
    wb.radius = br;
    wb.color = bc;

    // Corner rounding is entirely the SHADER's job (the rounded-rect SDF in the
    // border fragment shader clips the frame corners + draws the outline,
    // identically for decorated and borderless windows). It operates on the
    // COMPOSITED redirected texture, so it never clips individual client
    // subsurfaces — and crucially we do NOT touch the KWin window's own
    // BorderRadius: setting it made KWin clip the client surface independently,
    // which on a server-side-decorated window cut the inner surface and left the
    // shader's corner inset behind KWin's. KWin's square drop-shadow corner is
    // left as-is for now (a small nub at the rounded corner); synthesising the
    // shadow in-shader, KDE-Rounded-Corners style, is the follow-up.

    m_windowBorders.insert(windowId, wb);

    // Apply the offscreen border shader (redirect + setShader) unless an
    // animation transition currently owns this window's shader slot — in which
    // case reconcileBorderShader leaves the transition alone and the border
    // shader is (re-)applied when the transition ends. A geometry change drives
    // its own repaint, so no manual repaint is needed here for the steady case;
    // request one anyway so a border added to a STATIC window (no pending
    // damage) reaches paintWindow and the outline becomes visible immediately.
    reconcileBorderShader(windowId, w);
    // `w` is non-null here (the early-out above returns on !w), so no
    // KWin::effects guard is needed — addRepaintFull is a window method.
    w->addRepaintFull();
}

void PlasmaZonesEffect::updateAllBorders()
{
    clearAllBorders();

    // Iterate all effect windows and create borders for any window managed by
    // a mode (autotile or snap) that currently shows borders, OR matched by a
    // per-window border rule (which can draw on an otherwise-borderless /
    // floating window). updateWindowBorder self-gates on the merged effective
    // appearance, so calling it when rules exist is safe; reconcile the rule
    // title-bar override in the same pass so it tracks context changes.
    const bool haveRules = !m_shaderManager.animationRuleSet().isEmpty();
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted()) {
            continue;
        }
        const QString wid = getWindowId(w);
        // Self-heal compositor-initiated noBorder resets: KWin silently
        // re-decorates off-desktop windows on desktop switches. resyncWindow
        // is a self-guarding no-op unless the manager owns the window,
        // believes it hidden, and the compositor reports the decoration
        // back — so running it for every window here is cheap and covers
        // ALL owner kinds (autotile, snap, rule) on every desktop return,
        // activation, and border refresh.
        m_decorationManager->resyncWindow(wid);
        // Border overlays are visual, so only build them for windows on the
        // current desktop. Title-bar hiding (setNoBorder) is a persistent
        // decoration-state change that survives desktop switches, so reconcile
        // it for ALL windows the rule may match — otherwise a SetHideTitleBar
        // rule added while the matched window sits on another virtual desktop
        // would not take effect until that window is next activated.
        if (w->isOnCurrentDesktop() && (haveRules || resolveBorderStateFor(wid))) {
            updateWindowBorder(wid, w);
        }
        if (haveRules) {
            reconcileRuleHiddenTitleBar(wid, w);
        }
    }
    // When no rules remain, the per-window reconcile above is skipped (haveRules
    // is false), so a Rule owner or force-show veto from a now-removed
    // SetHideTitleBar rule would linger until effect teardown. Clear all rule
    // overrides in that case — a no-op when the manager tracks none (the
    // common no-rules path), so this costs nothing when nothing is overridden.
    if (!haveRules) {
        restoreAllRuleHiddenTitleBars();
    }
}

void PlasmaZonesEffect::reconcileRuleHiddenTitleBar(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w || windowId.isEmpty()) {
        return;
    }
    // Tri-state rule override, forwarded to the DecorationManager:
    //   unset → no opinion (mode owners decide)
    //   true  → rule hides (a Rule owner joins the mode owners)
    //   false → rule FORCE-SHOWS (a veto that pins the decoration visible
    //           over any mode owner; owners re-assert when the rule changes)
    // The manager owns the capability gate, the mode-ownership coordination
    // the old m_ruleHiddenTitleBars/modeBorderless dance approximated, and
    // the geometry re-assert across veto-driven decoration flips.
    const std::optional<ResolvedWindowAppearance> ovr = resolveWindowAppearance(
        m_shaderManager.animationRuleEvaluator(), windowRuleQueryFor(w, getWindowScreenId(w)), windowId);
    m_decorationManager->setRuleOverride(windowId, ovr ? ovr->hideTitleBar : std::nullopt);
}

bool PlasmaZonesEffect::isWindowMarkedSnapped(const QString& windowId) const
{
    return m_snapHandler->isTiledWindow(windowId);
}

const PhosphorCompositor::BorderState* PlasmaZonesEffect::resolveBorderStateFor(const QString& windowId) const
{
    // Autotile takes precedence; a window can transiently appear in both the
    // autotile and snap border sets during a mode switch (the call sites guard
    // against steady-state double-tracking via isAutotileScreen, but the
    // transition window is real), and resolving autotile-first is the
    // authoritative tie-break — this ordering is load-bearing, not cosmetic.
    const BorderState& autotile = m_autotileHandler->borderState();
    if (AutotileStateHelpers::shouldShowBorderForWindow(autotile, windowId)) {
        return &autotile;
    }
    if (m_snapHandler->shouldShowBorderForWindow(windowId)) {
        return &m_snapHandler->borderState();
    }
    return nullptr;
}

void PlasmaZonesEffect::restoreAllRuleHiddenTitleBars()
{
    // The authoritative window-rule state is gone (rule set emptied, daemon
    // loss, effect teardown): clear every Rule owner and force-show veto. The
    // manager restores a title bar only where no mode owner remains, so the
    // modes' decoration management is never fought.
    m_decorationManager->clearAllRuleOverrides();
}

// ─────────────────────────────────────────────────────────────────────────────
// Offscreen border shader
// ─────────────────────────────────────────────────────────────────────────────

KWin::GLShader* PlasmaZonesEffect::borderShader()
{
    if (m_borderShader) {
        return m_borderShader.get();
    }
    if (m_borderShaderCompileFailed) {
        return nullptr;
    }

    // Self-contained MapTexture shader pair. We ship BOTH stages (not just the
    // fragment) so the vertex→fragment varying name is OURS, not KWin's
    // generated convention: KWin's built-in MapTexture vertex stage names its
    // texcoord varying differently from our `vTexCoord`, and a name mismatch
    // would leave the fragment's `in vec2 vTexCoord` unlinked. The vertex stage
    // shares the attribute-slot layout (position @ location 0, texCoord @ 1, the
    // modelViewProjectionMatrix uniform) with the animation transition path's
    // kKwinDefaultVertexSource, but DELIBERATELY omits its Y-flip — see the
    // `vTexCoord = texCoord` note below; the fragment reconstructs the top-down
    // device pixel itself, so the rounded-rect gate lines up without it.
    //
    // `uTexture0` is the redirected window content (expanded geometry, device
    // px). KWin's OffscreenData::paint binds it to texture unit 0; a sampler2D
    // left unset defaults to unit 0, so the name is free — same as the
    // animation shaders, which also read `uTexture0` without an explicit
    // sampler bind.
    //
    // One analytic rounded-rect SDF over the window FRAME drives BOTH the corner
    // rounding and the outline (the KDE-Rounded-Corners / shapecorners model),
    // IDENTICALLY for decorated and borderless windows — no alpha-edge detection,
    // no per-window-type branch:
    //
    //   * Corner clip: the content alpha is multiplied down to 0 in the corner
    //     overhang (inside the frame box, outside the rounded rect), AA'd by
    //     fwidth. The drop shadow in the expanded margin is preserved (the cut is
    //     gated to the frame box). The redirected texture includes the server-side
    //     decoration, so a visible titlebar's corners round too. We do NOT set the
    //     KWin window BorderRadius (that clips the client surface independently and
    //     insets the corner); KWin's square shadow corner is left as-is for now.
    //
    //   * Outline: an inner band of `thickness` just inside the rounded edge, from
    //     the SAME field (d in [-thickness, 0]). Confined to the window body so it
    //     never paints onto the shadow. Interior Wayland subsurfaces are never
    //     individually clipped — the field is a single frame-edge band, which is
    //     why a translucent terminal with an opaque child widget gets no cutout.
    //
    // Coordinate reconstruction (KDE-Rounded-Corners' `tex_to_pixel` model):
    //   p            = vec2(vTexCoord.x, 1 - vTexCoord.y) * windowExpandedSize
    //   frameTopLeft = (frameGeometry.topLeft - expandedGeometry.topLeft) * scale
    //   frameSize    = frameGeometry.size * scale
    //   radius       = (borderRadius + borderWidth) * scale   // OUTER corner
    // p is the fragment's TOP-DOWN device pixel within the expanded (shadow-
    // padded) redirected texture; the frame rect sits at frameTopLeft..+frameSize
    // inside it, and the SDF is evaluated in that space. The shader reduces alpha
    // at the corners, so the border path runs the window translucent
    // (prePaintWindow sets it) for the clip to composite.
    static const QByteArray kBorderVertexSource = QByteArrayLiteral(
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
        // No Y-flip: pass the texcoord through so `texture(uTexture0, vTexCoord)`
        // samples the redirected window content upright (KWin's OffscreenData
        // composites its bottom-origin FBO so the natural texcoord renders the
        // window the right way up). vTexCoord is therefore Y-UP; the fragment
        // flips Y itself when it reconstructs the device pixel for the rounded-
        // rect SDF, so the SDF lines up with the top-down frameTopLeft the C++
        // side computes — this keeps asymmetric vertical shadow margins correct.
        "    vTexCoord = texCoord;\n"
        "    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);\n"
        "}\n");

    static const QByteArray kBorderFragmentSource = QByteArrayLiteral(
        "#version 450\n"
        "\n"
        "uniform sampler2D uTexture0;\n"
        "uniform vec2 windowExpandedSize;  // redirected/expanded texture size, device px\n"
        "uniform vec2 frameTopLeft;        // window frame top-left within the FBO, top-down device px\n"
        "uniform vec2 frameSize;           // window frame size (excludes shadows), device px\n"
        "uniform float radius;             // OUTER corner radius (border radius + width) * scale\n"
        "uniform float thickness;          // outline band width = border width * scale\n"
        "uniform vec4 outlineColor;        // straight (non-premultiplied) RGBA border colour\n"
        "\n"
        "in vec2 vTexCoord;\n"
        "out vec4 fragColor;\n"
        "\n"
        "void main() {\n"
        "    vec4 tex = texture(uTexture0, vTexCoord);\n"
        "\n"
        "    // Fragment's TOP-DOWN device pixel within the expanded (shadow-padded)\n"
        "    // redirected texture; the frame rect sits at frameTopLeft..+frameSize.\n"
        "    vec2  p       = vec2(vTexCoord.x, 1.0 - vTexCoord.y) * windowExpandedSize;\n"
        "    vec2  halfSz  = 0.5 * frameSize;\n"
        "    vec2  cen     = frameTopLeft + halfSz;\n"
        "    float r       = clamp(radius, 0.0, min(halfSz.x, halfSz.y));\n"
        "\n"
        "    // Analytic rounded-rect SDF over the frame (Inigo-Quilez); < 0 inside.\n"
        "    // This is the single field that both rounds the corners and places the\n"
        "    // outline, IDENTICALLY for decorated and borderless windows — the\n"
        "    // redirected texture includes the server-side decoration, so a visible\n"
        "    // titlebar's corners round too.\n"
        "    vec2  q  = abs(p - cen) - halfSz + r;\n"
        "    float d  = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;\n"
        "    float aa = max(fwidth(d), 1e-3);\n"
        "\n"
        "    // Axis-aligned frame-box distance; < 0 inside the frame rect. The drop\n"
        "    // shadow lives OUTSIDE this box (in the expanded margin), so gating the\n"
        "    // corner cut on it leaves the shadow untouched — only the corner overhang\n"
        "    // INSIDE the frame box (between the square corner and the rounded arc) is\n"
        "    // removed. Interior subsurfaces are never individually clipped.\n"
        "    float boxD       = max(abs(p.x - cen.x) - halfSz.x, abs(p.y - cen.y) - halfSz.y);\n"
        "    float inFrameBox = 1.0 - smoothstep(-aa, aa, boxD);\n"
        "    float inRound    = 1.0 - smoothstep(-aa, aa, d);\n"
        "    // Cut = the corner overhang ONLY: the region inside the square frame box\n"
        "    // yet outside the rounded rect. (inFrameBox - inRound) is exactly that\n"
        "    // difference and, crucially, is 0 along STRAIGHT edges (there boxD == d so\n"
        "    // the two masks cancel) — so straight window edges keep full content with\n"
        "    // no 1px transparency seam. A product like inFrameBox*(1-inRound) does NOT\n"
        "    // cancel at the edges and leaves that seam.\n"
        "    float cut = clamp(inFrameBox - inRound, 0.0, 1.0);\n"
        "\n"
        "    // Outline: an inner band of `thickness` just inside the rounded edge,\n"
        "    // from the SAME SDF (d in [-thickness, 0]). Confined to the window body\n"
        "    // so it never paints onto the shadow margin.\n"
        "    float band = inFrameBox * inRound * smoothstep(-thickness - aa, -thickness + aa, d);\n"
        "\n"
        "    // PREMULTIPLIED-ALPHA compositing. uTexture0 is premultiplied and KWin\n"
        "    // composites our output premultiplied, so the corner clip must scale rgb\n"
        "    // AND alpha together — scaling alpha alone leaves full-bright rgb over a\n"
        "    // falling alpha, i.e. a bright halo along the rounded corners/edges.\n"
        "    vec4 content = tex * (1.0 - cut);                  // clipped content, stays premultiplied\n"
        "    float oa = band * outlineColor.a;                 // outline coverage * its own alpha\n"
        "    vec4 outlineSrc = vec4(outlineColor.rgb * oa, oa); // premultiplied 'over' source\n"
        "    fragColor = outlineSrc + content * (1.0 - oa);\n"
        "}\n");

    if (!KWin::effects) {
        return nullptr;
    }
    auto shader = KWin::ShaderManager::instance()->generateCustomShader(KWin::ShaderTrait::MapTexture,
                                                                        kBorderVertexSource, kBorderFragmentSource);
    if (!shader || !shader->isValid()) {
        qCWarning(lcEffect) << "Failed to compile PlasmaZones border shader — borders disabled this session";
        m_borderShaderCompileFailed = true;
        return nullptr;
    }
    m_borderUWindowExpandedSizeLoc = shader->uniformLocation("windowExpandedSize");
    m_borderUFrameTopLeftLoc = shader->uniformLocation("frameTopLeft");
    m_borderUFrameSizeLoc = shader->uniformLocation("frameSize");
    m_borderURadiusLoc = shader->uniformLocation("radius");
    m_borderUThicknessLoc = shader->uniformLocation("thickness");
    m_borderUOutlineColorLoc = shader->uniformLocation("outlineColor");
    m_borderShader = std::move(shader);
    return m_borderShader.get();
}

void PlasmaZonesEffect::reconcileBorderShader(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    auto it = m_windowBorders.find(windowId);
    const bool wantsBorder = (it != m_windowBorders.end()) && it->width > 0 && it->color.isValid();

    // An in-flight animation transition owns the shader slot — the transition's
    // begin already called setShader(animationShader) and redirect(). Leave it
    // alone: the transition-end path re-applies the border shader (and keeps the
    // redirect) when the border still exists. Just clear our ownership flag so
    // the per-frame push and removeWindowBorder defer to the transition.
    if (m_shaderManager.findTransition(w)) {
        if (it != m_windowBorders.end()) {
            it->shaderApplied = false;
        }
        return;
    }

    if (wantsBorder) {
        // Compile-on-first-use; a failed compile latches and no-ops.
        KWin::GLShader* shader = borderShader();
        if (!shader) {
            // No border shader available (compile failed/latched). Don't leave the
            // window stuck redirected with a stale shader bound: a transition that
            // just ended hands the slot back here still redirected (its setShader
            // remains until we clear it), so tear the redirect down rather than
            // blit a dead shader forever. setShader(nullptr)/unredirect are no-ops
            // when the window was never redirected.
            setShader(w, nullptr);
            unredirect(w);
            it->shaderApplied = false;
            return;
        }
        // redirect() is idempotent for an already-redirected window; setShader()
        // replaces any prior pointer. Re-applying the same shader is a no-op.
        redirect(w);
        setShader(w, shader);
        it->shaderApplied = true;
    } else if (it != m_windowBorders.end() && it->shaderApplied) {
        // Border removed but we still own the slot and no transition raced in.
        setShader(w, nullptr);
        unredirect(w);
        it->shaderApplied = false;
    }
}

void PlasmaZonesEffect::pushBorderUniforms(KWin::EffectWindow* w, const WindowBorder& border, qreal scale)
{
    // drawWindow (the sole caller) has already resolved @p border, confirmed it
    // is applied, ruled out a transition owning the slot, and bound the shader,
    // so this just computes and writes the uniforms onto the bound program.
    KWin::GLShader* shader = m_borderShader.get();

    // The shader evaluates a rounded-rect SDF over the window FRAME to round the
    // corners + draw the outline. It needs the expanded (redirected) texture size
    // for the top-down pixel reconstruction plus the frame rect placed within that
    // texture, the outer corner radius, and the band thickness — all device px.
    // windowExpandedSize is the redirected FBO extent in device px; expandedGeometry
    // covers frame + drop shadow (falls back to frame when empty, e.g. a shadowless
    // window). frameTopLeft = (frameGeometry.topLeft - expandedGeometry.topLeft) *
    // scale is the frame's offset inside that texture (top-down device px), and
    // frameSize = frameGeometry.size * scale its extent.
    const QRectF frame = w->frameGeometry();
    QRectF expanded = w->expandedGeometry();
    if (expanded.isEmpty()) {
        expanded = frame;
    }
    const QVector2D windowExpandedSize(static_cast<float>(expanded.width() * scale),
                                       static_cast<float>(expanded.height() * scale));
    const QVector2D frameTopLeft(static_cast<float>((frame.left() - expanded.left()) * scale),
                                 static_cast<float>((frame.top() - expanded.top()) * scale));
    const QVector2D frameSize(static_cast<float>(frame.width() * scale), static_cast<float>(frame.height() * scale));
    const float thickness = static_cast<float>(border.width * scale);
    // OUTER radius = content radius + border width, so the outline band sits inside
    // it and the content corner ends one band-width in, at `border.radius`.
    const float radius = static_cast<float>((border.radius + border.width) * scale);

    const QColor& c = border.color;
    const QVector4D outlineColor(static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                                 static_cast<float>(c.blueF()), static_cast<float>(c.alphaF()));

    // The caller binds the border shader (a KWin::ShaderBinder kept in scope
    // through the subsequent effects->drawWindow) — setUniform writes to the
    // currently bound program, so we must NOT bind/unbind here or the uniforms
    // would be set on the wrong (or no) program.
    if (m_borderUWindowExpandedSizeLoc >= 0) {
        shader->setUniform(m_borderUWindowExpandedSizeLoc, windowExpandedSize);
    }
    if (m_borderUFrameTopLeftLoc >= 0) {
        shader->setUniform(m_borderUFrameTopLeftLoc, frameTopLeft);
    }
    if (m_borderUFrameSizeLoc >= 0) {
        shader->setUniform(m_borderUFrameSizeLoc, frameSize);
    }
    if (m_borderURadiusLoc >= 0) {
        shader->setUniform(m_borderURadiusLoc, radius);
    }
    if (m_borderUThicknessLoc >= 0) {
        shader->setUniform(m_borderUThicknessLoc, thickness);
    }
    if (m_borderUOutlineColorLoc >= 0) {
        shader->setUniform(m_borderUOutlineColorLoc, outlineColor);
    }
}

void PlasmaZonesEffect::drawWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                   KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                   KWin::WindowPaintData& data)
{
    // Apply the border shader passively for static bordered windows: bind it +
    // set its uniforms, then let OffscreenEffect::drawWindow re-blit the
    // redirected FBO through it. OffscreenData::paint re-binds the SAME program
    // (the one from setShader in reconcileBorderShader), and uniform values
    // persist in the program object across our ShaderBinder pop — so the values
    // we set here are live for that blit. This is the KDE-Rounded-Corners model:
    // the shader applies on every composite, idle included, with no re-render,
    // and no forced per-frame repaints. Skip during a transition (the animation
    // shader owns the setShader slot and paintWindow's transition branch drives
    // it) and during snapshot capture.
    if (!m_capturingSnapshot && !m_windowBorders.isEmpty() && m_borderShader && !m_shaderManager.findTransition(w)) {
        const auto bit = m_windowBorders.constFind(getWindowId(w));
        if (bit != m_windowBorders.constEnd() && bit->shaderApplied) {
            KWin::ShaderBinder binder(m_borderShader.get());
            pushBorderUniforms(w, *bit, viewport.scale());
        }
    }
    KWin::OffscreenEffect::drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
}

} // namespace PlasmaZones
