// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCloseEvent>
#include <QList>
#include <QMainWindow>
#include <QShortcut>
#include <QString>

class QComboBox;
class QLabel;
class QMenu;
class QQuickWidget;
class QSplitter;
class QTabWidget;

class KRecentFilesAction;

namespace KTextEditor {
class Document;
class Editor;
class View;
}

namespace PlasmaZones {

class GlslCompletionModel;
class MetadataEditorWidget;
class OutputPanel;
class PresetPanel;
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
    void createActions();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupLayout();
    void openShaderPackageDialog();
    void populateEditMenu();
    KTextEditor::View* activeView() const;
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
    void updateErrorMarks();
    void clearErrorMarks();

    KTextEditor::Editor* m_editor = nullptr;

    // Left panel: code editor tabs (top) + output panel (bottom)
    QTabWidget* m_tabWidget = nullptr;
    OutputPanel* m_outputPanel = nullptr;
    QSplitter* m_leftSplitter = nullptr;  // vertical: code on top, output on bottom

    // Right panel: preview (top) + tabbed panel (bottom)
    QQuickWidget* m_previewWidget = nullptr;
    QSplitter* m_rightSplitter = nullptr;  // vertical: preview on top, tabs on bottom

    // Right bottom tab widget: Parameters | Metadata | Presets
    QTabWidget* m_rightTabWidget = nullptr;
    ParameterPanel* m_parameterPanel = nullptr;
    MetadataEditorWidget* m_metadataEditor = nullptr;
    PresetPanel* m_presetPanel = nullptr;

    // Main splitter: left (code) + right (preview+tabs)
    QSplitter* m_mainSplitter = nullptr;

    // Live preview
    PreviewController* m_previewController = nullptr;

    GlslCompletionModel* m_completionModel = nullptr;
    QList<KTextEditor::Document*> m_ownedDocuments;
    QString m_packagePath;
    bool m_isNewPackage = false;

    // Shared actions (used in both menu bar and toolbar)
    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_compileAction = nullptr;
    QAction* m_validateAction = nullptr;
    QAction* m_resetParamsAction = nullptr;

    // Recent files
    KRecentFilesAction* m_recentAction = nullptr;

    // Menus
    QMenu* m_editMenu = nullptr;

    // View toggle actions
    QAction* m_toggleOutputAction = nullptr;
    QAction* m_togglePreviewAction = nullptr;
    QAction* m_toggleRightTabsAction = nullptr;

    // Toolbar widgets
    QComboBox* m_layoutCombo = nullptr;

    // Status bar widgets
    QLabel* m_fileLabel = nullptr;
    QLabel* m_cursorLabel = nullptr;
};

} // namespace PlasmaZones
