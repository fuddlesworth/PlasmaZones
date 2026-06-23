// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Private, built into PhosphorTheme, not installed. The default palette
// is an implementation detail of PaletteStore::resetToDefaults.

#include <QVariantMap>

namespace PhosphorTheme::detail {

QVariantMap defaultDarkPalette();

} // namespace PhosphorTheme::detail
