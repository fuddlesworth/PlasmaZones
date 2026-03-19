// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shadereditorwindow.h"
#include "glslcompletionmodel.h"
#include "metadataeditorwidget.h"
#include "newpackagedialog.h"
#include "outputpanel.h"
#include "parameterpanel.h"
#include "presetpanel.h"
#include "previewcontroller.h"
#include "shaderpackageio.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLoggingCategory>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

#include <KAboutData>
#include <KActionCollection>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KConfigGroup>
#include <KRecentFilesAction>
#include <KSharedConfig>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

Q_LOGGING_CATEGORY(lcShaderEditor, "plasmazones.shadereditor")

namespace PlasmaZones {

ShaderEditorWindow::ShaderEditorWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_editor = KTextEditor::Editor::instance();
    if (!m_editor) {
        qCCritical(lcShaderEditor) << "Failed to get KTextEditor::Editor instance";
        return;
    }

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);

    connect(m_tabWidget, &QTabWidget::currentChanged, this, &ShaderEditorWindow::updateStatusBar);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(index));
        auto* doc = view ? view->document() : nullptr;

        if (doc && doc->isModified()) {
            const int result = QMessageBox::question(
                this,
                i18n("Save Changes"),
                i18n("The file \"%1\" has been modified. Save changes?", m_tabWidget->tabText(index)),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

            if (result == QMessageBox::Cancel) {
                return;
            }
            if (result == QMessageBox::Save) {
                saveShaderPackage();
            }
        }

        m_tabWidget->removeTab(index);
        if (doc) {
            m_ownedDocuments.removeOne(doc);
            delete doc;
        }

        updateWindowTitle();
    });

    setupLayout();
    createActions();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();

    // Tab switching: Ctrl+Tab / Ctrl+Shift+Tab
    auto* nextTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    connect(nextTab, &QShortcut::activated, this, [this]() {
        if (m_tabWidget->count() > 1) {
            m_tabWidget->setCurrentIndex((m_tabWidget->currentIndex() + 1) % m_tabWidget->count());
        }
    });
    auto* prevTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(prevTab, &QShortcut::activated, this, [this]() {
        if (m_tabWidget->count() > 1) {
            m_tabWidget->setCurrentIndex((m_tabWidget->currentIndex() - 1 + m_tabWidget->count()) % m_tabWidget->count());
        }
    });

    resize(1400, 900);
    updateWindowTitle();
}

ShaderEditorWindow::~ShaderEditorWindow() = default;

bool ShaderEditorWindow::isValid() const
{
    return m_editor != nullptr;
}

KTextEditor::View* ShaderEditorWindow::activeView() const
{
    return qobject_cast<KTextEditor::View*>(m_tabWidget->currentWidget());
}

void ShaderEditorWindow::openShaderPackageDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        i18n("Open Shader Package Directory"),
        ShaderPackageIO::userShaderDirectory());
    if (!dir.isEmpty()) {
        openShaderPackage(dir);
    }
}

void ShaderEditorWindow::createActions()
{
    m_newAction = new QAction(QIcon::fromTheme(QStringLiteral("document-new")), i18n("&New Shader Package"), this);
    m_newAction->setShortcut(QKeySequence::New);
    m_newAction->setToolTip(i18n("New Shader Package"));
    connect(m_newAction, &QAction::triggered, this, &ShaderEditorWindow::newShaderPackage);

    m_openAction = new QAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("&Open Shader Package..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setToolTip(i18n("Open Shader Package"));
    connect(m_openAction, &QAction::triggered, this, &ShaderEditorWindow::openShaderPackageDialog);

    m_recentAction = new KRecentFilesAction(QIcon::fromTheme(QStringLiteral("document-open-recent")),
                                            i18n("Open &Recent"), this);
    m_recentAction->setMaxItems(10);
    connect(m_recentAction, &KRecentFilesAction::urlSelected, this, [this](const QUrl& url) {
        if (url.isLocalFile()) {
            openShaderPackage(url.toLocalFile());
        }
    });
    // Load saved recent entries
    const KSharedConfig::Ptr config = KSharedConfig::openConfig();
    m_recentAction->loadEntries(config->group(QStringLiteral("RecentPackages")));

    m_saveAction = new QAction(QIcon::fromTheme(QStringLiteral("document-save")), i18n("&Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setToolTip(i18n("Save Shader Package"));
    connect(m_saveAction, &QAction::triggered, this, &ShaderEditorWindow::saveShaderPackage);

    m_saveAsAction = new QAction(QIcon::fromTheme(QStringLiteral("document-save-as")), i18n("Save &As..."), this);
    m_saveAsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(m_saveAsAction, &QAction::triggered, this, &ShaderEditorWindow::saveShaderPackageAs);

    m_compileAction = new QAction(QIcon::fromTheme(QStringLiteral("run-build")), i18n("&Compile"), this);
    m_compileAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    m_compileAction->setToolTip(i18n("Compile shader (Ctrl+B)"));
    connect(m_compileAction, &QAction::triggered, this, [this]() {
        m_previewController->recompile();
    });

    m_validateAction = new QAction(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), i18n("&Validate"), this);
    m_validateAction->setToolTip(i18n("Validate shader for errors"));
    connect(m_validateAction, &QAction::triggered, this, [this]() {
        m_previewController->recompile();
    });

    m_resetParamsAction = new QAction(QIcon::fromTheme(QStringLiteral("edit-reset")), i18n("&Reset Parameters"), this);
    connect(m_resetParamsAction, &QAction::triggered, this, [this]() {
        if (m_parameterPanel && m_metadataEditor) {
            m_parameterPanel->loadFromMetadata(m_metadataEditor->toJson());
            m_previewController->setShaderParams(m_parameterPanel->currentUniformValues());
        }
    });

    // View toggle actions
    m_toggleOutputAction = new QAction(i18n("&Problems Panel"), this);
    m_toggleOutputAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    m_toggleOutputAction->setCheckable(true);
    m_toggleOutputAction->setChecked(true);
    connect(m_toggleOutputAction, &QAction::toggled, this, [this](bool visible) {
        m_outputPanel->setVisible(visible);
    });

    m_togglePreviewAction = new QAction(i18n("Live &Preview"), this);
    m_togglePreviewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    m_togglePreviewAction->setCheckable(true);
    m_togglePreviewAction->setChecked(true);
    connect(m_togglePreviewAction, &QAction::toggled, this, [this](bool visible) {
        m_previewWidget->setVisible(visible);
    });

    m_toggleRightTabsAction = new QAction(i18n("Pa&rameter Panel"), this);
    m_toggleRightTabsAction->setCheckable(true);
    m_toggleRightTabsAction->setChecked(true);
    connect(m_toggleRightTabsAction, &QAction::toggled, this, [this](bool visible) {
        if (m_rightTabWidget) {
            m_rightTabWidget->setVisible(visible);
        }
    });
}

void ShaderEditorWindow::populateEditMenu()
{
    m_editMenu->clear();

    auto* view = activeView();
    if (!view) {
        auto* noEditorAction = m_editMenu->addAction(i18n("No editor active"));
        noEditorAction->setEnabled(false);
        return;
    }

    // Pull standard editing actions from the active KTextEditor::View
    auto* ac = view->actionCollection();
    static const char* const editActions[] = {
        "edit_undo", "edit_redo", nullptr,
        "edit_cut", "edit_copy", "edit_paste", nullptr,
        "edit_select_all", nullptr,
        "edit_find", "edit_find_next", "edit_replace",
    };

    for (const char* const* p = editActions; p < editActions + sizeof(editActions) / sizeof(*editActions); ++p) {
        if (!*p) {
            m_editMenu->addSeparator();
        } else if (auto* action = ac->action(QString::fromLatin1(*p))) {
            m_editMenu->addAction(action);
        }
    }
}

void ShaderEditorWindow::setupMenuBar()
{
    // ── File ──
    auto* fileMenu = menuBar()->addMenu(i18n("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_recentAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction(i18n("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    // ── Edit (populated dynamically from active KTextEditor::View) ──
    m_editMenu = menuBar()->addMenu(i18n("&Edit"));
    connect(m_editMenu, &QMenu::aboutToShow, this, &ShaderEditorWindow::populateEditMenu);

    // ── Shader ──
    auto* shaderMenu = menuBar()->addMenu(i18n("&Shader"));
    shaderMenu->addAction(m_compileAction);
    shaderMenu->addAction(m_validateAction);
    shaderMenu->addAction(m_resetParamsAction);

    // ── View ──
    auto* viewMenu = menuBar()->addMenu(i18n("&View"));
    viewMenu->addAction(m_toggleOutputAction);
    viewMenu->addAction(m_togglePreviewAction);
    viewMenu->addAction(m_toggleRightTabsAction);

    viewMenu->addSeparator();

    // Font size actions — proxy to active KTextEditor::View's built-in zoom.
    // No shortcut set here: KTextEditor already owns Ctrl+Plus/Ctrl+Minus.
    auto* incFontAction = viewMenu->addAction(QIcon::fromTheme(QStringLiteral("zoom-in")), i18n("Increase Font Size"));
    connect(incFontAction, &QAction::triggered, this, [this]() {
        if (auto* view = activeView()) {
            if (auto* action = view->actionCollection()->action(QStringLiteral("view_inc_font_sizes"))) {
                action->trigger();
            }
        }
    });

    auto* decFontAction = viewMenu->addAction(QIcon::fromTheme(QStringLiteral("zoom-out")), i18n("Decrease Font Size"));
    connect(decFontAction, &QAction::triggered, this, [this]() {
        if (auto* view = activeView()) {
            if (auto* action = view->actionCollection()->action(QStringLiteral("view_dec_font_sizes"))) {
                action->trigger();
            }
        }
    });

    // ── Help ──
    auto* helpMenu = menuBar()->addMenu(i18n("&Help"));

    auto* aboutAction = helpMenu->addAction(QIcon::fromTheme(QStringLiteral("help-about")), i18n("&About PlasmaZones Shader Editor"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        const auto about = KAboutData::applicationData();
        QMessageBox::about(this, i18n("About PlasmaZones Shader Editor"),
            i18n("<h3>PlasmaZones Shader Editor</h3>"
                 "<p>%1</p>"
                 "<p>A visual editor for creating and testing GLSL shader packages "
                 "for PlasmaZones window tiling effects.</p>", about.version()));
    });
}

void ShaderEditorWindow::setupToolBar()
{
    auto* toolbar = addToolBar(i18n("Main"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(22, 22));

    toolbar->addAction(m_newAction);
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_saveAction);
    toolbar->addSeparator();
    toolbar->addAction(m_compileAction);
    toolbar->addAction(m_validateAction);
    toolbar->addSeparator();

    // Test layout dropdown
    auto* layoutLabel = new QLabel(i18n("Test Layout:"), toolbar);
    layoutLabel->setContentsMargins(4, 0, 4, 0);
    toolbar->addWidget(layoutLabel);

    m_layoutCombo = new QComboBox(toolbar);
    {
        // Block signals while populating to avoid spurious layout changes
        QSignalBlocker blocker(m_layoutCombo);
        const QStringList layoutNames = m_previewController->zoneLayoutNames();
        for (const QString& name : layoutNames) {
            m_layoutCombo->addItem(name);
        }
        m_layoutCombo->setCurrentIndex(m_previewController->zoneLayoutIndex());
    }
    connect(m_layoutCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_previewController->setZoneLayoutIndex(index);
    });
    toolbar->addWidget(m_layoutCombo);

    // Keep combo in sync when layout changes from QML (preview header cycle button)
    connect(m_previewController, &PreviewController::zoneLayoutNameChanged, this, [this]() {
        QSignalBlocker blocker(m_layoutCombo);
        m_layoutCombo->setCurrentIndex(m_previewController->zoneLayoutIndex());
    });
}

void ShaderEditorWindow::setupStatusBar()
{
    m_fileLabel = new QLabel(this);
    m_cursorLabel = new QLabel(this);

    statusBar()->addWidget(m_fileLabel, 1);
    statusBar()->addPermanentWidget(m_cursorLabel);

    m_fileLabel->setText(i18n("No file open"));
    m_cursorLabel->setText(QString());
}

void ShaderEditorWindow::setupLayout()
{
    // ── Preview controller (shared between preview pane and metadata editor) ──
    m_previewController = new PreviewController(this);

    // ── Preview widget (QQuickWidget hosting QML with ZoneShaderItem) ──
    m_previewWidget = new QQuickWidget(this);
    m_previewWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_previewWidget->engine()->rootContext()->setContextObject(
        new KLocalizedContext(m_previewWidget->engine()));
    m_previewWidget->engine()->rootContext()->setContextProperty(
        QStringLiteral("previewController"), m_previewController);
    connect(m_previewWidget, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status status) {
        if (status == QQuickWidget::Error) {
            const auto errors = m_previewWidget->errors();
            for (const auto& error : errors) {
                qCWarning(lcShaderEditor) << "Preview QML error:" << error.toString();
            }
        }
    });
    m_previewWidget->setSource(QUrl(QStringLiteral("qrc:/qml/PreviewPane.qml")));
    m_previewWidget->setMinimumHeight(120);

    // ── Right vertical splitter: preview (top) + metadata editor (bottom) ──
    m_rightSplitter = new QSplitter(Qt::Vertical, this);
    m_rightSplitter->addWidget(m_previewWidget);
    // Right panel tabs added later when a package is opened (setupRightPanel)
    m_rightSplitter->setStretchFactor(0, 3);  // preview gets more space initially

    // ── Output panel (bottom of code editor) ──
    m_outputPanel = new OutputPanel(this);
    m_outputPanel->setMaximumHeight(200);

    // Connect problem double-click to jump to line in code editor
    connect(m_outputPanel, &OutputPanel::problemDoubleClicked, this, [this](int line) {
        auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->currentWidget());
        if (view) {
            view->setCursorPosition(KTextEditor::Cursor(line - 1, 0));
            view->setFocus();
        }
    });

    // Connect preview controller status to output panel.
    // Animation stops on error (no oscillation), so direct connection is safe.
    auto updateOutputPanel = [this]() {
        const int status = m_previewController->status();
        if (status == PreviewController::StatusReady) {
            clearErrorMarks();
            int lineCount = 0;
            for (int i = 0; i < m_tabWidget->count(); ++i) {
                if (m_tabWidget->tabText(i).endsWith(QLatin1String(".frag"))) {
                    auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(i));
                    if (view) {
                        lineCount = view->document()->lines();
                    }
                    break;
                }
            }
            m_outputPanel->setCompilationSuccess(lineCount, {}, {});
        } else if (status == PreviewController::StatusError) {
            m_outputPanel->setCompilationError(m_previewController->errorLog());
            updateErrorMarks();
        }
    };
    connect(m_previewController, &PreviewController::statusChanged, this, updateOutputPanel);
    connect(m_previewController, &PreviewController::errorLogChanged, this, updateOutputPanel);

    // ── Left vertical splitter: code tabs (top) + output panel (bottom) ──
    m_leftSplitter = new QSplitter(Qt::Vertical, this);
    m_leftSplitter->addWidget(m_tabWidget);
    m_leftSplitter->addWidget(m_outputPanel);
    m_leftSplitter->setStretchFactor(0, 4);  // code editor gets most space
    m_leftSplitter->setStretchFactor(1, 1);  // output panel compact

    // ── Main horizontal splitter: left (code+output) + right (preview+tabs) ──
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->addWidget(m_leftSplitter);
    m_mainSplitter->addWidget(m_rightSplitter);
    m_mainSplitter->setStretchFactor(0, 3);  // code editor ~55%
    m_mainSplitter->setStretchFactor(1, 2);  // right panel ~45%

    m_rightSplitter->setMinimumWidth(350);

    setCentralWidget(m_mainSplitter);
}

void ShaderEditorWindow::connectDocumentToPreview(const QString& filename, KTextEditor::Document* doc)
{
    if (filename == QLatin1String("effect.frag")) {
        m_previewController->setFragmentDocument(doc);
    } else if (filename.startsWith(QLatin1String("pass")) && filename.endsWith(QLatin1String(".frag"))) {
        m_previewController->setBufferDocument(filename, doc);
    } else if (filename.endsWith(QLatin1String(".vert"))) {
        m_previewController->setVertexDocument(doc);
    }
}

void ShaderEditorWindow::addDocumentTab(const QString& filename, const QString& content,
                                        const QString& highlightMode)
{
    auto* doc = m_editor->createDocument(this);
    doc->setText(content);
    doc->setModified(false);
    doc->setHighlightingMode(highlightMode);

    // Configure mark types for error highlighting (before creating view)
    doc->setMarkDescription(KTextEditor::Document::Error, i18n("Error"));
    doc->setMarkDescription(KTextEditor::Document::Warning, i18n("Warning"));
    doc->setMarkIcon(KTextEditor::Document::Error, QIcon::fromTheme(QStringLiteral("dialog-error")));
    doc->setMarkIcon(KTextEditor::Document::Warning, QIcon::fromTheme(QStringLiteral("dialog-warning")));

    auto* view = doc->createView(m_tabWidget);

    // Enable icon border (gutter) so error/warning marks are visible
    view->setConfigValue(QStringLiteral("icon-bar"), true);

    // Register GLSL completion model for shader files
    if (filename.endsWith(QLatin1String(".frag")) || filename.endsWith(QLatin1String(".vert"))
        || filename.endsWith(QLatin1String(".glsl"))) {
        if (!m_completionModel) {
            m_completionModel = new GlslCompletionModel(this);
        }
        view->registerCompletionModel(m_completionModel);
        view->setAutomaticInvocationEnabled(true);
    }

    m_tabWidget->addTab(view, filename);
    m_ownedDocuments.append(doc);

    connect(view, &KTextEditor::View::cursorPositionChanged, this, [this](KTextEditor::View*, KTextEditor::Cursor cursor) {
        m_cursorLabel->setText(i18n("Line %1, Col %2", cursor.line() + 1, cursor.column() + 1));
    });
    connect(doc, &KTextEditor::Document::modifiedChanged, this, [this](KTextEditor::Document*) {
        updateWindowTitle();
    });

    connectDocumentToPreview(filename, doc);
}

void ShaderEditorWindow::setupRightPanel(const QString& metadataJson)
{
    // Remove old tab widget if any (this deletes m_parameterPanel and m_metadataEditor as children)
    if (m_rightTabWidget) {
        delete m_rightTabWidget;
        m_rightTabWidget = nullptr;
        m_parameterPanel = nullptr;
        m_metadataEditor = nullptr;
        m_presetPanel = nullptr;
    }

    m_rightTabWidget = new QTabWidget(m_rightSplitter);
    m_rightTabWidget->setMinimumHeight(200);
    m_rightSplitter->addWidget(m_rightTabWidget);
    // 60% preview / 40% tabs — preview is primary, tabs are secondary
    m_rightSplitter->setStretchFactor(0, 3);
    m_rightSplitter->setStretchFactor(1, 2);
    m_rightSplitter->setCollapsible(0, false);
    m_rightSplitter->setCollapsible(1, false);

    // Force explicit initial distribution. QQuickWidget's sizeHint
    // and QTabWidget's sizeHint compete; setSizes overrides both.
    const int totalHeight = m_rightSplitter->height();
    if (totalHeight > 0) {
        const int previewHeight = totalHeight * 60 / 100;
        m_rightSplitter->setSizes({previewHeight, totalHeight - previewHeight});
    } else {
        m_rightSplitter->setSizes({480, 320});
    }

    // Parameters tab
    m_parameterPanel = new ParameterPanel(m_rightTabWidget);
    m_parameterPanel->loadFromMetadata(metadataJson);
    m_rightTabWidget->addTab(m_parameterPanel, i18n("Parameters"));

    // Metadata tab
    m_metadataEditor = new MetadataEditorWidget(m_rightTabWidget);
    m_metadataEditor->loadFromJson(metadataJson);
    m_rightTabWidget->addTab(m_metadataEditor, i18n("Metadata"));

    // Presets tab
    m_presetPanel = new PresetPanel(m_rightTabWidget);
    m_presetPanel->loadFromMetadata(metadataJson);
    m_rightTabWidget->addTab(m_presetPanel, i18n("Presets"));

    // Connect parameter changes to live preview
    connect(m_parameterPanel, &ParameterPanel::parameterChanged, this, [this]() {
        m_previewController->setShaderParams(m_parameterPanel->currentUniformValues());
    });

    // Connect insert uniform from both panels
    connect(m_parameterPanel, &ParameterPanel::insertUniformRequested,
            this, &ShaderEditorWindow::insertTextAtCursor);
    connect(m_metadataEditor, &MetadataEditorWidget::insertUniformRequested,
            this, &ShaderEditorWindow::insertTextAtCursor);

    // Copy as Defaults -> write current param values back into metadata defaults
    connect(m_parameterPanel, &ParameterPanel::copyDefaultsRequested, this, [this]() {
        if (!m_metadataEditor) return;
        const QVariantMap values = m_parameterPanel->currentUniformValues();
        m_metadataEditor->updateParameterDefaults(values);
    });

    // Connect metadata changes -> update preview params, parameter panel, and multipass config
    connect(m_metadataEditor, &MetadataEditorWidget::modified, this, [this]() {
        updateWindowTitle();
        if (m_metadataEditor && m_parameterPanel) {
            const QString json = m_metadataEditor->toJson();
            m_previewController->loadDefaultParamsFromMetadata(json);
            m_previewController->updateMultipassConfig(json);
            m_parameterPanel->loadFromMetadata(json);
        }
    });

    // Reset All -> reload defaults from metadata
    connect(m_parameterPanel, &ParameterPanel::resetRequested, this, [this]() {
        if (m_metadataEditor) {
            m_parameterPanel->loadFromMetadata(m_metadataEditor->toJson());
            m_previewController->setShaderParams(m_parameterPanel->currentUniformValues());
        }
    });

    // Apply button -> update preview with current param values
    connect(m_parameterPanel, &ParameterPanel::applyRequested, this, [this]() {
        m_previewController->setShaderParams(m_parameterPanel->currentUniformValues());
    });

    // Preset panel: load selected preset into parameters
    connect(m_presetPanel, &PresetPanel::presetSelected, this, [this](const QVariantMap& values) {
        if (m_parameterPanel) {
            m_parameterPanel->applyUniformValues(values);
        }
        m_previewController->setShaderParams(m_parameterPanel->currentUniformValues());
    });

    // Preset panel: save current — request values from parameter panel
    connect(m_presetPanel, &PresetPanel::captureRequested, this, [this]() {
        if (m_parameterPanel) {
            m_presetPanel->saveCurrentValues(m_parameterPanel->currentUniformValues());
        }
    });

    // Preset panel: modified — mark metadata as dirty so hasUnsavedChanges() triggers
    connect(m_presetPanel, &PresetPanel::modified, this, [this]() {
        if (m_metadataEditor) {
            m_metadataEditor->setModified(true);
        }
        updateWindowTitle();
    });
}

void ShaderEditorWindow::insertTextAtCursor(const QString& text)
{
    const int idx = m_tabWidget->currentIndex();
    auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(idx));
    if (!view) {
        // Try the first shader tab
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(i));
            if (view) {
                m_tabWidget->setCurrentIndex(i);
                break;
            }
        }
    }
    if (view) {
        view->document()->insertText(view->cursorPosition(), text);
    }
}

void ShaderEditorWindow::newShaderPackage()
{
    if (!promptSaveIfModified()) {
        return;
    }

    NewPackageDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString shaderName = dialog.shaderName();
    if (shaderName.isEmpty()) {
        return;
    }
    const QString shaderId = dialog.shaderId();

    closeAllTabs();

    ShaderPackageContents contents = ShaderPackageIO::createTemplate(
        shaderId, shaderName, dialog.selectedFeatures());

    // Apply metadata overrides from the dialog (category, description, author)
    {
        QJsonDocument doc = QJsonDocument::fromJson(contents.metadataJson.toUtf8());
        QJsonObject obj = doc.object();
        const QString category = dialog.category();
        const QString description = dialog.description();
        const QString author = dialog.author();
        if (!category.isEmpty()) {
            obj[QStringLiteral("category")] = category;
        }
        if (!description.isEmpty()) {
            obj[QStringLiteral("description")] = description;
        }
        if (!author.isEmpty()) {
            obj[QStringLiteral("author")] = author;
        }
        contents.metadataJson = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }

    // Metadata editor in the right panel (below preview)
    setupRightPanel(contents.metadataJson);

    // Shader file tabs in the left panel
    for (const ShaderFile& sf : contents.files) {
        addDocumentTab(sf.filename, sf.content, QStringLiteral("GLSL"));
    }

    m_packagePath.clear();
    m_isNewPackage = true;
    if (m_tabWidget->count() > 0) {
        m_tabWidget->setCurrentIndex(0);
    }
    updateWindowTitle();

    m_previewController->setShaderDirectory(QString());
    m_previewController->loadDefaultParamsFromMetadata(contents.metadataJson);
    m_previewController->updateMultipassConfig(contents.metadataJson);

    // Auto-enable test audio when the template includes audio reactivity,
    // so the user immediately sees the visualizer working instead of static idle bars.
    if (dialog.selectedFeatures().testFlag(ShaderFeature::AudioReactive)) {
        m_previewController->setAudioEnabled(true);
    }

    qCInfo(lcShaderEditor) << "Created new shader package name=" << shaderName << "id=" << shaderId;
}

void ShaderEditorWindow::openShaderPackage(const QString& path)
{
    if (!promptSaveIfModified()) {
        return;
    }

    const QString absPath = QFileInfo(path).absoluteFilePath();

    const ShaderPackageContents contents = ShaderPackageIO::loadPackage(absPath);
    if (contents.metadataJson.isEmpty() && contents.files.isEmpty()) {
        QMessageBox::warning(this, i18n("Error"),
                             i18n("Failed to load shader package from:\n%1", absPath));
        return;
    }

    closeAllTabs();

    // Metadata editor in the right panel (below preview)
    if (!contents.metadataJson.isEmpty()) {
        setupRightPanel(contents.metadataJson);
    }

    // Shader file tabs in the left panel
    for (const ShaderFile& sf : contents.files) {
        addDocumentTab(sf.filename, sf.content, QStringLiteral("GLSL"));
    }

    m_packagePath = absPath;
    m_isNewPackage = false;
    if (m_tabWidget->count() > 0) {
        m_tabWidget->setCurrentIndex(0);
    }
    updateWindowTitle();

    m_previewController->setShaderDirectory(absPath);

    if (!contents.metadataJson.isEmpty()) {
        m_previewController->loadDefaultParamsFromMetadata(contents.metadataJson);
        m_previewController->updateMultipassConfig(contents.metadataJson);
    }

    // Track in recent files
    if (m_recentAction) {
        m_recentAction->addUrl(QUrl::fromLocalFile(absPath), QDir(absPath).dirName());
    }

    qCInfo(lcShaderEditor) << "Opened shader package from=" << absPath;
}

void ShaderEditorWindow::openShaderById(const QString& shaderId)
{
    const QString path = resolveShaderPath(shaderId);
    if (path.isEmpty()) {
        QMessageBox::warning(this, i18n("Error"),
                             i18n("Could not find shader with ID: %1", shaderId));
        return;
    }
    openShaderPackage(path);
}

void ShaderEditorWindow::saveShaderPackage()
{
    if (m_isNewPackage || m_packagePath.isEmpty()) {
        saveShaderPackageAs();
        return;
    }

    ShaderPackageContents contents;
    contents.dirPath = m_packagePath;

    contents.metadataJson = buildMetadataJsonForSave();

    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(i));
        if (!view) continue;
        auto* doc = view->document();
        if (!doc) continue;

        ShaderFile sf;
        sf.filename = m_tabWidget->tabText(i);
        sf.content = doc->text();
        contents.files.append(sf);
    }

    if (ShaderPackageIO::savePackage(m_packagePath, contents)) {
        for (auto* doc : m_ownedDocuments) {
            doc->setModified(false);
        }
        if (m_metadataEditor) {
            m_metadataEditor->setModified(false);
        }
        statusBar()->showMessage(i18n("Shader package saved to %1", m_packagePath), 3000);
        qCInfo(lcShaderEditor) << "Saved shader package to=" << m_packagePath;
    } else {
        QMessageBox::warning(this, i18n("Error"),
                             i18n("Failed to save shader package to:\n%1", m_packagePath));
    }
}

void ShaderEditorWindow::saveShaderPackageAs()
{
    QString startDir = m_packagePath;
    if (startDir.isEmpty()) {
        startDir = ShaderPackageIO::userShaderDirectory();
        if (m_metadataEditor) {
            QJsonDocument jsonDoc = QJsonDocument::fromJson(m_metadataEditor->toJson().toUtf8());
            if (jsonDoc.isObject()) {
                const QString id = jsonDoc.object().value(QStringLiteral("id")).toString();
                if (!id.isEmpty()) {
                    startDir += QStringLiteral("/") + id;
                }
            }
        }
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this, i18n("Save Shader Package To Directory"), startDir);
    if (dir.isEmpty()) {
        return;
    }

    // Build contents for the target directory without modifying m_packagePath yet
    ShaderPackageContents contents;
    contents.dirPath = dir;

    contents.metadataJson = buildMetadataJsonForSave();

    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(i));
        if (!view || !view->document()) continue;
        ShaderFile sf;
        sf.filename = m_tabWidget->tabText(i);
        sf.content = view->document()->text();
        contents.files.append(sf);
    }

    if (ShaderPackageIO::savePackage(dir, contents)) {
        m_packagePath = dir;
        m_isNewPackage = false;
        for (auto* doc : m_ownedDocuments) {
            doc->setModified(false);
        }
        if (m_metadataEditor) {
            m_metadataEditor->setModified(false);
        }
        m_previewController->setShaderDirectory(dir);
        statusBar()->showMessage(i18n("Shader package saved to %1", dir), 3000);
        qCInfo(lcShaderEditor) << "Saved shader package to=" << dir;
    } else {
        QMessageBox::warning(this, i18n("Error"),
                             i18n("Failed to save shader package to:\n%1", dir));
    }

    updateWindowTitle();
}

void ShaderEditorWindow::updateWindowTitle()
{
    QString title;
    if (m_packagePath.isEmpty() && !m_isNewPackage) {
        title = i18n("PlasmaZones Shader Editor");
    } else if (m_isNewPackage) {
        title = i18n("New Shader Package — PlasmaZones Shader Editor");
    } else {
        const QDir dir(m_packagePath);
        title = i18n("%1 — PlasmaZones Shader Editor", dir.dirName());
    }

    if (hasUnsavedChanges()) {
        title = QStringLiteral("* ") + title;
    }

    setWindowTitle(title);
}

void ShaderEditorWindow::updateStatusBar()
{
    const int idx = m_tabWidget->currentIndex();
    if (idx < 0) {
        m_fileLabel->setText(i18n("No file open"));
        m_cursorLabel->setText(QString());
        return;
    }
    m_fileLabel->setText(m_tabWidget->tabText(idx));
    auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(idx));
    if (view) {
        auto cursor = view->cursorPosition();
        m_cursorLabel->setText(i18n("Line %1, Col %2", cursor.line() + 1, cursor.column() + 1));
    } else {
        m_cursorLabel->setText(QString());
    }
}

bool ShaderEditorWindow::promptSaveIfModified()
{
    if (!hasUnsavedChanges()) {
        return true;
    }

    const int result = QMessageBox::question(
        this,
        i18n("Save Changes"),
        i18n("The current shader package has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (result == QMessageBox::Cancel) {
        return false;
    }
    if (result == QMessageBox::Save) {
        saveShaderPackage();
        if (hasUnsavedChanges()) return false;
    }
    return true;
}

bool ShaderEditorWindow::hasUnsavedChanges() const
{
    if (m_metadataEditor && m_metadataEditor->isModified()) {
        return true;
    }
    for (auto* doc : m_ownedDocuments) {
        if (doc->isModified()) return true;
    }
    return false;
}

void ShaderEditorWindow::closeAllTabs()
{
    // Clear code editor tabs
    m_tabWidget->clear();
    qDeleteAll(m_ownedDocuments);
    m_ownedDocuments.clear();

    // Remove right panel tab widget (deletes m_parameterPanel and m_metadataEditor as children)
    if (m_rightTabWidget) {
        delete m_rightTabWidget;
        m_rightTabWidget = nullptr;
        m_parameterPanel = nullptr;
        m_metadataEditor = nullptr;
        m_presetPanel = nullptr;
    }
}

void ShaderEditorWindow::closeEvent(QCloseEvent* event)
{
    if (promptSaveIfModified()) {
        // Persist recent files
        if (m_recentAction) {
            const KSharedConfig::Ptr config = KSharedConfig::openConfig();
            m_recentAction->saveEntries(config->group(QStringLiteral("RecentPackages")));
            config->sync();
        }
        event->accept();
    } else {
        event->ignore();
    }
}

QString ShaderEditorWindow::resolveShaderPath(const QString& shaderId) const
{
    if (shaderId.contains(QLatin1Char('/')) || shaderId.contains(QLatin1Char('\\'))) {
        qCWarning(lcShaderEditor) << "Invalid shader ID (contains path separators):" << shaderId;
        return {};
    }

    const QString userDir = ShaderPackageIO::userShaderDirectory() + QStringLiteral("/") + shaderId;
    if (QDir(userDir).exists() && QFile::exists(userDir + QStringLiteral("/metadata.json"))) {
        return userDir;
    }

    const QString sysDir = ShaderPackageIO::systemShaderDirectory();
    if (!sysDir.isEmpty()) {
        const QString systemPath = sysDir + QStringLiteral("/") + shaderId;
        if (QDir(systemPath).exists() && QFile::exists(systemPath + QStringLiteral("/metadata.json"))) {
            return systemPath;
        }
    }

    qCWarning(lcShaderEditor) << "Shader not found id=" << shaderId;
    return {};
}

QString ShaderEditorWindow::buildMetadataJsonForSave() const
{
    if (!m_metadataEditor) return {};
    QJsonDocument doc = QJsonDocument::fromJson(m_metadataEditor->toJson().toUtf8());
    QJsonObject obj = doc.object();
    if (m_presetPanel) {
        const QJsonObject presets = m_presetPanel->presetsJson();
        if (!presets.isEmpty()) {
            obj[QStringLiteral("presets")] = presets;
        } else {
            obj.remove(QStringLiteral("presets"));
        }
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

void ShaderEditorWindow::clearErrorMarks()
{
    constexpr uint errorWarningMask = KTextEditor::Document::Error | KTextEditor::Document::Warning;
    for (auto* doc : m_ownedDocuments) {
        const auto marks = doc->marks();
        for (auto it = marks.begin(); it != marks.end(); ++it) {
            if (it.value()->type & errorWarningMask) {
                doc->removeMark(it.key(), errorWarningMask);
            }
        }
    }
}

void ShaderEditorWindow::updateErrorMarks()
{
    clearErrorMarks();

    const int status = m_previewController->status();
    if (status != PreviewController::StatusError) {
        return;
    }

    if (m_tabWidget->count() == 0) {
        return;
    }

    // Parse the error log and apply marks.
    // Because ShaderIncludeResolver emits #line directives, the GPU compiler
    // reports line numbers in the ORIGINAL source file's coordinate space,
    // not the expanded file. So we use the error line number directly.
    const QString errorLog = m_previewController->errorLog();
    static const QRegularExpression errorPattern(QStringLiteral("(?:\\d*:(\\d+):|\\bline\\s+(\\d+))"));
    const QStringList errorLines = errorLog.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    // Determine which file the errors belong to by checking the error prefix.
    // "Fragment shader:" → effect.frag, "Vertex shader:" → zone.vert
    // Default to effect.frag if not specified.
    for (const QString& errLine : errorLines) {
        const auto match = errorPattern.match(errLine);
        if (!match.hasMatch()) {
            continue;
        }

        int errorLine = match.captured(1).toInt();
        if (errorLine == 0) {
            errorLine = match.captured(2).toInt();
        }
        if (errorLine <= 0) {
            continue;
        }

        // Determine target file from error prefix.
        // QShaderBaker prefixes: "Fragment shader:", "Vertex shader:"
        // PreviewController prefixes buffer pass errors: "Buffer pass pass0.frag ..."
        QString targetFile = QStringLiteral("effect.frag");
        if (errLine.contains(QLatin1String("Vertex shader"), Qt::CaseInsensitive)) {
            targetFile = QStringLiteral("zone.vert");
        } else {
            // Check for buffer pass filename in the error message
            static const QRegularExpression bufferPassPattern(QStringLiteral("\\b(pass\\d+\\.frag)\\b"));
            const auto passMatch = bufferPassPattern.match(errLine);
            if (passMatch.hasMatch()) {
                targetFile = passMatch.captured(1);
            }
        }

        // Find the KTextEditor::Document for this file
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            if (m_tabWidget->tabText(i) == targetFile) {
                auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(i));
                if (view && view->document() && errorLine > 0 && errorLine <= view->document()->lines()) {
                    const bool isError = errLine.contains(QLatin1String("ERROR"), Qt::CaseInsensitive);
                    const auto markType = isError
                        ? KTextEditor::Document::Error
                        : KTextEditor::Document::Warning;
                    view->document()->addMark(errorLine - 1, markType);
                    qCDebug(lcShaderEditor) << "Error mark on" << targetFile << "line" << errorLine;
                }
                break;
            }
        }
    }
}

} // namespace PlasmaZones
