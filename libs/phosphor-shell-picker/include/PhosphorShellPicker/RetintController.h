// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace PhosphorShellPicker {

/**
 * @brief The picker's live retint: try a palette on the real desktop,
 *        put the old one back when the pointer leaves.
 *
 * `preview(path)` is the hover path. It is debounced (150 ms by default)
 * so a pointer sweeping across the strip runs matugen once, for the
 * candidate it stops on; each new preview cancels the runner's in-flight
 * run first. When the runner reports a palette for the candidate still
 * pending, the tokens go to the store and the whole shell retints
 * through the Theme singleton's bindings. `previewTokens(map)` is the
 * same for a theme tile, without the runner.
 *
 * The first preview snapshots the store's palette. `clearPreview()`
 * re-applies that snapshot (the pointer left, or Escape), `commit()`
 * drops it (Apply), so the live palette stays. Incoming tokens are
 * filtered to the keys the snapshot already publishes, which is what
 * makes the restore exact (the store merges and cannot forget a key)
 * and is why a preview can never introduce a token the shell does not
 * know.
 *
 * The four `brand_stop_*` tokens never change through this controller
 * (05 §4: the spectrum is fixed; matugen moves ground, containers and
 * text). They are stripped from every incoming map before it is
 * applied, and the store's merge leaves them as they were.
 *
 * `runner` and `store` are duck-typed QObjects so a test can stand in a
 * fake for the matugen subprocess. The runner needs `run(QString)`,
 * `cancel()` and the `paletteReady(QVariantMap, QString)` /
 * `failed(QString, QString)` signals of PhosphorTheme::MatugenRunner;
 * the store needs a `palette` QVariantMap property and
 * `applyTokens(QVariantMap)`, the shape of PhosphorTheme::PaletteStore.
 */
class RetintController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* runner READ runner WRITE setRunner NOTIFY runnerChanged)
    Q_PROPERTY(QObject* store READ store WRITE setStore NOTIFY storeChanged)
    Q_PROPERTY(int debounceMs READ debounceMs WRITE setDebounceMs NOTIFY debounceMsChanged)
    /// Where `commit()` writes the committed palette, in the `{ "tokens":
    /// { ... } }` shape PaletteStore reads. Empty (the default) writes
    /// nothing. See `defaultPersistPath()`.
    Q_PROPERTY(QString persistPath READ persistPath WRITE setPersistPath NOTIFY persistPathChanged)
    /// `defaultPersistPath()` for QML, which cannot call a static through
    /// the type name of a non-singleton: `persistPath: retint.defaultPersistPath`.
    Q_PROPERTY(QString defaultPersistPath READ defaultPersistPath CONSTANT)
    /// The candidate (a wallpaper path, or a theme tile's label) whose
    /// palette is live, or empty when the store shows its own palette.
    Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewPathChanged)
    /// True while a snapshot is held: a preview is showing or pending.
    Q_PROPERTY(bool previewing READ isPreviewing NOTIFY previewingChanged)
    /// True while the runner is working on a preview.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit RetintController(QObject* parent = nullptr);
    ~RetintController() override;

    [[nodiscard]] QObject* runner() const;
    void setRunner(QObject* runner);

    [[nodiscard]] QObject* store() const;
    void setStore(QObject* store);

    [[nodiscard]] int debounceMs() const;
    void setDebounceMs(int ms);

    [[nodiscard]] QString persistPath() const;
    void setPersistPath(const QString& path);

    [[nodiscard]] QString previewPath() const;
    [[nodiscard]] bool isPreviewing() const;
    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] QString lastError() const;

    /// Hover a wallpaper candidate: debounced, cancels the previous run.
    Q_INVOKABLE void preview(const QString& wallpaperPath);
    /// Hover a theme tile: its tokens are applied at once under `label`.
    Q_INVOKABLE void previewTokens(const QVariantMap& tokens, const QString& label);
    /// The pointer left without Apply: put the snapshot back.
    Q_INVOKABLE void clearPreview();
    /// Apply: keep what is live (and what a still-pending run lands),
    /// forget the snapshot, and write `persistPath` when set.
    Q_INVOKABLE void commit();

    /// The palette file the shell reads at startup and `commit()` writes:
    /// `<AppLocalDataLocation>/palettes/current.json`, the layout
    /// phosphor-theme-cli's `--apply` uses.
    [[nodiscard]] static QString defaultPersistPath();

    /// `tokens` without the four brand stops. Pure, for the tests.
    [[nodiscard]] static QVariantMap withoutBrandStops(const QVariantMap& tokens);

    /// Write `tokens` to `path` atomically in the wrapped shape.
    [[nodiscard]] static bool writePalette(const QString& path, const QVariantMap& tokens);

Q_SIGNALS:
    void runnerChanged();
    void storeChanged();
    void debounceMsChanged();
    void persistPathChanged();
    void previewPathChanged();
    void previewingChanged();
    void busyChanged();
    void lastErrorChanged();
    /// The store now shows `path`'s palette.
    void previewApplied(const QString& path);
    /// The runner could not produce a palette for `path`.
    void previewFailed(const QString& path, const QString& reason);

private Q_SLOTS:
    void onPaletteReady(const QVariantMap& tokens, const QString& wallpaperPath);
    void onRunnerFailed(const QString& wallpaperPath, const QString& reason);

private:
    void startRun();
    void ensureSnapshot();
    void applyToStore(const QVariantMap& tokens);
    void setPreviewPath(const QString& path);
    void setBusy(bool busy);
    void setLastError(const QString& error);
    [[nodiscard]] QVariantMap storePalette() const;

    QPointer<QObject> m_runner;
    QPointer<QObject> m_store;
    QTimer* m_debounce = nullptr;
    QString m_persistPath;
    QString m_pendingPath;
    QString m_previewPath;
    QString m_lastError;
    QVariantMap m_snapshot;
    bool m_hasSnapshot = false;
    bool m_busy = false;
};

} // namespace PhosphorShellPicker
