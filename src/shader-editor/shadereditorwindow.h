// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCloseEvent>
#include <QList>
#include <QMainWindow>
#include <QString>

class QLabel;
class QQuickWidget;
class QSplitter;
class QTabWidget;

namespace KTextEditor {
class Document;
class Editor;
class View;
}

namespace PlasmaZones {

class PreviewController;

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
    void setupPreview();
    void addDocumentTab(const QString& filename, const QString& content, const QString& highlightMode);
    void connectDocumentToPreview(const QString& filename, KTextEditor::Document* doc);
    void updateWindowTitle();
    void updateStatusBar();
    bool promptSaveIfModified();
    bool hasUnsavedChanges() const;
    void closeAllTabs();
    QString resolveShaderPath(const QString& shaderId) const;

    KTextEditor::Editor* m_editor = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QSplitter* m_splitter = nullptr;
    QList<KTextEditor::Document*> m_ownedDocuments;
    QString m_packagePath;
    bool m_isNewPackage = false;

    // Live preview
    PreviewController* m_previewController = nullptr;
    QQuickWidget* m_previewWidget = nullptr;

    // Status bar widgets
    QLabel* m_fileLabel = nullptr;
    QLabel* m_cursorLabel = nullptr;
};

} // namespace PlasmaZones
