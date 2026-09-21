// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "daemon/overlayservice.h"
#include "qml_property_names.h"
#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <QQuickItem>
#include <PhosphorSurfaces/SurfaceManager.h>
#include "core/platform/logging.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/utils/utils.h"
#include "core/interfaces/shaderregistry.h"
#include "daemon/rendering/zonelabeltexturebuilder.h"
#include "phosphor_roles.h"

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/Surface.h>
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QImage>
#include <QGuiApplication>
#include <QPalette>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Support Methods
// ═══════════════════════════════════════════════════════════════════════════════

bool OverlayService::canUseShaders() const
{
#ifdef PLASMAZONES_SHADERS_ENABLED
    return m_shaderRegistry && m_shaderRegistry->shadersEnabled();
#else
    return false;
#endif
}

bool OverlayService::useShaderForScreen(QScreen* screen) const
{
    if (!screen) {
        return false;
    }
    // Resolve to virtual screen ID when the physical screen has subdivisions,
    // so shader-type checks use the correct per-virtual-screen layout.
    const QString physId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
    auto* mgr = m_screenManager;
    if (mgr && mgr->hasVirtualScreens(physId)) {
        // Check all virtual screens - if any uses a shader, return true.
        // This is used by initializeOverlay which creates per-virtual-screen windows.
        const QStringList vsIds = mgr->virtualScreenIdsFor(physId);
        for (const QString& vsId : vsIds) {
            if (useShaderForScreen(vsId)) {
                return true;
            }
        }
        return false;
    }
    return useShaderForScreen(physId);
}

void OverlayService::setPresetRegistry(PhosphorShaders::ShaderPresetRegistry* registry)
{
    if (m_presetRegistry == registry) {
        return;
    }
    // Disconnect from the outgoing registry before the borrow is overwritten, or a
    // re-set would leave a second connection behind. The daemon nulls this borrow
    // before tearing the store down, so the old pointer is still alive here.
    //
    // The precise handle, not `disconnect(registry, nullptr, this, nullptr)`: the
    // blanket form severs every slot this object has on that sender, which is safe
    // only while there is exactly one. This class keeps handles for exactly that
    // reason (see m_shadersChangedConnection). Note setSurfaceShaderRegistry still
    // uses the blanket form — correct there today because this object makes exactly
    // one connection to that sender, but it is the counter-example, not the model.
    if (m_presetsChangedConnection) {
        disconnect(m_presetsChangedConnection);
        m_presetsChangedConnection = {};
    }
    m_presetRegistry = registry;
    if (!m_presetRegistry) {
        return;
    }

    // A retuned preset changes the PARAMETERS of assignments already on screen,
    // never which pack they use, so a plain refresh is enough — no
    // recreate-on-type-mismatch, which only matters when an assignment flips
    // between rectangle and shader overlays. refreshVisibleWindows re-runs
    // updateOverlayWindow, which re-resolves through effectiveOverlayShader and
    // pushes the new uniforms.
    //
    // Not filtered on the PACK the signal names: an overlay window resolves its
    // pack per screen through the layout, so deciding whether any visible window
    // uses that pack costs the same walk the refresh does. It IS filtered on the
    // FAMILY, which the signal also carries and which is free to test — and each
    // family needs a different arm anyway, because each bakes its parameters
    // somewhere else.
    m_presetsChangedConnection = connect(m_presetRegistry, &PhosphorShaders::ShaderPresetRegistry::presetsChanged, this,
                                         [this](PhosphorShaders::ShaderFamily family, const QString&) {
                                             switch (family) {
                                             case PhosphorShaders::ShaderFamily::Overlay:
                                                 refreshVisibleWindows();
                                                 break;
                                             case PhosphorShaders::ShaderFamily::Animation:
                                                 // The flattened parameters are baked into SurfaceAnimator's
                                                 // per-role Config, which is only rebuilt here and on a
                                                 // shaderProfileTree edit — so without this arm a retuned
                                                 // animation preset never reached the OSD and popup show/hide
                                                 // legs until the user happened to edit the tree. "The next
                                                 // show re-resolves" is true of the effect, not of here.
                                                 if (m_settings) {
                                                     applyShaderProfilesToAnimator(m_settings->shaderProfileTree());
                                                 }
                                                 break;
                                             case PhosphorShaders::ShaderFamily::Surface:
                                                 // Mirror of the decorationProfileTreeChanged arm in
                                                 // setSettings: a visible popup's decoration chain is
                                                 // resolved at show time, so a retune has to be pushed into
                                                 // the slots that are already up. OSDs are omitted for the
                                                 // same reason they are there — they auto-dismiss sub-second.
                                                 reapplyVisiblePopupDecorations();
                                                 break;
                                             case PhosphorShaders::ShaderFamily::Pointer:
                                                 // The daemon does not render the cursor chain; the
                                                 // compositor owns it and has its own arm.
                                                 break;
                                             }
                                         });

    // APPLY ONCE at set time as well as on every later change, because on an init()
    // re-run nothing else would. The animator's per-role Config holds parameters
    // flattened against whichever registry was live when it was last built, and the
    // only other thing that rebuilds it is `setSettings` — whose entire body sits
    // behind `if (m_settings != settings)`. m_settings is ctor-owned and stop() never
    // resets it, so the second init passes the same pointer and the whole block is
    // skipped. The result was not a dangling pointer (Config holds value copies) but
    // stale TUNING: the OSD and popup show/hide legs kept the values flattened against
    // the destroyed store until a presetsChanged or a tree edit happened to arrive.
    if (m_settings) {
        applyShaderProfilesToAnimator(m_settings->shaderProfileTree());
    }
}

OverlayService::OverlaySource
OverlayService::overlaySourceFor(const PhosphorZones::ContextOverlayOverride& overlayOverride,
                                 const PhosphorZones::Layout* screenLayout)
{
    // THE precedence, stated once. Both effectiveOverlayShaderId and
    // effectiveOverlayShader switch on this rather than each re-deriving the ladder:
    // they used to, and the comment on one of them named the drift hazard without
    // removing it.
    //
    // A rule override wins outright — the registry has already picked the rule for this
    // layout's node in resolveContextOverlay, so by the time either function runs the
    // override IS the answer, and an engaged EMPTY id is the rule's "no shader"
    // sentinel rather than an absence. With no rule and no layout there is nothing to
    // resolve against; otherwise the cached tree answers.
    if (overlayOverride.shaderId) {
        return OverlaySource::Rule;
    }
    return screenLayout ? OverlaySource::Tree : OverlaySource::None;
}

QString OverlayService::effectiveOverlayShaderId(const PhosphorZones::ContextOverlayOverride& overlayOverride,
                                                 const PhosphorZones::Layout* screenLayout) const
{
    // The id half of effectiveOverlayShader, for callers that only ask "is a
    // shader in play here". Deliberately skips the preset flatten: a preset
    // changes an assignment's PARAMETERS and never which pack it uses, so the
    // answer is identical and the deep copy is not paid.
    switch (overlaySourceFor(overlayOverride, screenLayout)) {
    case OverlaySource::Rule:
        return *overlayOverride.shaderId;
    case OverlaySource::None:
        return {};
    case OverlaySource::Tree:
        break;
    }
    // The enum says a layout answers; it does not CARRY the pointer, so the Tree case
    // re-checks rather than trusting a contract stated two functions away. A future
    // reorder of that ladder then fails loudly here instead of dereferencing null.
    Q_ASSERT(screenLayout);
    if (!screenLayout) {
        return {};
    }
    return m_overlayShaderTree.resolve(screenLayout->id().toString()).shaderId;
}

OverlayShaderProfile
OverlayService::effectiveOverlayShader(const PhosphorZones::ContextOverlayOverride& overlayOverride,
                                       const PhosphorZones::Layout* screenLayout) const
{
    // Rule override wins both id and params: an engaged rule id with no params means
    // "that shader at its defaults", never "that shader with the tree's params" (see
    // the pre-tree semantics this preserves). WHICH source wins is overlaySourceFor's
    // single statement of the precedence; what each source means for the PARAMS is this
    // function's own business, which is why the two are separate.
    switch (overlaySourceFor(overlayOverride, screenLayout)) {
    case OverlaySource::Rule: {
        OverlayShaderProfile ruleProfile{*overlayOverride.shaderId, overlayOverride.shaderParams};
        ruleProfile.presetId = overlayOverride.shaderPresetId;
        // Flattened through the same function as the tree node below, so the rule
        // route and the tree route cannot drift on what a preset plus its deltas
        // means. That function lives beside the profile type, not here: the
        // flatten is a property of the profile rather than of this service.
        return m_presetRegistry ? withPresetsResolved(ruleProfile, *m_presetRegistry) : ruleProfile;
    }
    case OverlaySource::None:
        return {};
    case OverlaySource::Tree:
        break;
    }
    // Re-checked here too, for the reason the id twin gives: the enum value does not
    // carry the pointer.
    Q_ASSERT(screenLayout);
    if (!screenLayout) {
        return {};
    }
    // m_overlayShaderTree is the cached settings tree (see the member doc);
    // reading through ISettings here would re-parse the store per call. No
    // m_settings check: this reads the cache and never the interface, and
    // setSettings clears the cache when settings detach, so a detached service
    // resolves through an empty tree to the same empty profile a null check
    // would have returned.
    const OverlayShaderProfile profile = m_overlayShaderTree.resolve(screenLayout->id().toString());
    // Flatten the preset here, once, rather than at each consumer: the returned
    // profile's `parameters` are the EFFECTIVE tuning (the preset overlaid with
    // this assignment's own edits) and `presetId` is cleared to say the preset
    // has already been applied. Callers therefore never have to know a preset
    // was involved — which is what keeps `useShaderForScreen` and the two
    // window-update sites from each needing their own resolve step.
    //
    // With no preset registry injected, or a presetId naming no preset, this
    // leaves `parameters` exactly as stored.
    return m_presetRegistry ? withPresetsResolved(profile, *m_presetRegistry) : profile;
}

bool OverlayService::anyScreenUsesShader() const
{
    if (!canUseShaders()) {
        return false;
    }
    for (auto it = m_screenStates.cbegin(); it != m_screenStates.cend(); ++it) {
        if (useShaderForScreen(it.key())) {
            return true;
        }
    }
    return false;
}

bool OverlayService::useShaderForScreen(const QString& screenId) const
{
    if (!canUseShaders()) {
        return false;
    }
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);
    if (!screenLayout) {
        return false;
    }
    // A context overlay rule may override the resolved shader / style for
    // this (screen, desktop, activity). Resolve once and apply over the tree.
    const PhosphorZones::ContextOverlayOverride overlayOverride = overlayOverrideForScreen(m_layoutManager, screenId);
    // Id only. This is a per-frame path while an overlay is up — the audio
    // spectrum drives it per screen per frame at the configured shader frame
    // rate — and the full resolve deep-copies the preset's parameter map
    // (overlayPresetDeltas detaches the QMap on its first insert) to produce
    // values this function then discards.
    const QString effectiveShaderId = effectiveOverlayShaderId(overlayOverride, screenLayout);
    if (ShaderRegistry::isNoneShader(effectiveShaderId)) {
        return false;
    }

    // LayoutPreview mode requires standard QML overlay (ZonePreview can't be rendered in GLSL).
    // If any zone resolves to LayoutPreview mode, fall back to standard overlay for this screen.
    // A context rule's style override slots between the per-zone override and the
    // layout value: zone > rule > layout > global.
    int globalMode = m_settings ? static_cast<int>(m_settings->overlayDisplayMode()) : 0;
    int layoutMode = screenLayout->overlayDisplayMode();
    for (const auto* zone : screenLayout->zones()) {
        int resolved = zone->overlayDisplayMode() >= 0 ? zone->overlayDisplayMode()
            : overlayOverride.style                    ? *overlayOverride.style
                                                       : (layoutMode >= 0 ? layoutMode : globalMode);
        if (resolved == 1) { // OverlayDisplayMode::LayoutPreview
            return false;
        }
    }

    return m_shaderRegistry && m_shaderRegistry->shader(effectiveShaderId).isValid();
}

void OverlayService::startShaderAnimation()
{
    if (!m_shaderUpdateTimer) {
        m_shaderUpdateTimer = new QTimer(this);
        m_shaderUpdateTimer->setTimerType(Qt::PreciseTimer);
        connect(m_shaderUpdateTimer, &QTimer::timeout, this, &OverlayService::updateShaderUniforms);
    }

    // Get frame rate from settings (schema already clamps; this re-clamp only
    // guards the null-settings fallback). Bounds via ConfigDefaults so a
    // future range change cannot silently diverge from the schema's clamp.
    const int frameRate = qBound(ConfigDefaults::shaderFrameRateMin(),
                                 m_settings ? m_settings->shaderFrameRate() : ConfigDefaults::shaderFrameRate(),
                                 ConfigDefaults::shaderFrameRateMax());
    // Use qRound for more accurate frame timing (e.g., 60fps -> 17ms not 16ms)
    const int interval = qRound(1000.0 / frameRate);
    m_shaderUpdateTimer->start(interval);

    // CAVA runs independently (spun up lazily by syncCavaState when audio-viz
    // is enabled and something audio-reactive is on screen). Just sync config
    // in case frame rate changed since CAVA was started.
    if (m_audioProvider && m_audioProvider->isRunning() && m_settings) {
        PhosphorAudio::SpectrumOptions opts = m_audioProvider->options();
        opts.framerate = frameRate;
        m_audioProvider->setOptions(opts);
    }

    qCDebug(lcOverlay) << "Shader animation started at" << frameRate << "fps";
}

void OverlayService::stopShaderAnimation()
{
    // Don't stop CAVA here — winding it down is owned by syncCavaState /
    // scheduleIdleQuiesce (deferred a grace period so a quick re-show keeps it
    // warm, then stopped once the overlay is no longer displaying). This function
    // only clears stale spectrum from the overlay windows.
    // audioSpectrum lives on mainOverlaySlot() (the slot
    // Item that hosts the shader content), not on the shell window
    // root. PassiveOverlayShell.qml's mainOverlaySlot declares
    // `property var audioSpectrum: []` and the inner shader content
    // binds to mainOverlaySlot.audioSpectrum.
    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        if (!it_.value().overlayPhysScreen) {
            continue;
        }
        auto* slot = it_.value().mainOverlaySlot();
        if (slot) {
            writeQmlProperty(slot, QString(OverlayQmlPropertyNames::AudioSpectrum), QVariantList());
        }
    }
    // Animation-shader path: clear the SurfaceAnimator's cached
    // spectrum so any in-flight transition stops sampling stale audio
    // and any subsequent attach starts fresh at silence. Mirrors the
    // overlay-window clear above.
    if (m_surfaceAnimator) {
        m_surfaceAnimator->setAudioSpectrum({});
    }
    if (m_shaderUpdateTimer) {
        m_shaderUpdateTimer->stop();
        qCDebug(lcOverlay) << "Shader animation stopped";
    }
}

QList<QQuickItem*> OverlayService::visibleAudioDecorationSlots() const
{
    // The decoration hosts are the OSD + the three popups, per screen; each is a
    // SurfaceDecoration carrying an audioSpectrum property. A slot is fed audio
    // only while it is visible AND its current chain has an audio-reactive pack
    // (recorded by applyDecoration as the dynamic _wantsAudioDecoration flag).
    QList<QQuickItem*> out;
    for (auto it = m_screenStates.cbegin(); it != m_screenStates.cend(); ++it) {
        const PerScreenOverlayState& st = it.value();
        for (QQuickItem* slot :
             {st.osdSlot(), st.snapAssistSlot(), st.layoutPickerSlot(), st.zoneSelectorSlot(), st.cheatsheetSlot()}) {
            if (slot && slot->isVisible()
                && slot->property(OverlayQmlPropertyNames::WantsAudioDecoration.data()).toBool()) {
                out.append(slot);
            }
        }
    }
    return out;
}

void OverlayService::onAudioSpectrumUpdated(const QVector<float>& spectrum)
{
    // Pass QVector<float> wrapped in QVariant to avoid per-element QVariant boxing.
    // ZoneShaderItem::setAudioSpectrum() detects and unwraps QVector<float> directly.
    const QVariant wrapped = QVariant::fromValue(spectrum);
    // Only push to the main overlay while it is actually displaying. During
    // either grace window — post-hide, or warm-idled after a drag (m_visible
    // stays true but m_overlayIdled is set, the windows mapped-but-blanked) —
    // CAVA may still be running; pushing here would repaint invisible surfaces
    // every frame for no benefit. isOverlayDisplaying() covers both.
    if (isOverlayDisplaying()) {
        for (auto it = m_screenStates.cbegin(); it != m_screenStates.cend(); ++it) {
            if (!it.value().overlayPhysScreen) {
                continue;
            }
            auto* slot = it.value().mainOverlaySlot();
            if (slot && useShaderForScreen(it.key())) {
                writeQmlProperty(slot, QString(OverlayQmlPropertyNames::AudioSpectrum), wrapped);
            }
        }
    }
    // Animation-shader path: feed the same spectrum into the
    // SurfaceAnimator so every active transition shader (snap-assist
    // popup, OSD, layout-picker, zone-selector show/hide) sees the
    // live audio data on `iAudioSpectrumSize` / the audio bindings.
    // The SurfaceAnimator pushes the spectrum to every active shader
    // item and caches it for items that attach mid-stream.
    if (m_surfaceAnimator) {
        m_surfaceAnimator->setAudioSpectrum(spectrum);
    }

    // Daemon-surface decoration audio: feed the same spectrum to any displaying
    // OSD / popup whose decoration chain carries an audio-reactive pack, so its
    // SurfaceDecoration forwards it to each stage's SurfaceShaderItem. Empty (a
    // no-op) unless such a surface is visible — which is also what keeps CAVA
    // running, so this only fires when there is a real audio spectrum to push.
    for (QQuickItem* slot : visibleAudioDecorationSlots()) {
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::AudioSpectrum), wrapped);
    }
}

void OverlayService::updateShaderUniforms()
{
    // Pinned to the GUI thread by m_shaderUpdateTimer (a QObject parented
    // to `this`, fired only on the thread that owns it). The frame-counter
    // overflow guard below uses fetch_add + store NOT as a TOCTOU-safe
    // sequence but as cheap relaxed-atomic increments - the assert pins
    // the thread invariant in debug builds so a future refactor that
    // drives the timer from a worker thread surfaces here rather than
    // as silently-corrupted iFrame on simultaneous invocations.
    Q_ASSERT(thread() == QThread::currentThread());

    qint64 currentTime;
    {
        QMutexLocker locker(&m_shaderTimerMutex);
        if (!m_shaderTimer.isValid()) {
            return;
        }
        currentTime = m_shaderTimer.elapsed();
    }

    // Keep iTime as double through the whole pipeline. ZoneShaderNodeRhi splits
    // it into iTime (wrapped) + iTimeHi at the final GPU upload - see
    // kShaderTimeWrap. Casting to float32 here would requantize the counter
    // before the wrap, reintroducing the freezing bug at long uptimes.
    const double iTime = static_cast<double>(currentTime) / 1000.0;

    // Calculate delta time with clamp (sleep/resume / GC stall protection).
    // Cap pinned to PhosphorAnimation::Limits::MaxShaderTimeDeltaSeconds
    // so the daemon and the surface-animator runtimes share one source
    // of truth - bumping one without the other was the prior drift risk.
    const qint64 lastTime = m_lastFrameTime.exchange(currentTime);
    float iTimeDelta = qMin(static_cast<float>(currentTime - lastTime) / 1000.0f,
                            PhosphorAnimation::Limits::MaxShaderTimeDeltaSeconds);

    // Prevent frame counter overflow (~193 days at 60fps before the
    // reset cap kicks in).
    constexpr int kFrameOverflowReset = 1'000'000'000;
    int frame = m_frameCount.fetch_add(1);
    if (frame > kFrameOverflowReset) {
        m_frameCount.store(0);
    }

    // Update zone data for shaders if dirty (highlight changed, layout changed, etc.)
    if (m_zoneDataDirty) {
        updateZonesForAllWindows();
    }

    // Update per-frame shader uniforms on the main-overlay slot Item
    // for every screen with main overlay active. iTime/iTimeDelta/
    // iFrame are properties declared on mainOverlaySlot in
    // PassiveOverlayShell.qml, not on the shell window root -
    // RenderNodeOverlayContent binds to mainOverlaySlot.iTime, so writes to the
    // window root would create dynamic properties that QML never observes.
    for (auto it = m_screenStates.cbegin(); it != m_screenStates.cend(); ++it) {
        if (!it.value().overlayPhysScreen || !it.value().shell) {
            continue;
        }
        auto* slot = it.value().mainOverlaySlot();
        auto* window = it.value().shell->shellWindow();
        if (slot && window && window->isVisible()) {
            writeQmlProperty(slot, QStringLiteral("iTime"), static_cast<qreal>(iTime));
            writeQmlProperty(slot, QStringLiteral("iTimeDelta"), static_cast<qreal>(iTimeDelta));
            writeQmlProperty(slot, QStringLiteral("iFrame"), frame);
        }
    }
}

} // namespace PlasmaZones
