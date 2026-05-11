// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/Role.h>

#include "internal.h"

namespace PhosphorLayer {

Q_LOGGING_CATEGORY(lcPhosphorLayer, "phosphorlayer")

// ── Role fluent modifiers ──────────────────────────────────────────────

Role Role::withLayer(Layer l) const
{
    Role r = *this;
    r.layer = l;
    return r;
}

Role Role::withAnchors(Anchors a) const
{
    Role r = *this;
    r.anchors = a;
    return r;
}

Role Role::withExclusiveZone(int z) const
{
    Role r = *this;
    r.exclusiveZone = z;
    return r;
}

Role Role::withKeyboard(KeyboardInteractivity k) const
{
    Role r = *this;
    r.keyboard = k;
    return r;
}

Role Role::withMargins(QMargins m) const
{
    Role r = *this;
    r.defaultMargins = m;
    return r;
}

Role Role::withScopePrefix(QString prefix) const
{
    Role r = *this;
    r.scopePrefix = std::move(prefix);
    return r;
}

bool Role::isValid() const
{
    if (scopePrefix.isEmpty()) {
        return false;
    }
    // Overlay sits above everything and ignores other surfaces' zones,
    // so specifying a non-negative exclusive zone is a consumer mistake.
    if (layer == Layer::Overlay && exclusiveZone >= 0) {
        return false;
    }
    // Margins are meaningful only when at least one edge is anchored.
    // wlr-layer-shell ignores them on a fully unanchored surface. A role
    // that ships default margins without any anchor is a silent consumer
    // mistake: the compositor discards them and the consumer is left
    // wondering why their positioning hint had no effect. (The per-instance
    // marginsOverride escape hatch lives on SurfaceConfig, so this check
    // only constrains the role's *default* margins.)
    if (anchors == AnchorNone && !defaultMargins.isNull()) {
        return false;
    }
    return true;
}

} // namespace PhosphorLayer
