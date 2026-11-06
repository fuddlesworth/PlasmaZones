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
///   - it loads, and registerInto registers exactly the pages and extensions
///     its metadata declares, with matching controller ids
///   - every page controller, headless domain and extension domain emits
///     applyResult and discardResult exactly once when applied and discarded
///   - every entry its search providers return points at one of its pages or
///     a page it extends, and a setting or section entry on its own page at
///     an anchor that page lists
///   - with addTargetModule(), every extension aimed at a target module fits
///     the slot and version that module offers
///   - with setPreviousAnchors(), no page or anchor of the previous release
///     is gone unless the page's anchors version went up
///
/// Needs a QCoreApplication (or QGuiApplication) running on this thread.
class PHOSPHORCONTROL_EXPORT FacetTestHost
{
public:
    explicit FacetTestHost(QString modulePath);

    /// How long to wait for a domain's applyResult or discardResult.
    void setResultTimeoutMs(int ms);

    /// Load the module at @p modulePath too, so the module under test's
    /// extensions aimed at it are checked against its slots.
    void addTargetModule(const QString& modulePath);

    /// The anchors.json of the module's previous release
    /// (phosphor_add_facet_module_metadata's FACET_ANCHORS_FILE).
    void setPreviousAnchors(const QString& anchorsPath);

    /// Every problem found, one line each. Empty means the module passes.
    [[nodiscard]] QStringList run();

private:
    QString m_modulePath;
    QStringList m_targetPaths;
    QString m_previousAnchorsPath;
    int m_resultTimeoutMs = 2000;
};

} // namespace PhosphorControl
