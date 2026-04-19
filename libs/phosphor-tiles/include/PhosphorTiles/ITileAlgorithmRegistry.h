// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

#include <PhosphorLayoutApi/ILayoutSourceRegistry.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>

#include <QList>
#include <QString>
#include <QStringList>

namespace PhosphorTiles {

class TilingAlgorithm;

/**
 * @brief Abstract contract for a tiling-algorithm registry.
 *
 * Symmetric with @c PhosphorZones::IZoneLayoutRegistry — the two
 * provider libraries expose the same shape: an
 * @c ILayoutSourceRegistry-derived interface plus one concrete
 * implementation. Fixture tests stub this contract rather than the
 * concrete @c AlgorithmRegistry; the upcoming
 * @c AutotileLayoutSourceFactory binds to this interface so
 * dependency injection can hand in alternative implementations
 * (per-process instances, test fakes, future scripted-engine
 * registries).
 *
 * Inherits @c PhosphorLayout::ILayoutSourceRegistry for the unified
 * @c contentsChanged signal that @c AutotileLayoutSource self-wires
 * to invalidate its preview cache on any registry change. The three
 * finer-grained signals declared here (@c algorithmRegistered,
 * @c algorithmUnregistered, @c previewParamsChanged) stay available
 * for callers that need to discriminate between cause types — e.g.
 * UI code that animates only on preview-param changes.
 */
class PHOSPHORTILES_EXPORT ITileAlgorithmRegistry : public PhosphorLayout::ILayoutSourceRegistry
{
    Q_OBJECT
public:
    explicit ITileAlgorithmRegistry(QObject* parent = nullptr);
    ~ITileAlgorithmRegistry() override;

    // ─── Enumeration / query ───────────────────────────────────────────────

    /// Resolve an algorithm by its stable id. Returns nullptr when no
    /// algorithm with that id is registered.
    virtual TilingAlgorithm* algorithm(const QString& id) const = 0;

    /// All registered algorithm ids, in registration order.
    virtual QStringList availableAlgorithms() const = 0;

    /// Every registered algorithm pointer. Ownership stays with the
    /// registry.
    virtual QList<TilingAlgorithm*> allAlgorithms() const = 0;

    /// Whether an algorithm is registered under @p id.
    virtual bool hasAlgorithm(const QString& id) const = 0;

    /// Convenience: the registry's recommended default algorithm.
    /// Concrete registries determine the id via their own policy — see
    /// @c AlgorithmRegistry::defaultAlgorithmId.
    virtual TilingAlgorithm* defaultAlgorithm() const = 0;

    // ─── Mutation ──────────────────────────────────────────────────────────

    /// Register an algorithm under @p id. Ownership transfers to the
    /// registry. Re-registering an existing id replaces the old
    /// algorithm; @c algorithmUnregistered(id, replacing=true) fires
    /// before @c algorithmRegistered(id).
    virtual void registerAlgorithm(const QString& id, TilingAlgorithm* algorithm) = 0;

    /// Unregister and delete the algorithm with @p id. Returns true
    /// when an algorithm was found and removed.
    virtual bool unregisterAlgorithm(const QString& id) = 0;

    // ─── Preview params ────────────────────────────────────────────────────

    /// Apply the user-configured tiling parameters. Emits
    /// @c previewParamsChanged when the stored value differs from the
    /// new one; @c AutotileLayoutSource reacts by invalidating its
    /// preview cache.
    virtual void setPreviewParams(const AlgorithmPreviewParams& params) = 0;

    /// The currently-configured preview parameters.
    virtual const AlgorithmPreviewParams& previewParams() const noexcept = 0;

Q_SIGNALS:
    /// An algorithm has been registered. On replacement (re-registration
    /// of an existing id), @c algorithmUnregistered(id, true) fires
    /// first, then @c algorithmRegistered(id). The new algorithm is
    /// already queryable via @c algorithm(id) when either signal fires.
    void algorithmRegistered(const QString& id);

    /// An algorithm has been unregistered or is being replaced.
    /// @p replacing is true when a new algorithm has already been
    /// registered under @p id; false when the algorithm was explicitly
    /// removed.
    void algorithmUnregistered(const QString& id, bool replacing);

    /// Preview parameters changed via @c setPreviewParams. Preview
    /// caches (e.g. @c AutotileLayoutSource) invalidate on this so the
    /// next preview render picks up the new master-count / split-ratio
    /// / per-algorithm saved values.
    void previewParamsChanged();
};

} // namespace PhosphorTiles
