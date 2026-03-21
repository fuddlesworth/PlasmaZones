// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../cavaservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/layoutmanager.h"
#include "../../core/shaderregistry.h"
#include "../../core/utils.h"
#include <QQuickWindow>
#include <QScreen>

namespace PlasmaZones {

void OverlayService::setSettings(ISettings* settings)
{
    if (m_settings != settings) {
        // Disconnect from old settings signals
        if (m_settings) {
            disconnect(m_settings, &ISettings::settingsChanged, this, nullptr);
            disconnect(m_settings, &ISettings::enableShaderEffectsChanged, this, nullptr);
            disconnect(m_settings, &ISettings::enableAudioVisualizerChanged, this, nullptr);
            disconnect(m_settings, &ISettings::audioSpectrumBarCountChanged, this, nullptr);
            disconnect(m_settings, &ISettings::shaderFrameRateChanged, this, nullptr);
        }
        // Disconnect old ShaderRegistry connection (if any) to prevent duplicates
        if (auto* registry = ShaderRegistry::instance()) {
            disconnect(registry, &ShaderRegistry::shadersChanged, this, nullptr);
        }

        m_settings = settings;

        // Connect to new settings signals
        if (m_settings) {
            auto refreshZoneSelectors = [this]() {
                for (QScreen* screen : m_zoneSelectorWindows.keys()) {
                    updateZoneSelectorWindow(screen);
                }
            };
            connect(m_settings, &ISettings::settingsChanged, this, refreshZoneSelectors);

            connect(m_settings, &ISettings::enableShaderEffectsChanged, this, [this]() {
                // When shader effects setting changes, recreate overlay windows if visible
                // to switch between shader and non-shader overlay types
                if (m_visible) {
                    // Check if we were using shaders before the setting changed
                    // (shader timer running indicates we were using shader overlay)
                    const bool wasUsingShader = m_shaderUpdateTimer && m_shaderUpdateTimer->isActive();
                    const bool shouldUseShader = anyScreenUsesShader();

                    // Only recreate if the overlay type actually needs to change
                    if (wasUsingShader != shouldUseShader) {
                        qCInfo(lcOverlay) << "Shader effects setting changed, recreating overlay windows"
                                          << "- was=" << wasUsingShader << "now=" << shouldUseShader;

                        // Stop shader animation if it was running
                        if (wasUsingShader) {
                            stopShaderAnimation();
                        }

                        // Store current visibility state
                        const bool wasVisible = m_visible;

                        // Recreate all overlay windows (each gets correct type per-screen)
                        const auto screens = m_overlayWindows.keys();
                        for (QScreen* screen : screens) {
                            destroyOverlayWindow(screen);
                        }

                        // Recreate windows with correct type per-screen
                        for (QScreen* screen : screens) {
                            if (!m_settings || !m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
                                createOverlayWindow(screen);
                                updateOverlayWindow(screen);
                                if (wasVisible && m_overlayWindows.value(screen)) {
                                    m_overlayWindows.value(screen)->show();
                                }
                            }
                        }

                        // Start shader animation if any screen needs it
                        if (shouldUseShader && wasVisible) {
                            updateZonesForAllWindows(); // Push initial zone data
                            startShaderAnimation();
                        }
                    }
                }
            });

            connect(m_settings, &ISettings::enableAudioVisualizerChanged, this,
                    &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::audioSpectrumBarCountChanged, this,
                    &OverlayService::syncCavaState);
            connect(m_settings, &ISettings::shaderFrameRateChanged, this,
                    &OverlayService::syncCavaState);

            // Hot-reload shaders when files change on disk.
            // ShaderRegistry detects file changes via QFileSystemWatcher and emits
            // shadersChanged(). We tell each overlay window's ZoneShaderItem to
            // re-read its source from disk by invoking loadShader().
            if (auto* registry = ShaderRegistry::instance()) {
                connect(registry, &ShaderRegistry::shadersChanged, this, [this]() {
                    if (!m_settings || !m_settings->enableShaderEffects()) {
                        return;
                    }
                    qCInfo(lcOverlay) << "Shader files changed on disk, triggering hot-reload";
                    for (auto* window : std::as_const(m_overlayWindows)) {
                        if (window && window->property("isShaderOverlay").toBool()) {
                            QMetaObject::invokeMethod(window, "loadShader");
                        }
                    }
                    if (m_shaderPreviewWindow && m_shaderPreviewWindow->property("isShaderOverlay").toBool()) {
                        QMetaObject::invokeMethod(m_shaderPreviewWindow, "loadShader");
                    }
                });
            }

            // Eagerly start CAVA at daemon boot so spectrum data is warm when overlay shows
            syncCavaState();
        }
    }
}

void OverlayService::setLayoutManager(ILayoutManager* layoutManager)
{
    // Disconnect from old layout manager if exists
    if (m_layoutManager) {
        auto* oldManager = dynamic_cast<LayoutManager*>(m_layoutManager);
        if (oldManager) {
            disconnect(oldManager, &LayoutManager::activeLayoutChanged, this, nullptr);
            disconnect(oldManager, &LayoutManager::layoutAssigned, this, nullptr);
        }
    }

    m_layoutManager = layoutManager;

    // Connect to layout change signals from the concrete LayoutManager
    // ILayoutManager is a pure interface without signals, so we need to cast
    if (m_layoutManager) {
        auto* manager = dynamic_cast<LayoutManager*>(m_layoutManager);
        if (manager) {
            // Update visible zone selector and overlay windows when layout changes.
            // Hidden windows are skipped — showZoneSelector()/show() refresh before showing.
            connect(manager, &LayoutManager::activeLayoutChanged, this, [this](Layout* /*layout*/) {
                refreshVisibleWindows();
            });
            connect(manager, &LayoutManager::layoutAssigned, this,
                    [this](const QString& /*screenId*/, int /*virtualDesktop*/, Layout* /*layout*/) {
                        refreshVisibleWindows();
                    });
        }
    }
}

void OverlayService::refreshVisibleWindows()
{
    if (m_zoneSelectorVisible) {
        for (QScreen* screen : m_zoneSelectorWindows.keys()) {
            updateZoneSelectorWindow(screen);
        }
    }
    if (m_visible) {
        for (QScreen* screen : m_overlayWindows.keys()) {
            updateOverlayWindow(screen);
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
            for (auto* window : std::as_const(m_overlayWindows)) {
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
