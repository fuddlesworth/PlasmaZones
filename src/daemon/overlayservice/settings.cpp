// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../cavaservice.h"
#include <PhosphorRendering/ShaderCompiler.h>
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
            disconnect(m_settings, &ISettings::overlayDisplayModeChanged, this, nullptr);
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
                for (const QString& sid : m_screenStates.keys()) {
                    updateZoneSelectorWindow(sid);
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
            // re-read its source from disk by invoking loadShader().
            if (auto* registry = ShaderRegistry::instance()) {
                connect(registry, &ShaderRegistry::shadersChanged, this, [this]() {
                    if (!m_settings || !m_settings->enableShaderEffects()) {
                        return;
                    }
                    qCInfo(lcOverlay) << "Shader files changed on disk, triggering hot-reload";
                    PhosphorRendering::ShaderCompiler::clearCache();
                    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
                        auto* window = it_.value().overlayWindow;
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
        for (const QString& sid : m_screenStates.keys()) {
            updateZoneSelectorWindow(sid);
        }
    }
    if (m_visible) {
        for (const QString& screenId : m_screenStates.keys()) {
            QScreen* physScreen = m_screenStates.value(screenId).overlayPhysScreen;
            if (physScreen) {
                updateOverlayWindow(screenId, physScreen);
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
