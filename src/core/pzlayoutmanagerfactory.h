// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// PlasmaZones-flavoured @ref PhosphorZones::LayoutManager factory.
// Lives here (project-side) so PZ-specific config-schema knowledge
// (@c ConfigDefaults schema strings, @c createAssignmentsBackend
// factory, hardcoded layout directory path) stays out of the
// project-agnostic @c phosphor-zones library.

#pragma once

#include "plasmazones_export.h"

#include <memory>

class QObject;

namespace PhosphorZones {
class LayoutManager;
}

namespace PlasmaZones {

/**
 * @brief Construct a PhosphorZones::LayoutManager wired against PlasmaZones config defaults.
 *
 * Pulls the schema-key strings from @c ConfigDefaults, the persistent
 * backend from @c createAssignmentsBackend(), and the legacy-migration
 * fallback from @c createDefaultConfigBackend(). Every PlasmaZones
 * composition root (daemon, editor, settings) and test fixture goes
 * through this — never construct @c PhosphorZones::LayoutManager
 * directly from PZ code.
 *
 * @param parent Qt parent for the resulting manager.
 */
PLASMAZONES_EXPORT std::unique_ptr<PhosphorZones::LayoutManager> makePzLayoutManager(QObject* parent = nullptr);

} // namespace PlasmaZones
