// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtCore/QtGlobal>

namespace PhosphorFsLoader {

/// Per-file size cap for JSON metadata reads. Files larger than this are
/// skipped with a warning — guards the GUI thread against a pathological
/// user JSON. 1 MiB is far above any legitimate curve / profile / layout
/// schema in this library's ecosystem.
///
/// Canonical home, in its own tiny header so a validator that needs only
/// the figure (JsonEnvelopeValidator) does not pull in all of
/// DirectoryLoader.h. `DirectoryLoader::kMaxFileBytes` aliases this.
inline constexpr qint64 kMaxJsonFileBytes = 1 * 1024 * 1024;

} // namespace PhosphorFsLoader
