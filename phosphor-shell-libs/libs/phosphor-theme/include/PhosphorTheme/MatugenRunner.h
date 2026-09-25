// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/phosphortheme_export.h>

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QProcess;

namespace PhosphorTheme {

// Spawns the external `matugen` binary on a wallpaper path, captures its
// JSON output, parses it into a `token name -> QColor` map, and emits
// `paletteReady` for the caller (typically a PaletteStore) to apply.
//
// MatugenRunner deliberately does NOT couple to PaletteStore directly:
// the runner emits the parsed map, and the caller decides whether to
// merge into the live palette, persist it to disk, or both. This keeps
// the matugen integration testable without spinning up a full theme
// service, and lets the CLI (which only wants the map) reuse the runner
// without dragging in QML.
//
// Failure cases:
//   - matugen not on PATH                       → `failed` with reason
//   - wallpaper path missing                    → `failed`
//   - matugen exits non-zero                    → `failed` with stderr tail
//   - JSON unparseable / unexpected schema      → `failed` with reason
//
// All failure paths leave any concurrent run state cleaned up.
class PHOSPHORTHEME_EXPORT MatugenRunner : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // matugen invocation: `matugenBinary` is resolved on PATH unless an
    // absolute path is given. Defaults to "matugen".
    Q_PROPERTY(QString matugenBinary READ matugenBinary WRITE setMatugenBinary NOTIFY matugenBinaryChanged)

    // Dark / light variant selection, matugen emits both; we pick the
    // requested one before exposing. Default: "dark".
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)

    // Candidate-color preference. Matugen needs this flag whenever an
    // image yields multiple source colors and matugen runs
    // non-interactively. We invoke it as a subprocess with no TTY, so
    // this is always our case. Default is "saturation", which picks the
    // most saturated candidate and matches user expectations for a
    // vibrant wallpaper-derived palette. Valid matugen values are
    // "darkness", "lightness", "saturation", "less-saturation", "value",
    // and "closest-to-fallback".
    Q_PROPERTY(QString prefer READ prefer WRITE setPrefer NOTIFY preferChanged)

    // True while a `run()` call is in flight. Bind UI busy indicators here.
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

public:
    explicit MatugenRunner(QObject* parent = nullptr);
    ~MatugenRunner() override;

    [[nodiscard]] QString matugenBinary() const;
    void setMatugenBinary(const QString& binary);

    [[nodiscard]] QString mode() const;
    void setMode(const QString& mode);

    [[nodiscard]] QString prefer() const;
    void setPrefer(const QString& prefer);

    [[nodiscard]] bool isRunning() const;

    // Spawn matugen on `wallpaperPath`. Emits `paletteReady` on success,
    // `failed` on any error path. A second `run` while one is already in
    // flight cancels the previous run before starting.
    Q_INVOKABLE void run(const QString& wallpaperPath);

    // QUrl overload for QML callers (e.g. FileDialog.selectedFile is a
    // url, not a string). Converts via QUrl::toLocalFile so every legal
    // file URL form resolves correctly. Non-file URLs are rejected via
    // the `failed` signal so a passing-but-wrong call surfaces visibly
    // instead of spawning matugen with a malformed argument.
    Q_INVOKABLE void run(const QUrl& wallpaperUrl);

    // Cancel an in-flight run. Safe to call when nothing is running.
    Q_INVOKABLE void cancel();

    // Parse a matugen JSON byte array directly, no subprocess. Exposed
    // for tests and for shells that drive matugen via a different
    // mechanism (e.g. an existing CLI pipeline). Returns the token map;
    // empty on parse failure (no signal fires).
    [[nodiscard]] QVariantMap parseMatugenJson(const QByteArray& json, const QString& mode) const;

Q_SIGNALS:
    void matugenBinaryChanged();
    void modeChanged();
    void preferChanged();
    void runningChanged();

    // The matugen invocation completed and yielded a non-empty token map.
    // The map has Phosphor-canonical snake_case keys; any matugen names
    // that don't round-trip have already been remapped.
    void paletteReady(const QVariantMap& tokens, const QString& wallpaperPath);

    // Anything that prevented `paletteReady` from firing, missing
    // binary, non-zero exit, parse error. `reason` is human-readable.
    void failed(const QString& wallpaperPath, const QString& reason);

private:
    void teardownProcess();
    void cancelInflight();
    void disposeProcess();
    void emitRunningChangedIfTransitioned();

    QString m_matugenBinary;
    QString m_mode;
    QString m_prefer;
    QString m_pendingWallpaper;
    std::unique_ptr<QProcess> m_process;
    // Last value we actually emitted via runningChanged. The signal
    // contract is "edge-triggered on real transitions". Sampling
    // isRunning() at emit time isn't enough because Qt flips
    // QProcess::state() to NotRunning BEFORE delivering errorOccurred,
    // so a lambda observing isRunning() can miss the transition it's
    // supposed to report. Tracking what we already told consumers
    // pins the contract regardless of Qt's internal ordering.
    bool m_lastEmittedRunning = false;
};

} // namespace PhosphorTheme
