// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

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

    CurveRegistry();
    ~CurveRegistry();

    /**
     * @brief Register a factory for @p typeId.
     *
     * Overwrites any prior registration for the same @p typeId — callers
     * that want to wrap/extend a built-in can register after startup.
     *
     * @return true if this replaced an existing registration.
     */
    bool registerFactory(const QString& typeId, Factory factory);

    /// Remove a previously-registered factory. No-op if not registered.
    bool unregisterFactory(const QString& typeId);

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

    /// Lists all registered typeIds in insertion order.
    QStringList knownTypes() const;

    /// True if @p typeId has a registered factory.
    bool has(const QString& typeId) const;

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
