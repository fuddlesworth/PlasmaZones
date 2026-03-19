// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "outputpanel.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <KLocalizedString>

namespace PlasmaZones {

OutputPanel::OutputPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabPosition(QTabWidget::South);
    m_tabWidget->setDocumentMode(true);

    // ── Problems tab ──
    m_problemsTree = new QTreeWidget;
    m_problemsTree->setHeaderLabels({i18n("Severity"), i18n("Line"), i18n("Message")});
    m_problemsTree->setRootIsDecorated(false);
    m_problemsTree->setAlternatingRowColors(true);
    m_problemsTree->header()->setStretchLastSection(true);
    m_problemsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_problemsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    connect(m_problemsTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item) return;
        const int line = item->text(1).toInt();
        if (line > 0) {
            Q_EMIT problemDoubleClicked(line);
        }
    });

    m_tabWidget->addTab(m_problemsTree, i18n("Problems"));

    // ── Output tab ──
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(9);

    m_outputText = new QTextEdit;
    m_outputText->setReadOnly(true);
    m_outputText->setFont(monoFont);
    m_tabWidget->addTab(m_outputText, i18n("Output"));

    // ── Compiler tab ──
    m_compilerText = new QTextEdit;
    m_compilerText->setReadOnly(true);
    m_compilerText->setFont(monoFont);
    m_tabWidget->addTab(m_compilerText, i18n("Compiler"));

    layout->addWidget(m_tabWidget);
}

void OutputPanel::setCompilationSuccess(int lineCount, const QStringList& uniforms, const QStringList& includes)
{
    // Problems: clear, show success
    m_problemsTree->clear();
    m_tabWidget->setTabText(0, i18n("Problems"));

    // Output: compilation summary
    const QColor successColor = palette().color(QPalette::Active, QPalette::Link);
    m_outputText->clear();
    m_outputText->append(QStringLiteral("<span style='color:%1'>&#10003; Shader compiled successfully (GLSL 450, %2 lines)</span>")
        .arg(successColor.name(), QString::number(lineCount)));
    if (!uniforms.isEmpty()) {
        m_outputText->append(QStringLiteral("  Uniforms: %1").arg(uniforms.join(QStringLiteral(", "))));
    }
    if (!includes.isEmpty()) {
        m_outputText->append(QStringLiteral("  Includes resolved: %1").arg(includes.join(QStringLiteral(", "))));
    }

    // Compiler: raw success
    m_compilerText->clear();
    m_compilerText->append(i18n("Compilation successful."));
}

void OutputPanel::setCompilationError(const QString& errorLog)
{
    // Problems: parse error lines
    m_problemsTree->clear();

    if (errorLog.isEmpty()) {
        m_tabWidget->setTabText(0, i18n("Problems"));
        return;
    }

    // Try to extract line numbers from error messages
    // Common formats: "ERROR: 0:42: ..." or "line 42: ..." or just raw text
    const QStringList lines = errorLog.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    int errorCount = 0;

    for (const QString& line : lines) {
        auto* item = new QTreeWidgetItem(m_problemsTree);

        // Extract line number from GLSL error formats:
        // "ERROR: 0:42: ..." (string:line), "ERROR: :42: ..." (no string ID), "line 42: ..."
        int errorLine = 0;
        static const QRegularExpression linePattern(QStringLiteral("(?:\\d*:(\\d+):|\\bline\\s+(\\d+))"));
        const auto match = linePattern.match(line);
        if (match.hasMatch()) {
            errorLine = match.captured(1).toInt();
            if (errorLine == 0) {
                errorLine = match.captured(2).toInt();
            }
        }

        const bool isError = line.contains(QLatin1String("ERROR"), Qt::CaseInsensitive)
                          || line.contains(QLatin1String("error"), Qt::CaseSensitive);
        const bool isWarning = line.contains(QLatin1String("WARNING"), Qt::CaseInsensitive)
                            || line.contains(QLatin1String("warning"), Qt::CaseSensitive);

        if (isError) {
            item->setText(0, i18n("Error"));
            item->setForeground(0, QGuiApplication::palette().color(QPalette::Active, QPalette::ToolTipText));
            item->setBackground(0, QColor(255, 68, 68, 40));
            errorCount++;
        } else if (isWarning) {
            item->setText(0, i18n("Warning"));
            item->setForeground(0, QGuiApplication::palette().color(QPalette::Active, QPalette::Link));
        } else {
            item->setText(0, i18n("Info"));
            errorCount++;
        }

        item->setText(1, errorLine > 0 ? QString::number(errorLine) : QString());
        item->setText(2, line.trimmed());
    }

    if (errorCount == 0) {
        errorCount = lines.size();
    }

    m_tabWidget->setTabText(0, i18n("Problems (%1)", errorCount));
    m_tabWidget->setCurrentIndex(0);

    // Compiler: raw error output
    const QColor errorColor = QGuiApplication::palette().color(QPalette::Active, QPalette::ToolTipText);
    m_compilerText->clear();
    m_compilerText->append(QStringLiteral("<span style='color:%1'>%2</span>")
        .arg(errorColor.name(), errorLog.toHtmlEscaped()));

    // Output
    m_outputText->clear();
    m_outputText->append(QStringLiteral("<span style='color:%1'>&#10007; Shader compilation failed</span>")
        .arg(errorColor.name()));
}

void OutputPanel::clearOutput()
{
    m_problemsTree->clear();
    m_outputText->clear();
    m_compilerText->clear();
    m_tabWidget->setTabText(0, i18n("Problems"));
}

void OutputPanel::appendOutput(const QString& text)
{
    m_outputText->append(text);
}

} // namespace PlasmaZones
