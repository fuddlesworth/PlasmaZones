// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QObject>
#include <QString>

namespace PhosphorShell {

// Exposed to QML as a context property (`Environment`) by ShellEngine —
// not registered as a QML singleton. Q_INVOKABLE methods are still
// callable from QML through the context-property pointer.
class PHOSPHORSHELL_EXPORT Environment : public QObject
{
    Q_OBJECT

public:
    explicit Environment(QObject* parent = nullptr);
    ~Environment() override;

    [[nodiscard]] Q_INVOKABLE QString get(const QString& name) const;
    [[nodiscard]] Q_INVOKABLE bool has(const QString& name) const;
};

} // namespace PhosphorShell
