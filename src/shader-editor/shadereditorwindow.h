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

class MetadataEditorWidget;
class ParameterPanel;
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
    void setupLayout();
    void addDocumentTab(const QString& filename, const QString& content, const QString& highlightMode);
    void connectDocumentToPreview(const QString& filename, KTextEditor::Document* doc);
    void setupRightPanel(const QString& metadataJson);
    void insertTextAtCursor(const QString& text);
    void updateWindowTitle();
    void updateStatusBar();
    bool promptSaveIfModified();
    bool hasUnsavedChanges() const;
    void closeAllTabs();
    QString resolveShaderPath(const QString& shaderId) const;

    KTextEditor::Editor* m_editor = nullptr;

    // Left panel: code editor tabs (only .frag/.vert files)
    QTabWidget* m_tabWidget = nullptr;

    // Right panel: preview (top) + tabbed panel (bottom)
    QQuickWidget* m_previewWidget = nullptr;
    QSplitter* m_rightSplitter = nullptr;  // vertical: preview on top, tabs on bottom

    // Right bottom tab widget: Parameters | Metadata | Presets
    QTabWidget* m_rightTabWidget = nullptr;
    ParameterPanel* m_parameterPanel = nullptr;
    MetadataEditorWidget* m_metadataEditor = nullptr;

    // Main splitter: left (code) + right (preview+tabs)
    QSplitter* m_mainSplitter = nullptr;

    // Live preview
    PreviewController* m_previewController = nullptr;

    QList<KTextEditor::Document*> m_ownedDocuments;
    QString m_packagePath;
    bool m_isNewPackage = false;

    // Status bar widgets
    QLabel* m_fileLabel = nullptr;
    QLabel* m_cursorLabel = nullptr;
};

} // namespace PlasmaZones
