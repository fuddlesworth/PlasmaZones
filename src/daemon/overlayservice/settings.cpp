// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "qml_property_names.h"
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include "../../core/cavaoptions.h"
#include "../../core/logging.h"
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include "../../core/shaderregistry.h"
#include "../../core/utils.h"
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

namespace PlasmaZones {

void OverlayService::setSettings(ISettings* settings)
{
    if (m_settings != settings) {
        // Single-sweep disconnect of every (m_settings → this) connection,
        // fail-safe vs. future connects that forget a paired disconnect.
        if (m_settings) {
            disconnect(m_settings, nullptr, this, nullptr);
        }
        // The shader-registry connection (different sender, see
        // m_shaderRegistry below) is NOT covered by the sweep above and
        // is tracked separately via m_shadersChangedConnection so a
        // future second shadersChanged slot on this receiver can't be
        // accidentally severed by a (src, sig, this, nullptr) call.
        if (m_shadersChangedConnection) {
            QObject::disconnect(m_shadersChangedConnection);
            m_shadersChangedConnection = {};
        }

        m_settings = settings;

        // Connect to new settings signals
        if (m_settings) {
            auto refreshZoneSelectors = [this]() {
                for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
                    updateZoneSelectorWindow(it.key());
                }
            };
            // updateZoneSelectorWindow reads ~20 settings (zone padding, border
            // width / radius, font, color, plus per-screen resolved
            // ZoneSelectorConfig fields) and pushes them as QML properties.
            // Connecting to ~20 specific *Changed signals would track the
            // dependency graph manually with no functional difference: QML
            // property writes short-circuit on equal value, so the worst case
            // for the catch-all is N redundant property lookups across the
            // selector windows - measured in microseconds. The catch-all is
            // the maintenance-cheap choice; specific connections below cover
            // the cases where the response is structurally different
            // (overlay-window recreation, audio-spectrum start/stop, shader
            // tree apply).
            connect(m_settings, &ISettings::settingsChanged, this, refreshZoneSelectors);

            // Recreate overlay windows when the overlay display mode changes
            // (e.g. compact mode can't use shader overlays). Connected to the
            // specific signal instead of settingsChanged to avoid redundant work.
            connect(m_settings, &ISettings::overlayDisplayModeChanged, this,
                    &OverlayService::recreateOverlayWindowsOnTypeMismatch);

            connect(m_settings, &ISettings::enableAudioVisualizerChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioSpectrumBarCountChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::shaderFrameRateChanged, this, &OverlayService::syncCavaState);
            // The full CAVA analysis parameter set (Shaders.Audio). Every knob
            // routes through the same reconcile: setOptions no-ops on an
            // unchanged set and restarts capture at most once per change.
            connect(m_settings, &ISettings::audioAutosensChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioSensitivityChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioNoiseReductionChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioLowerCutoffHzChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioHigherCutoffHzChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioMonstercatChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioWavesChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioChannelModeChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioReverseChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioExtraSmoothingChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioInputMethodChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioInputSourceChanged, this, &OverlayService::syncCavaState);

            // Shader profile tree drives the per-overlay shader effect (osd.show,
            // popup.zoneSelector, etc.). Push it into the SurfaceAnimator
            // now that settings are available, and re-push on every edit so
            // users editing the tree at runtime see the new effects on the next
            // show - no daemon restart needed. registerConfigForRole only
            // affects subsequent show()/hide(), so animations mid-flight keep
            // their bound config (matches motion-tree live-reload semantics).
            applyShaderProfilesToAnimator(m_settings->shaderProfileTree());
            connect(m_settings, &ISettings::shaderProfileTreeChanged, this, [this]() {
                if (m_settings) {
                    applyShaderProfilesToAnimator(m_settings->shaderProfileTree());
                }
            });

            // Per-surface decoration tree drives each overlay's decoration
            // (border/titlebar + shader-pack chain), resolved + pushed at show
            // time. Re-apply it to any currently-visible transient overlay on a
            // live edit so the decoration preview updates without waiting for the
            // next show. Connected to the specific signal (not the settingsChanged
            // catch-all) so unrelated edits don't re-bake decoration; each
            // applyDecoration is null-safe per slot, so screens without a wired
            // slot are skipped. OSDs are intentionally omitted — they auto-dismiss
            // sub-second, so a live re-decorate has no observable effect.
            connect(m_settings, &ISettings::decorationProfileTreeChanged, this, [this]() {
                for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
                    const auto& state = it.value();
                    if (m_zoneSelectorVisible)
                        applyDecoration(state.zoneSelectorSlot(), QStringLiteral("popup.zoneSelector"));
                    if (m_snapAssistVisible)
                        applyDecoration(state.snapAssistSlot(), QStringLiteral("popup.snapAssist"));
                    if (m_layoutPickerVisible)
                        applyDecoration(state.layoutPickerSlot(), QStringLiteral("popup.layoutPicker"));
                }
            });

            // Global animations toggle: when off, SurfaceAnimator snaps
            // beginShow / beginHide to the target opacity and fires
            // completion synchronously, skipping motion + shader legs.
            // Mirrors the kwin-effect's `m_windowAnimator->isEnabled()`
            // gate on `tryBeginShaderForEvent` - single
            // `Settings::animationsEnabled` flag stops every animation
            // on both runtimes.
            if (m_surfaceAnimator) {
                m_surfaceAnimator->setEnabled(m_settings->animationsEnabled());
            }
            connect(m_settings, &ISettings::animationsEnabledChanged, this, [this]() {
                if (m_settings && m_surfaceAnimator) {
                    m_surfaceAnimator->setEnabled(m_settings->animationsEnabled());
                }
            });

            // Hot-reload shaders when files change on disk.
            // ShaderRegistry detects file changes via QFileSystemWatcher and emits
            // shadersChanged(). We tell each overlay window's ZoneShaderItem to
            // re-read its source from disk by invoking reloadShader() (inherited
            // Q_INVOKABLE from PhosphorRendering::ShaderEffect).
            // Daemon must call setShaderRegistry() before the first updateSettings()
            // - without it, this branch is silently skipped and on-disk shader edits
            // won't propagate until the next daemon restart.
            if (m_shaderRegistry) {
                m_shadersChangedConnection = connect(m_shaderRegistry, &ShaderRegistry::shadersChanged, this, [this]() {
                    qCInfo(lcOverlay) << "Shader files changed on disk, triggering hot-reload";
                    PhosphorRendering::ShaderCompiler::clearCache();
                    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
                        if (!it_.value().overlayPhysScreen) {
                            continue;
                        }
                        auto* slot = it_.value().mainOverlaySlot();
                        if (slot && slot->property("useShader").toBool()) {
                            QMetaObject::invokeMethod(slot, "reloadShader");
                        }
                    }
                    if (m_shaderPreviewWindow
                        && m_shaderPreviewWindow->property(OverlayQmlPropertyNames::IsShaderOverlay.data()).toBool()) {
                        QMetaObject::invokeMethod(m_shaderPreviewWindow, "reloadShader");
                    }
                });
            }

            // Reconcile CAVA with current settings + visibility. At boot the
            // overlay is hidden, so this no longer starts CAVA — it spins up on
            // the first show() and stops (after a grace period) on hide().
            syncCavaState();
        }
    }
}

void OverlayService::setLayoutManager(PhosphorZones::IZoneLayoutRegistry* layoutManager)
{
    // Disconnect from old layout manager if exists. The four catalog /
    // assignment signals are declared on PhosphorZones::IZoneLayoutRegistry
    // and reach this slot via Qt's metaobject signal table.
    if (m_layoutManager) {
        disconnect(m_layoutManager, &PhosphorZones::IZoneLayoutRegistry::activeLayoutChanged, this, nullptr);
        disconnect(m_layoutManager, &PhosphorZones::IZoneLayoutRegistry::layoutAssigned, this, nullptr);
        disconnect(m_layoutManager, &PhosphorZones::IZoneLayoutRegistry::layoutAdded, this, nullptr);
        disconnect(m_layoutManager, &PhosphorZones::IZoneLayoutRegistry::layoutRemoved, this, nullptr);
    }
    // Disconnect any per-PhosphorZones::Layout connections to active layouts the previous manager owned
    for (const QPointer<PhosphorZones::Layout>& layout : std::as_const(m_observedLayouts)) {
        if (layout) {
            disconnect(layout, &PhosphorZones::Layout::layoutModified, this, nullptr);
        }
    }
    m_observedLayouts.clear();

    m_layoutManager = layoutManager;

    if (m_layoutManager) {
        // Update visible zone selector and overlay windows when layout changes.
        // Hidden windows are skipped: showZoneSelector()/show() refresh before showing.
        connect(m_layoutManager, &PhosphorZones::IZoneLayoutRegistry::activeLayoutChanged, this,
                [this](PhosphorZones::Layout* layout) {
                    observeLayoutForLiveEdits(layout);
                    refreshVisibleWindows();
                });
        connect(m_layoutManager, &PhosphorZones::IZoneLayoutRegistry::layoutAssigned, this,
                [this](const QString& /*screenId*/, int /*virtualDesktop*/, PhosphorZones::Layout* layout) {
                    observeLayoutForLiveEdits(layout);
                    refreshVisibleWindows();
                });
        // Observe newly-created layouts so edits reach the overlay before
        // the layout is ever activated/assigned (e.g. user creates a new
        // layout in the editor and immediately tweaks its shader).
        connect(m_layoutManager, &PhosphorZones::IZoneLayoutRegistry::layoutAdded, this,
                [this](PhosphorZones::Layout* layout) {
                    observeLayoutForLiveEdits(layout);
                });
        // Drop per-layout connections + the m_observedLayouts entry when
        // a layout is deleted. Without this, QPointer auto-null would
        // leave tombstone entries in m_observedLayouts that only get
        // compacted the next time observeLayoutForLiveEdits() runs:
        // unbounded growth during editor create/delete sessions.
        connect(m_layoutManager, &PhosphorZones::IZoneLayoutRegistry::layoutRemoved, this,
                &OverlayService::stopObservingLayout);

        // Observe EVERY loaded layout, not just the globally-active one.
        // A per-screen-assigned layout loaded from disk at startup never
        // triggers activeLayoutChanged / layoutAssigned, so its
        // layoutModified signal would otherwise be invisible to us:
        // editor edits to its shader/zones required a daemon restart to
        // take effect. Observing the whole set is cheap (one signal
        // connection per layout) and idempotent thanks to the dedupe
        // pass in observeLayoutForLiveEdits.
        for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
            observeLayoutForLiveEdits(layout);
        }
        // Redundant after the loop, but keeps the intent obvious in case
        // activeLayout() is ever loaded through a different path than
        // PhosphorZones::IZoneLayoutRegistry::layouts().
        observeLayoutForLiveEdits(m_layoutManager->activeLayout());
    }
}

void OverlayService::setAlgorithmRegistry(PhosphorTiles::ITileAlgorithmRegistry* registry)
{
    m_algorithmRegistry = registry;
}

void OverlayService::setAutotileLayoutSource(PhosphorLayout::ILayoutSource* source)
{
    m_autotileLayoutSource = source;
}

void OverlayService::observeLayoutForLiveEdits(PhosphorZones::Layout* layout)
{
    if (!layout) {
        return;
    }
    // Walk the list, skipping null QPointers (entries auto-cleared on PhosphorZones::Layout destroy).
    // Compact stale entries while we're at it so the list doesn't grow without bound.
    for (auto it = m_observedLayouts.begin(); it != m_observedLayouts.end();) {
        if (it->isNull()) {
            it = m_observedLayouts.erase(it);
        } else if (it->data() == layout) {
            return; // Already observing
        } else {
            ++it;
        }
    }
    // PhosphorZones::Layout::layoutModified fires whenever any Q_PROPERTY changes (shaderId,
    // shaderParams, zones, appearance, etc.). Without this hook the editor's
    // changes only reach the live overlay after a layout switch or daemon
    // restart, since PhosphorZones::IZoneLayoutRegistry::activeLayoutChanged only fires on switch.
    //
    // Route through a coalescing shim: zone-drag in the editor can fire
    // layoutModified dozens of times per second; refreshVisibleWindows is
    // the expensive path (rebuilds zone variant lists + uploads labels).
    // The shim schedules a single refresh at the next event-loop tick.
    connect(layout, &PhosphorZones::Layout::layoutModified, this, [this]() {
        if (m_refreshCoalescePending) {
            return;
        }
        m_refreshCoalescePending = true;
        // 16 ms ≈ one display frame - small enough that live-edit feels
        // instant, large enough that a burst of Q_PROPERTY writes collapses
        // into one refresh.
        QTimer::singleShot(16, this, [this]() {
            m_refreshCoalescePending = false;
            // A layout shaderId edit can flip a screen between rectangle and
            // shader overlay modes (none↔shader). refreshVisibleWindows alone
            // can't apply that flip: updateOverlayWindow's shader-apply branch
            // is gated on the slot's CURRENT useShader mode, so a newly-enabled
            // shader is skipped and the overlay keeps drawing rectangles until
            // a hide/show (or daemon restart) rebuilds the slot. Run the
            // type-mismatch recreate first — a no-op when no flip is needed —
            // mirroring the overlayDisplayMode setting path so live edits take
            // effect immediately. Only meaningful while visible; a hidden
            // overlay rebuilds with the correct type on its next show.
            if (m_visible) {
                recreateOverlayWindowsOnTypeMismatch();
            }
            refreshVisibleWindows();
        });
    });
    m_observedLayouts.append(QPointer<PhosphorZones::Layout>(layout));
}

void OverlayService::stopObservingLayout(PhosphorZones::Layout* layout)
{
    if (!layout) {
        return;
    }
    disconnect(layout, &PhosphorZones::Layout::layoutModified, this, nullptr);
    for (auto it = m_observedLayouts.begin(); it != m_observedLayouts.end();) {
        if (it->isNull() || it->data() == layout) {
            it = m_observedLayouts.erase(it);
        } else {
            ++it;
        }
    }
}

void OverlayService::refreshVisibleWindows()
{
    if (m_zoneSelectorVisible) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            updateZoneSelectorWindow(it.key());
        }
    }
    if (m_visible) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            QScreen* physScreen = it.value().overlayPhysScreen;
            if (physScreen) {
                updateOverlayWindow(it.key(), physScreen);
            }
        }
    }
}

// Grace period after the overlay goes idle before the render loop + CAVA are
// actually stopped, so rapid drag thrash (modifier release/re-press, quick
// re-trigger) keeps everything warm and avoids per-show spin-up.
static constexpr int kIdleQuiesceGraceMs = 5000;

bool OverlayService::isOverlayDisplaying() const
{
    const bool previewVisible = m_shaderPreviewWindow && m_shaderPreviewWindow->isVisible();
    return (m_visible && !m_overlayIdled) || previewVisible;
}

void OverlayService::syncCavaState()
{
    if (!m_audioProvider || !m_settings) {
        return;
    }

    // CAVA is a continuous audio-capture + FFT child process feeding a per-frame
    // spectrum; running it while nothing is displayed burns CPU on capture AND
    // on per-frame overlay repaints. Run it only while audio-viz is enabled AND
    // something that reacts to audio is on screen: the overlay (un-idled), the
    // editor's shader preview, or a decoration surface (OSD / popup) carrying an
    // audio-reactive pack. A plain decoration never starts audio (it declares no
    // `audio` flag, so visibleAudioDecorationSlots() ignores it).
    const bool wantRun =
        m_settings->enableAudioVisualizer() && (isOverlayDisplaying() || !visibleAudioDecorationSlots().isEmpty());

    if (wantRun) {
        if (m_idleQuiesceTimer) {
            m_idleQuiesceTimer->stop(); // cancel any pending grace-period quiesce
        }
        m_audioProvider->setOptions(cavaOptionsFromSettings(m_settings));
        if (!m_audioProvider->isRunning()) {
            m_audioProvider->start();
        }
        return;
    }

    // Audio-viz turned OFF entirely: stop now and clear any stale spectrum from
    // the surfaces. Merely idle/hidden (still enabled): defer via the grace
    // timer so a quick re-trigger keeps it warm.
    if (!m_settings->enableAudioVisualizer()) {
        if (m_idleQuiesceTimer) {
            m_idleQuiesceTimer->stop();
        }
        if (m_audioProvider->isRunning()) {
            m_audioProvider->stop();
            for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
                const auto& st = it_.value();
                if (st.overlayPhysScreen) {
                    if (auto* slot = st.mainOverlaySlot()) {
                        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::AudioSpectrum), QVariantList());
                    }
                }
                // Decoration slots (OSD / popups) carry their own audioSpectrum,
                // so an audio-reactive border must settle to silence too rather
                // than freeze on the last pushed frame. Independent of the zone
                // overlay, so cleared regardless of overlayPhysScreen.
                for (QQuickItem* deco :
                     {st.osdSlot(), st.snapAssistSlot(), st.layoutPickerSlot(), st.zoneSelectorSlot()}) {
                    if (deco) {
                        writeQmlProperty(deco, QString(OverlayQmlPropertyNames::AudioSpectrum), QVariantList());
                    }
                }
            }
            if (m_shaderPreviewWindow) {
                writeQmlProperty(m_shaderPreviewWindow, QString(OverlayQmlPropertyNames::AudioSpectrum),
                                 QVariantList());
            }
        }
        return;
    }
    scheduleIdleQuiesce();
}

void OverlayService::scheduleIdleQuiesce()
{
    // Nothing to wind down if neither the render loop nor CAVA is active.
    const bool shaderTimerActive = m_shaderUpdateTimer && m_shaderUpdateTimer->isActive();
    const bool cavaActive = m_audioProvider && m_audioProvider->isRunning();
    if (!shaderTimerActive && !cavaActive) {
        return;
    }
    if (!m_idleQuiesceTimer) {
        m_idleQuiesceTimer = new QTimer(this);
        m_idleQuiesceTimer->setSingleShot(true);
        m_idleQuiesceTimer->setInterval(kIdleQuiesceGraceMs);
        connect(m_idleQuiesceTimer, &QTimer::timeout, this, [this]() {
            // Re-check at fire time: a show()/refreshFromIdle() within the grace
            // window both cancels this timer and resumes, but guard anyway. The
            // overlay's QQuickWindows are intentionally left alive (NVIDIA
            // teardown-deadlock avoidance); we only pause the 60 Hz shader
            // render loop and the CAVA capture. Mirror syncCavaState's wantRun
            // predicate: a visible audio decoration also keeps CAVA alive, so it
            // must veto the quiesce too.
            if (isOverlayDisplaying() || !visibleAudioDecorationSlots().isEmpty()) {
                return;
            }
            stopShaderAnimation();
            if (m_audioProvider && m_audioProvider->isRunning()) {
                m_audioProvider->stop();
            }
        });
    }
    m_idleQuiesceTimer->start();
}

} // namespace PlasmaZones
