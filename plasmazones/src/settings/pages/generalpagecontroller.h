// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "config/configdefaults.h"

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace PlasmaZones {

class ISettings;

/// Q_PROPERTY surface for the "General" settings page.
///
/// Owns the startup-time rendering-backend and GPU-device snapshots that keep
/// the "restart required" InlineMessage visible across page navigation (the
/// backend picker's options come from SettingsController::valueOptions and
/// the schema), the enumerated GPU list for the rendering-device picker,
/// plus the animation-duration / min-distance / stagger-interval slider
/// bounds that the animation curve editors consume, plus the
/// window-filtering bounds (snap-side, animation-side and decoration-side)
/// that the shared WindowFilterCard reads.
///
/// Import/export of the full config stays on SettingsController — those are
/// top-level app actions that touch every page, not a "General" concern.
///
/// No per-page staged state; isDirty/apply/discard are no-ops. Value-based
/// dirty tracking for General flows through the pageOwnedConfigKeys manifest
/// (settingscontroller_pagetopology.cpp), with SettingsController's
/// meta-object NOTIFY loop attributing edits to the active page.
class GeneralPageController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(QString startupRenderingBackend READ startupRenderingBackend NOTIFY startupSnapshotsChanged)
    Q_PROPERTY(QString startupGpuDevice READ startupGpuDevice NOTIFY startupSnapshotsChanged)

    // {text, value} rows for the GPU picker: "auto" first, then the
    // machine's GPUs from DRM render-node enumeration (GpuDeviceList,
    // enumerated once per process — a GPU hot-plugged mid-session appears
    // after the settings app restarts). A stored value that no longer
    // matches any present GPU (card removed, config copied from another
    // machine) is appended as a synthetic "Unavailable device" row so the
    // combo can still display the persisted selection; the row is rebuilt on
    // every gpuDeviceChanged so external writes stay displayable and the
    // synthetic row disappears once a present device is selected.
    Q_PROPERTY(QVariantList availableGpus READ availableGpus NOTIFY availableGpusChanged)

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
    Q_PROPERTY(int animationMinimumWindowWidthMin READ animationMinimumWindowWidthMin CONSTANT)
    Q_PROPERTY(int animationMinimumWindowWidthMax READ animationMinimumWindowWidthMax CONSTANT)
    Q_PROPERTY(int animationMinimumWindowHeightMin READ animationMinimumWindowHeightMin CONSTANT)
    Q_PROPERTY(int animationMinimumWindowHeightMax READ animationMinimumWindowHeightMax CONSTANT)
    Q_PROPERTY(int decorationMinimumWindowWidthMin READ decorationMinimumWindowWidthMin CONSTANT)
    Q_PROPERTY(int decorationMinimumWindowWidthMax READ decorationMinimumWindowWidthMax CONSTANT)
    Q_PROPERTY(int decorationMinimumWindowHeightMin READ decorationMinimumWindowHeightMin CONSTANT)
    Q_PROPERTY(int decorationMinimumWindowHeightMax READ decorationMinimumWindowHeightMax CONSTANT)

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

    /// Re-snapshot the startup backend/GPU values from the live settings.
    /// SettingsController calls this on the daemon's running rising edge:
    /// a freshly started daemon has just read the current config, so the
    /// "restart required" banner must stop comparing against the values the
    /// SETTINGS APP started with.
    void rebaselineStartupSnapshots();

    QString startupRenderingBackend() const
    {
        return m_startupRenderingBackend;
    }
    QString startupGpuDevice() const
    {
        return m_startupGpuDevice;
    }
    QVariantList availableGpus() const
    {
        return m_availableGpus;
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
    int decorationMinimumWindowWidthMin() const
    {
        return ConfigDefaults::decorationMinimumWindowWidthMin();
    }
    int decorationMinimumWindowWidthMax() const
    {
        return ConfigDefaults::decorationMinimumWindowWidthMax();
    }
    int decorationMinimumWindowHeightMin() const
    {
        return ConfigDefaults::decorationMinimumWindowHeightMin();
    }
    int decorationMinimumWindowHeightMax() const
    {
        return ConfigDefaults::decorationMinimumWindowHeightMax();
    }

Q_SIGNALS:
    /// Both startup snapshots were re-baselined (daemon restart observed).
    void startupSnapshotsChanged();
    /// The GPU picker rows changed (external gpuDevice write added or
    /// removed the synthetic "Unavailable device" row).
    void availableGpusChanged();

private Q_SLOTS:
    /// Rebuild the picker rows from the cached enumeration + the current
    /// stored value. Connected to ISettings::gpuDeviceChanged.
    void rebuildAvailableGpus();

private:
    ISettings& m_settings;

    /// Backend / GPU values at controller construction (re-baselined on
    /// daemon restart). Survive page recreation so the "restart required"
    /// InlineMessage stays visible after navigating away and back.
    QString m_startupRenderingBackend;
    QString m_startupGpuDevice;

    /// Raw GpuDeviceList::enumerate() result, cached once per process.
    QVariantList m_enumeratedGpus;
    QVariantList m_availableGpus;
};

} // namespace PlasmaZones
