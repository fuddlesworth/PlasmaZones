// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellGlobal.h>
#include <PhosphorShell/ScreenModel.h>

namespace PhosphorShell {

ShellGlobal::ShellGlobal(QObject* parent)
    : QObject(parent)
{
}

ShellGlobal::~ShellGlobal() = default;

ScreenModel* ShellGlobal::screens() const
{
    return m_screens;
}

void ShellGlobal::setScreenModel(ScreenModel* model)
{
    m_screens = model;
}

} // namespace PhosphorShell
