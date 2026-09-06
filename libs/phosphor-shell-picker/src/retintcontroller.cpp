// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellPicker/RetintController.h>

#include <PhosphorTheme/IThemeService.h>

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(lcRetint, "phosphorshellpicker.retint")

namespace PhosphorShellPicker {

namespace {

constexpr int kDefaultDebounceMs = 150;

bool isBrandStop(const QString& key)
{
    using PhosphorTheme::TokenNames;
    return key == QLatin1String(TokenNames::BrandStop0) || key == QLatin1String(TokenNames::BrandStop1)
        || key == QLatin1String(TokenNames::BrandStop2) || key == QLatin1String(TokenNames::BrandStop3);
}

} // namespace

RetintController::RetintController(QObject* parent)
    : QObject(parent)
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDefaultDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &RetintController::startRun);
}

RetintController::~RetintController() = default;

QObject* RetintController::runner() const
{
    return m_runner;
}

void RetintController::setRunner(QObject* runner)
{
    if (m_runner == runner) {
        return;
    }
    if (m_runner) {
        disconnect(m_runner, nullptr, this, nullptr);
    }
    m_runner = runner;
    // The in-flight run belonged to the OLD runner. Nothing will answer for it
    // now, so drop the state that waits on it rather than leave preview()
    // refusing against a run that cannot land.
    m_pendingPath.clear();
    setBusy(false);
    if (m_runner) {
        // By signature rather than by type so a test's fake runner
        // (any QObject with MatugenRunner's two signals) connects too.
        connect(m_runner, SIGNAL(paletteReady(QVariantMap, QString)), this, SLOT(onPaletteReady(QVariantMap, QString)));
        connect(m_runner, SIGNAL(failed(QString, QString)), this, SLOT(onRunnerFailed(QString, QString)));
    }
    Q_EMIT runnerChanged();
}

QObject* RetintController::store() const
{
    return m_store;
}

void RetintController::setStore(QObject* store)
{
    if (m_store == store) {
        return;
    }
    // The snapshot was taken FROM the old store, so it must not survive the
    // swap: the next clearPreview() would otherwise merge the previous store's
    // tokens into the new one. Dropped rather than restored, because the old
    // store is no longer ours to write to.
    if (m_hasSnapshot) {
        m_snapshot.clear();
        m_hasSnapshot = false;
        Q_EMIT previewingChanged();
    }
    m_store = store;
    Q_EMIT storeChanged();
}

int RetintController::debounceMs() const
{
    return m_debounce->interval();
}

void RetintController::setDebounceMs(int ms)
{
    const int clamped = qMax(0, ms);
    if (m_debounce->interval() == clamped) {
        return;
    }
    m_debounce->setInterval(clamped);
    Q_EMIT debounceMsChanged();
}

QString RetintController::persistPath() const
{
    return m_persistPath;
}

void RetintController::setPersistPath(const QString& path)
{
    if (m_persistPath == path) {
        return;
    }
    m_persistPath = path;
    Q_EMIT persistPathChanged();
}

QString RetintController::previewPath() const
{
    return m_previewPath;
}

bool RetintController::isPreviewing() const
{
    return m_hasSnapshot;
}

bool RetintController::isBusy() const
{
    return m_busy;
}

QString RetintController::lastError() const
{
    return m_lastError;
}

QString RetintController::defaultPersistPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("palettes/current.json"));
}

QVariantMap RetintController::withoutBrandStops(const QVariantMap& tokens)
{
    QVariantMap result;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        if (!isBrandStop(it.key())) {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}

bool RetintController::writePalette(const QString& path, const QVariantMap& tokens)
{
    if (path.isEmpty()) {
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qCWarning(lcRetint) << "cannot create the palette directory for" << path;
        return false;
    }
    QJsonObject tokenObject;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        const QColor color = it.value().value<QColor>();
        if (!color.isValid()) {
            continue;
        }
        tokenObject.insert(it.key(), color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb));
    }
    const QJsonObject root{{QLatin1String("tokens"), tokenObject}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcRetint) << "cannot open" << path << "for writing:" << file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(lcRetint) << "cannot commit" << path << ":" << file.errorString();
        return false;
    }
    return true;
}

void RetintController::preview(const QString& wallpaperPath)
{
    if (wallpaperPath.isEmpty()) {
        clearPreview();
        return;
    }
    if (wallpaperPath == m_previewPath && !m_busy && !m_debounce->isActive()) {
        // Already showing this candidate.
        return;
    }
    if (wallpaperPath == m_pendingPath && (m_busy || m_debounce->isActive())) {
        // Already on its way.
        return;
    }
    ensureSnapshot();
    m_pendingPath = wallpaperPath;
    setLastError({});
    // A sweep across the strip restarts the window; only the candidate
    // the pointer rests on reaches the runner.
    m_debounce->start();
}

void RetintController::previewTokens(const QVariantMap& tokens, const QString& label)
{
    m_debounce->stop();
    if (m_busy && m_runner) {
        QMetaObject::invokeMethod(m_runner, "cancel");
        setBusy(false);
    }
    m_pendingPath.clear();
    ensureSnapshot();
    setLastError({});
    applyToStore(tokens);
    setPreviewPath(label);
    Q_EMIT previewApplied(label);
}

void RetintController::clearPreview()
{
    m_debounce->stop();
    m_pendingPath.clear();
    // Cancel only if there is still a runner to cancel, but clear busy
    // UNCONDITIONALLY. m_runner is a QPointer: a runner destroyed mid-run, or
    // a host that swapped it out, would otherwise leave busy latched true and
    // preview() would refuse through both of its guards for the life of the
    // controller — the strip stuck reading "retinting".
    if (m_busy && m_runner) {
        QMetaObject::invokeMethod(m_runner, "cancel");
    }
    setBusy(false);
    if (m_hasSnapshot) {
        // The snapshot carries every key the store had, so the merge
        // puts each one back and nothing a preview changed survives.
        if (m_store) {
            QMetaObject::invokeMethod(m_store, "applyTokens", Q_ARG(QVariantMap, m_snapshot));
        }
        m_snapshot.clear();
        m_hasSnapshot = false;
        Q_EMIT previewingChanged();
    }
    setPreviewPath({});
}

void RetintController::commit()
{
    m_debounce->stop();
    // A run still in flight lands on its own and stays: with no snapshot
    // there is nothing to restore, which is the committed state.
    if (m_hasSnapshot) {
        m_snapshot.clear();
        m_hasSnapshot = false;
        Q_EMIT previewingChanged();
    }
    if (!m_persistPath.isEmpty() && m_store) {
        if (!writePalette(m_persistPath, storePalette())) {
            qCWarning(lcRetint) << "the committed palette was not saved to" << m_persistPath;
        }
    }
}

void RetintController::onPaletteReady(const QVariantMap& tokens, const QString& wallpaperPath)
{
    if (wallpaperPath != m_pendingPath) {
        // A run the controller no longer waits for (cancelled, or the
        // pointer moved on): never let it land.
        return;
    }
    setBusy(false);
    m_pendingPath.clear();
    if (!m_hasSnapshot) {
        // commit() ran while this was in flight: the palette lands and is
        // kept, and persists like a commit of a landed preview.
        applyToStore(tokens);
        setPreviewPath(wallpaperPath);
        if (!m_persistPath.isEmpty() && m_store && !writePalette(m_persistPath, storePalette())) {
            qCWarning(lcRetint) << "the committed palette was not saved to" << m_persistPath;
        }
        Q_EMIT previewApplied(wallpaperPath);
        return;
    }
    applyToStore(tokens);
    setPreviewPath(wallpaperPath);
    Q_EMIT previewApplied(wallpaperPath);
}

void RetintController::onRunnerFailed(const QString& wallpaperPath, const QString& reason)
{
    if (wallpaperPath != m_pendingPath) {
        return;
    }
    setBusy(false);
    m_pendingPath.clear();
    setLastError(reason);
    qCWarning(lcRetint) << "no palette for" << wallpaperPath << ":" << reason;
    Q_EMIT previewFailed(wallpaperPath, reason);
}

void RetintController::startRun()
{
    if (m_pendingPath.isEmpty()) {
        return;
    }
    if (!m_runner) {
        setLastError(QStringLiteral("no palette runner"));
        Q_EMIT previewFailed(m_pendingPath, m_lastError);
        m_pendingPath.clear();
        return;
    }
    // Cancel first, then run: a runner that is still working on the
    // previous candidate must not report it after this one.
    QMetaObject::invokeMethod(m_runner, "cancel");
    setBusy(true);
    const QString path = m_pendingPath;
    QMetaObject::invokeMethod(m_runner, "run", Q_ARG(QString, path));
}

void RetintController::ensureSnapshot()
{
    if (m_hasSnapshot) {
        return;
    }
    m_snapshot = storePalette();
    m_hasSnapshot = true;
    Q_EMIT previewingChanged();
}

void RetintController::applyToStore(const QVariantMap& tokens)
{
    if (!m_store) {
        return;
    }
    // Only the tokens the palette already publishes, minus the brand
    // stops: the restore is then an exact merge of the snapshot, and the
    // spectrum stays where the brand put it.
    const QVariantMap known = m_hasSnapshot ? m_snapshot : storePalette();
    QVariantMap filtered;
    const QVariantMap incoming = withoutBrandStops(tokens);
    for (auto it = incoming.constBegin(); it != incoming.constEnd(); ++it) {
        if (known.contains(it.key())) {
            filtered.insert(it.key(), it.value());
        }
    }
    if (filtered.isEmpty()) {
        return;
    }
    QMetaObject::invokeMethod(m_store, "applyTokens", Q_ARG(QVariantMap, filtered));
}

void RetintController::setPreviewPath(const QString& path)
{
    if (m_previewPath == path) {
        return;
    }
    m_previewPath = path;
    Q_EMIT previewPathChanged();
}

void RetintController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT busyChanged();
}

void RetintController::setLastError(const QString& error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    Q_EMIT lastErrorChanged();
}

QVariantMap RetintController::storePalette() const
{
    if (!m_store) {
        return {};
    }
    return m_store->property("palette").toMap();
}

} // namespace PhosphorShellPicker
