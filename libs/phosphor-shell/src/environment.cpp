// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Environment.h>

namespace PhosphorShell {

namespace {
// POSIX env names cannot contain `=` or NUL. Reject defensively at the
// QML boundary so we don't pass garbage into qEnvironmentVariable.
bool isValidName(const QString& name)
{
    if (name.isEmpty()) {
        return false;
    }
    if (name.contains(QLatin1Char('=')) || name.contains(QChar(0))) {
        return false;
    }
    return true;
}
} // namespace

Environment::Environment(QObject* parent)
    : QObject(parent)
{
}

Environment::~Environment() = default;

QString Environment::get(const QString& name) const
{
    if (!isValidName(name)) {
        return {};
    }
    // Stash the QByteArray in a local — passing a temporary's .constData()
    // directly to qEnvironmentVariable would dangle the moment the
    // expression ends.
    const QByteArray utf8 = name.toUtf8();
    return qEnvironmentVariable(utf8.constData());
}

bool Environment::has(const QString& name) const
{
    if (!isValidName(name)) {
        return false;
    }
    const QByteArray utf8 = name.toUtf8();
    return qEnvironmentVariableIsSet(utf8.constData());
}

} // namespace PhosphorShell
