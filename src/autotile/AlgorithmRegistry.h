// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace PlasmaZones {

class TilingAlgorithm;

/**
 * @brief Singleton registry for tiling algorithms
 *
 * AlgorithmRegistry provides factory access to all available tiling algorithms.
 * It manages algorithm lifecycle and provides discovery for UI components.
 *
 * Built-in algorithms are registered automatically on first access:
 * - Master-Stack (default): Classic master/stack layout
 * - Columns: Equal-width vertical columns
 * - BSP: Binary space partitioning
 *
 * Future algorithms (Monocle, Fibonacci, Rows, ThreeColumn) can be added
 * by implementing TilingAlgorithm and calling registerAlgorithm().
 *
 * Usage:
 * @code
 * auto *algo = AlgorithmRegistry::instance()->algorithm("master-stack");
 * if (algo) {
 *     auto zones = algo->calculateZones(windowCount, screenGeometry, state);
 * }
 * @endcode
 *
 * @note Thread Safety: The singleton instance() method uses Meyer's singleton
 *       pattern (C++11 static local), which is thread-safe. Read operations
 *       (algorithm(), availableAlgorithms(), hasAlgorithm()) are thread-safe.
 *       Registration/unregistration should only occur during initialization
 *       or from the main thread.
 *
 * @see TilingAlgorithm for the algorithm interface
 * @see DBus::AutotileAlgorithm in constants.h for algorithm ID constants
 */
class PLASMAZONES_EXPORT AlgorithmRegistry : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AlgorithmRegistry)

public:
    /**
     * @brief Get the singleton instance
     *
     * Creates the registry and registers built-in algorithms on first call.
     *
     * @return Pointer to the global AlgorithmRegistry instance
     */
    static AlgorithmRegistry *instance();

    /**
     * @brief Register a tiling algorithm
     *
     * The registry takes ownership of the algorithm. If an algorithm with
     * the same ID already exists, the old one is deleted and replaced.
     *
     * @param id Unique identifier for the algorithm (use DBus::AutotileAlgorithm constants)
     * @param algorithm Algorithm instance (ownership transferred)
     */
    void registerAlgorithm(const QString &id, TilingAlgorithm *algorithm);

    /**
     * @brief Unregister and delete an algorithm
     *
     * @param id Algorithm ID to remove
     * @return true if algorithm was found and removed
     */
    bool unregisterAlgorithm(const QString &id);

    /**
     * @brief Get an algorithm by ID
     *
     * @param id Algorithm identifier
     * @return Pointer to algorithm, or nullptr if not found
     */
    TilingAlgorithm *algorithm(const QString &id) const;

    /**
     * @brief Get list of all registered algorithm IDs
     *
     * @return List of algorithm IDs in registration order
     */
    QStringList availableAlgorithms() const noexcept;

    /**
     * @brief Get all registered algorithm instances
     *
     * @return List of algorithm pointers (owned by registry)
     */
    QList<TilingAlgorithm *> allAlgorithms() const;

    /**
     * @brief Check if an algorithm is registered
     *
     * @param id Algorithm identifier
     * @return true if algorithm exists
     */
    bool hasAlgorithm(const QString &id) const noexcept;

    /**
     * @brief Get the default algorithm ID
     *
     * @return "master-stack" (the traditional tiling WM default)
     */
    static QString defaultAlgorithmId() noexcept;

    /**
     * @brief Get the default algorithm instance
     *
     * Convenience method equivalent to algorithm(defaultAlgorithmId())
     *
     * @return Pointer to default algorithm
     */
    TilingAlgorithm *defaultAlgorithm() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Preview utilities for unified layout model (shared by zone selector,
    // overlay service, daemon OSD, and KCM algorithm preview)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Monocle preview offset per zone (3% diagonal inset per stacked window)
    static constexpr qreal MonoclePreviewOffset = 0.03;

    /**
     * @brief Convert pixel zones to relative geometry with monocle offset handling
     *
     * Shared utility for both generatePreviewZones() (layout cards/selector)
     * and KCMPlasmaZones::generateAlgorithmPreview() (live algorithm preview).
     * Detects monocle-style layouts (all zones identical) and applies centered
     * diagonal offsets so stacked windows are visually distinguishable.
     *
     * @param zones Pixel-coordinate zones from TilingAlgorithm::calculateZones()
     * @param previewRect The rect used for calculateZones (for normalization)
     * @return QVariantList of zone maps with zoneNumber and relativeGeometry
     */
    static QVariantList zonesToRelativeGeometry(const QVector<QRect> &zones, const QRect &previewRect);

    /**
     * @brief Generate preview zones for an algorithm as QVariantList
     *
     * Creates a representative preview with 3 windows showing how the algorithm
     * arranges windows. Used by zone selector and layout OSD.
     *
     * @param algorithm The tiling algorithm to preview
     * @return QVariantList of zone maps with relativeGeometry (0.0-1.0)
     */
    static QVariantList generatePreviewZones(TilingAlgorithm *algorithm);

    /**
     * @brief Convert an algorithm to QVariantMap for QML consumption
     *
     * Creates a layout-compatible variant map including id (with autotile: prefix),
     * name, description, zones preview, and category.
     *
     * @param algorithm The tiling algorithm
     * @param algorithmId The algorithm's registry ID
     * @return QVariantMap suitable for zone selector/OSD
     */
    static QVariantMap algorithmToVariantMap(TilingAlgorithm *algorithm, const QString &algorithmId);

Q_SIGNALS:
    /**
     * @brief Emitted when an algorithm is registered
     * @param id The registered algorithm's ID
     */
    void algorithmRegistered(const QString &id);

    /**
     * @brief Emitted when an algorithm is unregistered
     * @param id The removed algorithm's ID
     */
    void algorithmUnregistered(const QString &id);

private:
    explicit AlgorithmRegistry(QObject *parent = nullptr);
    ~AlgorithmRegistry() override;

    /**
     * @brief Register all built-in algorithms
     *
     * Called automatically during construction.
     */
    void registerBuiltInAlgorithms();

    /**
     * @brief Find if an algorithm pointer is already registered
     *
     * @param algorithm Pointer to check
     * @return ID of existing registration, or empty string if not found
     */
    QString findAlgorithmId(TilingAlgorithm *algorithm) const;

    /**
     * @brief Remove algorithm from internal data structures
     *
     * Does NOT delete the algorithm or emit signals. Used by both
     * registerAlgorithm (for replacement) and unregisterAlgorithm.
     *
     * @param id Algorithm ID to remove
     * @return Pointer to removed algorithm, or nullptr if not found
     */
    TilingAlgorithm *removeAlgorithmInternal(const QString &id);

    QHash<QString, TilingAlgorithm *> m_algorithms;
    QStringList m_registrationOrder; ///< Preserve order for UI
};

/**
 * @brief Pending algorithm registration data
 */
struct PLASMAZONES_EXPORT PendingAlgorithmRegistration {
    QString id;
    int priority;
    std::function<TilingAlgorithm*()> factory;
};

/**
 * @brief Global list of pending algorithm registrations
 *
 * This is separate from the template to ensure all registrations go to the
 * same list regardless of template instantiation.
 */
PLASMAZONES_EXPORT QList<PendingAlgorithmRegistration> &pendingAlgorithmRegistrations();

/**
 * @brief Helper for static self-registration of built-in algorithms
 *
 * Use this in algorithm .cpp files to register at static initialization time.
 * New algorithms can be added without
 * modifying AlgorithmRegistry.
 *
 * Usage in algorithm .cpp file:
 * @code
 * namespace {
 * PlasmaZones::AlgorithmRegistrar<MyAlgorithm> registrar(
 *     DBus::AutotileAlgorithm::MyAlgo, 10);  // priority 10
 * }
 * @endcode
 *
 * @tparam T Algorithm class (must inherit from TilingAlgorithm)
 */
template<typename T>
class AlgorithmRegistrar
{
public:
    /**
     * @brief Register an algorithm at static initialization time
     *
     * @param id Algorithm identifier (use DBus::AutotileAlgorithm constants)
     * @param priority Registration order (lower = registered first, default 100)
     */
    explicit AlgorithmRegistrar(const QString &id, int priority = 100)
    {
        // Store in the global (non-template) pending list
        pendingAlgorithmRegistrations().append({id, priority, []() { return new T(); }});
    }
};

} // namespace PlasmaZones