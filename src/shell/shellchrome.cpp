// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShellChrome.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/SurfaceChainCompose.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceThemeResolve.h>
#include <PhosphorTheme/PaletteStore.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <algorithm>

Q_LOGGING_CATEGORY(lcShellChrome, "phosphorshell.chrome")

namespace PhosphorShellApp {

namespace {

QVariant unwrapDBusVariant(QVariant value)
{
    while (value.canConvert<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }
    return value;
}

QColor tokenOr(const PhosphorTheme::PaletteStore* palette, const QString& name, const QColor& fallback)
{
    if (!palette) {
        return fallback;
    }
    const QColor c = palette->token(name);
    return c.isValid() ? c : fallback;
}

} // namespace

ShellChrome::ShellChrome(QObject* parent)
    : ShellChrome(defaultPackSearchPaths(), parent)
{
    subscribeToDaemon();
    fetchTree();
}

ShellChrome::ShellChrome(const QStringList& packSearchPaths, QObject* parent)
    : QObject(parent)
    , m_registry(std::make_unique<PhosphorSurfaceShaders::SurfaceShaderRegistry>(nullptr))
    , m_presetStore(std::make_unique<PhosphorShaders::ShaderPresetStore>(nullptr))
{
    if (!packSearchPaths.isEmpty()) {
        // Last path is the user's, as the daemon orders them.
        m_registry->setUserPath(packSearchPaths.last());
        m_registry->addSearchPaths(packSearchPaths);
    }

    // The SURFACE family only: that is the one the shell resolves. A family the shell
    // does not consult would still cost a QFileSystemWatcher and a whole-directory
    // re-parse on every preset the user saves, which is the reason every other
    // consumer names its families too.
    m_presetStore->load(PhosphorShaders::standardUserPresetRoot(), {PhosphorShaders::ShaderFamily::Surface});

    // Pack-declared presets live in each pack's metadata.json, which only the pack
    // registry parses, so push what it has now and again on every reload. The
    // projection is the shared `seedPackPresets`, not a local copy.
    const auto seedPresets = [this]() {
        PhosphorShaders::seedPackPresets(m_presetStore->registry(), PhosphorShaders::ShaderFamily::Surface,
                                         m_registry->availableEffects());
    };
    seedPresets();
    connect(m_registry.get(), &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, m_presetStore.get(),
            seedPresets);
    // A preset retuned anywhere — this process, the settings app, a text editor — has
    // to move the chrome that is already on screen. `revision` is what every QML
    // binding here re-resolves on.
    connect(&m_presetStore->registry(), &PhosphorShaders::ShaderPresetRegistry::presetsChanged, this,
            [this](PhosphorShaders::ShaderFamily family, const QString&) {
                if (family == PhosphorShaders::ShaderFamily::Surface) {
                    bump();
                }
            });
}

void ShellChrome::setPalette(PhosphorTheme::PaletteStore* palette)
{
    if (m_palette == palette) {
        return;
    }
    if (m_palette) {
        disconnect(m_palette, nullptr, this, nullptr);
    }
    m_palette = palette;
    if (m_palette) {
        connect(m_palette, &PhosphorTheme::PaletteStore::paletteChanged, this, &ShellChrome::bump);
    }
    bump();
}

ShellChrome::~ShellChrome() = default;

int ShellChrome::revision() const
{
    return m_revision;
}

QObject* ShellChrome::decorationComponent() const
{
    return m_decorationComponent;
}

void ShellChrome::setDecorationComponent(QObject* component)
{
    if (m_decorationComponent == component) {
        return;
    }
    m_decorationComponent = component;
    Q_EMIT decorationComponentChanged();
}

QStringList ShellChrome::defaultPackSearchPaths()
{
    // The daemon's setupSurfaceShaderEffects, verbatim: system dirs lowest
    // priority first, the user dir last and materialised so the loader can
    // watch it.
    QStringList dirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/surface"), QStandardPaths::LocateDirectory);
    std::reverse(dirs.begin(), dirs.end());
    const QString userDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/plasmazones/surface");
    if (!dirs.contains(userDir)) {
        dirs.append(userDir);
    }
    QDir().mkpath(userDir);
    return dirs;
}

const PhosphorSurfaceShaders::DecorationProfileTree& ShellChrome::tree() const
{
    return m_tree;
}

bool ShellChrome::setTreeJson(const QString& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcShellChrome) << "decorationProfileTree is not a JSON object; keeping the current tree";
        return false;
    }
    auto tree = PhosphorSurfaceShaders::DecorationProfileTree::fromJson(doc.object());
    if (tree == m_tree) {
        return true;
    }
    m_tree = std::move(tree);
    bump();
    return true;
}

QVariantList ShellChrome::chainFor(const QString& surfacePath) const
{
    QVariantList stages;
    // FLATTENED, like every other surface consumer. `resolve()` walks the tree but does
    // not apply presets, and `effectiveParameters()` below is the post-flatten read —
    // so without this a surface naming a preset rendered without its values, and
    // without the declared-range clamp `resolveParams` applies.
    const PhosphorSurfaceShaders::DecorationProfile profile = PhosphorSurfaceShaders::withPresetsResolved(
        m_tree.resolve(surfacePath), m_presetStore->registry(), PhosphorShaders::ShaderFamily::Surface);
    const QStringList chain = profile.enabledChain();
    if (chain.isEmpty()) {
        return stages;
    }
    const QVariantMap allParams = profile.effectiveParameters();
    // The pack flag resolver's theme: the spectrum's own tokens, so a pack
    // that asks for the accent gets the focus colour of the chrome around it.
    const PhosphorSurfaceShaders::SurfaceThemeColors theme{
        tokenOr(m_palette, QStringLiteral("primary"), QColor(0x3b, 0x82, 0xf6)),
        tokenOr(m_palette, QStringLiteral("outline"), QColor(0x33, 0x41, 0x55)),
        tokenOr(m_palette, QStringLiteral("surface"), QColor(0x0b, 0x10, 0x20)),
        tokenOr(m_palette, QStringLiteral("on_surface"), QColor(0xe6, 0xed, 0xff)),
    };
    for (const QString& packId : chain) {
        if (!m_registry->hasEffect(packId)) {
            qCDebug(lcShellChrome) << surfacePath << ": pack" << packId << "is not installed; stage skipped";
            continue;
        }
        const PhosphorSurfaceShaders::SurfaceShaderEffect effect = m_registry->effect(packId);
        if (!effect.isValid()) {
            qCDebug(lcShellChrome) << surfacePath << ": pack" << packId << "has no fragment shader; stage skipped";
            continue;
        }
        QVariantMap params = allParams.value(packId).toMap();
        PhosphorSurfaceShaders::resolveThemeParamColors(effect, params, theme);
        stages.append(PhosphorSurfaceShaders::composeStageMap(effect, params));
    }
    return stages;
}

double ShellChrome::outerPaddingFor(const QString& surfacePath) const
{
    // Flattened for the same reason chainFor is: a preset can move the very parameter
    // a pack's padding request is computed from, so reading the unflattened map here
    // would size the chrome's margin against values the stages do not draw with.
    const PhosphorSurfaceShaders::DecorationProfile profile = PhosphorSurfaceShaders::withPresetsResolved(
        m_tree.resolve(surfacePath), m_presetStore->registry(), PhosphorShaders::ShaderFamily::Surface);
    const QVariantMap allParams = profile.effectiveParameters();
    double padding = 0.0;
    const QStringList chain = profile.enabledChain();
    for (const QString& packId : chain) {
        if (!m_registry->hasEffect(packId)) {
            continue;
        }
        const PhosphorSurfaceShaders::SurfaceShaderEffect effect = m_registry->effect(packId);
        if (!effect.isValid()) {
            continue;
        }
        padding = std::max(padding, PhosphorSurfaceShaders::paddingRequest(effect, allParams.value(packId).toMap()));
    }
    return std::clamp(padding, 0.0, static_cast<double>(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx));
}

void ShellChrome::subscribeToDaemon()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = QString(PhosphorProtocol::Service::Name);
    const QString path = QString(PhosphorProtocol::Service::ObjectPath);
    const QString settings = QString(PhosphorProtocol::Service::Interface::Settings);
    // Any setting change refetches: the tree is one key and the signal
    // carries none, so this is the daemon's own contract for followers.
    bus.connect(service, path, settings, QStringLiteral("settingsChanged"), this, SLOT(fetchTree()));
    // A daemon that (re)appears publishes a fresh tree.
    auto* watcher = new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &ShellChrome::fetchTree);
    // The registry watches its directories; a pack installed while the
    // shell runs re-resolves too.
    connect(m_registry.get(), &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this, &ShellChrome::bump);
}

void ShellChrome::fetchTree()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Settings), QStringLiteral("getSetting"));
    call << QString(PhosphorProtocol::Service::SettingProperty::DecorationProfileTree);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const QDBusPendingReply<QVariant> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcShellChrome) << "decorationProfileTree unavailable:" << reply.error().message();
            return;
        }
        setTreeJson(unwrapDBusVariant(reply.value()).toString());
    });
}

void ShellChrome::bump()
{
    ++m_revision;
    Q_EMIT revisionChanged();
}

} // namespace PhosphorShellApp
