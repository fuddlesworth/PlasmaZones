// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

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
/// scroll geometry config back; settings change signals are wired by the
/// daemon externally, exactly as for autotile and snap.
class IScrollSettings
{
public:
    virtual ~IScrollSettings() = default;

    virtual int scrollInnerGap() const = 0;
    virtual int scrollOuterGap() const = 0;
    virtual double scrollDefaultColumnWidth() const = 0;
    virtual bool scrollCenterFocusedColumn() const = 0;
    virtual QVariantList scrollPresetColumnWidths() const = 0;
    virtual QVariantList scrollPresetWindowHeights() const = 0;
};

} // namespace PhosphorEngine

Q_DECLARE_INTERFACE(PhosphorEngine::IScrollSettings, "org.plasmazones.IScrollSettings")
