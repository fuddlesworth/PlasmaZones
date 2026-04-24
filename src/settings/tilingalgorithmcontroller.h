// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorTiles {
class AlgorithmRegistry;
}

namespace PlasmaZones {

class Settings;

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
class TilingAlgorithmController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int autotileGapMin READ autotileGapMin CONSTANT)
    Q_PROPERTY(int autotileGapMax READ autotileGapMax CONSTANT)
    Q_PROPERTY(int autotileMaxWindowsMin READ autotileMaxWindowsMin CONSTANT)
    Q_PROPERTY(int autotileMasterCountMin READ autotileMasterCountMin CONSTANT)
    Q_PROPERTY(qreal autotileSplitRatioMin READ autotileSplitRatioMin CONSTANT)
    Q_PROPERTY(qreal autotileSplitRatioStepMin READ autotileSplitRatioStepMin CONSTANT)
    Q_PROPERTY(qreal autotileSplitRatioStepMax READ autotileSplitRatioStepMax CONSTANT)

public:
    explicit TilingAlgorithmController(Settings* settings, PhosphorTiles::AlgorithmRegistry* registry,
                                       QObject* parent = nullptr);

    int autotileGapMin() const;
    int autotileGapMax() const;
    int autotileMaxWindowsMin() const;
    int autotileMasterCountMin() const;
    qreal autotileSplitRatioMin() const;
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

Q_SIGNALS:
    void customParamChanged(const QString& algorithmId, const QString& paramName);

    /// Generic "something changed" — SettingsController hooks this to its
    /// dirty-tracking slot so custom-param writes flip needsSave.
    void changed();

private:
    QVariantMap savedCustomParams(const QString& algorithmId) const;

    Settings* m_settings = nullptr;
    PhosphorTiles::AlgorithmRegistry* m_registry = nullptr;
};

} // namespace PlasmaZones
