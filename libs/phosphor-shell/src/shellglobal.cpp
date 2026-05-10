// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellGlobal.h>
#include <PhosphorShell/PersistentProperties.h>
#include <PhosphorShell/ScreenModel.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcShellGlobal, "phosphorshell.shellglobal")

namespace PhosphorShell {

ShellGlobal::ShellGlobal(QObject* parent)
    : QObject(parent)
{
}

ShellGlobal::~ShellGlobal() = default;

ScreenModel* ShellGlobal::screens() const
{
    return m_screens.data();
}

void ShellGlobal::setScreenModel(ScreenModel* model)
{
    if (m_screens.data() == model) {
        return;
    }
    m_screens = model;
    Q_EMIT screensChanged();
}

QObject* ShellGlobal::singleton(const QString& reloadId) const
{
    // QPointer<PersistentProperties> auto-clears on QObject destruction;
    // value() returns the (possibly null) wrapped pointer.
    return m_singletons.value(reloadId).data();
}

void ShellGlobal::registerSingleton(const QString& reloadId, PersistentProperties* props)
{
    if (m_singletons.contains(reloadId) && m_singletons.value(reloadId).data() != props) {
        qCWarning(lcShellGlobal) << "Replacing existing singleton for reloadId" << reloadId;
    }
    m_singletons.insert(reloadId, QPointer<PersistentProperties>(props));
}

void ShellGlobal::clearSingletons()
{
    m_singletons.clear();
}

} // namespace PhosphorShell
