// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Environment.h>

namespace PhosphorShell {

Environment::Environment(QObject* parent)
    : QObject(parent)
{
}

Environment::~Environment() = default;

QString Environment::get(const QString& name) const
{
    return qEnvironmentVariable(name.toUtf8().constData());
}

bool Environment::has(const QString& name) const
{
    return qEnvironmentVariableIsSet(name.toUtf8().constData());
}

} // namespace PhosphorShell
