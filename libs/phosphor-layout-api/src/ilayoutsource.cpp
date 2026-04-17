// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/ILayoutSource.h>

namespace PhosphorLayout {

// Out-of-line virtual destructor anchors the vtable in this TU. Without
// it every consumer .cpp that includes ILayoutSource.h would emit its
// own copy of the vtable as a weak symbol, bloating debug info and
// risking link-time duplicate-vtable warnings.
ILayoutSource::~ILayoutSource() = default;

} // namespace PhosphorLayout
