// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorTiles {
class AlgorithmRegistry;
}

namespace PlasmaZones {

class ISettings;

/// Q_PROPERTY surface for the "Tiling → Algorithm" settings page.
///
/// Owns the page-specific knobs: slider bounds (autotile gap + split-ratio
/// + master-count + max-windows) and the custom-parameter CRUD surface
/// (`customParamsForAlgorithm` / `setCustomParam` / `customParamChanged`).
///
/// System-wide algorithm catalog accessors (`availableAlgorithms`,
/// `generateAlgorithmPreview`, `generateAlgorithmDefaultPreview`) intentionally
/// stay on SettingsController — they're shared across multiple pages /
/// sub-components (AlgorithmPreview, NewAlgorithmDialog, TilingOrderingPage)
/// and don't belong to any single page.
class TilingAlgorithmController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(int autotileGapMin READ autotileGapMin CONSTANT)
    Q_PROPERTY(int autotileGapMax READ autotileGapMax CONSTANT)
    Q_PROPERTY(int autotileMaxWindowsMin READ autotileMaxWindowsMin CONSTANT)
    Q_PROPERTY(int autotileMaxWindowsMax READ autotileMaxWindowsMax CONSTANT)
    Q_PROPERTY(int autotileMasterCountMin READ autotileMasterCountMin CONSTANT)
    Q_PROPERTY(int autotileMasterCountMax READ autotileMasterCountMax CONSTANT)
    Q_PROPERTY(qreal autotileSplitRatioMin READ autotileSplitRatioMin CONSTANT)
    Q_PROPERTY(qreal autotileSplitRatioMax READ autotileSplitRatioMax CONSTANT)
    Q_PROPERTY(qreal autotileSplitRatioStepMin READ autotileSplitRatioStepMin CONSTANT)
    Q_PROPERTY(qreal autotileSplitRatioStepMax READ autotileSplitRatioStepMax CONSTANT)

public:
    explicit TilingAlgorithmController(ISettings& settings, PhosphorTiles::AlgorithmRegistry& registry,
                                       QObject* parent = nullptr);

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

    int autotileGapMin() const;
    int autotileGapMax() const;
    int autotileMaxWindowsMin() const;
    int autotileMaxWindowsMax() const;
    int autotileMasterCountMin() const;
    int autotileMasterCountMax() const;
    qreal autotileSplitRatioMin() const;
    qreal autotileSplitRatioMax() const;
    qreal autotileSplitRatioStepMin() const;
    qreal autotileSplitRatioStepMax() const;

    /// Return the currently saved custom-param values for @p algorithmId,
    /// merged with the algorithm's defaults for any unset keys. Empty list
    /// if the algorithm doesn't declare custom params.
    Q_INVOKABLE QVariantList customParamsForAlgorithm(const QString& algorithmId) const;

    /// Persist a custom param, coercing the value to the declared type
    /// (`number` clamps to min/max, `enum` validates options). Emits
    /// `customParamChanged(algorithmId, paramName)` on successful write;
    /// `changed()` drives page dirty tracking via SettingsController.
    Q_INVOKABLE void setCustomParam(const QString& algorithmId, const QString& paramName, const QVariant& value);

    /// Per-algorithm built-in tuning (split ratio, master count, max windows).
    /// Each algorithm keeps its own values, mirroring the daemon's
    /// per-algorithm save/restore (AutotileConfig::AlgorithmSettings). Returns
    /// a map with keys "splitRatio", "masterCount", "maxWindows". Split ratio
    /// and max windows fall back to the algorithm's declared defaults; master
    /// count falls back to the global default (algorithms don't declare one).
    /// All values are clamped to their configured ranges.
    Q_INVOKABLE QVariantMap algorithmSettingsFor(const QString& algorithmId) const;

    /// Persist a per-algorithm built-in value (clamped to its allowed range),
    /// writing ONLY the per-algorithm entry — never the global current value.
    /// The daemon restores the per-algorithm entry into the active config, so
    /// writing the global here would let the daemon's switch-time save clobber
    /// a sibling algorithm's slot. Emits `changed` for dirty tracking.
    Q_INVOKABLE void setAlgorithmSplitRatio(const QString& algorithmId, qreal value);
    Q_INVOKABLE void setAlgorithmMasterCount(const QString& algorithmId, int value);
    Q_INVOKABLE void setAlgorithmMaxWindows(const QString& algorithmId, int value);

Q_SIGNALS:
    void customParamChanged(const QString& algorithmId, const QString& paramName);

    /// Generic "something changed" — SettingsController hooks this to its
    /// dirty-tracking slot so custom-param and per-algorithm writes flip
    /// needsSave.
    void changed();

private:
    QVariantMap savedCustomParams(const QString& algorithmId) const;
    /// Read-modify-write a single key in the per-algorithm settings entry,
    /// preserving the other keys (incl. customParams). Returns false (no-op)
    /// when the coerced value already matches what's stored.
    bool writeAlgorithmField(const QString& algorithmId, QLatin1String key, const QVariant& value);

    ISettings* m_settings = nullptr;
    PhosphorTiles::AlgorithmRegistry* m_registry = nullptr;
};

} // namespace PlasmaZones
