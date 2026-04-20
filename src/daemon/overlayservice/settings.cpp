// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../cavaservice.h"
#include <PhosphorRendering/ShaderCompiler.h>
#include "../../core/logging.h"
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "../../core/shaderregistry.h"
#include "../../core/utils.h"
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

namespace PlasmaZones {

void OverlayService::setSettings(ISettings* settings)
{
    if (m_settings != settings) {
        // Disconnect from old settings signals
        if (m_settings) {
            disconnect(m_settings, &ISettings::settingsChanged, this, nullptr);
            disconnect(m_settings, &ISettings::overlayDisplayModeChanged, this, nullptr);
            disconnect(m_settings, &ISettings::enableShaderEffectsChanged, this, nullptr);
            disconnect(m_settings, &ISettings::enableAudioVisualizerChanged, this, nullptr);
            disconnect(m_settings, &ISettings::audioSpectrumBarCountChanged, this, nullptr);
            disconnect(m_settings, &ISettings::shaderFrameRateChanged, this, nullptr);
        }
        // Disconnect the specific shadersChanged lambda we stashed below.
        // disconnect(src, sig, this, nullptr) would sever ALL slots on this
        // receiver — fine today, but a trap if another shadersChanged
        // handler is ever added.
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
            connect(m_settings, &ISettings::settingsChanged, this, refreshZoneSelectors);

            // Recreate overlay windows when the overlay display mode changes
            // (e.g. compact mode can't use shader overlays). Connected to the
            // specific signal instead of settingsChanged to avoid redundant work.
            connect(m_settings, &ISettings::overlayDisplayModeChanged, this,
                    &OverlayService::recreateOverlayWindowsOnTypeMismatch);

            connect(m_settings, &ISettings::enableShaderEffectsChanged, this, [this]() {
                if (m_visible) {
                    recreateOverlayWindowsOnTypeMismatch();
                }
            });

            connect(m_settings, &ISettings::enableAudioVisualizerChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioSpectrumBarCountChanged, this, &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::shaderFrameRateChanged, this, &OverlayService::syncCavaState);

            // Hot-reload shaders when files change on disk.
            // ShaderRegistry detects file changes via QFileSystemWatcher and emits
            // shadersChanged(). We tell each overlay window's ZoneShaderItem to
            // re-read its source from disk by invoking reloadShader() (inherited
            // Q_INVOKABLE from PhosphorRendering::ShaderEffect).
            // Daemon must call setShaderRegistry() before the first updateSettings()
            // — without it, this branch is silently skipped and on-disk shader edits
            // won't propagate until the next daemon restart.
            if (m_shaderRegistry) {
                m_shadersChangedConnection = connect(m_shaderRegistry, &ShaderRegistry::shadersChanged, this, [this]() {
                    if (!m_settings || !m_settings->enableShaderEffects()) {
                        return;
                    }
                    qCInfo(lcOverlay) << "Shader files changed on disk, triggering hot-reload";
                    PhosphorRendering::ShaderCompiler::clearCache();
                    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
                        auto* window = it_.value().overlayWindow;
                        if (window && window->property("isShaderOverlay").toBool()) {
                            QMetaObject::invokeMethod(window, "reloadShader");
                        }
                    }
                    if (m_shaderPreviewWindow && m_shaderPreviewWindow->property("isShaderOverlay").toBool()) {
                        QMetaObject::invokeMethod(m_shaderPreviewWindow, "reloadShader");
                    }
                });
            }

            // Eagerly start CAVA at daemon boot so spectrum data is warm when overlay shows
            syncCavaState();
        }
    }
}

void OverlayService::setLayoutManager(PhosphorZones::LayoutRegistry* layoutManager)
{
    // Disconnect from old layout manager if exists
    if (m_layoutManager) {
        auto* oldManager = dynamic_cast<PhosphorZones::LayoutRegistry*>(m_layoutManager);
        if (oldManager) {
            disconnect(oldManager, &PhosphorZones::LayoutRegistry::activeLayoutChanged, this, nullptr);
            disconnect(oldManager, &PhosphorZones::LayoutRegistry::layoutAssigned, this, nullptr);
            disconnect(oldManager, &PhosphorZones::LayoutRegistry::layoutAdded, this, nullptr);
            disconnect(oldManager, &PhosphorZones::LayoutRegistry::layoutRemoved, this, nullptr);
        }
    }
    // Disconnect any per-PhosphorZones::Layout connections to active layouts the previous manager owned
    for (const QPointer<PhosphorZones::Layout>& layout : std::as_const(m_observedLayouts)) {
        if (layout) {
            disconnect(layout, &PhosphorZones::Layout::layoutModified, this, nullptr);
        }
    }
    m_observedLayouts.clear();

    m_layoutManager = layoutManager;

    // Connect to layout change signals from the concrete PhosphorZones::LayoutRegistry
    // PhosphorZones::LayoutRegistry is a pure interface without signals, so we need to cast
    if (m_layoutManager) {
        auto* manager = dynamic_cast<PhosphorZones::LayoutRegistry*>(m_layoutManager);
        if (manager) {
            // Update visible zone selector and overlay windows when layout changes.
            // Hidden windows are skipped — showZoneSelector()/show() refresh before showing.
            connect(manager, &PhosphorZones::LayoutRegistry::activeLayoutChanged, this,
                    [this](PhosphorZones::Layout* layout) {
                        observeLayoutForLiveEdits(layout);
                        refreshVisibleWindows();
                    });
            connect(manager, &PhosphorZones::LayoutRegistry::layoutAssigned, this,
                    [this](const QString& /*screenId*/, int /*virtualDesktop*/, PhosphorZones::Layout* layout) {
                        observeLayoutForLiveEdits(layout);
                        refreshVisibleWindows();
                    });
            // Observe newly-created layouts so edits reach the overlay before
            // the layout is ever activated/assigned (e.g. user creates a new
            // layout in the editor and immediately tweaks its shader).
            connect(manager, &PhosphorZones::LayoutRegistry::layoutAdded, this, [this](PhosphorZones::Layout* layout) {
                observeLayoutForLiveEdits(layout);
            });
            // Drop per-layout connections + the m_observedLayouts entry when
            // a layout is deleted. Without this, QPointer auto-null would
            // leave tombstone entries in m_observedLayouts that only get
            // compacted the next time observeLayoutForLiveEdits() runs —
            // unbounded growth during editor create/delete sessions.
            connect(manager, &PhosphorZones::LayoutRegistry::layoutRemoved, this, &OverlayService::stopObservingLayout);

            // Observe EVERY loaded layout, not just the globally-active one.
            // A per-screen-assigned layout loaded from disk at startup never
            // triggers activeLayoutChanged / layoutAssigned, so its
            // layoutModified signal would otherwise be invisible to us —
            // editor edits to its shader/zones required a daemon restart to
            // take effect. Observing the whole set is cheap (one signal
            // connection per layout) and idempotent thanks to the dedupe
            // pass in observeLayoutForLiveEdits.
            for (PhosphorZones::Layout* layout : manager->layouts()) {
                observeLayoutForLiveEdits(layout);
            }
            // Redundant after the loop, but keeps the intent obvious in case
            // activeLayout() is ever loaded through a different path than
            // PhosphorZones::LayoutRegistry::layouts().
            observeLayoutForLiveEdits(manager->activeLayout());
        }
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
    // restart, since PhosphorZones::LayoutRegistry::activeLayoutChanged only fires on switch.
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
        // 16 ms ≈ one display frame — small enough that live-edit feels
        // instant, large enough that a burst of Q_PROPERTY writes collapses
        // into one refresh.
        QTimer::singleShot(16, this, [this]() {
            m_refreshCoalescePending = false;
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

void OverlayService::syncCavaState()
{
    if (!m_cavaService || !m_settings) {
        return;
    }
    if (m_settings->enableAudioVisualizer()) {
        m_cavaService->setBarCount(m_settings->audioSpectrumBarCount());
        m_cavaService->setFramerate(m_settings->shaderFrameRate());
        if (!m_cavaService->isRunning()) {
            m_cavaService->start();
        }
    } else {
        if (m_cavaService->isRunning()) {
            m_cavaService->stop();
            for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
                auto* window = it_.value().overlayWindow;
                if (window) {
                    writeQmlProperty(window, QStringLiteral("audioSpectrum"), QVariantList());
                }
            }
            if (m_shaderPreviewWindow) {
                writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("audioSpectrum"), QVariantList());
            }
        }
    }
}

} // namespace PlasmaZones
