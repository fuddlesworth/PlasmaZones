// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace PhosphorAnimation {

/// String-id <-> curve factory registry. Enables config round-trip
/// ("typeId:params" strings) and third-party curve extension at runtime.
/// Thread-safe (internal QMutex) — concurrent access never corrupts the store
/// and each individual operation is atomic. Registration is expected on the
/// composition root / loader thread; the only compound operation,
/// registerFactory's "was replaced" bool return, is exact only for
/// single-threaded registration. Per-process instance, owned by composition root.
class PHOSPHORANIMATION_EXPORT CurveRegistry
{
public:
    /// String-form factory: receives typeId + parameter substring after the colon.
    using Factory = std::function<std::shared_ptr<const Curve>(const QString& typeId, const QString& params)>;

    /// JSON factory: receives parameter fields, returns curve or nullptr on validation failure.
    using JsonFactory = std::function<std::shared_ptr<const Curve>(const QJsonObject& parameters)>;

    CurveRegistry();
    ~CurveRegistry();

    /// Register an untagged factory for @p typeId. Replaces prior registration.
    bool registerFactory(const QString& typeId, Factory factory);

    /// Register a tagged factory. Tagged entries are bulk-removable via unregisterByOwner.
    bool registerFactory(const QString& typeId, Factory factory, const QString& ownerTag);

    /// Register both string-form and JSON-parameter factories.
    bool registerFactory(const QString& typeId, Factory stringFactory, JsonFactory jsonFactory,
                         const QString& ownerTag);

    /// Remove a previously-registered factory. No-op if not registered.
    bool unregisterFactory(const QString& typeId);

    /// Remove every factory matching @p ownerTag. Empty tag is rejected.
    int unregisterByOwner(const QString& ownerTag);

    /// Parse @p spec ("typeId:params" or "x1,y1,x2,y2") into a curve.
    /// Never returns nullptr — falls back to OutCubic on failure.
    std::shared_ptr<const Curve> create(const QString& spec) const;

    /// Like create() but returns nullptr on any failure.
    std::shared_ptr<const Curve> tryCreate(const QString& spec) const;

    /// Build a curve from @p typeId + JSON parameters. Returns nullptr on failure.
    std::shared_ptr<const Curve> tryCreateFromJson(const QString& typeId, const QJsonObject& parameters) const;

    /// All registered typeIds, sorted alphabetically (a stable, deterministic
    /// order for membership checks; not registration order).
    QStringList knownTypes() const;

    /// True if @p typeId has a registered factory.
    bool has(const QString& typeId) const;

    /// True if @p typeId names a built-in factory (auto-registered by constructor).
    /// CurveLoader uses this to reject user JSON files that would shadow built-ins.
    static bool isBuiltinTypeId(const QString& typeId);

    CurveRegistry(const CurveRegistry&) = delete;
    CurveRegistry& operator=(const CurveRegistry&) = delete;
    CurveRegistry(CurveRegistry&&) = delete;
    CurveRegistry& operator=(CurveRegistry&&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorAnimation
