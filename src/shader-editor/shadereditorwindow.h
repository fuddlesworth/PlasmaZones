// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCloseEvent>
#include <QList>
#include <QMainWindow>
#include <QString>

class QLabel;
class QTabWidget;

namespace KTextEditor {
class Document;
class Editor;
class View;
}

namespace PlasmaZones {

class ShaderEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ShaderEditorWindow(QWidget* parent = nullptr);
    ~ShaderEditorWindow() override;

    bool isValid() const;

    void newShaderPackage();
    void openShaderPackage(const QString& path);
    void openShaderById(const QString& shaderId);
    void saveShaderPackage();
    void saveShaderPackageAs();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupMenuBar();
    void setupStatusBar();
    void addDocumentTab(const QString& filename, const QString& content, const QString& highlightMode);
    void updateWindowTitle();
    void updateStatusBar();
    bool promptSaveIfModified();
    bool hasUnsavedChanges() const;
    void closeAllTabs();
    QString resolveShaderPath(const QString& shaderId) const;

    KTextEditor::Editor* m_editor = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QList<KTextEditor::Document*> m_ownedDocuments;
    QString m_packagePath;
    bool m_isNewPackage = false;

    // Status bar widgets
    QLabel* m_fileLabel = nullptr;
    QLabel* m_cursorLabel = nullptr;
};

} // namespace PlasmaZones
