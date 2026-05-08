// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QString>
#include <QUrl>

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT ShellLoader
{
public:
    explicit ShellLoader(const QString& shellName = QStringLiteral("phosphor-shell"));

    QUrl resolve() const;
    QString shellConfigDir() const;

private:
    QString m_shellName;
};

} // namespace PhosphorShell
