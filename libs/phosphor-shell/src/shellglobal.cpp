// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellGlobal.h>
#include <PhosphorShell/PersistentProperties.h>
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

QObject* ShellGlobal::singleton(const QString& reloadId) const
{
    return m_singletons.value(reloadId, nullptr);
}

void ShellGlobal::registerSingleton(const QString& reloadId, PersistentProperties* props)
{
    m_singletons.insert(reloadId, props);
}

void ShellGlobal::clearSingletons()
{
    m_singletons.clear();
}

} // namespace PhosphorShell
