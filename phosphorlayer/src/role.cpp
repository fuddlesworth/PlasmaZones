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
    // Overlay sits above everything and ignores other surfaces' zones —
    // specifying a non-negative exclusive zone is a consumer mistake.
    if (layer == Layer::Overlay && exclusiveZone >= 0) {
        return false;
    }
    // Margins are meaningful only when at least one edge is anchored —
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

// ── Preset definitions ─────────────────────────────────────────────────
// Declared `extern const` in the header so presets compose via QString
// (which isn't constexpr). Dynamic-init cost is one-time at library load.

namespace Roles {

const Role FullscreenOverlay{
    Layer::Overlay, AnchorAll, -1, KeyboardInteractivity::None, QMargins(), QStringLiteral("pl-fullscreen")};

const Role TopPanel{Layer::Top, Anchor::Top | Anchor::Left | Anchor::Right,
                    0,          KeyboardInteractivity::OnDemand,
                    QMargins(), QStringLiteral("pl-top-panel")};

const Role BottomPanel{Layer::Top, Anchor::Bottom | Anchor::Left | Anchor::Right,
                       0,          KeyboardInteractivity::OnDemand,
                       QMargins(), QStringLiteral("pl-bottom-panel")};

const Role LeftDock{Layer::Top, Anchor::Top | Anchor::Bottom | Anchor::Left,
                    0,          KeyboardInteractivity::OnDemand,
                    QMargins(), QStringLiteral("pl-left-dock")};

const Role RightDock{Layer::Top, Anchor::Top | Anchor::Bottom | Anchor::Right,
                     0,          KeyboardInteractivity::OnDemand,
                     QMargins(), QStringLiteral("pl-right-dock")};

const Role CenteredModal{
    Layer::Top, AnchorNone, -1, KeyboardInteractivity::Exclusive, QMargins(), QStringLiteral("pl-modal")};

const Role CornerToast{Layer::Top, Anchor::Top | Anchor::Right,      -1, KeyboardInteractivity::None,
                       QMargins(), QStringLiteral("pl-corner-toast")};

const Role Background{Layer::Background,           AnchorAll,  0,
                      KeyboardInteractivity::None, QMargins(), QStringLiteral("pl-background")};

const Role FloatingOverlay{
    Layer::Overlay, AnchorNone, -1, KeyboardInteractivity::None, QMargins(), QStringLiteral("pl-floating")};

} // namespace Roles

} // namespace PhosphorLayer
