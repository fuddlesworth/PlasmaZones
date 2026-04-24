// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

class Settings;

/// Q_PROPERTY surface for the "General" settings page.
///
/// Owns the rendering-backend picker data (options list, translated display
/// names, and the startup-time backend snapshot that keeps the "restart
/// required" InlineMessage visible across page navigation), plus the
/// animation-duration / min-distance / stagger-interval slider bounds that
/// the EasingSettings sub-component consumes.
///
/// Import/export of the full config stays on SettingsController — those are
/// top-level app actions that touch every page, not a "General" concern.
class GeneralPageController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QStringList renderingBackendOptions READ renderingBackendOptions CONSTANT)
    Q_PROPERTY(QStringList renderingBackendDisplayNames READ renderingBackendDisplayNames CONSTANT)
    Q_PROPERTY(QString startupRenderingBackend READ startupRenderingBackend CONSTANT)

    Q_PROPERTY(int animationDurationMin READ animationDurationMin CONSTANT)
    Q_PROPERTY(int animationDurationMax READ animationDurationMax CONSTANT)
    Q_PROPERTY(int animationMinDistanceMin READ animationMinDistanceMin CONSTANT)
    Q_PROPERTY(int animationMinDistanceMax READ animationMinDistanceMax CONSTANT)
    Q_PROPERTY(int animationStaggerIntervalMin READ animationStaggerIntervalMin CONSTANT)
    Q_PROPERTY(int animationStaggerIntervalMax READ animationStaggerIntervalMax CONSTANT)

public:
    explicit GeneralPageController(Settings* settings, QObject* parent = nullptr);

    QStringList renderingBackendOptions() const
    {
        return ConfigDefaults::renderingBackendOptions();
    }
    QStringList renderingBackendDisplayNames() const
    {
        return m_renderingBackendDisplayNames;
    }
    QString startupRenderingBackend() const
    {
        return m_startupRenderingBackend;
    }

    int animationDurationMin() const
    {
        return ConfigDefaults::animationDurationMin();
    }
    int animationDurationMax() const
    {
        return ConfigDefaults::animationDurationMax();
    }
    int animationMinDistanceMin() const
    {
        return ConfigDefaults::animationMinDistanceMin();
    }
    int animationMinDistanceMax() const
    {
        return ConfigDefaults::animationMinDistanceMax();
    }
    int animationStaggerIntervalMin() const
    {
        return ConfigDefaults::animationStaggerIntervalMin();
    }
    int animationStaggerIntervalMax() const
    {
        return ConfigDefaults::animationStaggerIntervalMax();
    }

private:
    /// Translated at construction so QML binds a stable list.
    QStringList m_renderingBackendDisplayNames;
    /// Backend value at controller construction. Survives page recreation so
    /// the "restart required" InlineMessage stays visible after navigating
    /// away and back.
    QString m_startupRenderingBackend;
};

} // namespace PlasmaZones
