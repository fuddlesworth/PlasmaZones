// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCloseEvent>
#include <QList>
#include <QMainWindow>

namespace ads {
class CDockAreaWidget;
class CDockManager;
class CDockWidget;
}

namespace KTextEditor {
class MovingRange;
}
#include <QShortcut>
#include <QString>

class QComboBox;
class QLabel;
class QMenu;
class QQuickView;
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
    void exportShaderPackage();

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
    QString buildMetadataJsonForSave() const;

    KTextEditor::Editor* m_editor = nullptr;

    // Central widget: code editor tabs
    QTabWidget* m_tabWidget = nullptr;

    // Dock manager (ads)
    ads::CDockManager* m_dockManager = nullptr;
    ads::CDockAreaWidget* m_previewArea = nullptr;

    // Dock widgets (ads)
    ads::CDockWidget* m_previewDock = nullptr;
    ads::CDockWidget* m_outputDock = nullptr;
    ads::CDockWidget* m_paramsDock = nullptr;
    ads::CDockWidget* m_metadataDock = nullptr;
    ads::CDockWidget* m_presetsDock = nullptr;

    // Panel contents (owned by their dock widgets)
    QQuickView* m_previewView = nullptr;
    QWidget* m_previewWidget = nullptr;
    OutputPanel* m_outputPanel = nullptr;
    ParameterPanel* m_parameterPanel = nullptr;
    MetadataEditorWidget* m_metadataEditor = nullptr;
    PresetPanel* m_presetPanel = nullptr;

    // Live preview
    PreviewController* m_previewController = nullptr;

    GlslCompletionModel* m_completionModel = nullptr;
    QList<KTextEditor::MovingRange*> m_errorRanges;
    QHash<int, QString> m_errorMessages; // line (0-indexed) → error text for tooltips
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
    QAction* m_exportAction = nullptr;
    QAction* m_resetParamsAction = nullptr;

    // Recent files
    KRecentFilesAction* m_recentAction = nullptr;

    // Menus
    QMenu* m_editMenu = nullptr;


    // Toolbar widgets
    QComboBox* m_layoutCombo = nullptr;

    // Status bar widgets
    QLabel* m_fileLabel = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_shaderInfoLabel = nullptr;
    QLabel* m_compileStatusLabel = nullptr;
};

} // namespace PlasmaZones
