// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "phosphorcontrol_export.h"

#include <QString>
#include <QStringList>

namespace PhosphorControl {

/// Checks one Facet module without a window, for the module's own CI.
///
/// run() copies the module library into a private search path, hosts it in a
/// FacetHost over a fresh ApplicationController, loads it, and checks that:
///   - the catalog accepts it (IID, ApiVersion, a valid "Id")
///   - its metadata is valid
///   - it loads, and registerInto registers exactly the pages its metadata
///     declares, with matching controller ids
///   - every page controller and headless domain emits applyResult and
///     discardResult exactly once when applied and discarded
///
/// Needs a QCoreApplication (or QGuiApplication) running on this thread.
class PHOSPHORCONTROL_EXPORT FacetTestHost
{
public:
    explicit FacetTestHost(QString modulePath);

    /// How long to wait for a domain's applyResult or discardResult.
    void setResultTimeoutMs(int ms);

    /// Every problem found, one line each. Empty means the module passes.
    [[nodiscard]] QStringList run();

private:
    QString m_modulePath;
    int m_resultTimeoutMs = 2000;
};

} // namespace PhosphorControl
