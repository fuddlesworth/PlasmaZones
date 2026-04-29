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

/**
 * @brief String-id ↔ curve factory registry.
 *
 * Enables two things:
 *
 * 1. **Config round-trip.** Settings, config files, and D-Bus strings
 *    serialize curves as `"typeId:params"` (e.g., `"spring:12,0.8"`)
 *    or — for cubic-bezier specifically — the bare `"x1,y1,x2,y2"`
 *    form. The registry parses these back into concrete
 *    `shared_ptr<const Curve>` instances without callers needing to
 *    know which subclass the string refers to.
 *
 * 2. **Third-party extension.** Plugins / shell extensions / user
 *    scripts register additional curve types at runtime via
 *    `registerFactory()`. Required for niri / Hyprland / Quickshell-
 *    level customization where users define their own curves in config.
 *
 * ## Built-in curves (auto-registered)
 *
 *   - `bezier`                    → Easing (cubic-bezier)
 *   - `elastic-in/out/in-out`     → Easing (elastic)
 *   - `bounce-in/out/in-out`      → Easing (bounce)
 *   - `spring`                    → Spring (angular-frequency + damping)
 *
 * ## Cubic-bezier wire format
 *
 * The canonical wire format is the bare `"x1,y1,x2,y2"` string (four
 * comma-separated numbers, no colon) — this is what `toString()` emits.
 * The prefixed `"bezier:x1,y1,x2,y2"` form is also accepted on parse so
 * older configs and hand-written settings round-trip without silently
 * degrading to the OutCubic default. Both forms dispatch to the
 * `bezier` factory with identical semantics. The prefix is matched
 * case-insensitively so `"Bezier:…"`, `"SPRING:…"`, etc. also resolve.
 *
 * ## Thread safety
 *
 * All methods are thread-safe. Internal `QMutex` guards the factory map.
 * Factory functions themselves must be reentrant (the common
 * `Easing::fromString` / `Spring::fromString` helpers are).
 *
 * ## Ownership
 *
 * Per-process instance, owned by the composition root. PlasmaZones
 * daemon, KCM, editor, and the KWin effect each construct one
 * `CurveRegistry` and pass it to every `Profile::fromJson` /
 * `ProfileTree::fromJson` call. The previous process-wide singleton
 * (`instance()`) was removed in the singleton-sweep #2 refactor —
 * see project_plugin_based_compositor.md for the rationale.
 *
 * ## Owner-tagged partitioning
 *
 * Third-party registrants (loaders, plugins) pass an `ownerTag` to the
 * three- / four-arg `registerFactory` overloads. The tag is stored
 * alongside the factory and lets `unregisterByOwner(tag)` bulk-remove
 * every factory a single registrant installed — without evicting other
 * registrants that happen to share a typeId. Mirrors
 * `PhosphorProfileRegistry`'s `reloadFromOwner` / `clearOwner` partition.
 */
class PHOSPHORANIMATION_EXPORT CurveRegistry
{
public:
    /// Factory function: receives the typeId + the trailing parameter
    /// substring (after the colon, or the whole string when the bare
    /// cubic-bezier form was matched) and returns an immutable Curve
    /// instance. May return nullptr to signal parse failure — callers
    /// fall back to a default curve.
    using Factory = std::function<std::shared_ptr<const Curve>(const QString& typeId, const QString& params)>;

    /// JSON-parameters factory: receives a JSON object containing the
    /// curve's parameter fields and returns an immutable Curve instance.
    /// Used by `CurveLoader` to build curves from the JSON `"parameters"`
    /// block without the loader needing to know each typeId's parameter
    /// shape. May return nullptr to signal validation failure — the
    /// factory emits its own `qCWarning` on rejection.
    using JsonFactory = std::function<std::shared_ptr<const Curve>(const QJsonObject& parameters)>;

    CurveRegistry();
    ~CurveRegistry();

    /**
     * @brief Register a factory for @p typeId (untagged).
     *
     * Overwrites any prior registration for the same @p typeId. The
     * owner tag is stored as an empty string; these entries are never
     * evicted by `unregisterByOwner` (appropriate for process-lifetime
     * registrations such as built-ins).
     *
     * @return true if this replaced an existing registration.
     */
    bool registerFactory(const QString& typeId, Factory factory);

    /**
     * @brief Register a factory for @p typeId tagged with @p ownerTag.
     *
     * Tagged entries can be bulk-removed via `unregisterByOwner` — use
     * this from transient registrants (loaders, plugins) so a tear-down
     * of the owner cleans up every factory it installed without
     * evicting entries owned by a different registrant.
     *
     * An empty @p ownerTag behaves identically to the untagged overload.
     *
     * @return true if this replaced an existing registration.
     */
    bool registerFactory(const QString& typeId, Factory factory, const QString& ownerTag);

    /**
     * @brief Register both a string-form and a JSON-parameter factory.
     *
     * `CurveLoader::Sink::parseFile` dispatches to `tryCreateFromJson`
     * using the registered `JsonFactory`, keeping the parameter-shape
     * schema in the registry. Third-party curves that register a JSON
     * factory become JSON-loadable automatically.
     *
     * Pass a null @p jsonFactory to register a string-only entry.
     *
     * @return true if this replaced an existing registration.
     */
    bool registerFactory(const QString& typeId, Factory stringFactory, JsonFactory jsonFactory,
                         const QString& ownerTag);

    /// Remove a previously-registered factory. No-op if not registered.
    bool unregisterFactory(const QString& typeId);

    /**
     * @brief Remove every factory whose owner tag matches @p ownerTag.
     *
     * Emits an info log line per removed key so rollback is observable
     * in diagnostic traces. An empty @p ownerTag is rejected (it would
     * otherwise match every untagged built-in and wipe the registry).
     *
     * @return number of entries removed.
     */
    int unregisterByOwner(const QString& ownerTag);

    /**
     * @brief Parse @p spec into an immutable curve, or a default curve.
     *
     * Accepts:
     *   - `"typeId:params"`  — dispatched to the registered factory
     *   - `"x1,y1,x2,y2"`    — cubic-bezier wire format
     *
     * Never returns `nullptr`. Empty specs, unknown typeIds, and factory
     * parse failures all fall through to a best-effort default
     * (cubic-bezier outCubic) with a warning logged for the latter two.
     * Use `tryCreate()` if you need to distinguish "valid curve" from
     * "default substituted for bad input".
     */
    std::shared_ptr<const Curve> create(const QString& spec) const;

    /**
     * @brief Parse @p spec into an immutable curve, returning null on
     * any failure.
     *
     * Same acceptable forms as `create()`, but returns `nullptr` for
     * empty input, unknown typeIds, and factory parse failures instead
     * of substituting a default. Callers get an explicit signal that
     * the input was invalid and can log / fall back as they prefer.
     */
    std::shared_ptr<const Curve> tryCreate(const QString& spec) const;

    /**
     * @brief Build an immutable curve from @p typeId + JSON @p parameters.
     *
     * Dispatches to the `JsonFactory` registered for @p typeId; returns
     * `nullptr` when no JSON factory is registered or when the factory's
     * parameter validation rejects the input. Makes `CurveRegistry` the
     * sole schema authority for curve JSON parsing.
     */
    std::shared_ptr<const Curve> tryCreateFromJson(const QString& typeId, const QJsonObject& parameters) const;

    /// Lists all registered typeIds in insertion order.
    QStringList knownTypes() const;

    /// True if @p typeId has a registered factory.
    bool has(const QString& typeId) const;

    /**
     * @brief True if @p typeId names one of the built-in factories
     *        (auto-registered by the constructor).
     *
     * User-authored JSON curves whose `name` matches a built-in typeId
     * would silently shadow the built-in under `registerFactory`'s
     * replace-semantic. `CurveLoader` consults this to reject such
     * files up-front with a clear diagnostic instead of letting a
     * user's `"name": "spring"` permanently break the built-in spring
     * curve for the rest of the process lifetime.
     *
     * Comparison is case-sensitive — all built-in typeIds are
     * lower-case by convention.
     */
    static bool isBuiltinTypeId(const QString& typeId);

    // Not copyable / movable — owned by composition root via member or
    // unique_ptr. Move would invalidate the QMutex inside Impl.
    CurveRegistry(const CurveRegistry&) = delete;
    CurveRegistry& operator=(const CurveRegistry&) = delete;
    CurveRegistry(CurveRegistry&&) = delete;
    CurveRegistry& operator=(CurveRegistry&&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorAnimation
