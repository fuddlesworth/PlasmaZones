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

} // namespace PhosphorShellPatterns
