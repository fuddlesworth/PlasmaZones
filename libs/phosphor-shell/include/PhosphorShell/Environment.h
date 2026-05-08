// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT Environment : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Environment)
    QML_SINGLETON

public:
    explicit Environment(QObject* parent = nullptr);
    ~Environment() override;

    Q_INVOKABLE QString get(const QString& name) const;
    Q_INVOKABLE bool has(const QString& name) const;
};

} // namespace PhosphorShell
