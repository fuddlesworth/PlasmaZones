// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorOverlay/ShellHost.h>

namespace PhosphorOverlay {

ShellHost::ShellHost(QObject* parent)
    : QObject(parent)
{
}

ShellHost::~ShellHost() = default;

} // namespace PhosphorOverlay
