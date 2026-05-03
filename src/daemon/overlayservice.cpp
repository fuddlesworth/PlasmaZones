// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayservice/internal.h"
#include "overlayservice.h"
#include "snapassistthumbnailprovider.h"

#include <PhosphorAudio/CavaSpectrumProvider.h>

#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorSurfaces/SurfaceManagerConfig.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../common/layoutpreviewserialize.h"
#include "../core/unifiedlayoutlist.h"
#include "../core/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "../core/utils.h"
#include "../core/constants.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QMutexLocker>

#include "../core/logging.h"
#include "pz_qml_i18n.h"
#include "vulkan_support.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorLayer/defaults/DefaultScreenProvider.h>
#include <PhosphorLayer/defaults/PhosphorWaylandTransport.h>
#include <QQuickItem>
#include "overlayservice/pz_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

// Tear down every PhosphorLayer::Surface referenced by a state entry.
// The Surface owns its QQuickWindow; deleteLater cascades into ~Surface
// which unmaps the layer surface and schedules the window for deletion.
// We never touch the QQuickWindow* directly — double-destroying a Surface-
// owned window was the source of UB in a prior revision of this file.
void releaseSurfacesInState(OverlayService::PerScreenOverlayState& state)
{
    QObject::disconnect(state.overlayGeomConnection);
    state.overlayGeomConnection = {};
    if (state.overlaySurface) {
        state.overlaySurface->deleteLater();
    }
    if (state.zoneSelectorSurface) {
        state.zoneSelectorSurface->deleteLater();
    }
    if (state.notificationSurface) {
        state.notificationSurface->deleteLater();
    }
    state.overlaySurface = nullptr;
    state.zoneSelectorSurface = nullptr;
    state.notificationSurface = nullptr;
    state.overlayWindow = nullptr;
    state.zoneSelectorWindow = nullptr;
    state.notificationWindow = nullptr;
    state.overlayPhysScreen = nullptr;
    state.zoneSelectorPhysScreen = nullptr;
    state.notificationPhysScreen = nullptr;
}

// Release every surface across the state map, then clear it.
void cleanupAllScreenStates(QHash<QString, OverlayService::PerScreenOverlayState>& states)
{
    for (auto& state : states) {
        releaseSurfacesInState(state);
    }
    states.clear();
}

// Release surfaces for state entries whose key starts with @p prefix,
// then erase those entries from the map.
//
// Semantics: prefix is typically `physId + PhosphorIdentity::VirtualScreenId::Separator`, so
// this function matches ONLY virtual-screen entries (`physId/vs:N`) and
// deliberately skips the bare-physId entry (`physId`). Callers that need
// to clean up the bare entry must do so separately — see the
// onVirtualScreensChangedHandler call site where both are explicitly
// cleaned in sequence.
void cleanupVirtualScreenStates(QHash<QString, OverlayService::PerScreenOverlayState>& states, const QString& prefix)
{
    for (auto it = states.begin(); it != states.end();) {
        if (it.key().startsWith(prefix)) {
            releaseSurfacesInState(it.value());
            it = states.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

namespace {

// ── Per-role Config builders ───────────────────────────────────────────
// One factory per overlay role so adding/tweaking a 6th overlay touches
// exactly one named function rather than appending to a 100-line
// setupSurfaceAnimator. Each builder documents the visual shape it
// encodes; setupSurfaceAnimator just wires the builder output to the
// matching PzRole.
//
// **Surface-family separation policy.** Profile paths are partitioned by
// surface family so each overlay tunes independently:
//
//   - **OSD family (`osd.*`)** — genuine OSDs only. Used by the
//     Notification surface (LayoutOsd / NavigationOsd). A JSON edit to
//     `osd.hide` affects OSDs and ONLY OSDs.
//   - **Popup family (`panel.popup.<surface>.*`)** — non-OSD overlay
//     surfaces. Each gets its own leaf paths under
//     `panel.popup.<surface>` per surface's needs:
//       • `panel.popup.layoutPicker.{show, hide, popIn}` (opacity legs +
//         scale show; hide-scale couples to `.hide`)
//       • `panel.popup.zoneSelector.{show, hide}` (opacity-only)
//       • `panel.popup.snapAssist.show` (destroy-on-hide; no `.hide`
//         leaf because no frame ever paints)
//     A JSON edit to `panel.popup.layoutPicker.hide` affects ONLY the
//     layout picker; siblings are unaffected.
//
// Built-in defaults for every path ship under `data/profiles/`. The
// PhosphorProfileRegistry's `resolve()` is exact-match (no walk-up), so
// each path needs an explicit JSON or it falls back to library defaults
// (150 ms OutCubic). User overrides at
// `~/.local/share/plasmazones/profiles/<path>.json` win over the
// shipped defaults via the loader's owner-tagged precedence.
//
// **Within-family scale-leg coupling (intentional, scoped).** Each
// surface family's hide-leg-scale reuses the surface family's
// hide-leg-opacity path (e.g. `osd.hide` drives both opacity and scale
// hide for OSDs). Editing `osd.hide` to add Spring physics affects both
// legs of the OSD hide. This is the same pattern OSDs and (formerly)
// LayoutPicker shared — kept INSIDE each surface family but not BETWEEN
// them, which is the change here. If a future surface needs decoupled
// opacity/scale tuning, introduce a sibling path
// (e.g. `panel.popup.layoutPicker.popOut`) and register it on
// `hideScaleProfile`.

namespace PAL = PhosphorAnimationLayer;
namespace PAS = PhosphorAnimationShaders;

/// Resolve a path against the shader profile tree. A default-constructed
/// tree (empty baseline + no overrides) resolves every path to an empty
/// effect id, equivalent to "no shader leg" — motion runs alone, identical
/// to the pre-shader-wireup behaviour. The setSettings() handler later
/// re-registers configs with the live tree once settings exist.
///
/// **Source-of-truth note.** The settings UI gates its shader picker on
/// `src/core/animationshadersupportedpaths.h::shaderSupportedEventPaths`,
/// which enumerates exactly the paths consumed by `resolveShaderEffect`
/// in the build*Config functions below. When a new shader-leg surface
/// lands here, append its leg paths to that list in lockstep so the
/// settings UI starts surfacing the picker on the new path.
QString resolveShaderEffect(const PAS::ShaderProfileTree& tree, const QString& path)
{
    return tree.resolve(path).effectiveEffectId();
}

/// Resolve a path against the shader profile tree and extract the per-event
/// parameter overrides. Empty map when the profile didn't override anything
/// — the shader runs with its declared defaults from metadata.json.
QVariantMap resolveShaderParameters(const PAS::ShaderProfileTree& tree, const QString& path)
{
    return tree.resolve(path).effectiveParameters();
}

/// Default config — empty. Surfaces that route through the animator
/// without a registered config fall back to AnimatedValue's library
/// default (150 ms OutCubic), same as a missing-profile lookup. Every
/// Surface that goes through Surface::show()/hide() in this service has
/// an explicit registration below; the empty default is documentation.
PAL::SurfaceAnimator::Config buildDefaultConfig()
{
    return PAL::SurfaceAnimator::Config{};
}

/// LayoutOsd / NavigationOsd: identical fade-and-pop shape. Shader leg
/// resolves osd.show / osd.hide from the user's tree so OSDs can fly in
/// (slide), dissolve, etc. independently of zone or popup events.
///
/// **Genuine-OSD surface family.** All four leg paths live under
/// `osd.*`. A JSON edit to `osd.hide` affects OSDs and ONLY OSDs —
/// LayoutPicker / ZoneSelector / SnapAssist live in the
/// `panel.popup.*` family.
PAL::SurfaceAnimator::Config buildOsdConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{.showProfile = PP::OsdShow,
                                        .hideProfile = PP::OsdHide,
                                        .showScaleProfile = PP::OsdShow,
                                        .hideScaleProfile = PP::OsdHide,
                                        .showScaleFrom = 0.8,
                                        .hideScaleTo = 0.9,
                                        .showShaderEffectId = resolveShaderEffect(tree, PP::OsdShow),
                                        .hideShaderEffectId = resolveShaderEffect(tree, PP::OsdHide),
                                        .showShaderProfile = PP::OsdShow,
                                        .hideShaderProfile = PP::OsdHide,
                                        .showShaderParameters = resolveShaderParameters(tree, PP::OsdShow),
                                        .hideShaderParameters = resolveShaderParameters(tree, PP::OsdHide)};
}

/// LayoutPicker: OSD-style fade-and-pop shape with a softer scale
/// envelope (0.9→1 vs the OSD's 0.8→1) since the picker is a larger
/// surface.
///
/// **Popup surface family — dedicated path partition.** Every leg
/// resolves under `panel.popup.layoutPicker.*`, NOT under `osd.*`. A
/// JSON edit to `panel.popup.layoutPicker.hide` affects ONLY the layout
/// picker; OSD timings stay independent. Built-in defaults for every
/// path ship under `data/profiles/panel.popup.layoutPicker.*.json`
/// mirroring the prior OSD-borrowed timings so the migration is
/// behaviour-preserving.
///
/// Shader legs key on the same `.show` / `.hide` leaves so a user can
/// dissolve in and slide out (or any asymmetric pair). Both leaves walk
/// up to `panel.popup.layoutPicker` and on to `panel.popup`, so a user
/// who wants symmetric treatment overrides the surface path once and
/// skips the leaves.
PAL::SurfaceAnimator::Config buildLayoutPickerConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{
        .showProfile = PP::PanelPopupLayoutPickerShow,
        .hideProfile = PP::PanelPopupLayoutPickerHide,
        .showScaleProfile = PP::PanelPopupLayoutPickerShow,
        .hideScaleProfile = PP::PanelPopupLayoutPickerHide,
        .showScaleFrom = 0.9,
        .hideScaleTo = 0.95,
        .showShaderEffectId = resolveShaderEffect(tree, PP::PanelPopupLayoutPickerShow),
        .hideShaderEffectId = resolveShaderEffect(tree, PP::PanelPopupLayoutPickerHide),
        .showShaderProfile = PP::PanelPopupLayoutPickerShow,
        .hideShaderProfile = PP::PanelPopupLayoutPickerHide,
        .showShaderParameters = resolveShaderParameters(tree, PP::PanelPopupLayoutPickerShow),
        .hideShaderParameters = resolveShaderParameters(tree, PP::PanelPopupLayoutPickerHide)};
}

/// ZoneSelector: opacity-only show/hide. `keepMappedOnHide=true` so the
/// hide animation actually paints.
///
/// **Popup surface family — dedicated path partition.** Every leg
/// resolves under `panel.popup.zoneSelector.*`, NOT under the shared
/// `panel.popup` baseline or the generic `widget.fadeOut` it previously
/// borrowed. A JSON edit to `panel.popup.zoneSelector.hide` affects
/// ONLY the zone selector. Built-in defaults under
/// `data/profiles/panel.popup.zoneSelector.*.json` mirror the prior
/// `panel.popup` (show, 150 ms widget-out) and `widget.fadeOut`
/// (hide, 400 ms cubic-in) timings so the migration is
/// behaviour-preserving.
PAL::SurfaceAnimator::Config buildZoneSelectorConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{
        .showProfile = PP::PanelPopupZoneSelectorShow,
        .hideProfile = PP::PanelPopupZoneSelectorHide,
        .showScaleProfile = {},
        .hideScaleProfile = {},
        .showShaderEffectId = resolveShaderEffect(tree, PP::PanelPopupZoneSelectorShow),
        .hideShaderEffectId = resolveShaderEffect(tree, PP::PanelPopupZoneSelectorHide),
        .showShaderProfile = PP::PanelPopupZoneSelectorShow,
        .hideShaderProfile = PP::PanelPopupZoneSelectorHide,
        .showShaderParameters = resolveShaderParameters(tree, PP::PanelPopupZoneSelectorShow),
        .hideShaderParameters = resolveShaderParameters(tree, PP::PanelPopupZoneSelectorHide)};
}

/// SnapAssist: pop-in show only. The overlay uses destroy-on-hide
/// (keepMappedOnHide=false), so ~Surface synchronously cancels any
/// in-flight beginHide before the hide animation can paint a frame.
/// Only `panel.popup.snapAssist.show` is meaningful — `.hide` is
/// intentionally absent from the taxonomy because no frame ever paints.
///
/// **Inheritance caveat (popup-family-only).** ShaderProfileTree::resolve
/// walks parent paths, so within the popup family a user setting
/// `panel.popup` to dissolve cascades to LayoutPicker and ZoneSelector
/// (both legs) and to SnapAssist's show leg via the chain
/// `panel.popup.<surface>.<leg>` → `panel.popup.<surface>` →
/// `panel.popup` → `global`. SnapAssist's HIDE leg deliberately drops
/// that walk-up — the surface destroys before paint, so the resolve
/// would be contract-pure-and-runtime-no-op. The genuine OSD
/// (`osd.show`/`osd.hide`) is in a separate subtree and is NOT touched
/// by `panel.popup` overrides regardless. If someone ever flips
/// `keepMappedOnHide` to true on snap-assist, restore the
/// `resolveShaderEffect(tree, PanelPopupSnapAssistHide)` line below in
/// lockstep with adding the new path constant.
PAL::SurfaceAnimator::Config buildSnapAssistConfig(const PAS::ShaderProfileTree& tree)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    return PAL::SurfaceAnimator::Config{// Popup surface family — dedicated path. A user editing
                                        // `panel.popup.snapAssist.show.json` affects ONLY the snap
                                        // assist; siblings are unaffected. Built-in default mirrors the
                                        // prior `panel.popup` (150 ms widget-out) so behaviour is
                                        // preserved.
                                        .showProfile = PP::PanelPopupSnapAssistShow,
                                        .hideProfile = {},
                                        .showScaleProfile = {},
                                        .hideScaleProfile = {},
                                        .showShaderEffectId = resolveShaderEffect(tree, PP::PanelPopupSnapAssistShow),
                                        .hideShaderEffectId = {},
                                        .showShaderProfile = PP::PanelPopupSnapAssistShow,
                                        .hideShaderProfile = {},
                                        .showShaderParameters =
                                            resolveShaderParameters(tree, PP::PanelPopupSnapAssistShow),
                                        .hideShaderParameters = {}};
}

} // namespace

void OverlayService::setupSurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& profileRegistry)
{
    namespace PAL = PhosphorAnimationLayer;

    // Two existing surface types deliberately BYPASS this animator and
    // therefore never read any config (default or per-role):
    //   - Overlay (zone overlay rendering): hidden via direct
    //     window->hide() in dismissOverlayWindow because the rapid
    //     drag-mode path uses _idled property toggling rather than the
    //     show/hide state machine.
    //   - ShaderPreview (editor preview window): shown via direct
    //     window->show() in showShaderPreview because the editor controls
    //     visibility imperatively and re-creates on every open.
    m_surfaceAnimator = std::make_unique<PAL::SurfaceAnimator>(profileRegistry, buildDefaultConfig());
    if (m_animShaderRegistry) {
        m_surfaceAnimator->setAnimationShaderRegistry(m_animShaderRegistry);
    }

    // Profile names are the same paths PhosphorMotionAnimation in QML
    // uses today, so the live-reload path (drop a JSON, see it apply on
    // next show) automatically applies to the C++ side too.
    //
    // Phase-2 surface unification: a single per-screen
    // PzRoles::Notification surface (NotificationOverlay.qml) hosts both
    // layout-OSD and navigation-OSD content via a Loader. Production
    // surfaces are scoped `plasmazones-notification-{screenId}-{gen}`
    // and resolve to this config via the animator's longest-prefix
    // lookup.
    //
    // Initial registration runs with an empty tree — m_settings is wired
    // later via setSettings(). A default-constructed tree resolves every
    // path to an empty effect id, so this pass installs motion-only
    // configs (identical to the pre-shader-wireup behaviour). Once
    // settings exist, setSettings calls applyShaderProfilesToAnimator
    // again with the live tree, and connects shaderProfileTreeChanged
    // for live-reload. This keeps the constructor's invariant ("animator
    // is ready before any Surface is created") while deferring the
    // shader wiring to the moment settings are available.
    applyShaderProfilesToAnimator(PAS::ShaderProfileTree{});
}

void OverlayService::applyShaderProfilesToAnimator(const PAS::ShaderProfileTree& tree)
{
    if (!m_surfaceAnimator) {
        return;
    }
    // Diagnostic log gated on lcOverlay().isDebugEnabled() — qCDebug
    // gates the OUTPUT but Qt evaluates argument expressions
    // unconditionally, so the seven extra resolveShaderEffect calls
    // would run even when debug logging is disabled. Each
    // ShaderProfileTree::resolve walks the parent chain and overlay-
    // merges every override; on a typical settings-edit signal storm
    // this is wasted work the explicit gate eliminates.
    if (lcOverlay().isDebugEnabled()) {
        namespace PP = PhosphorAnimation::ProfilePaths;
        qCDebug(lcOverlay).nospace()
            << "applyShaderProfilesToAnimator — overrides=" << tree.overriddenPaths().size()
            << " resolved: osd.show=" << resolveShaderEffect(tree, PP::OsdShow)
            << " osd.hide=" << resolveShaderEffect(tree, PP::OsdHide)
            << " zoneSelector.show=" << resolveShaderEffect(tree, PP::PanelPopupZoneSelectorShow)
            << " zoneSelector.hide=" << resolveShaderEffect(tree, PP::PanelPopupZoneSelectorHide)
            << " layoutPicker.show=" << resolveShaderEffect(tree, PP::PanelPopupLayoutPickerShow)
            << " layoutPicker.hide=" << resolveShaderEffect(tree, PP::PanelPopupLayoutPickerHide)
            << " snapAssist.show=" << resolveShaderEffect(tree, PP::PanelPopupSnapAssistShow);
    }
    m_surfaceAnimator->registerConfigForRole(PzRoles::Notification, buildOsdConfig(tree));
    m_surfaceAnimator->registerConfigForRole(PzRoles::LayoutPicker, buildLayoutPickerConfig(tree));
    m_surfaceAnimator->registerConfigForRole(PzRoles::ZoneSelector, buildZoneSelectorConfig(tree));
    m_surfaceAnimator->registerConfigForRole(PzRoles::SnapAssist, buildSnapAssistConfig(tree));
}

void OverlayService::primeSurfaceRenderPipeline(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    // contains-check covers BOTH lifecycle stages of a single prime
    // (pending warm + window-armed): once a surface is in the set, no
    // path adds another stateChanged or frameSwapped lambda to it.
    // Without this gate, an external double-call to
    // primeSurfaceRenderPipeline (e.g. show path that races a screen
    // reconfigure) would arm a second frameSwapped connection — one
    // would fire and hide the surface mid-content.
    if (m_primingSurfaces.contains(surface)) {
        return;
    }

    // Single destroyed-cleanup per surface (per OverlayService
    // instance), tracked in m_primingDestroyedConnections. Replaces
    // the earlier `pz_primingDestroyedConnected` dynamic-property
    // gate which leaked across service instances — a fresh service
    // re-encountering the same Surface* would skip wiring its own
    // cleanup. The slot's static_cast on `dying` is safe because the
    // resulting pointer is only used as a hash-map key (compare-by-
    // address); ~QObject has already run by the time destroyed fires.
    if (!m_primingDestroyedConnections.contains(surface)) {
        QMetaObject::Connection destroyedConn = connect(surface, &QObject::destroyed, this, [this](QObject* dying) {
            auto* surf = static_cast<PhosphorLayer::Surface*>(dying);
            m_primingSurfaces.remove(surf);
            m_primingFrameConnections.remove(surf);
            m_primingDestroyedConnections.remove(surf);
        });
        m_primingDestroyedConnections.insert(surface, destroyedConn);
    }

    auto* window = surface->window();
    if (!window) {
        // Surface hasn't materialised a QQuickWindow yet — Surface::warmUp
        // is asynchronous for content that compiles off the main thread.
        // Defer until warm completes (stateChanged Warming → Hidden).
        // Disconnect on the FIRST Hidden — even if the window is somehow
        // still null we drop the connection rather than letting it stay
        // armed forever and re-fire on every later state change. The
        // recursive call lands in the window-non-null branch which adds
        // to m_primingSurfaces and installs the frameSwapped path.
        //
        // Insert into m_primingSurfaces NOW (warm-pending sentinel) so
        // an external second call to primeSurfaceRenderPipeline before
        // the first warm completes hits the contains() guard above and
        // bails — without this, the second call would queue a SECOND
        // stateChanged lambda whose recursive call lands in the window-
        // path's contains() bail at line `m_primingSurfaces.contains
        // (surface) → return` after the first one already inserted +
        // armed, leaking the second stateChanged slot for the rest of
        // the surface's lifetime.
        //
        // Disconnects on Hidden OR Failed: a surface stuck in Failed
        // never reaches Hidden, so without the Failed branch the
        // sentinel sits in m_primingSurfaces indefinitely, blocking
        // any future re-prime even after a recovery path puts the
        // surface back into a usable state.
        m_primingSurfaces.insert(surface);
        QPointer<PhosphorLayer::Surface> guard(surface);
        auto warmConn = std::make_shared<QMetaObject::Connection>();
        *warmConn = connect(surface, &PhosphorLayer::Surface::stateChanged, this,
                            [this, guard, warmConn](PhosphorLayer::Surface::State newState) {
                                if (newState != PhosphorLayer::Surface::State::Hidden
                                    && newState != PhosphorLayer::Surface::State::Failed) {
                                    return;
                                }
                                QObject::disconnect(*warmConn);
                                if (!guard) {
                                    return;
                                }
                                // Drop the warm-pending sentinel BEFORE the
                                // recursive call so the window-path's
                                // contains() guard re-evaluates to false
                                // and proceeds to insert + arm the
                                // frameSwapped handler.
                                //
                                // CRITICAL: gate the recursive prime on
                                // `m_primingSurfaces.remove(...)` returning
                                // true. If a user-show called
                                // cancelSurfacePrime during the warm-pending
                                // window, the surface was already removed
                                // from the set, `remove()` returns false,
                                // and we MUST NOT recurse — recursion would
                                // re-arm a fresh prime cycle whose
                                // frameSwapped-driven hide() races the
                                // user's just-shown content off the screen.
                                // Without this guard, cancelSurfacePrime is
                                // a silent no-op while the warm hasn't
                                // completed.
                                const bool stillPriming = m_primingSurfaces.remove(guard.data());
                                if (stillPriming && newState == PhosphorLayer::Surface::State::Hidden
                                    && guard->window() != nullptr) {
                                    primeSurfaceRenderPipeline(guard.data());
                                }
                            });
        return;
    }
    m_primingSurfaces.insert(surface);

    // Hide on the first frameSwapped after surface->show(). By that
    // point the wl_surface is mapped, the Vulkan swapchain has at
    // least one image, and the QML scene-graph (including any
    // QSGLayer that the shader path will later use) has rendered
    // at least one frame.
    //
    // The connection is tracked in m_primingFrameConnections so
    // cancelSurfacePrime can disconnect it explicitly — without
    // tracking, the connection survives until next paint and we
    // accumulate one stale slot per prime cycle for the surface's
    // lifetime under rapid show/hide.
    QPointer<PhosphorLayer::Surface> guard(surface);
    QMetaObject::Connection frameConn = connect(window, &QQuickWindow::frameSwapped, this, [this, guard]() {
        if (!guard) {
            // Surface died after the connection was armed but before
            // first frameSwapped — the destroyed-signal lambda in
            // m_primingDestroyedConnections has already cleaned the
            // map entry, and Qt's sender-destruction auto-disconnect
            // (window dies with surface) will retire this lambda
            // shortly. Nothing to do here.
            return;
        }
        const auto connIt = m_primingFrameConnections.find(guard.data());
        if (connIt != m_primingFrameConnections.end()) {
            QObject::disconnect(connIt.value());
            m_primingFrameConnections.erase(connIt);
        }
        // Only hide if the user hasn't already taken over the surface
        // (cancelSurfacePrime would have removed us from the set).
        if (m_primingSurfaces.remove(guard.data())) {
            guard->hide();
        }
    });
    m_primingFrameConnections.insert(surface, frameConn);
    surface->show();
}

void OverlayService::cancelSurfacePrime(PhosphorLayer::Surface* surface)
{
    // Idempotent — called from every user show path so a non-priming
    // surface short-circuits cheaply. Disconnect the frameSwapped
    // lambda EXPLICITLY (tracked in m_primingFrameConnections) so the
    // queued hide-on-first-paint never fires after a user-show. The
    // m_primingSurfaces.remove() is the secondary guard the lambda
    // would also check, but explicit disconnection is the safer
    // primary contract — any future event-loop pump between cancel
    // and the user's surface->show() is now harmless. Surfaces that
    // get torn down outside of cancelSurfacePrime are cleaned via the
    // destroyed signal connection in m_primingDestroyedConnections.
    m_primingSurfaces.remove(surface);
    const auto connIt = m_primingFrameConnections.find(surface);
    if (connIt != m_primingFrameConnections.end()) {
        QObject::disconnect(connIt.value());
        m_primingFrameConnections.erase(connIt);
    }
}

OverlayService::OverlayService(Phosphor::Screens::ScreenManager* screenManager, ShaderRegistry* shaderRegistry,
                               PhosphorAnimation::PhosphorProfileRegistry* profileRegistry, QObject* parent)
    : IOverlayService(parent)
    , m_screenProvider(std::make_unique<PhosphorLayer::DefaultScreenProvider>())
    , m_transport(std::make_unique<PhosphorLayer::PhosphorWaylandTransport>())
{
    m_screenManager = screenManager;
    m_shaderRegistry = shaderRegistry;

    // The profile registry is non-optional: SurfaceAnimator binds to it by
    // reference. Composition roots own a single PhosphorProfileRegistry
    // and thread it through every consumer — fail loud if the wiring is
    // wrong rather than silently falling back to library defaults.
    Q_ASSERT_X(profileRegistry, "OverlayService::OverlayService",
               "profileRegistry must not be null — composition root must own and inject the registry");

    // Phase-5 SurfaceAnimator. One instance drives every overlay's
    // show/hide via Profile-resolved curves; per-Role configs install
    // below in setupSurfaceAnimator(). Constructed BEFORE the
    // SurfaceFactory because the factory's Deps captures the animator
    // pointer; Surfaces produced after this point dispatch through it
    // on every show/hide.
    setupSurfaceAnimator(*profileRegistry);

    m_surfaceFactory = std::make_unique<PhosphorLayer::SurfaceFactory>(
        PhosphorLayer::SurfaceFactory::Deps{.transport = m_transport.get(),
                                            .screens = m_screenProvider.get(),
                                            .engineProvider = nullptr,
                                            .animator = m_surfaceAnimator.get(),
                                            .loggingCategory = QStringLiteral("plasmazones.overlay")});

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString pipelineCachePath =
        cacheDir.isEmpty() ? QString() : (cacheDir + QStringLiteral("/plasmazones-pipeline.cache"));

    QVulkanInstance* externalVulkanInstance = nullptr;
#if QT_CONFIG(vulkan)
    externalVulkanInstance = qApp->property(PlasmaZones::PzVulkanInstanceProperty).value<QVulkanInstance*>();
#endif

    // Construct the thumbnail provider eagerly so the borrowed @c m_thumbnailProvider
    // pointer is non-null from this point onwards. The SurfaceManager (and
    // its engine) is created next; the engineConfigurator releases ownership
    // to the engine once it exists. Until then the unique_ptr keeps the
    // provider alive — there is no longer any window where a D-Bus
    // setSnapAssistThumbnail call would silently drop because the engine
    // hasn't materialised yet.
    m_thumbnailProviderOwned = std::make_unique<SnapAssistThumbnailProvider>();
    m_thumbnailProvider.store(m_thumbnailProviderOwned.get(), std::memory_order_release);

    m_surfaceManager = std::make_unique<PhosphorSurfaces::SurfaceManager>(PhosphorSurfaces::SurfaceManagerConfig{
        .surfaceFactory = m_surfaceFactory.get(),
        .engineConfigurator =
            [this](QQmlEngine& engine) {
                auto* localizedContext = new PzLocalizedContext(&engine);
                engine.rootContext()->setContextObject(localizedContext);
                engine.rootContext()->setContextProperty(QStringLiteral("overlayService"), this);

                // Bounded LRU cache + image provider for Snap Assist thumbnails.
                // QQmlEngine::addImageProvider takes ownership; transfer the
                // already-live provider out of the unique_ptr so the engine
                // becomes the sole owner. The borrowed @c m_thumbnailProvider
                // raw pointer remains valid for the engine's lifetime, which
                // outlives every QML element it spawns, so QML callbacks
                // that hit requestImage are safe.
                //
                // The engine's @c destroyed signal nulls @c m_thumbnailProvider
                // before any subsequent D-Bus dispatch can dereference it.
                // Without this hook, late @c setSnapAssistThumbnail traffic
                // arriving after the engine is gone (e.g. forced
                // SurfaceManager teardown outside @c ~OverlayService) would
                // see a dangling raw pointer.
                //
                // Re-entrancy: if @c engineConfigurator is ever invoked again
                // (a future SurfaceManager that recreates its engine), the
                // unique_ptr will be empty after the first @c release(). Mint
                // a fresh provider so the second engine isn't quietly
                // unregistered from snap-assist thumbnails. Today the engine
                // is single-instance for the daemon's lifetime, but defending
                // here costs ~3 lines and removes a foot-gun if that
                // invariant ever changes.
                if (!m_thumbnailProviderOwned) {
                    m_thumbnailProviderOwned = std::make_unique<SnapAssistThumbnailProvider>();
                    m_thumbnailProvider.store(m_thumbnailProviderOwned.get(), std::memory_order_release);
                }
                engine.addImageProvider(QString::fromLatin1(SnapAssistThumbnailProvider::ProviderId),
                                        m_thumbnailProviderOwned.release());
                QObject::connect(&engine, &QObject::destroyed, this, [this]() {
                    m_thumbnailProvider.store(nullptr, std::memory_order_release);
                });
            },
        .pipelineCachePath = pipelineCachePath,
        .vulkanInstance = externalVulkanInstance,
        .vulkanApiVersion = PlasmaZones::PzVulkanApiVersion,
    });

    // Connect to screen changes (with safety check for early initialization)
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, &OverlayService::handleScreenAdded);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OverlayService::handleScreenRemoved);
    } else {
        qCWarning(lcOverlay) << "Overlay: created before QGuiApplication, screen signals not connected";
    }

    // Connect to virtual screen configuration changes
    if (auto* mgr = m_screenManager) {
        auto onVirtualScreensChangedHandler = [this](const QString& physicalScreenId) {
            // Destroy old overlays for this physical screen, recreate with new config
            QScreen* physScreen = Phosphor::Screens::ScreenIdentity::findByIdOrName(physicalScreenId);
            if (!physScreen) {
                // Physical screen removed -- destroy windows and clean up stale virtual screen entries
                const QString prefix = physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator;
                cleanupVirtualScreenStates(m_screenStates, prefix);
                // Also clean up the bare physical-ID entry (no /vs:N suffix) —
                // cleanupVirtualScreenStates only matches entries starting with "physId/",
                // not the bare "physId" key itself.
                destroyOverlayWindow(physicalScreenId);
                destroyZoneSelectorWindow(physicalScreenId);
                destroyNotificationWindow(physicalScreenId);
                m_screenStates.remove(physicalScreenId);
                return;
            }

            // If the new config HAS virtual screens for this physical ID,
            // destroy any overlay window keyed by the bare physical screen ID
            // itself. Virtual screens use prefixed keys; the bare key would be
            // a leftover from the previous (non-virtual) configuration.
            auto* mgr2 = m_screenManager;
            if (mgr2 && mgr2->hasVirtualScreens(physicalScreenId)) {
                destroyOverlayWindow(physicalScreenId);
                destroyZoneSelectorWindow(physicalScreenId);
                destroyNotificationWindow(physicalScreenId);
            }

            // Clear selected zone before destroying windows — the selection references
            // zone geometry from the old virtual screen config and would be stale.
            clearSelectedZone();

            // Track whether zone selectors were visible before destruction so we can
            // recreate them for the new virtual screen configuration.
            const bool hadZoneSelector = m_zoneSelectorVisible;

            // Destroy all window types (overlays, selectors, OSDs, snap assist, layout picker)
            destroyAllWindowsForPhysicalScreen(physScreen);

            // Reset zone selector flag — the windows were destroyed, so the flag
            // must be cleared to allow re-showing. Without this, the guard at the
            // top of showZoneSelector() prevents recreation.
            if (hadZoneSelector) {
                m_zoneSelectorVisible = false;
            }

            // Recreate with new virtual screen config if visible. Reuses
            // mgr2 from above — the Phosphor::Screens::ScreenManager singleton doesn't change
            // mid-lambda, so re-querying would just be noise.
            if (isVisible()) {
                if (mgr2 && mgr2->hasVirtualScreens(physicalScreenId)) {
                    for (const QString& vsId : mgr2->virtualScreenIdsFor(physicalScreenId)) {
                        QRect vsGeom = mgr2->screenGeometry(vsId);
                        if (vsGeom.isValid()) {
                            createOverlayWindow(vsId, physScreen, vsGeom);
                        }
                    }
                } else {
                    createOverlayWindow(physScreen);
                }
            }

            // Recreate zone selectors for the new virtual screen configuration.
            // Defer to the next event loop pass to allow PhosphorZones::LayoutRegistry to process
            // assignment migrations for the new virtual screen IDs first, ensuring
            // the zone selector shows the correct layout list.
            if (hadZoneSelector) {
                m_zoneSelectorRecreationPending = true;
                QTimer::singleShot(0, this, [this]() {
                    m_zoneSelectorRecreationPending = false;
                    // m_zoneSelectorVisible was set to false above (to allow recreation).
                    // If an external showZoneSelector() ran during the event loop pass between
                    // posting this timer and its execution, it will have set m_zoneSelectorVisible
                    // back to true — in that case we must NOT call showZoneSelector() again
                    // (double-show). The !m_zoneSelectorVisible guard handles exactly this:
                    // false means "no interim show happened, we still need to recreate";
                    // true means "already re-shown, skip".
                    if (!m_zoneSelectorVisible) {
                        showZoneSelector();
                    }
                });
            }
        };
        connect(mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged, this, onVirtualScreensChangedHandler);
        // Regions-only changes (swap/rotate/boundary-resize) also need the
        // overlay windows destroyed and recreated with the new VS geometry.
        // The handler is heavy but only runs when overlays are visible
        // (active drag), so the cost is bounded.
        connect(mgr, &Phosphor::Screens::ScreenManager::virtualScreenRegionsChanged, this,
                onVirtualScreensChangedHandler);
    }

    // Connect to system sleep/resume via logind to restart shader timer after wake.
    // This prevents large iTimeDelta jumps when system resumes from sleep.
    // Track the connect result so the dtor can disconnect cleanly rather than
    // leaving a dead entry in QDBusConnection's slot table until the session ends.
    m_prepareForSleepConnected = QDBusConnection::systemBus().connect(
        QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"), this,
        SLOT(onPrepareForSleep(bool)));
    if (!m_prepareForSleepConnected) {
        qCDebug(lcOverlay) << "PrepareForSleep D-Bus signal subscription failed (logind not available?) —"
                           << "shader-timer restart on resume will not run";
    }

    // Reset shader error state on construction (fresh start after reboot)
    m_pendingShaderError.clear();

    m_audioProvider = std::make_unique<PhosphorAudio::CavaSpectrumProvider>();
    connect(m_audioProvider.get(), &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated, this,
            &OverlayService::onAudioSpectrumUpdated);

    // Keep-alive is managed by m_surfaceManager (created in its constructor).
}

bool OverlayService::isVisible() const
{
    return m_visible;
}

bool OverlayService::isZoneSelectorVisible() const
{
    return m_zoneSelectorVisible;
}

OverlayService::~OverlayService()
{
    // Disconnect from QGuiApplication first so we don't get screen-related callbacks
    // while we're destroying windows.
    if (qGuiApp) {
        disconnect(qGuiApp, nullptr, this, nullptr);
    }

    if (m_prepareForSleepConnected) {
        QDBusConnection::systemBus().disconnect(QStringLiteral("org.freedesktop.login1"),
                                                QStringLiteral("/org/freedesktop/login1"),
                                                QStringLiteral("org.freedesktop.login1.Manager"),
                                                QStringLiteral("PrepareForSleep"), this, SLOT(onPrepareForSleep(bool)));
        m_prepareForSleepConnected = false;
    }

    // Clean up all window types before engine is destroyed. The Surface owns
    // the QQuickWindow, so deleteLater on the Surface cascades into
    // ~Surface → ~Impl → window teardown in the right order. Never destroy
    // the window directly — that races against ~Surface and dereferences a
    // deleted pointer in ~Impl.
    cleanupAllScreenStates(m_screenStates);

    // Singleton surfaces (snap assist, layout picker, shader preview) are
    // QObject children of `this`, so the QObject parent-child system would
    // destroy them AFTER our own destructor body runs — i.e. after the
    // member destructors. Schedule their deletion now so SurfaceManager's
    // drain loop picks them up before the engine is destroyed.
    if (m_snapAssistSurface) {
        m_snapAssistSurface->deleteLater();
        m_snapAssistSurface = nullptr;
    }
    if (m_layoutPickerSurface) {
        m_layoutPickerSurface->deleteLater();
        m_layoutPickerSurface = nullptr;
    }
    if (m_shaderPreviewSurface) {
        m_shaderPreviewSurface->deleteLater();
        m_shaderPreviewSurface = nullptr;
    }

    // Drain deferred-delete events NOW, while all OverlayService members are
    // still alive. Surface destructors may touch m_screenStates, m_shaderRegistry,
    // etc. — if we let ~m_surfaceManager's drain run instead, those members could
    // already be destroyed (C++ member destruction order is reverse declaration).
    m_surfaceManager->drainDeferredDeletes();

    // Explicitly disconnect + clear the prime-tracking maps so the
    // invariant ("every Connection retired before its sender's window
    // is gone") doesn't depend on Qt's receiver-context auto-disconnect
    // ordering during member destruction. After drainDeferredDeletes
    // every prime-tracked surface is destroyed, so most Connections are
    // already retired by sender-destruction; this loop is defensive
    // against any future path that adds prime-tracked surfaces outside
    // of m_screenStates / the three explicit singletons.
    for (const auto& conn : std::as_const(m_primingFrameConnections)) {
        QObject::disconnect(conn);
    }
    m_primingFrameConnections.clear();
    for (const auto& conn : std::as_const(m_primingDestroyedConnections)) {
        QObject::disconnect(conn);
    }
    m_primingDestroyedConnections.clear();
    m_primingSurfaces.clear();
}

PhosphorLayer::Surface* OverlayService::createLayerSurface(LayerSurfaceParams params)
{
    if (!params.screen) {
        qCWarning(lcOverlay) << "createLayerSurface: screen is null for" << params.windowType;
        return nullptr;
    }

    PhosphorLayer::SurfaceConfig cfg;
    cfg.role = std::move(params.role);
    cfg.contentUrl = std::move(params.qmlUrl);
    cfg.screen = params.screen;
    cfg.windowProperties = std::move(params.windowProperties);
    cfg.anchorsOverride = std::move(params.anchorsOverride);
    cfg.marginsOverride = std::move(params.marginsOverride);
    cfg.keepMappedOnHide = params.keepMappedOnHide;
    // SurfaceConfig::initialSize uses isEmpty() as the "unset" sentinel —
    // forwarding the param verbatim preserves that contract (empty here →
    // empty there → fall back to screen geometry inside surface.cpp).
    cfg.initialSize = params.initialSize;
    cfg.debugName = QString::fromUtf8(params.windowType);

    return m_surfaceManager->createSurface(std::move(cfg), this);
}

PhosphorLayer::Surface* OverlayService::createWarmedOsdSurface(const PhosphorLayer::Role& role, const QUrl& qmlUrl,
                                                               QScreen* physScreen, const char* windowType)
{
    // Warm-up size matches NotificationOverlay.qml's QML literal (240x70).
    // This only governs the size of the warm-up commit — every per-show
    // path in osd.cpp goes through assertWindowOnScreen + sizeAndCenterOsd,
    // both of which still call setGeometry / setWidth / setHeight against
    // the live window, so per-show swapchain resizes still happen on every
    // show as before. What changes here is that the daemon stops paying for
    // a full-screen swapchain for an OSD whose visible content is a tiny
    // toast: holding ~17 fullscreen swapchains (one per warmed overlay
    // across virtual screens) cost ~25 MB each at 4K on NVIDIA's proprietary
    // stack, and a content-sized warm-up brings that down to the toast's
    // own footprint.
    auto* surface = createLayerSurface({.qmlUrl = qmlUrl,
                                        .screen = physScreen,
                                        .role = role,
                                        .windowType = windowType,
                                        .keepMappedOnHide = true,
                                        .initialSize = QSize(240, 70)});
    if (!surface) {
        return nullptr;
    }

    // Wire the QML-side auto-dismiss signal to Surface::hide(). The OSD
    // content components (LayoutOsdContent, NavigationOsdContent) both
    // expose `signal dismissRequested()` driven by their shared
    // OsdDismissable timer; the unified NotificationOverlay host
    // re-emits each loaded content's signal as its own dismissRequested.
    // LayoutPickerOverlay uses the same name (post-#9 rename) for
    // backdrop-click dismissal. String-based connect is the only path
    // because QML-defined signals aren't addressable via Qt5
    // `&Class::signal` pointers.
    if (auto* window = surface->window()) {
        QObject::connect(window, SIGNAL(dismissRequested()), surface, SLOT(hide()));
    }
    return surface;
}

void OverlayService::show()
{
    if (m_visible) {
        return;
    }

    // Check if we should show on all monitors or just the cursor's screen
    bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();

    QScreen* cursorScreen = nullptr;
    if (!showOnAllMonitors) {
        // Find the screen containing the cursor
        cursorScreen = QGuiApplication::screenAt(QCursor::pos());
        if (!cursorScreen) {
            // Fallback to primary screen if cursor position detection fails
            cursorScreen = Utils::primaryScreen();
        }
        // If the cursor's screen has PlasmaZones disabled, don't show overlay at all
        // Check both physical and effective (virtual) screen IDs
        if (cursorScreen && m_settings) {
            QString effectiveId = Utils::effectiveScreenIdAt(m_screenManager, QCursor::pos(), cursorScreen);
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, effectiveId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                return;
            }
        }
    }

    initializeOverlay(cursorScreen);
}

void OverlayService::showAtPosition(int cursorX, int cursorY)
{
    // Check if we should show on all monitors or just the cursor's screen
    bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();

    QScreen* cursorScreen = nullptr;
    if (!showOnAllMonitors) {
        // Find the screen containing the cursor using provided coordinates
        // This works on Wayland where QCursor::pos() doesn't work
        cursorScreen = Utils::findScreenAtPosition(cursorX, cursorY);
        if (!cursorScreen) {
            // Fallback to primary screen if no screen contains the cursor position
            cursorScreen = Utils::primaryScreen();
        }

        // If the cursor's screen has PlasmaZones disabled, don't show overlay at all
        // Check both physical and effective (virtual) screen IDs
        if (cursorScreen && m_settings) {
            QString effectiveId = Utils::effectiveScreenIdAt(m_screenManager, QPoint(cursorX, cursorY), cursorScreen);
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, effectiveId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                return;
            }
        }
    }

    const QPoint cursorPos(cursorX, cursorY);

    if (m_visible) {
        // One-overlay-per-VS architecture: every VS already has a live
        // overlay window from initializeOverlay. Cross-VS switching is
        // just a matter of flipping per-window _idled state — no
        // re-init, no rekey, no layer-shell re-anchor. This sidesteps
        // the earlier "wrong spot" bug where rekey moved the map entry
        // but left the layer surface anchored to the previous VS's
        // bounds, and the full NVIDIA vkDestroyDevice deadlock on any
        // destroy path.
        if (!cursorScreen) {
            cursorScreen = Utils::findScreenAtPosition(cursorPos);
        }
        if (!cursorScreen) {
            return;
        }
        const QString cursorEffectiveId =
            Utils::effectiveScreenIdAt(m_screenManager, QPoint(cursorX, cursorY), cursorScreen);
        if (cursorEffectiveId.isEmpty()) {
            return;
        }
        // Per-VS toggle (autotile→snap mid-session) leaves us with a VS that
        // is no longer excluded but never had its overlay window created at
        // first-show time. Detect that here — the cursor is now on a non-
        // excluded VS with no live window — and fall through to full init
        // so Phase 3 creates the window. Without this, applyIdleStateForCursor
        // finds nothing to flip and the overlay never becomes visible until
        // the next hide()/show() cycle.
        const bool cursorVsHasWindow = m_screenStates.contains(cursorEffectiveId)
            && m_screenStates.value(cursorEffectiveId).overlayWindow != nullptr;
        if (cursorVsHasWindow || m_excludedScreens.contains(cursorEffectiveId)) {
            m_currentOverlayScreenId = showOnAllMonitors ? QString() : cursorEffectiveId;
            applyIdleStateForCursor(cursorEffectiveId, showOnAllMonitors);
            return;
        }
        // Fall through — initializeOverlay will (re)build the per-VS window
        // set against the current excluded set and resume normal operation.
    }

    initializeOverlay(cursorScreen, cursorPos);
}

void OverlayService::hide()
{
    if (!m_visible) {
        return;
    }

    m_visible = false;
    m_currentOverlayScreenId.clear();

    // Stop shader animation
    stopShaderAnimation();

    // Do NOT invalidate m_shaderTimer - keeps iTime continuous across show/hide
    // so animations feel less predictable and don't restart

    // Destroy overlay windows instead of hiding them. On Vulkan with Wayland
    // layer-shell, window->hide() destroys the wl_surface but the Qt Vulkan
    // backend doesn't properly reinitialize the VkSwapchainKHR when the window
    // is re-shown, causing the scene graph render loop to stall. Destroying the
    // window entirely and creating a fresh one on the next show() avoids this.
    // initializeOverlay() will call createOverlayWindow() since overlay windows
    // are now destroyed.
    const QStringList screenIds = m_screenStates.keys();
    for (const QString& screenId : screenIds) {
        destroyOverlayWindow(screenId);
    }

    m_pendingShaderError.clear();

    Q_EMIT visibilityChanged(false);
}

void OverlayService::toggle()
{
    if (m_visible) {
        hide();
    } else {
        show();
    }
}

void OverlayService::setIdleForDragPause()
{
    // Blank the overlay's shader output without destroying QQuickWindows.
    // The heavy hide() path pays a ~QQuickWindow Vulkan teardown per screen
    // which blocks the main thread on the scene graph render thread — and
    // with modifier-key thrashing during a drag we ended up paying that cost
    // many times per second, stalling D-Bus dispatch long enough for
    // kwin-effect's endDrag to time out and the user to see multi-second lag.
    //
    // Here we only clear the per-window QML properties that drive the shader
    // (zones, zoneCount, highlights). Windows, Vulkan swap chains, and layer
    // surfaces stay alive. On the next activation tick, refreshFromIdle()
    // re-pushes the current zone data — cheap because the labels-texture
    // build is hash-cached on unchanged inputs.
    if (!m_visible) {
        return;
    }
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        QQuickWindow* window = it.value().overlayWindow;
        if (!window) {
            continue;
        }
        // _idled gates content.visible and toggles Qt.WindowTransparentForInput
        // in the overlay QML (RenderNodeOverlay.qml / ZoneOverlay.qml). That
        // makes the wl_surface effectively invisible and non-input-absorbing
        // in place, without destroying the QQuickWindow. Blanking only the
        // zones properties below is not sufficient — on some shaders the
        // base pass still renders visible output when zoneCount==0, and the
        // input region stays active until the flag change lands.
        writeQmlProperty(window, QStringLiteral("_idled"), true);
        writeQmlProperty(window, QStringLiteral("zones"), QVariantList());
        writeQmlProperty(window, QStringLiteral("zoneCount"), 0);
        writeQmlProperty(window, QStringLiteral("highlightedCount"), 0);
        writeQmlProperty(window, QStringLiteral("highlightedZoneId"), QString());
        writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), QVariantList());
        // NOTE: labelsTextureHash is intentionally NOT cleared here. The QML
        // side's labelsTexture property still holds the previously-built image
        // (setProperty was never called with a new one); it just isn't sampled
        // while zoneCount is 0. Keeping the hash means refreshFromIdle() with
        // unchanged zones hits the cache and costs one hash compute instead
        // of rebuilding 23 MB of pixels.
    }
    // CRITICAL: mark zone data CLEAN, not dirty. The shader animation tick
    // (shader.cpp:245) re-runs updateZonesForAllWindows() whenever dirty is
    // set, which would rebuild the real zones and undo the blank. The idle
    // state is "what we just wrote, do not re-derive from layout data until
    // refreshFromIdle() is called."
    m_zoneDataDirty = false;
    // NOTE: we deliberately do NOT call stopShaderAnimation() here. The
    // shader timer keeps ticking at ~60 Hz while idled, but with zoneCount
    // set to 0 the per-frame work collapses to a handful of uniform uploads
    // to a surface that's rendering no visible geometry — bounded cost, O(1)
    // per screen. Pausing and restarting the timer across the idle cycle
    // would require additional state tracking in refreshFromIdle() and add
    // a startup transient on every modifier re-press. Left unchanged for
    // simplicity; revisit if profiling ever shows it as a hot spot.
}

void OverlayService::refreshFromIdle()
{
    // Restore zone data after a setIdleForDragPause() blank and flip
    // the active VS's overlay back to visible.
    //
    // setIdleForDragPause() unconditionally idles every overlay (zones
    // blanked + _idled=true), so refreshFromIdle() re-pushes zone data
    // to all of them and then applies the cursor-based idle state to
    // un-idle the one the cursor is currently on. The L2 labels-texture
    // hash cache keeps the shader-path re-push cheap on unchanged inputs.
    if (!m_visible) {
        return;
    }
    updateZonesForAllWindows();
    // Resolve the cursor's current VS — the drag adaptor keeps
    // m_currentOverlayScreenId updated via showAtPosition, so this
    // reflects the last VS the cursor was observed on.
    const bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();
    applyIdleStateForCursor(m_currentOverlayScreenId, showOnAllMonitors);
}

void OverlayService::applyIdleStateForCursor(const QString& activeEffectiveId, bool showOnAllMonitors)
{
    // One-overlay-per-VS idle state: iterate every live overlay window
    // and flip its _idled QML property based on whether its VS should
    // currently be accepting input / rendering content.
    //
    // - showOnAllMonitors=true  → all overlays un-idled (all VSes active)
    // - showOnAllMonitors=false → only activeEffectiveId un-idled
    // - activeEffectiveId empty → all overlays idled (no active VS —
    //   used by setIdleForDragPause when drag-end hasn't chosen a next
    //   cursor position yet, or when the cursor sits on a disabled VS)
    //
    // The write is idempotent: QML property binding only re-evaluates
    // when the value actually changes, so flipping _idled on a window
    // that's already in the target state is free.
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        QQuickWindow* window = it.value().overlayWindow;
        if (!window) {
            continue;
        }
        const bool shouldBeActive =
            showOnAllMonitors || (it.key() == activeEffectiveId && !activeEffectiveId.isEmpty());
        writeQmlProperty(window, QStringLiteral("_idled"), !shouldBeActive);
    }
}

void OverlayService::setAnimationShaderRegistry(PhosphorAnimationShaders::AnimationShaderRegistry* registry)
{
    m_animShaderRegistry = registry;
    if (m_surfaceAnimator) {
        m_surfaceAnimator->setAnimationShaderRegistry(registry);
    }
}

void OverlayService::updateSettings(ISettings* settings)
{
    setSettings(settings);

    // Sync CAVA state with current settings.  The signal-based handlers
    // (enableAudioVisualizerChanged, etc.) connected in setSettings() only
    // fire when load() detects a value change.  When the KCM uses batch
    // setSettings + reloadSettings, the in-memory values are already updated
    // by the batch setters before load() runs, so load() sees no change and
    // the signals never fire.  Syncing here ensures CAVA always reflects
    // the current configuration.
    syncCavaState();

    // Hide overlay and zone selector on disabled screens/desktops/activities,
    // then refresh remaining (non-disabled) windows with the new settings.
    hideDisabledAndRefresh();

    // If the selector was visible but got disabled via settings, hide it immediately.
    if (m_zoneSelectorVisible && m_settings && !m_settings->zoneSelectorEnabled()) {
        hideZoneSelector();
    }
}

void OverlayService::setLayout(PhosphorZones::Layout* layout)
{
    if (m_layout != layout) {
        m_layout = layout;
        // Mark zone data as dirty when layout changes to ensure shader overlay updates
        m_zoneDataDirty = true;
    }
}

PhosphorZones::Layout* OverlayService::resolveScreenLayout(QScreen* screen) const
{
    // Physical QScreen* overload: derives screenId and delegates.
    // Callers with a known virtual screenId should use the QString overload directly.
    if (!screen) {
        return m_layout;
    }
    return resolveScreenLayout(Phosphor::Screens::ScreenIdentity::identifierFor(screen));
}

PhosphorZones::Layout* OverlayService::resolveScreenLayout(const QString& screenId) const
{
    PhosphorZones::Layout* screenLayout = nullptr;
    if (m_layoutManager && !screenId.isEmpty()) {
        screenLayout = m_layoutManager->layoutForScreen(screenId, m_currentVirtualDesktop, m_currentActivity);
        if (!screenLayout) {
            screenLayout = m_layoutManager->defaultLayout();
        }
    }
    if (!screenLayout) {
        screenLayout = m_layout;
    }
    return screenLayout;
}

void OverlayService::hideDisabledAndRefresh()
{
    // Destroy windows on screens where the current context is disabled.
    // Destroy (not hide) to free GPU resources for permanently inactive contexts.
    if (m_settings) {
        const QStringList screenIds = m_screenStates.keys();
        for (const QString& screenId : screenIds) {
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                destroyZoneSelectorWindow(screenId);
                if (m_visible) {
                    destroyOverlayWindow(screenId);
                }
            }
        }
    }

    // Update remaining (non-disabled) zone selector and overlay windows
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        const QString& screenId = it.key();
        if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId, m_currentVirtualDesktop,
                              m_currentActivity)) {
            continue;
        }
        if (it.value().zoneSelectorWindow) {
            updateZoneSelectorWindow(screenId);
        }
        if (m_visible && it.value().overlayWindow && it.value().overlayPhysScreen) {
            updateOverlayWindow(screenId, it.value().overlayPhysScreen);
        }
    }
}

void OverlayService::setCurrentVirtualDesktop(int desktop)
{
    if (m_currentVirtualDesktop != desktop) {
        m_currentVirtualDesktop = desktop;
        qCInfo(lcOverlay) << "Virtual desktop changed to" << desktop;
        hideDisabledAndRefresh();
    }
}

void OverlayService::setCurrentActivity(const QString& activityId)
{
    if (m_currentActivity != activityId) {
        m_currentActivity = activityId;
        qCInfo(lcOverlay) << "Activity changed activity=" << activityId;
        hideDisabledAndRefresh();
    }
}

void OverlayService::setupForScreen(QScreen* screen)
{
    // Set up overlay windows for all effective screens on this physical screen
    auto* mgr = m_screenManager;
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    if (mgr && mgr->hasVirtualScreens(physId)) {
        for (const QString& vsId : mgr->virtualScreenIdsFor(physId)) {
            if (!m_screenStates.contains(vsId) || !m_screenStates[vsId].overlayWindow) {
                QRect vsGeom = mgr->screenGeometry(vsId);
                if (!vsGeom.isValid()) {
                    qCWarning(lcOverlay) << "setupForScreen: invalid geometry for virtual screen" << vsId
                                         << "— skipping overlay creation";
                    continue;
                }
                createOverlayWindow(vsId, screen, vsGeom);
            }
        }
    } else {
        if (!m_screenStates.contains(physId) || !m_screenStates[physId].overlayWindow) {
            createOverlayWindow(screen);
        }
    }
}

void OverlayService::removeScreen(QScreen* screen)
{
    destroyOverlayWindow(screen);
}

void OverlayService::assertWindowOnScreen(QWindow* window, QScreen* screen, const QRect& geometry)
{
    if (!window || !screen) {
        return;
    }
    if (window->screen() != screen) {
        window->setScreen(screen);
    }
    // For virtual screens (geometry differs from physical), positioning is handled by
    // LayerShellQt margins. Calling setGeometry with absolute coordinates would override
    // those margins, causing double-positioning. Only set geometry for physical screens.
    const QRect targetGeom = geometry.isValid() ? geometry : screen->geometry();
    if (targetGeom == screen->geometry()) {
        window->setGeometry(targetGeom);
    }
    // Virtual screens: size is set by the caller; position is set by LayerShellQt margins.
}

void OverlayService::handleScreenAdded(QScreen* screen)
{
    if (!m_visible || !screen) {
        return;
    }
    const QString physScreenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);

    auto* mgr = m_screenManager;
    if (mgr && mgr->hasVirtualScreens(physScreenId)) {
        // Create overlays for each virtual screen on this physical screen
        for (const QString& vsId : mgr->virtualScreenIdsFor(physScreenId)) {
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, vsId, m_currentVirtualDesktop,
                                  m_currentActivity)) {
                continue;
            }
            QRect vsGeom = mgr->screenGeometry(vsId);
            if (vsGeom.isValid()) {
                createOverlayWindow(vsId, screen, vsGeom);
                updateOverlayWindow(vsId, screen);
                if (auto* window = m_screenStates.value(vsId).overlayWindow) {
                    assertWindowOnScreen(window, screen, vsGeom);
                    window->show();
                }
            }
        }
    } else {
        createOverlayWindow(screen);
        updateOverlayWindow(screen);
        if (auto* window = m_screenStates.value(physScreenId).overlayWindow) {
            assertWindowOnScreen(window, screen);
            window->show();
        }
    }
}

void OverlayService::destroyAllWindowsForPhysicalScreen(QScreen* screen)
{
    // Remove all windows associated with this physical screen
    // (includes any virtual screens on this physical screen)
    const QStringList screenIds = m_screenStates.keys();
    for (const QString& id : screenIds) {
        const auto& state = m_screenStates[id];
        if (state.overlayPhysScreen == screen || state.zoneSelectorPhysScreen == screen
            || state.notificationPhysScreen == screen) {
            destroyOverlayWindow(id);
            destroyZoneSelectorWindow(id);
            destroyNotificationWindow(id);
            // If every window for this screen-id was already released (or
            // this state entry never actually held any — e.g. an OSD
            // creation failed earlier), drop the empty shell so screen
            // hot-plug cycles don't slowly accumulate dead keys. Matches
            // cleanupVirtualScreenStates semantics: the state entry is
            // meaningless without at least one live window.
            auto& s = m_screenStates[id];
            if (!s.overlaySurface && !s.zoneSelectorSurface && !s.notificationSurface) {
                m_screenStates.remove(id);
            }
        }
    }

    // Clean up snap assist and layout picker if on this physical screen
    if (m_snapAssistScreen == screen) {
        destroySnapAssistWindow();
    }
    if (m_layoutPickerScreen == screen) {
        destroyLayoutPickerWindow();
    }

    // Drop notification-window "creation failed" sentinels for screen ids
    // rooted on this physical screen. Without this, if the same physical
    // monitor is reconnected (hot-plug cycle) it inherits the stale flag
    // and we silently refuse to recreate the OSD. Matching is prefix-based
    // because virtual-screen ids embed the physical id as the prefix.
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    if (!physId.isEmpty()) {
        const QString vsPrefix = physId + PhosphorIdentity::VirtualScreenId::Separator;
        for (auto it = m_notificationCreationFailed.begin(); it != m_notificationCreationFailed.end();) {
            if (*it == physId || it->startsWith(vsPrefix)) {
                it = m_notificationCreationFailed.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Drop the dedup sentinel for this physical screen so a hot-plug cycle
    // doesn't suppress the first navigation OSD on the reconnected monitor
    // when it lands inside the implicit 200 ms timeout window. The dedup is
    // keyed on the screenId at time-of-fire, and a removed-then-readded
    // monitor reuses the same id — without this clear, a navigation action
    // on the readded screen within 200 ms of the last action on the
    // pre-removal incarnation gets silently swallowed.
    if (!physId.isEmpty()
        && (m_lastNavigationScreenId == physId
            || m_lastNavigationScreenId.startsWith(physId + PhosphorIdentity::VirtualScreenId::Separator))) {
        m_lastNavigationActionKey.clear();
        m_lastNavigationScreenId.clear();
        m_lastNavigationTime.invalidate();
    }
}

void OverlayService::handleScreenRemoved(QScreen* screen)
{
    destroyAllWindowsForPhysicalScreen(screen);
}

OverlayService::LayoutIncludeFlags OverlayService::resolvePerScreenLayoutInclude(const QString& screenId) const
{
    // Both buildLayoutsList (populates the popup) and visibleLayoutCount
    // (used by isNearTriggerEdge to size the keep-visible bar) go through
    // here so the trigger geometry matches the rendered popup row count.
    // If the resolver ever skips setting one of the fields, the struct's
    // in-class defaults (both true) supply a safe "show everything"
    // fallback rather than UB.
    LayoutIncludeFlags flags{m_includeManualLayouts, m_includeAutotileLayouts};
    if (!m_layoutManager) {
        return flags;
    }
    const QString resolvedId = Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)
        ? Phosphor::Screens::ScreenIdentity::idForName(screenId)
        : screenId;
    if (resolvedId.isEmpty()) {
        return flags;
    }
    const QString assignmentId =
        m_layoutManager->assignmentIdForScreen(resolvedId, m_currentVirtualDesktop, m_currentActivity);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
        flags.manual = false;
        flags.autotile = true;
    } else {
        flags.manual = true;
        flags.autotile = false;
    }
    return flags;
}

QVariantList OverlayService::buildLayoutsList(const QString& screenId, QSize autotilePreviewCanvas) const
{
    const auto inc = resolvePerScreenLayoutInclude(screenId);
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, screenId, m_currentVirtualDesktop, m_currentActivity, inc.manual,
        inc.autotile, Utils::screenAspectRatio(m_screenManager, screenId),
        m_settings && m_settings->filterLayoutsByAspectRatio(),
        PhosphorZones::LayoutUtils::buildCustomOrder(m_settings, inc.manual, inc.autotile), m_autotileLayoutSource,
        autotilePreviewCanvas);
    return PlasmaZones::toVariantList(entries);
}

void OverlayService::setLayoutFilter(bool includeManual, bool includeAutotile)
{
    if (m_includeManualLayouts == includeManual && m_includeAutotileLayouts == includeAutotile) {
        return;
    }
    m_includeManualLayouts = includeManual;
    m_includeAutotileLayouts = includeAutotile;
    // Refresh visible zone selector windows with updated layout list
    refreshVisibleWindows();
}

void OverlayService::setExcludedScreens(const QSet<QString>& screenIds)
{
    m_excludedScreens = screenIds;
}

int OverlayService::visibleLayoutCount(const QString& screenId) const
{
    // Mirror buildLayoutsList's per-screen include resolution. Pre-fix the
    // raw m_includeManualLayouts/m_includeAutotileLayouts flags were used
    // here — both default true — so on screens where the popup actually
    // showed only manual (or only autotile) layouts, this returned the
    // sum of both, inflating the row count and blowing barHeight up to
    // ~screen height. isNearTriggerEdge then kept the popup visible
    // wherever the cursor was during the drag.
    const auto inc = resolvePerScreenLayoutInclude(screenId);
    // Ordering doesn't affect count — skip custom order for performance.
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, screenId, m_currentVirtualDesktop, m_currentActivity, inc.manual,
        inc.autotile, Utils::screenAspectRatio(m_screenManager, screenId),
        m_settings && m_settings->filterLayoutsByAspectRatio(),
        /*customOrder=*/{}, m_autotileLayoutSource);
    return entries.size();
}

void OverlayService::onPrepareForSleep(bool goingToSleep)
{
    if (goingToSleep) {
        // System going to sleep - nothing to do
        return;
    }

    // System waking up - restart shader timer to avoid large iTimeDelta
    QMutexLocker locker(&m_shaderTimerMutex);
    if (m_visible && m_shaderTimer.isValid()) {
        m_shaderTimer.restart();
        m_lastFrameTime.store(0);
        qCInfo(lcOverlay) << "Shader timer restarted after system resume";
    }
}

void OverlayService::onShaderError(const QString& errorLog)
{
    qCWarning(lcOverlay) << "Shader error during overlay:" << errorLog;
    m_pendingShaderError = errorLog;
    // Don't set m_shaderErrorPending - retry shaders on next show (fix bugs, don't mask)
}

} // namespace PlasmaZones
