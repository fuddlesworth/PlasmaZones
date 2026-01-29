// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QHash>
#include <QObject>
#include <QStringList>

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
 * @note Thread Safety: The registry is created on first access. After that,
 *       read operations (algorithm(), availableAlgorithms()) are thread-safe.
 *       Registration should only occur during initialization.
 *
 * @see TilingAlgorithm for the algorithm interface
 * @see DBus::AutotileAlgorithm in constants.h for algorithm ID constants
 */
class PLASMAZONES_EXPORT AlgorithmRegistry : public QObject
{
    Q_OBJECT

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
    QStringList availableAlgorithms() const;

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
    bool hasAlgorithm(const QString &id) const;

    /**
     * @brief Get the default algorithm ID
     *
     * @return "master-stack" (the traditional tiling WM default)
     */
    static QString defaultAlgorithmId();

    /**
     * @brief Get the default algorithm instance
     *
     * Convenience method equivalent to algorithm(defaultAlgorithmId())
     *
     * @return Pointer to default algorithm
     */
    TilingAlgorithm *defaultAlgorithm() const;

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

    // Prevent copying
    AlgorithmRegistry(const AlgorithmRegistry &) = delete;
    AlgorithmRegistry &operator=(const AlgorithmRegistry &) = delete;

    /**
     * @brief Register all built-in algorithms
     *
     * Called automatically during construction.
     */
    void registerBuiltInAlgorithms();

    QHash<QString, TilingAlgorithm *> m_algorithms;
    QStringList m_registrationOrder; // Preserve order for UI

    static AlgorithmRegistry *s_instance;
};

} // namespace PlasmaZones
