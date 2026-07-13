// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorSurface/DecorationProfileTree.h>

#include "StubSettings.h"

/// StubSettings that genuinely STORES the decoration tree (the base stub's
/// setter is a no-op) and emits decorationProfileTreeChanged on a real change,
/// so a controller's read-mutate-write loop is observable. Shared by the
/// decoration page-controller and decoration-set tests, which both need it —
/// two copies would drift.
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
