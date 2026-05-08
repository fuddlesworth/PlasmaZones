// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace PhosphorShell {

class ScreenModel;

class PHOSPHORSHELL_EXPORT ShellGlobal : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PhosphorShell)
    QML_SINGLETON

    Q_PROPERTY(ScreenModel* screens READ screens CONSTANT)

public:
    explicit ShellGlobal(QObject* parent = nullptr);
    ~ShellGlobal() override;

    ScreenModel* screens() const;
    void setScreenModel(ScreenModel* model);

private:
    ScreenModel* m_screens = nullptr;
};

} // namespace PhosphorShell
