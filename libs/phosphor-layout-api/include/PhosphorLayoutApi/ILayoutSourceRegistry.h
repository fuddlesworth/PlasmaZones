// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorlayoutapi_export.h>

#include <QObject>

namespace PhosphorLayout {

/// Abstract notifier surface for every registry that feeds an
/// @c ILayoutSource.
///
/// A provider library (phosphor-zones, phosphor-tiles, future
/// phosphor-scrolling, …) has some concrete registry class that owns
/// its in-memory catalogue of layouts and mutates it over time. That
/// registry inherits @c ILayoutSourceRegistry so the matching
/// @c ILayoutSource can self-wire a single @c contentsChanged signal
/// in its constructor, without every consumer having to know which
/// provider-specific signal to bridge.
///
/// Providers with finer-grained signals of their own (e.g.
/// @c AlgorithmRegistry's algorithmRegistered / algorithmUnregistered
/// / previewParamsChanged) keep them alongside this interface and
/// internally forward each one into @c contentsChanged — callers that
/// only care about "something changed" listen on the unified signal;
/// callers that need discrimination stay on the provider-specific
/// ones.
///
/// Intentionally QObject-derived. The corresponding zones interface
/// (@c PhosphorZones::ILayoutRegistry) inherits this type to pick up
/// the signal, and concrete registries inherit through it rather than
/// through @c QObject directly so there is still exactly one QObject
/// base along every inheritance path — the multi-inheritance hazard
/// described in @c PhosphorZones::ILayoutManager only shows up when a
/// class ends up with two distinct QObject subobjects, which this
/// single-root pattern prevents.
class PHOSPHORLAYOUTAPI_EXPORT ILayoutSourceRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ILayoutSourceRegistry(QObject* parent = nullptr);
    ~ILayoutSourceRegistry() override;

Q_SIGNALS:
    /// Emitted when the set of layouts this registry produces changes
    /// in any way that invalidates cached previews — entries added,
    /// removed, renamed, or re-parameterised. Corresponds 1:1 with
    /// @c ILayoutSource::contentsChanged on the source side; concrete
    /// @c ILayoutSource implementations are expected to bridge the
    /// two directly in their constructor.
    void contentsChanged();
};

} // namespace PhosphorLayout
