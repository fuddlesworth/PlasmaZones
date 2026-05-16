// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/ILayoutSource.h>

namespace PhosphorLayout {

ILayoutSource::ILayoutSource(QObject* parent)
    : QObject(parent)
{
}

// Out-of-line destructor anchors the vtable in this TU so every consumer
// .cpp that includes ILayoutSource.h doesn't emit its own copy as a weak
// symbol.
ILayoutSource::~ILayoutSource() = default;

} // namespace PhosphorLayout
