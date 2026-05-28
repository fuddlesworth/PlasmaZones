// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// RAII envelope around SettingsController::beginExternalEdit /
// endExternalEdit. Extracted from settingscontroller.h so that
// header stays under the project's 800-line cap (CLAUDE.md). Only
// `settingscontroller.h` re-includes this header so consumers that
// already `#include "settingscontroller.h"` pick the scope up
// transitively without any call-site change.

#pragma once

#include <QString>

namespace PlasmaZones {

class SettingsController;

/// RAII envelope for begin/end external-edit pairs. The reference
/// member implicitly deletes copy/move so the scope is unique.
///
/// Implementation lives in `externaleditscope.h` itself (header-only
/// inline) — the bodies are two-line forwards to public methods on
/// SettingsController, so the only build cost is the forward
/// declaration above.
class ExternalEditScope
{
public:
    ExternalEditScope(SettingsController& owner, const QString& page);
    ~ExternalEditScope();
    ExternalEditScope(const ExternalEditScope&) = delete;
    ExternalEditScope& operator=(const ExternalEditScope&) = delete;

private:
    SettingsController& m_owner;
};

} // namespace PlasmaZones
