// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Patterns.h>

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

// ── Pattern preset definitions ─────────────────────────────────────────
// Declared `extern const Role&` in Patterns.h so consumers can refer to
// them as `const Role&` values; the underlying Role objects live here
// as `static const` (TU-local). Panel/Toast are factory functions that
// build Role values from primitive parameters (Edge, Corner).
//
// Dynamic-init cost is one-time at library load (QString scopePrefix
// prevents constexpr).

namespace Patterns {

namespace {

const Role kWallpaper{Layer::Background,           AnchorAll,  0,
                      KeyboardInteractivity::None, QMargins(), QStringLiteral("pl-background")};

const Role kHud{
    Layer::Overlay, AnchorAll, -1, KeyboardInteractivity::None, QMargins(), QStringLiteral("pl-fullscreen")};

const Role kModal{Layer::Top, AnchorNone, -1, KeyboardInteractivity::Exclusive, QMargins(), QStringLiteral("pl-modal")};

const Role kFloating{
    Layer::Overlay, AnchorNone, -1, KeyboardInteractivity::None, QMargins(), QStringLiteral("pl-floating")};

} // namespace

const Role& Wallpaper = kWallpaper;
const Role& Hud = kHud;
const Role& Modal = kModal;
const Role& Floating = kFloating;

Role Panel(Edge edge)
{
    Anchors anchors;
    QString prefix;
    switch (edge) {
    case Edge::Top:
        anchors = Anchor::Top | Anchor::Left | Anchor::Right;
        prefix = QStringLiteral("pl-top-panel");
        break;
    case Edge::Bottom:
        anchors = Anchor::Bottom | Anchor::Left | Anchor::Right;
        prefix = QStringLiteral("pl-bottom-panel");
        break;
    case Edge::Left:
        anchors = Anchor::Top | Anchor::Bottom | Anchor::Left;
        prefix = QStringLiteral("pl-left-panel");
        break;
    case Edge::Right:
        anchors = Anchor::Top | Anchor::Bottom | Anchor::Right;
        prefix = QStringLiteral("pl-right-panel");
        break;
    }
    return Role{Layer::Top, anchors, 0, KeyboardInteractivity::OnDemand, QMargins(), std::move(prefix)};
}

Role Toast(Corner corner)
{
    Anchors anchors;
    QString prefix;
    switch (corner) {
    case Corner::TopLeft:
        anchors = Anchor::Top | Anchor::Left;
        prefix = QStringLiteral("pl-top-left-toast");
        break;
    case Corner::TopRight:
        anchors = Anchor::Top | Anchor::Right;
        prefix = QStringLiteral("pl-top-right-toast");
        break;
    case Corner::BottomLeft:
        anchors = Anchor::Bottom | Anchor::Left;
        prefix = QStringLiteral("pl-bottom-left-toast");
        break;
    case Corner::BottomRight:
        anchors = Anchor::Bottom | Anchor::Right;
        prefix = QStringLiteral("pl-bottom-right-toast");
        break;
    }
    return Role{Layer::Top, anchors, -1, KeyboardInteractivity::None, QMargins(), std::move(prefix)};
}

} // namespace Patterns

} // namespace PhosphorLayer
