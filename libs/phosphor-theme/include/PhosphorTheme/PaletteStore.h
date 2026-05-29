// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/IThemeService.h>
#include <PhosphorTheme/phosphortheme_export.h>

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QFileSystemWatcher;
class QJsonDocument;
class QTimer;

namespace PhosphorTheme {

// Concrete IThemeService backed by an in-memory QVariantMap. JSON files are
// loaded eagerly and watched for hot-reload; the file's `tokens` object
// (token name → "#RRGGBB" or "#AARRGGBB") merges into the active palette
// (existing tokens absent from the new payload survive) and
// `paletteChanged` fires once per reload that actually changed at least
// one token. Use `resetToDefaults()` for a true replace-with-defaults.
//
// Path handling: `loadFromFile(path)` canonicalises through
// QFileInfo::canonicalFilePath() before storing as the watched source,
// so symlinks are resolved to their target. A passed-in symlink to a
// palette file will have the underlying target file watched, not the
// symlink itself. This is by design: hot-reload should track the file
// whose contents change, not an indirection that may swap targets
// without notifying the watcher. When canonicalFilePath() returns
// empty (the path doesn't exist yet, or canonicalisation can't
// resolve the target), loadFromFile falls back to
// QFileInfo::absoluteFilePath() so the subsequent open() still
// surfaces the user-visible path in any error message. No base-dir
// restriction is applied; callers pass paths they trust (KCM, demo,
// scripted setup).
//
// QML side: registered as a QML singleton via `QML_ELEMENT QML_SINGLETON`,
// imported as `import Phosphor.Theme` → `PaletteStore.token("primary")`.
// QML files in Phosphor.Theme (Theme.qml, etc.) wrap PaletteStore in
// nicer property aliases so consumers write `Theme.primary` instead of
// `PaletteStore.token("primary")`.
//
// This is a per-engine instance, not a process global. The engine owns
// the lifetime; alternate implementations swap in via
// `qmlRegisterSingletonInstance` before module import.
class PHOSPHORTHEME_EXPORT PaletteStore : public QObject, public IThemeService
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // QML-friendly accessor exposing the full token map. Bindings on
    // `palette` re-evaluate when the active palette changes.
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY paletteChanged)

    // The currently-watched JSON path, or empty if the active palette
    // came from `loadFromJson` / defaults.
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourcePathChanged)

public:
    explicit PaletteStore(QObject* parent = nullptr);
    ~PaletteStore() override;
    Q_DISABLE_COPY_MOVE(PaletteStore)

    // The canonical built-in dark palette. Static accessor for callers
    // that need the defaults without paying for a PaletteStore instance
    // (which arms a QFileSystemWatcher + holds an inotify fd). Use this
    // for preset palettes, snapshot tests, and any read-only consumer.
    [[nodiscard]] static QVariantMap defaultPalette();

    // IThemeService.
    [[nodiscard]] QVariantMap palette() const override;
    [[nodiscard]] Q_INVOKABLE QColor token(const QString& name) const override;
    // loadFromJson is atomic: a failed parse / shape check leaves
    // sourcePath, the watcher, and the active palette unchanged
    // (the implementation validates the payload up-front, then
    // commits only on success). A previously-file-loaded store
    // therefore keeps its watcher armed across a failed
    // loadFromJson call. Callers can treat any false return as a
    // pure no-op.
    Q_INVOKABLE bool loadFromJson(const QByteArray& json) override;
    // loadFromFile is INTENTIONALLY ASYMMETRIC with loadFromJson:
    // it commits the new sourcePath + swaps the watcher BEFORE
    // applying the parsed tokens, so a shape-failure return
    // (`tokens` key non-object, empty token map) still leaves
    // sourcePath pointing at the new file and the watcher armed
    // against it. Rationale: those failures are recoverable in
    // the user's editor without re-triggering load — the file
    // exists, the JSON parses, the contents just had no usable
    // tokens. Keeping the watcher armed means the next save in
    // the editor reloads automatically. Outright parse failures
    // (malformed JSON) still leave sourcePath untouched because
    // the parse runs before any commit. Outright I/O failures
    // (cannot open, short read) also leave sourcePath untouched
    // for the same reason. Callers that need atomic-commit
    // semantics across every failure mode should validate the
    // file out-of-band and route through loadFromJson instead.
    //
    // Note: on shape-failure paths (TokensKeyNotObject /
    // NoUsableTokens) the new sourcePath is still committed before
    // the failure surfaces, so `sourcePathChanged` may fire even
    // when this returns false. QML bindings observing both
    // sourcePath and loadError must be prepared to see the new
    // path arrive together with a loadError on the same edit.
    Q_INVOKABLE bool loadFromFile(const QString& path) override;
    Q_INVOKABLE void applyTokens(const QVariantMap& tokens) override;
    Q_INVOKABLE void resetToDefaults() override;

    [[nodiscard]] QString sourcePath() const;

Q_SIGNALS:
    void paletteChanged();
    void sourcePathChanged();

    // Fires when `loadFromFile` failed to parse or read the file. The
    // active palette is unchanged. Wired to the demo's status bar so a
    // bad save is visible without crashing the shell.
    void loadError(const QString& path, const QString& reason);

private:
    /// Merge variant. Takes an already-parsed JsonDocument so
    /// callers that validated the document up-front (loadFromFile,
    /// reloadFromCurrentPath) can route through the same merge
    /// logic without paying for a second QJsonDocument::fromJson
    /// and without losing context about WHICH validation step
    /// failed (parse vs token-map empty). Returns a typed error
    /// code so the caller can surface the distinction in loadError.
    enum class ApplyResult {
        Ok,
        TokensKeyNotObject,
        NoUsableTokens,
    };
    /// Single source of truth for the wrapped-vs-flat shape
    /// traversal. Given a parsed JSON document, returns the tokens
    /// object the merge step would iterate (empty QJsonObject on
    /// shape error). Both validateDoc and applyParsedJson route
    /// through this helper so the two cannot drift on the shape
    /// rules: a future failure mode added here surfaces in BOTH
    /// the validator and the applier in lockstep. See the file-
    /// top comment in palettestore.cpp for the rationale.
    ///
    /// On shape error (`tokens` key present but not an object) the
    /// returned object is empty AND outShapeError is set to
    /// TokensKeyNotObject. On a syntactically fine but empty /
    /// no-usable-color payload the returned object may be non-empty
    /// (the validator/applier walks its entries to determine usability)
    /// and outShapeError stays Ok — the NoUsableTokens verdict only
    /// surfaces after the entry walk.
    static QJsonObject extractTokensOrEmpty(const QJsonDocument& doc, ApplyResult& outShapeError);
    /// Pure shape-validator for a parsed JSON document. Returns the
    /// same ApplyResult value applyParsedJson would return for the
    /// document, but WITHOUT applying or mutating any state. Lets
    /// loadFromJson decide whether the payload will succeed BEFORE
    /// touching the watcher / m_sourcePath, satisfying the "failed
    /// load does not observably mutate state" contract without
    /// duplicating the shape-check logic across two call sites.
    /// Implemented via extractTokensOrEmpty so it can't drift from
    /// applyParsedJson on the shape rules.
    static ApplyResult validateDoc(const QJsonDocument& doc);
    ApplyResult applyParsedJson(const QJsonDocument& doc);
    /// Disarms the active filesystem watch (file + parent directory),
    /// cancels any pending debounced reload, and clears m_sourcePath
    /// (emitting sourcePathChanged if it was non-empty). The
    /// implementation counterpart to IThemeService::loadFromJson's
    /// "in-process blob, not a watched file" contract.
    void dropWatcherAndClearSourcePath();
    /// Merges incoming tokens over the current palette. Existing
    /// keys absent from `tokens` are preserved; values are
    /// normalised to QColor. Emits paletteChanged when at least
    /// one stored value differs from the incoming one. Pure
    /// in-memory merge: does NOT touch the watcher, the source
    /// path, or trigger any file I/O.
    void applyPalette(const QVariantMap& tokens);
    void reloadFromCurrentPath();

    QVariantMap m_palette;
    QString m_sourcePath;
    std::unique_ptr<QFileSystemWatcher> m_watcher;
    std::unique_ptr<QTimer> m_reloadDebounce;
};

} // namespace PhosphorTheme
