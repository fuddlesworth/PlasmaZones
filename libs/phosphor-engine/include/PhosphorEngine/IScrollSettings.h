// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>

#include <QObject>
#include <QVariantList>

namespace PhosphorEngine {

/// Geometry configuration the niri-style scroll engine pulls through an
/// abstract interface — full parity with IAutotileSettings / ISnapSettings.
///
/// The daemon hands ScrollEngine a QObject* implementing this interface via
/// PlacementEngineBase::setEngineSettings(); the engine qobject_casts to
/// IScrollSettings at point of use (see ScrollEngine::scrollSettings()) and
/// reads the global defaults its effective*() resolvers fall back to when a
/// screen has no per-screen override. Getters only — the engine never writes
/// scroll geometry config back.
///
/// Implementations MUST emit a Qt change signal whenever any of the values
/// returned by these getters changes — the engine's effective*() resolvers
/// run lazily, so without a change notification the daemon (which observes
/// these signals on the concrete QObject — see Settings's
/// scrollInnerGapChanged / scrollOuterGapChanged / scrollDefaultColumnWidthChanged
/// / scrollCenterFocusedColumnChanged / scrollPresetColumnWidthsChanged /
/// scrollPresetWindowHeightsChanged signals) cannot trigger the
/// placementChanged re-resolve that propagates the new geometry to KWin.
/// IAutotileSettings and ISnapSettings rely on the same daemon-side signal
/// wiring; this interface follows that precedent.
///
/// The class is exported so its typeinfo has default visibility across the
/// libphosphor-scroll-engine ↔ daemon shared-library boundary; the engine
/// resolves the daemon's settings backend via `qobject_cast<IScrollSettings*>`,
/// and losing the export silently turns every effective*() resolver into a
/// fall-through to the engine's hard-coded defaults. Mirrors IScrollEngine /
/// IScrollNavigation, both of which document and apply the same export.
class PHOSPHORENGINE_EXPORT IScrollSettings
{
public:
    virtual ~IScrollSettings() = default;

    virtual int scrollInnerGap() const = 0;
    virtual int scrollOuterGap() const = 0;
    virtual qreal scrollDefaultColumnWidth() const = 0;
    virtual bool scrollCenterFocusedColumn() const = 0;
    virtual QVariantList scrollPresetColumnWidths() const = 0;
    virtual QVariantList scrollPresetWindowHeights() const = 0;
};

} // namespace PhosphorEngine

Q_DECLARE_INTERFACE(PhosphorEngine::IScrollSettings, "org.plasmazones.IScrollSettings")
