// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingalgorithm.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// TilingAlgorithmRegistry
// ═══════════════════════════════════════════════════════════════════════════════

TilingAlgorithmRegistry* TilingAlgorithmRegistry::instance()
{
    static TilingAlgorithmRegistry registry;
    return &registry;
}

TilingAlgorithmRegistry::TilingAlgorithmRegistry()
{
    // Register built-in algorithms
    registerAlgorithm(std::make_unique<ColumnsTilingAlgorithm>());
}

void TilingAlgorithmRegistry::registerAlgorithm(std::unique_ptr<TilingAlgorithm> algorithm)
{
    if (!algorithm) {
        return;
    }
    // Reject duplicate IDs
    const auto& id = algorithm->id();
    for (const auto& existing : m_algorithms) {
        if (existing->id() == id) {
            qWarning("TilingAlgorithmRegistry: duplicate algorithm id '%s' rejected", qPrintable(id));
            return;
        }
    }
    m_algorithms.push_back(std::move(algorithm));
}

TilingAlgorithm* TilingAlgorithmRegistry::algorithm(const QString& id) const
{
    for (const auto& alg : m_algorithms) {
        if (alg->id() == id) {
            return alg.get();
        }
    }
    return nullptr;
}

QStringList TilingAlgorithmRegistry::algorithmIds() const
{
    QStringList ids;
    ids.reserve(m_algorithms.size());
    for (const auto& alg : m_algorithms) {
        ids.append(alg->id());
    }
    return ids;
}

QVector<const TilingAlgorithm*> TilingAlgorithmRegistry::algorithms() const
{
    QVector<const TilingAlgorithm*> result;
    result.reserve(m_algorithms.size());
    for (const auto& alg : m_algorithms) {
        result.append(alg.get());
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ColumnsTilingAlgorithm
// ═══════════════════════════════════════════════════════════════════════════════

QVector<QRectF> ColumnsTilingAlgorithm::generateZones(int windowCount, const TilingParams& /*params*/) const
{
    if (windowCount <= 0) {
        return {};
    }

    if (windowCount == 1) {
        return {QRectF(0.0, 0.0, 1.0, 1.0)};
    }

    QVector<QRectF> zones;
    zones.reserve(windowCount);
    const qreal width = 1.0 / windowCount;
    for (int i = 0; i < windowCount; ++i) {
        const qreal x = i * width;
        // Last column absorbs rounding error to guarantee full coverage
        const qreal w = (i == windowCount - 1) ? (1.0 - x) : width;
        zones.append(QRectF(x, 0.0, w, 1.0));
    }
    return zones;
}

} // namespace PlasmaZones
