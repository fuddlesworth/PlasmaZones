// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellPatterns/Patterns.h>

namespace PhosphorShellPatterns {

using PhosphorLayer::Anchor;
using PhosphorLayer::AnchorAll;
using PhosphorLayer::AnchorNone;
using PhosphorLayer::Anchors;
using PhosphorLayer::KeyboardInteractivity;
using PhosphorLayer::Layer;
using PhosphorLayer::Role;

// Each preset is exposed via a function-local static (Meyers singleton).
// Construction is deferred to first call, thread-safe by §6.7.4
// "Initialization of block-scope static variables", and dodges the static
// initialization order fiasco entirely. Consumers can store derived Roles
// (built via `withScopePrefix(...)`, `withMargins(...)`, etc.) in their
// own `inline const` globals without depending on dynamic-init ordering
// across translation units or shared libraries.

const Role& Wallpaper()
{
    static const Role r{Layer::Background,           AnchorAll,  0,
                        KeyboardInteractivity::None, QMargins(), QStringLiteral("pl-background")};
    return r;
}

const Role& Hud()
{
    static const Role r{
        Layer::Overlay, AnchorAll, -1, KeyboardInteractivity::None, QMargins(), QStringLiteral("pl-fullscreen")};
    return r;
}

const Role& Modal()
{
    static const Role r{
        Layer::Top, AnchorNone, -1, KeyboardInteractivity::Exclusive, QMargins(), QStringLiteral("pl-modal")};
    return r;
}

const Role& Floating()
{
    static const Role r{
        Layer::Overlay, AnchorNone, -1, KeyboardInteractivity::None, QMargins(), QStringLiteral("pl-floating")};
    return r;
}

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

} // namespace PhosphorShellPatterns
