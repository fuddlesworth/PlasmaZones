// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Out-of-line definitions for the otherwise header-only
// `MetadataPackScanStrategy<Payload>` template. Hosts the default
// internal logging category used when a consumer doesn't pass its own
// via `setLoggingCategory`. Declared in the header (so template code
// can reference it); defined here so we don't end up with multiple
// independent category instances across translation units that
// instantiate the template.

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

namespace PhosphorFsLoader::detail {

Q_LOGGING_CATEGORY(lcMetadataPackScan, "phosphorfsloader.metadatapackscan")

} // namespace PhosphorFsLoader::detail
