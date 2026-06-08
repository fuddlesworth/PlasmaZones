// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"
#include "../core/settings_interfaces.h"
#include "../shaderpreview/ishaderpreviewbackend.h"

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorZones/ZoneJsonKeys.h>

namespace PlasmaZones {

/// Settings-app IShaderPreviewBackend.
///
/// Feeds ShaderPreviewController from the settings process's own local overlay
/// ShaderRegistry (no D-Bus round-trip — the settings app already scans the
/// shader dirs for its browser) and ISettings (audio-visualizer config). The
/// settings app has no edited layout, so previewZones() returns the shipped
/// master-stack layout so the live preview matches the baked preview.png
/// thumbnails (see previewZones() below).
///
/// Both pointers are borrowed; SettingsController owns the registry + settings
/// and outlives this backend.
class RegistryShaderPreviewBackend : public IShaderPreviewBackend
{
public:
    RegistryShaderPreviewBackend(PhosphorShaders::ShaderRegistry* registry, ISettings* settings)
        : m_registry(registry)
        , m_settings(settings)
    {
    }

    ~RegistryShaderPreviewBackend() override = default;

    QVariantMap shaderInfo(const QString& shaderId) const override
    {
        return m_registry ? m_registry->shaderInfo(shaderId) : QVariantMap();
    }

    QVariantMap translateParams(const QString& shaderId, const QVariantMap& params) const override
    {
        return m_registry ? m_registry->translateParamsToUniforms(shaderId, params) : QVariantMap();
    }

    // The SAME master-stack layout (1 master + 3-stack) the shader-render tool
    // uses to generate the static preview.png thumbnails
    // (data/layouts/master-stack.json + tools/shader-render/layoutloader.cpp),
    // so the live preview matches the shipped previews. Relative geometry
    // (fractional 0–1); the controller scales to preview pixels and applies the
    // theme's default zone appearance (only X/Y/Width/Height + ZoneNumber set).
    QVariantList previewZones() const override
    {
        const auto makeZone = [](double x, double y, double w, double h, int number) {
            QVariantMap z;
            z[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)] = x;
            z[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)] = y;
            z[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)] = w;
            z[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)] = h;
            z[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)] = number;
            return z;
        };
        return {makeZone(0.0, 0.0, 0.6, 1.0, 1), makeZone(0.6, 0.0, 0.4, 0.333333, 2),
                makeZone(0.6, 0.333333, 0.4, 0.333334, 3), makeZone(0.6, 0.666667, 0.4, 0.333333, 4)};
    }

    // Only consulted for fixed-geometry zones, of which this backend's
    // master-stack zones (all relative geometry) have none; a sane default keeps
    // the contract total.
    QSize targetScreenSize() const override
    {
        return QSize(1920, 1080);
    }

    bool audioVisualizerEnabled() const override
    {
        return m_settings && m_settings->enableAudioVisualizer();
    }

    int audioBarCount() const override
    {
        return m_settings ? m_settings->audioSpectrumBarCount() : ConfigDefaults::audioSpectrumBarCount();
    }

private:
    PhosphorShaders::ShaderRegistry* m_registry; // borrowed
    ISettings* m_settings; // borrowed
};

} // namespace PlasmaZones
