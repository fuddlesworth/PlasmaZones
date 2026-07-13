// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorSurface/DecorationProfileTree.h>

#include "StubSettings.h"

/// Named StubSettings alias for the decoration-tree tests (page-controller and
/// decoration-set), shared so their setup reads the same way and cannot drift.
///
/// The base StubSettings now genuinely stores the tree and emits
/// decorationProfileTreeChanged on a real change (it was upgraded from a no-op
/// setter — see StubSettings::setDecorationProfileTreeJson), so these overrides
/// mirror the base rather than adding behaviour. They are kept only as the named
/// seam; a test could equally use StubSettings directly.
class TreeStubSettings : public PlasmaZones::StubSettings
{
public:
    using PlasmaZones::StubSettings::StubSettings;

    PhosphorSurfaceShaders::DecorationProfileTree decorationProfileTree() const override
    {
        return m_tree;
    }
    void setDecorationProfileTree(const PhosphorSurfaceShaders::DecorationProfileTree& tree) override
    {
        if (m_tree == tree)
            return;
        m_tree = tree;
        Q_EMIT decorationProfileTreeChanged();
        Q_EMIT settingsChanged();
    }

private:
    PhosphorSurfaceShaders::DecorationProfileTree m_tree;
};
