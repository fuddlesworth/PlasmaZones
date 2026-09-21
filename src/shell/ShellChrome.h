// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorShaders/ShaderPresetStore.h>

#include <QHash>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <memory>

namespace PhosphorTheme {
class PaletteStore;
}

namespace PhosphorShellApp {

// Surface packs on the shell's chrome (A1 §2.4, "one engine, one
// material"): every chrome surface is a decoration host exactly like a
// window frame, and the pack a focused window wears runs on the bar, the
// popouts, the OSD bands, the toasts, the picker and the lock clock.
//
// A layer-shell client cannot be decorated by the compositor around a
// sub-rect of its surface (the bar's band is a strip of a taller surface,
// an OSD band a sliver of a screen-sized one), so the shell hosts the
// chain itself, the daemon's overlay pattern: the same SurfaceDecoration
// QML host, one SurfaceShaderItem per stage, driven by the same
// DecorationProfileTree the Decoration pages edit and the same pack files
// the compositor compiles for windows. What differs from the compositor
// path is only who draws.
//
// This object resolves a surface path (`shell.phosphor.bar`, ...) to the
// stage list that host consumes. The tree comes from the daemon's Settings
// interface (already seeded with the shell's defaults, so a fresh install
// is decorated) and follows settingsChanged; the packs come from the
// registry over the same search paths the daemon scans; the theme colours
// a pack may ask for come from the shell's own palette. `revision` bumps
// whenever any of those move, so a QML binding that reads it re-resolves.
//
// Exposed to QML as the `ShellChrome` context property. shell.qml hands it
// the one decoration Component every surface instantiates through
// `decorationComponent`, because a per-screen delegate cannot see an id in
// shell.qml but can read a context property.
class ShellChrome : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QObject* decorationComponent READ decorationComponent WRITE setDecorationComponent NOTIFY
                   decorationComponentChanged)

public:
    /// Production: the daemon's tree over D-Bus, the installed pack dirs.
    explicit ShellChrome(QObject* parent = nullptr);
    /// Explicit pack search paths, no D-Bus subscription (tests).
    ShellChrome(const QStringList& packSearchPaths, QObject* parent);
    ~ShellChrome() override;

    /// The palette a pack's theme colours resolve from. Per QML engine (the
    /// PaletteStore singleton), so the host sets it from its engine hook;
    /// null falls back to the spectrum's built-in navy tokens.
    void setPalette(PhosphorTheme::PaletteStore* palette);

    [[nodiscard]] int revision() const;
    [[nodiscard]] QObject* decorationComponent() const;
    void setDecorationComponent(QObject* component);

    /// The stage list for @p surfacePath: one map per enabled pack in the
    /// resolved chain ({source, vertexSource, preamble, params, animated,
    /// ...} as SurfaceDecoration reads them). Empty when undecorated.
    [[nodiscard]] Q_INVOKABLE QVariantList chainFor(const QString& surfacePath) const;
    /// `m_tree.resolve(path)` with every layer's preset flattened in, memoised per path
    /// for the life of one revision.
    ///
    /// QML calls `chainFor` AND `outerPaddingFor` for every surface on each `revision`
    /// bump (a palette change, a settings refetch, a pack reload, a preset retune), and
    /// each used to run its own tree walk plus its own preset flatten. The cache is
    /// cleared in `bump()`, which is the one place anything either of them reads can
    /// change, so a stale entry is not reachable: the revision IS the invalidation.
    [[nodiscard]] const PhosphorSurfaceShaders::DecorationProfile& resolvedProfile(const QString& surfacePath) const;

    /// The chain's largest declared outer margin, logical px, clamped.
    [[nodiscard]] Q_INVOKABLE double outerPaddingFor(const QString& surfacePath) const;

    /// Replace the tree from its JSON form (the daemon's publication).
    /// Returns false and keeps the current tree on malformed input.
    bool setTreeJson(const QString& json);
    [[nodiscard]] const PhosphorSurfaceShaders::DecorationProfileTree& tree() const;

    /// The pack directories the daemon scans, in the daemon's order.
    [[nodiscard]] static QStringList defaultPackSearchPaths();

Q_SIGNALS:
    void revisionChanged();
    void decorationComponentChanged();

private Q_SLOTS:
    void fetchTree();
    void bump();

private:
    void subscribeToDaemon();

    std::unique_ptr<PhosphorSurfaceShaders::SurfaceShaderRegistry> m_registry;
    /// Named parameter presets for the SURFACE family, so a shell surface whose
    /// assignment names one renders with that preset's values.
    ///
    /// The shell is a decoration consumer like any other, and without this it was the
    /// one that silently was not: `chainFor` and `outerPaddingFor` read
    /// `effectiveParameters()` on an UNFLATTENED profile, so a shell surface (or an
    /// ancestor it inherits from) that named a preset rendered with the preset's values
    /// MISSING and with the pack's declared min/max unenforced, since that clamp only
    /// happens inside `resolveParams`. Every other surface consumer — the compositor's
    /// decorations, the daemon's OSD, the pointer pass — flattens.
    ///
    /// Declared AFTER m_registry: the seeding connection below reads the registry, and
    /// reverse member destruction tears this down first.
    std::unique_ptr<PhosphorShaders::ShaderPresetStore> m_presetStore;
    /// Flattened profiles for this revision; see `resolvedProfile`. Mutable because both
    /// readers are const and the cache is a memo, not state a caller can observe.
    mutable QHash<QString, PhosphorSurfaceShaders::DecorationProfile> m_resolvedCache;
    PhosphorSurfaceShaders::DecorationProfileTree m_tree;
    QPointer<PhosphorTheme::PaletteStore> m_palette;
    QPointer<QObject> m_decorationComponent;
    int m_revision = 0;
};

} // namespace PhosphorShellApp
