// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

class ISettings;

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
///
/// Pure CONSTANT facade — no per-page staged state; isDirty/apply/discard
/// are no-ops. Dirty tracking is global through SettingsController's
/// meta-object loop on Settings's Q_PROPERTYs: any rendering-backend
/// selection that calls `m_settings.setRenderingBackend` trips the
/// Q_PROPERTY NOTIFY, which SettingsController's `onSettingsPropertyChanged`
/// slot maps to the active page (or the top of `m_externalEditStack`).
/// The "General" page therefore participates in dirty tracking via
/// the Settings property surface, not via this controller's own
/// signals.
class GeneralPageController : public PhosphorControl::PageController
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

    // Window-filtering SpinBox bounds — bound from the schema, NOT from
    // QML literals, so the SpinBox visible range always tracks the
    // schema-allowed range. A user value persisted at the high end of
    // the schema range (e.g. 1500 px when the schema allows 0–2000)
    // would otherwise silently truncate when the SpinBox clamped the
    // bound `value` to the QML literal. Both the snap-side
    // (GeneralPage's "Window filtering" card) and the animation-side
    // (AnimationsGeneralPage's filtering knobs) bind through these
    // since they share this controller.
    Q_PROPERTY(int minimumWindowWidthMin READ minimumWindowWidthMin CONSTANT)
    Q_PROPERTY(int minimumWindowWidthMax READ minimumWindowWidthMax CONSTANT)
    Q_PROPERTY(int minimumWindowHeightMin READ minimumWindowHeightMin CONSTANT)
    Q_PROPERTY(int minimumWindowHeightMax READ minimumWindowHeightMax CONSTANT)
    // Fallback shown by the General page's min-size spinboxes while the
    // daemon-seeded managed baseline rule is absent (fresh profile before the
    // first seed) — the on-by-default thresholds the seed will carry, so the
    // display doesn't jump when it lands.
    Q_PROPERTY(int minimumWindowWidthDefault READ minimumWindowWidthDefault CONSTANT)
    Q_PROPERTY(int minimumWindowHeightDefault READ minimumWindowHeightDefault CONSTANT)
    Q_PROPERTY(int animationMinimumWindowWidthMin READ animationMinimumWindowWidthMin CONSTANT)
    Q_PROPERTY(int animationMinimumWindowWidthMax READ animationMinimumWindowWidthMax CONSTANT)
    Q_PROPERTY(int animationMinimumWindowHeightMin READ animationMinimumWindowHeightMin CONSTANT)
    Q_PROPERTY(int animationMinimumWindowHeightMax READ animationMinimumWindowHeightMax CONSTANT)

public:
    /// Reference parameter, not pointer: the ISettings instance is required
    /// at construction time (to snapshot the current rendering backend) and
    /// must not be null. Taking it by reference makes the precondition a
    /// compile-time guarantee. ISettings (not the concrete Settings) per
    /// CLAUDE.md so unit tests can stub.
    explicit GeneralPageController(ISettings& settings, QObject* parent = nullptr);

    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }

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

    int minimumWindowWidthMin() const
    {
        return ConfigDefaults::minimumWindowWidthMin();
    }
    int minimumWindowWidthMax() const
    {
        return ConfigDefaults::minimumWindowWidthMax();
    }
    int minimumWindowHeightMin() const
    {
        return ConfigDefaults::minimumWindowHeightMin();
    }
    int minimumWindowHeightMax() const
    {
        return ConfigDefaults::minimumWindowHeightMax();
    }
    int minimumWindowWidthDefault() const
    {
        return ConfigDefaults::minimumWindowWidth();
    }
    int minimumWindowHeightDefault() const
    {
        return ConfigDefaults::minimumWindowHeight();
    }
    int animationMinimumWindowWidthMin() const
    {
        return ConfigDefaults::animationMinimumWindowWidthMin();
    }
    int animationMinimumWindowWidthMax() const
    {
        return ConfigDefaults::animationMinimumWindowWidthMax();
    }
    int animationMinimumWindowHeightMin() const
    {
        return ConfigDefaults::animationMinimumWindowHeightMin();
    }
    int animationMinimumWindowHeightMax() const
    {
        return ConfigDefaults::animationMinimumWindowHeightMax();
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
