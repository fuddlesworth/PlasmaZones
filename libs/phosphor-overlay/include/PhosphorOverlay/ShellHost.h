// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ShellHost — per-screen layer-shell shell host that owns one wl_surface
// and hosts the QQuickItem tree of named slots living on it.
//
// Phase 1 scaffolding: empty class with a default ctor/dtor only. The
// real mechanism (passive-shell creation, screen hot-plug handling,
// scope-gen rekey, slot lifecycle) lands in Phase 2 when
// OverlayService::ensurePassiveShellFor and friends move in here.
//
// API stability promise (per the plugin-based-compositor direction):
// once Phase 2 lands a real public surface, this header is part of the
// library's stable ABI. The Phase 1 stub does not pin any public
// surface beyond the type's existence.

#include <PhosphorOverlay/phosphoroverlay_export.h>

#include <QObject>

namespace PhosphorOverlay {

class PHOSPHOROVERLAY_EXPORT ShellHost : public QObject
{
    Q_OBJECT

public:
    explicit ShellHost(QObject* parent = nullptr);
    ~ShellHost() override;
};

} // namespace PhosphorOverlay
