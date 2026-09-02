// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief One zone-overlay shader selection: which pack, with what parameters.
 *
 * The overlay analogue of PhosphorAnimationShaders::ShaderProfile, but
 * without optional fields: an override node replaces the baseline wholly
 * (id AND params together), so there is no per-field inherit to encode.
 * An empty shaderId means "no shader" — as the baseline that is simply
 * the unset default, as an override it explicitly suppresses a baseline
 * shader for that layout.
 */
class PLASMAZONES_EXPORT OverlayShaderProfile
{
public:
    QString shaderId;
    QVariantMap parameters;

    bool isEmpty() const
    {
        return shaderId.isEmpty() && parameters.isEmpty();
    }

    static constexpr auto JsonFieldShaderId = "shaderId";
    static constexpr auto JsonFieldParameters = "parameters";

    QJsonObject toJson() const;
    static OverlayShaderProfile fromJson(const QJsonObject& obj);

    bool operator==(const OverlayShaderProfile& other) const
    {
        // JSON-normalized parameter compare: a parameter map built in C++
        // (int variants) must compare equal to the same values read back
        // from disk (doubles), or a re-applied identical assignment would
        // defeat the settings setters' value-equality no-op gates.
        return shaderId == other.shaderId
            && QJsonObject::fromVariantMap(parameters) == QJsonObject::fromVariantMap(other.parameters);
    }
    bool operator!=(const OverlayShaderProfile& other) const
    {
        return !(*this == other);
    }
};

/**
 * @brief Zone-overlay shader assignments: a global baseline plus
 *        per-layout overrides.
 *
 * The overlay counterpart of the animation ShaderProfileTree and the
 * decoration DecorationProfileTree, but FLAT: paths are layout UUIDs
 * (braced `QUuid::toString()` form, per the project convention), not a
 * dot-path hierarchy, and the only inheritance step is override →
 * baseline. resolve() returns the layout's override when one exists,
 * otherwise the baseline.
 *
 * Persisted as one nested JSON entry under
 * `Snapping.OverlayShaders/OverlayShaderTree`:
 * `{ "baseline": {node}, "overrides": { "{uuid}": {node} } }`.
 *
 * Value type, not internally synchronized. Same as the sibling trees.
 */
class PLASMAZONES_EXPORT OverlayShaderTree
{
public:
    OverlayShaderTree() = default;

    // ─────── Lookup ───────

    /// Override for @p layoutId when present, else the baseline.
    OverlayShaderProfile resolve(const QString& layoutId) const;
    OverlayShaderProfile directOverride(const QString& layoutId) const;
    bool hasOverride(const QString& layoutId) const;
    /// Overridden layout ids in sorted order — deterministic and identical
    /// whether the tree was built in-session or reloaded from JSON (whose
    /// object keys are sorted), so no consumer can come to depend on an
    /// order that does not survive a restart.
    QStringList overriddenLayouts() const;
    bool isEmpty() const;

    // ─────── Mutation ───────

    void setOverride(const QString& layoutId, const OverlayShaderProfile& profile);
    bool clearOverride(const QString& layoutId);

    // ─────── Baseline ───────

    OverlayShaderProfile baseline() const
    {
        return m_baseline;
    }
    void setBaseline(const OverlayShaderProfile& profile);

    // ─────── Serialization ───────

    static constexpr auto JsonFieldBaseline = "baseline";
    static constexpr auto JsonFieldOverrides = "overrides";

    QJsonObject toJson() const;
    static OverlayShaderTree fromJson(const QJsonObject& obj);

    // ─────── Equality ───────

    bool operator==(const OverlayShaderTree& other) const;
    bool operator!=(const OverlayShaderTree& other) const
    {
        return !(*this == other);
    }

private:
    OverlayShaderProfile m_baseline;
    QHash<QString, OverlayShaderProfile> m_overrides;
};

} // namespace PlasmaZones
