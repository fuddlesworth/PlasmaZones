// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QLabel;
class QTabWidget;
class QTextEdit;
class QTreeWidget;

namespace PlasmaZones {

class OutputPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutputPanel(QWidget* parent = nullptr);

    void setCompilationSuccess(int lineCount, const QStringList& uniforms, const QStringList& includes);
    void setCompilationError(const QString& errorLog);
    void clearOutput();
    void appendOutput(const QString& text);

Q_SIGNALS:
    void problemDoubleClicked(int line);

private:
    QTabWidget* m_tabWidget = nullptr;
    QTreeWidget* m_problemsTree = nullptr;
    QTextEdit* m_outputText = nullptr;
    QTextEdit* m_compilerText = nullptr;
    QLabel* m_problemsBadge = nullptr;
};

} // namespace PlasmaZones
