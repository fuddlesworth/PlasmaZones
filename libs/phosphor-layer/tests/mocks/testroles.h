// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/Role.h>

#include <QMargins>
#include <QString>

/// Inline Role fixtures for phosphor-layer's own tests.
///
/// The axis-2 named patterns (Hud, Modal, …) live in the sibling library
/// `phosphor-shell-patterns`; this test-only helper duplicates the
/// minimal Role shapes the layer-lib tests need so phosphor-layer can be
/// built and tested without taking a dependency on its own consumer.
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
