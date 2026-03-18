// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shadereditorwindow.h"
#include "metadataeditorwidget.h"
#include "outputpanel.h"
#include "parameterpanel.h"
#include "previewcontroller.h"
#include "shaderpackageio.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMenuBar>
#include <QMessageBox>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>

#include <KLocalizedString>
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
    setupMenuBar();
    setupStatusBar();

    resize(1400, 900);
    updateWindowTitle();
}

ShaderEditorWindow::~ShaderEditorWindow() = default;

bool ShaderEditorWindow::isValid() const
{
    return m_editor != nullptr;
}

void ShaderEditorWindow::setupMenuBar()
{
    auto* fileMenu = menuBar()->addMenu(i18n("&File"));

    auto* newAction = fileMenu->addAction(i18n("&New Shader Package"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &ShaderEditorWindow::newShaderPackage);

    auto* openAction = fileMenu->addAction(i18n("&Open Shader Package..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this,
            i18n("Open Shader Package Directory"),
            ShaderPackageIO::userShaderDirectory());
        if (!dir.isEmpty()) {
            openShaderPackage(dir);
        }
    });

    fileMenu->addSeparator();

    auto* saveAction = fileMenu->addAction(i18n("&Save"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &ShaderEditorWindow::saveShaderPackage);

    auto* saveAsAction = fileMenu->addAction(i18n("Save &As..."));
    saveAsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(saveAsAction, &QAction::triggered, this, &ShaderEditorWindow::saveShaderPackageAs);

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction(i18n("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);
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
    if (filename.endsWith(QLatin1String(".frag"))) {
        m_previewController->setFragmentDocument(doc);
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

    auto* view = doc->createView(m_tabWidget);
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

    // Presets tab (stub)
    auto* presetsWidget = new QWidget(m_rightTabWidget);
    auto* presetsLayout = new QVBoxLayout(presetsWidget);
    presetsLayout->addWidget(new QLabel(i18n("Presets coming soon..."), presetsWidget));
    presetsLayout->addStretch();
    m_rightTabWidget->addTab(presetsWidget, i18n("Presets"));

    // Connect parameter changes to live preview
    connect(m_parameterPanel, &ParameterPanel::parameterChanged, this, [this]() {
        m_previewController->setShaderParams(m_parameterPanel->currentUniformValues());
    });

    // Connect insert uniform from both panels
    connect(m_parameterPanel, &ParameterPanel::insertUniformRequested,
            this, &ShaderEditorWindow::insertTextAtCursor);
    connect(m_metadataEditor, &MetadataEditorWidget::insertUniformRequested,
            this, &ShaderEditorWindow::insertTextAtCursor);

    // Connect metadata changes -> update preview params + parameter panel
    connect(m_metadataEditor, &MetadataEditorWidget::modified, this, [this]() {
        updateWindowTitle();
        if (m_metadataEditor && m_parameterPanel) {
            const QString json = m_metadataEditor->toJson();
            m_previewController->loadDefaultParamsFromMetadata(json);
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

    bool ok = false;
    const QString shaderName = QInputDialog::getText(
        this,
        i18n("New Shader Package"),
        i18n("Shader name:"),
        QLineEdit::Normal,
        i18n("My Custom Shader"),
        &ok);

    if (!ok || shaderName.isEmpty()) {
        return;
    }

    const QString shaderId = ShaderPackageIO::sanitizeId(shaderName);

    closeAllTabs();

    const ShaderPackageContents contents = ShaderPackageIO::createTemplate(shaderId, shaderName);

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

    if (m_metadataEditor) {
        contents.metadataJson = m_metadataEditor->toJson();
    }

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

    const QString previousPath = m_packagePath;
    const bool wasNew = m_isNewPackage;

    m_packagePath = dir;
    m_isNewPackage = false;
    saveShaderPackage();

    if (hasUnsavedChanges()) {
        m_packagePath = previousPath;
        m_isNewPackage = wasNew;
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
    }
}

void ShaderEditorWindow::closeEvent(QCloseEvent* event)
{
    if (promptSaveIfModified()) {
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

} // namespace PlasmaZones
