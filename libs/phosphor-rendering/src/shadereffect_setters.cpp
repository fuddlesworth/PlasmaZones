// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// ShaderEffect's property setters, split out of shadereffect.cpp by concern
// (that file was past the size ceiling): the PR_VEC4/PR_COLOR DRY macros and
// the shadertoy, source/buffer, custom-parameter, custom-color and
// preamble/scaffold/include-path setter families. Construction, texture
// loading, status management, node sync and scene-graph integration stay in
// shadereffect.cpp — matching the shadernoderhi{core,setters,uniforms}.cpp
// partitioning in this library.

#include <PhosphorRendering/ShaderEffect.h>

#include "internal.h"

#include <QElapsedTimer>
#include <QQuickWindow>

namespace PhosphorRendering {

// ============================================================================
// DRY helpers
// ============================================================================

// Setter macro for vec4 custom params (index into array)
#define PR_VEC4_SETTER(Name, idx)                                                                                      \
    void ShaderEffect::set##Name(const QVector4D& params)                                                              \
    {                                                                                                                  \
        if (m_customParams[idx] == params)                                                                             \
            return;                                                                                                    \
        m_customParams[idx] = params;                                                                                  \
        Q_EMIT customParamsChanged();                                                                                  \
        update();                                                                                                      \
    }

// Setter macro for QColor custom colors (index into array)
#define PR_COLOR_SETTER(Name, idx)                                                                                     \
    void ShaderEffect::set##Name(const QColor& color)                                                                  \
    {                                                                                                                  \
        if (m_customColors[idx] == color)                                                                              \
            return;                                                                                                    \
        m_customColors[idx] = color;                                                                                   \
        Q_EMIT customColorsChanged();                                                                                  \
        update();                                                                                                      \
    }

// ============================================================================
// Shadertoy Uniform Setters
// ============================================================================

void ShaderEffect::setITime(qreal time)
{
    // Relative comparison: a fixed 1e-9 absolute epsilon falls below ULP for
    // m_iTime once it grows past ~1, so animation would silently freeze after
    // a short runtime. Compare via qFuzzyCompare with +1.0 offset to handle
    // both near-zero and large values uniformly.
    if (qFuzzyCompare(m_iTime + 1.0, time + 1.0)) {
        return;
    }
    m_iTime = time;
    Q_EMIT iTimeChanged();
    update();
}

void ShaderEffect::setITimeDelta(qreal delta)
{
    if (qFuzzyCompare(m_iTimeDelta + 1.0, delta + 1.0)) {
        return;
    }
    m_iTimeDelta = delta;
    Q_EMIT iTimeDeltaChanged();
    update();
}

void ShaderEffect::setIFrame(int frame)
{
    if (m_iFrame == frame) {
        return;
    }
    m_iFrame = frame;
    Q_EMIT iFrameChanged();
    update();
}

void ShaderEffect::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    if (m_playing) {
        // Reset the wall-clock baseline so the next tick produces a sensible
        // delta (not a several-second jump from the time the property was
        // last toggled). iTime is *not* reset — toggling playing off and on
        // resumes from whatever iTime value the shader was at.
        m_playingLastFrameSeconds = 0.0;
    }
    updatePlayingConnection();
    Q_EMIT playingChanged();
}

void ShaderEffect::updatePlayingConnection()
{
    // Always tear down the previous connection before deciding whether to
    // re-establish it. itemChange (window changes) and setPlaying both call
    // through here; tearing down unconditionally avoids leaking a stale
    // connection to a previous window.
    QObject::disconnect(m_playingConnection);
    m_playingConnection = {};
    if (!m_playing) {
        return;
    }
    QQuickWindow* w = window();
    if (!w) {
        // Item not parented to a window yet. itemChange(ItemSceneChange)
        // will re-call us when the item gets a window.
        return;
    }
    // afterAnimating fires once per frame on the GUI thread, immediately
    // before the render thread is asked to synchronize. That's the right
    // place to advance per-frame uniforms — the values we set here land in
    // the next sync without thread-marshalling. afterFrameEnd would fire on
    // the render thread under Qt's threaded render loop, and emitting our
    // *Changed signals from there could trigger QML JS bindings on the
    // wrong thread (V4 is GUI-thread-only).
    m_playingConnection = QObject::connect(w, &QQuickWindow::afterAnimating, this, &ShaderEffect::onPlayingTick);
    // Kick a first frame so the shader paints the new state immediately
    // (otherwise it'd wait until something else dirties the scene).
    update();
}

void ShaderEffect::onPlayingTick()
{
    if (!m_playing) {
        return;
    }
    // QElapsedTimer wall-clock seconds — monotonic, immune to NTP jumps.
    // Static across this TU because nsecsElapsed() needs a fixed start
    // anchor; the monotonic value is read into a per-instance baseline
    // (m_playingLastFrameSeconds) so each instance's delta is fully
    // independent of every other.
    static QElapsedTimer s_clock;
    if (!s_clock.isValid()) {
        s_clock.start();
    }
    const qreal now = s_clock.nsecsElapsed() * 1e-9;

    // Skip the per-frame property pump for invisible / off-screen /
    // zero-sized items, and for shaders that aren't ready (compile
    // failure, still loading, no source set). afterAnimating fires on
    // EVERY frame of the host window, so without these gates every
    // playing ShaderEffect on the window pays setITime / setITimeDelta /
    // setIFrame / 3×update cost regardless. The visual side-effect of
    // skipping is that animation appears frozen — desired behaviour.
    //
    // Crucially, update m_playingLastFrameSeconds even on the skip path
    // so the next visible tick computes a SMALL delta (the time between
    // two consecutive frames) instead of a huge one (the time since the
    // item was last visible — which would produce a giant iTime jump
    // and a visible animation skip on re-show).
    if (!isVisible() || width() <= 0 || height() <= 0 || m_status.load(std::memory_order_acquire) != Status::Ready) {
        m_playingLastFrameSeconds = now;
        return;
    }

    const qreal delta = (m_playingLastFrameSeconds > 0.0) ? (now - m_playingLastFrameSeconds) : 0.0;
    m_playingLastFrameSeconds = now;

    // Increment iTime by the frame delta rather than assigning `now`
    // directly so toggling `playing` off and on doesn't produce a giant
    // visual jump — iTime is the shader's animation clock, not wall time.
    setITime(m_iTime + delta);
    setITimeDelta(delta);
    setIFrame(m_iFrame + 1);
    // setITime/setITimeDelta/setIFrame each call update(); the scene graph
    // coalesces multiple update() requests on the same frame so this is
    // cheap.
}

void ShaderEffect::setIsReversed(bool reverse)
{
    if (m_isReversed == reverse) {
        return;
    }
    m_isReversed = reverse;
    // Exposed as a Q_PROPERTY (isReversed) for QML-binding parity with the
    // rest of the animation-state setters. SurfaceAnimator still pushes this
    // imperatively at each leg attach; the property + signal close the
    // asymmetry without changing the imperative call site.
    Q_EMIT isReversedChanged();
    update();
}

void ShaderEffect::setIResolution(const QSizeF& resolution)
{
    if (m_iResolution == resolution) {
        return;
    }
    m_iResolution = resolution;
    Q_EMIT iResolutionChanged();
    update();
}

void ShaderEffect::setIMouse(const QPointF& mouse)
{
    if (m_iMouse == mouse) {
        return;
    }
    m_iMouse = mouse;
    Q_EMIT iMouseChanged();
    update();
}

// ============================================================================
// Shader Source / Buffer Setters
// ============================================================================

void ShaderEffect::setShaderSource(const QUrl& source)
{
    if (m_shaderSource == source) {
        return;
    }
    if (!isLocalShaderUrl(source)) {
        // Full refusal: no state changes. Setting Status::Error here while
        // the previously-baked shader keeps rendering would make status,
        // the property value, and the on-screen output disagree three ways
        // (and a later reloadShader() would flip back to Ready, erasing the
        // record). A refused write leaves everything as it was; the warning
        // is the report.
        qCWarning(lcShaderNode) << "setShaderSource: unsupported URL scheme" << source.scheme()
                                << "— only file:// and qrc: are accepted; keeping" << m_shaderSource;
        return;
    }
    m_shaderSource = source;
    m_shaderDirty = true;
    setStatus(Status::Loading);
    Q_EMIT shaderSourceChanged();
    update();
}

void ShaderEffect::setVertexShaderUrl(const QUrl& source)
{
    if (m_vertexShaderUrl == source) {
        return;
    }
    if (!isLocalShaderUrl(source)) {
        // Full refusal, same rationale as setShaderSource: a refused write
        // must leave status, property value, and rendered output agreeing.
        qCWarning(lcShaderNode) << "setVertexShaderUrl: unsupported URL scheme" << source.scheme()
                                << "— only file:// and qrc: are accepted; keeping" << m_vertexShaderUrl;
        return;
    }
    m_vertexShaderUrl = source;
    m_shaderDirty = true;
    if (source.isValid() && !source.isEmpty()) {
        setStatus(Status::Loading);
    }
    Q_EMIT vertexShaderUrlChanged();
    update();
}

void ShaderEffect::setSourceItem(QQuickItem* item)
{
    if (m_sourceItem.data() == item) {
        return;
    }
    // Self-reference (sampling literally `this`) is rejected; ANCESTOR
    // sampling is supported and load-bearing (SurfaceAnimator parents
    // shaderItem under shaderAnchor for coord-system mapping, then calls
    // setSourceItem(shaderAnchor) so the anchor's layer texture binds to
    // uTexture0). Qt's layer system uses a back-buffer so sampling an
    // ancestor reads last-frame's content — no infinite recursion. An
    // earlier ancestor-walk guard here silently broke every shader leg.
    if (item == this) {
        if (!m_warnedSelfSourceItem) {
            qCWarning(lcShaderNode) << "setSourceItem: refused — candidate is `this`; cannot sample own output.";
            m_warnedSelfSourceItem = true;
        }
        return;
    }
    m_sourceItem = item;
    if (item) {
        // Force `layer.enabled = true` so the QQuickItem becomes a
        // texture provider. The naive single-step
        // `item->setProperty("layer.enabled", true)` doesn't work —
        // Qt's meta-object system doesn't auto-resolve nested property
        // paths; the call sets a brand new dynamic property called
        // "layer.enabled" on the item without ever touching
        // QQuickItemLayer. Diagnostic logging confirmed this:
        // `isTextureProvider()` stayed false immediately after a
        // setProperty call that "succeeded".
        //
        // The two-step access via `item->property("layer")` resolves
        // the QQuickItemLayer sub-object (a QObject in its own right)
        // and `layer->setProperty("enabled", true)` flips the real
        // backing flag, which synchronously triggers QSGLayer creation
        // and makes `isTextureProvider()` return true. Already-true is
        // idempotent — Qt's layer property setter early-returns on
        // unchanged values.
        //
        // We don't restore the previous value on unset because we
        // can't know whether the consumer wanted layer for other
        // reasons; callers that need symmetric teardown should track
        // and reset `layer.enabled` themselves.
        if (!item->isTextureProvider()) {
            QObject* layer = item->property("layer").value<QObject*>();
            if (layer) {
                layer->setProperty("enabled", true);
            }
        }
    }
    // No m_shaderDirty here. The SRB rebind is already covered:
    // updatePaintNode() pushes the new provider via
    // setSourceTextureProvider() which calls resetAllBindingsAndPipelines()
    // when the pointer changes. Forcing a full shader recompile every
    // sourceItem swap (the previous behaviour) was wasted work — the
    // baked QShader doesn't depend on which texture is bound.
    Q_EMIT sourceItemChanged();
    update();
}

// ============================================================================
// Custom Parameters (DRY macro)
// ============================================================================

PR_VEC4_SETTER(CustomParams1, 0)
PR_VEC4_SETTER(CustomParams2, 1)
PR_VEC4_SETTER(CustomParams3, 2)
PR_VEC4_SETTER(CustomParams4, 3)
PR_VEC4_SETTER(CustomParams5, 4)
PR_VEC4_SETTER(CustomParams6, 5)
PR_VEC4_SETTER(CustomParams7, 6)
PR_VEC4_SETTER(CustomParams8, 7)

// ============================================================================
// Custom Colors (DRY macro)
// ============================================================================

PR_COLOR_SETTER(CustomColor1, 0)
PR_COLOR_SETTER(CustomColor2, 1)
PR_COLOR_SETTER(CustomColor3, 2)
PR_COLOR_SETTER(CustomColor4, 3)
PR_COLOR_SETTER(CustomColor5, 4)
PR_COLOR_SETTER(CustomColor6, 5)
PR_COLOR_SETTER(CustomColor7, 6)
PR_COLOR_SETTER(CustomColor8, 7)
PR_COLOR_SETTER(CustomColor9, 8)
PR_COLOR_SETTER(CustomColor10, 9)
PR_COLOR_SETTER(CustomColor11, 10)
PR_COLOR_SETTER(CustomColor12, 11)
PR_COLOR_SETTER(CustomColor13, 12)
PR_COLOR_SETTER(CustomColor14, 13)
PR_COLOR_SETTER(CustomColor15, 14)
PR_COLOR_SETTER(CustomColor16, 15)

#undef PR_VEC4_SETTER
#undef PR_COLOR_SETTER

// ============================================================================
// Shader Include Paths
// ============================================================================

void ShaderEffect::setParamPreamble(const QString& preamble)
{
    if (m_paramPreamble == preamble) {
        return;
    }
    m_paramPreamble = preamble;
    Q_EMIT paramPreambleChanged();
    // Same reload requirement as setShaderIncludePaths: the preamble is spliced
    // inside the node's loadFragmentShader and the expanded+spliced source is
    // cached, so a pure re-bake would carry the OLD defines. Force a full
    // reload (not just a dirty flag) so the node re-splices. Inlined rather
    // than calling reloadShader() to keep a statusChanged binding from looping
    // back through this setter.
    if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
        setStatus(Status::Loading);
    }
    m_shaderDirty = true;
    update();
}

void ShaderEffect::setEntryScaffold(const QString& prologue, const QList<PhosphorShaders::EntryCandidate>& candidates)
{
    if (m_entryPrologue == prologue && m_entryCandidates == candidates) {
        return;
    }
    m_entryPrologue = prologue;
    m_entryCandidates = candidates;
    // Same reload requirement as setParamPreamble: the scaffold is applied
    // inside the node's loadFragmentShader and the assembled+expanded source is
    // cached, so a pure re-bake would carry the OLD scaffold. Force a full
    // reload; inlined (not reloadShader()) to keep a statusChanged binding from
    // looping back through this setter.
    if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
        setStatus(Status::Loading);
    }
    m_shaderDirty = true;
    update();
}

void ShaderEffect::setShaderIncludePaths(const QStringList& paths)
{
    if (m_shaderIncludePaths == paths) {
        return;
    }
    m_shaderIncludePaths = paths;
    // Marking dirty alone is not enough: include expansion happens inside
    // loadFragmentShader()/loadVertexShader() and the expanded source is
    // cached on the node. A pure re-bake of the cached source would still
    // carry the OLD include contents. Inline the two-line reload instead of
    // calling reloadShader() so a QML binding on statusChanged can't loop
    // back through this setter.
    if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
        setStatus(Status::Loading);
    }
    m_shaderDirty = true;
    update();
}

} // namespace PhosphorRendering
