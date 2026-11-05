// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/Role.h>

#include <QMargins>
#include <QString>

/// Inline Role fixtures for phosphor-layer's own tests.
///
/// The axis-2 named patterns (Hud, Modal, ...) live in the sibling library
/// `phosphor-shell-patterns`. This test-only helper duplicates the
/// minimal Role shapes the layer-lib tests need so phosphor-layer can be
/// built and tested without taking a dependency on its own consumer.
///
/// These fixtures are deliberate test doubles, NOT shape-mirrors of the
/// production patterns. Their job is to exercise Role-handling code in
/// the layer lib (factory, registry, surface lifecycle) with structurally
/// valid Role values; `phosphor-shell-patterns` has its own tests
/// (`tests/test_patterns.cpp`) that pin the production Pattern shapes
/// against the wlr-layer-shell contract. Do not "fix" drift between this
/// helper and the production patterns by importing the real recipes
/// here. That would create a circular build dependency.
namespace PhosphorLayer::Testing {

inline Role makeModalRole(QString scopePrefix = QStringLiteral("test-modal"))
{
    return Role{Layer::Top, AnchorNone, -1, KeyboardInteractivity::Exclusive, QMargins(), std::move(scopePrefix)};
}

inline Role makeHudRole(QString scopePrefix = QStringLiteral("test-hud"))
{
    return Role{Layer::Overlay, AnchorAll, -1, KeyboardInteractivity::None, QMargins(), std::move(scopePrefix)};
}

inline Role makeToastTopRightRole(QString scopePrefix = QStringLiteral("test-top-right-toast"))
{
    return Role{Layer::Top, Anchor::Top | Anchor::Right, -1, KeyboardInteractivity::None,
                QMargins(), std::move(scopePrefix)};
}

} // namespace PhosphorLayer::Testing
