// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shadereditorwindow.h"
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
#include <QRegularExpression>
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
            delete doc; // deleting doc also deletes its views
        }

        updateWindowTitle();
    });

    setupPreview();
    setupMenuBar();
    setupStatusBar();

    resize(1400, 800);
    updateWindowTitle();
}

ShaderEditorWindow::~ShaderEditorWindow() = default;

bool ShaderEditorWindow::isValid() const
{
    return m_editor != nullptr;
}

void ShaderEditorWindow::setupMenuBar()
{
    // File menu
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

    // Edit menu — KTextEditor views provide their own edit actions (undo, redo,
    // cut, copy, paste, find, replace) via the right-click context menu and
    // standard keyboard shortcuts. No need to duplicate them here.
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

void ShaderEditorWindow::setupPreview()
{
    m_previewController = new PreviewController(this);

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
        } else if (status == QQuickWidget::Ready) {
            qCInfo(lcShaderEditor) << "Preview QML loaded successfully";
        }
    });

    m_previewWidget->setSource(QUrl(QStringLiteral("qrc:/qml/PreviewPane.qml")));

    m_previewWidget->setMinimumWidth(300);

    // Use splitter for editor tabs + live preview
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_tabWidget);
    m_splitter->addWidget(m_previewWidget);
    m_splitter->setStretchFactor(0, 3); // editor gets ~60%
    m_splitter->setStretchFactor(1, 2); // preview gets ~40%

    setCentralWidget(m_splitter);
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

    // Connect document to live preview if it's a shader file
    connectDocumentToPreview(filename, doc);
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

    // Generate a filesystem-safe ID from the name
    QString shaderId = shaderName.toLower();
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression leadTrailDash(QStringLiteral("^-|-$"));
    shaderId.replace(nonAlnum, QStringLiteral("-"));
    shaderId.remove(leadTrailDash);
    if (shaderId.isEmpty()) {
        shaderId = QStringLiteral("custom-shader");
    }

    closeAllTabs();

    const ShaderPackageContents contents = ShaderPackageIO::createTemplate(shaderId, shaderName);

    // Add metadata.json tab
    addDocumentTab(QStringLiteral("metadata.json"), contents.metadataJson, QStringLiteral("JSON"));

    // Add shader file tabs
    for (const ShaderFile& sf : contents.files) {
        addDocumentTab(sf.filename, sf.content, QStringLiteral("GLSL"));
    }

    m_packagePath.clear();
    m_isNewPackage = true;
    m_tabWidget->setCurrentIndex(0);
    updateWindowTitle();

    // New package has no directory yet; includes resolve from system dir only
    m_previewController->setShaderDirectory(QString());

    qCInfo(lcShaderEditor) << "Created new shader package name=" << shaderName << "id=" << shaderId;
}

void ShaderEditorWindow::openShaderPackage(const QString& path)
{
    if (!promptSaveIfModified()) {
        return;
    }

    // Resolve to absolute path (handles relative CLI paths like "data/shaders/cosmic-flow")
    const QString absPath = QFileInfo(path).absoluteFilePath();

    const ShaderPackageContents contents = ShaderPackageIO::loadPackage(absPath);
    if (contents.metadataJson.isEmpty() && contents.files.isEmpty()) {
        QMessageBox::warning(this, i18n("Error"),
                             i18n("Failed to load shader package from:\n%1", absPath));
        return;
    }

    closeAllTabs();

    // Add metadata.json tab
    if (!contents.metadataJson.isEmpty()) {
        addDocumentTab(QStringLiteral("metadata.json"), contents.metadataJson, QStringLiteral("JSON"));
    }

    // Add shader file tabs
    for (const ShaderFile& sf : contents.files) {
        addDocumentTab(sf.filename, sf.content, QStringLiteral("GLSL"));
    }

    m_packagePath = absPath;
    m_isNewPackage = false;
    m_tabWidget->setCurrentIndex(0);
    updateWindowTitle();

    // Set shader directory for #include resolution in live preview
    m_previewController->setShaderDirectory(absPath);

    // Load default shader parameters from metadata.json for preview
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

    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(i));
        if (!view) continue;
        auto* doc = view->document();
        if (!doc) continue;

        const QString tabName = m_tabWidget->tabText(i);
        if (tabName == QLatin1String("metadata.json")) {
            contents.metadataJson = doc->text();
        } else {
            ShaderFile sf;
            sf.filename = tabName;
            sf.content = doc->text();
            contents.files.append(sf);
        }
    }

    if (ShaderPackageIO::savePackage(m_packagePath, contents)) {
        for (auto* doc : m_ownedDocuments) {
            doc->setModified(false);
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
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            if (m_tabWidget->tabText(i) == QLatin1String("metadata.json")) {
                auto* view = qobject_cast<KTextEditor::View*>(m_tabWidget->widget(i));
                if (view) {
                    QJsonDocument jsonDoc = QJsonDocument::fromJson(view->document()->text().toUtf8());
                    if (jsonDoc.isObject()) {
                        const QString id = jsonDoc.object().value(QStringLiteral("id")).toString();
                        if (!id.isEmpty()) startDir += QStringLiteral("/") + id;
                    }
                }
                break;
            }
        }
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this,
        i18n("Save Shader Package To Directory"),
        startDir);

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
    for (auto* doc : m_ownedDocuments) {
        if (doc->isModified()) return true;
    }
    return false;
}

void ShaderEditorWindow::closeAllTabs()
{
    m_tabWidget->clear();
    qDeleteAll(m_ownedDocuments);
    m_ownedDocuments.clear();
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

    // Check user shader directory first
    const QString userDir = ShaderPackageIO::userShaderDirectory() + QStringLiteral("/") + shaderId;
    if (QDir(userDir).exists() && QFile::exists(userDir + QStringLiteral("/metadata.json"))) {
        return userDir;
    }

    // Check system shader directory
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
