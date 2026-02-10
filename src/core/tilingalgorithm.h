// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

namespace PlasmaZones {

/**
 * @brief Parameters passed to tiling algorithms
 *
 * All state lives in Layout and is passed here — algorithms are stateless pure functions.
 */
struct PLASMAZONES_EXPORT TilingParams {
    qreal masterRatio = 0.5;   ///< Ratio of master area (0.1–0.9)
    int masterCount = 1;       ///< Number of master windows
    qreal aspectRatio = 0.0;   ///< Screen aspect ratio (set by caller, 0 = unknown)
};

/**
 * @brief Abstract base for tiling algorithms
 *
 * Algorithms produce a vector of QRectF in relative coordinates (0.0–1.0),
 * matching Zone::relativeGeometry. They are stateless — all configuration
 * arrives via TilingParams.
 */
class PLASMAZONES_EXPORT TilingAlgorithm
{
public:
    virtual ~TilingAlgorithm() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString description() const = 0;

    /**
     * @brief Generate zone rectangles for a given window count
     * @param windowCount Number of windows to tile (0 = empty layout)
     * @param params Tiling parameters (master ratio, count, etc.)
     * @return Zones in relative coordinates (0.0–1.0)
     */
    virtual QVector<QRectF> generateZones(int windowCount, const TilingParams& params) const = 0;

    /** @brief Index of the "master" zone (default: 0) */
    virtual int masterIndex() const { return 0; }
};

/**
 * @brief Singleton registry of available tiling algorithms
 *
 * Built-in algorithms are registered in the constructor.
 * Follows the same singleton pattern as ShaderRegistry.
 */
class PLASMAZONES_EXPORT TilingAlgorithmRegistry
{
public:
    static TilingAlgorithmRegistry* instance();

    void registerAlgorithm(std::unique_ptr<TilingAlgorithm> algorithm);
    TilingAlgorithm* algorithm(const QString& id) const;
    QStringList algorithmIds() const;
    QVector<const TilingAlgorithm*> algorithms() const;

private:
    TilingAlgorithmRegistry();
    std::vector<std::unique_ptr<TilingAlgorithm>> m_algorithms;
};

/**
 * @brief Simple equal-width columns algorithm
 *
 * Placeholder to validate the TilingAlgorithm interface.
 * Generates N equal-width vertical columns for N windows.
 */
class PLASMAZONES_EXPORT ColumnsTilingAlgorithm : public TilingAlgorithm
{
public:
    QString id() const override { return QStringLiteral("columns"); }
    QString name() const override { return QStringLiteral("Columns"); }
    QString description() const override { return QStringLiteral("Equal-width vertical columns (ignores master ratio/count)"); }
    QVector<QRectF> generateZones(int windowCount, const TilingParams& params) const override;
};

} // namespace PlasmaZones
