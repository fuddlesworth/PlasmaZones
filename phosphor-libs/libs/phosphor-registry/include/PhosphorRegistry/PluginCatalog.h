// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QPluginLoader;
QT_END_NAMESPACE

namespace PhosphorFsLoader {
class WatchedDirectorySet;
}

namespace PhosphorRegistry {

/// One kind of plugin the suite loads, such as Facet modules or bar widgets.
///
/// A plugin of a seam is a Qt plugin (`qt_add_plugin` + `Q_PLUGIN_METADATA`)
/// whose IID equals `iid` and whose embedded metadata JSON carries at least
/// `"Id"` (a non-empty `[A-Za-z0-9][A-Za-z0-9._-]*` string) and
/// `"ApiVersion"` equal to `apiVersion`. Everything else in that JSON belongs
/// to the seam and is handed through untouched.
struct PHOSPHORREGISTRY_EXPORT PluginSeam
{
    /// Directory name under each plugin root, for example `facet`.
    QString name;
    /// The IID every plugin of this seam declares, for example
    /// `org.phosphor_works.Facet.Module/1`.
    QString iid;
    /// The `ApiVersion` this build loads. A plugin built for another one is
    /// refused before its library is loaded.
    int apiVersion = 0;
};

/// Where a plugin was found. Earlier origins override later ones.
enum class PluginOrigin : quint8 {
    /// A directory from PHOSPHOR_WORKS_PLUGIN_PATH, for running against
    /// plugins straight from a build tree.
    Development,
    /// `$XDG_DATA_HOME/phosphor-works/plugins/<seam>`.
    User,
    /// `<Qt library path>/phosphor-works/<seam>`, where packages install.
    System,
};

/// A directory to search and the origin its plugins get.
struct PHOSPHORREGISTRY_EXPORT PluginSearchPath
{
    QString directory;
    PluginOrigin origin = PluginOrigin::System;
};

/// A plugin known from its metadata. Building one never loads the library.
struct PHOSPHORREGISTRY_EXPORT PluginMetadata
{
    QString id;
    int apiVersion = 0;
    /// Absolute path of the plugin library.
    QString filePath;
    PluginOrigin origin = PluginOrigin::System;
    /// The plugin's own metadata JSON, as given to Q_PLUGIN_METADATA.
    QJsonObject metaData;
};

/// A plugin file the catalog found and will not offer, with the reason.
struct PHOSPHORREGISTRY_EXPORT RefusedPlugin
{
    QString filePath;
    /// The id from its metadata, or empty when the metadata had none.
    QString id;
    QString reason;
};

/// Finds the plugins of one seam, reads their metadata without loading them,
/// and loads one only when asked.
///
/// Search order, highest priority first (see defaultSearchPaths):
///   1. each directory in PHOSPHOR_WORKS_PLUGIN_PATH, plus `/<seam>`
///   2. `$XDG_DATA_HOME/phosphor-works/plugins/<seam>`
///   3. each Qt library path, plus `/phosphor-works/<seam>`
/// A plugin found earlier overrides one with the same id found later, and the
/// override is logged.
///
/// A plugin is refused, with the reason kept in refused() and logged once, when:
///   - its file or directory is group- or world-writable
///   - it is not a Qt plugin, or its IID is not the seam's
///   - its metadata has no valid `"Id"`
///   - its `"ApiVersion"` is not the seam's
///   - another plugin in the same directory has the same id
/// Symlinked files are never considered: code loads only from a real file in a
/// searched directory.
///
/// Loaded plugins are never unloaded. A plugin removed from disk disappears
/// from plugins() and emits pluginRemoved, but a loaded instance stays valid.
/// A loaded plugin whose file changes keeps running the old code until the
/// process restarts, because the dynamic linker keeps one mapping per path.
///
/// GUI thread only, like WatchedDirectorySet.
class PHOSPHORREGISTRY_EXPORT PluginCatalog : public QObject
{
    Q_OBJECT
public:
    /// The environment variable holding development plugin roots,
    /// colon-separated. Each root holds one subdirectory per seam.
    static constexpr const char* DevelopmentPathVariable = "PHOSPHOR_WORKS_PLUGIN_PATH";

    explicit PluginCatalog(PluginSeam seam, QObject* parent = nullptr);
    ~PluginCatalog() override;
    Q_DISABLE_COPY_MOVE(PluginCatalog)

    /// The directories searched for @p seamName when no search paths are set,
    /// highest priority first.
    [[nodiscard]] static QList<PluginSearchPath> defaultSearchPaths(const QString& seamName);

    [[nodiscard]] const PluginSeam& seam() const;

    /// Replace the directories to search, highest priority first. Must be
    /// called before scan(). Without it the catalog uses defaultSearchPaths.
    void setSearchPaths(const QList<PluginSearchPath>& paths);
    [[nodiscard]] QList<PluginSearchPath> searchPaths() const;

    /// Watch the search paths and rescan when they change (on by default).
    /// Must be called before scan().
    void setLiveReload(bool enabled);

    /// Scan the search paths and read every plugin's metadata. The first call
    /// registers the directories and arms the watch. Later calls rescan.
    void scan();

    /// Every plugin on offer, sorted by id.
    [[nodiscard]] QList<PluginMetadata> plugins() const;
    [[nodiscard]] std::optional<PluginMetadata> plugin(const QString& id) const;
    /// The plugin files refused by the last scan, sorted by path.
    [[nodiscard]] QList<RefusedPlugin> refused() const;

    /// Load the plugin @p id and return its root object, or nullptr with the
    /// reason in @p errorString. A second call returns the same object.
    QObject* load(const QString& id, QString* errorString = nullptr);
    [[nodiscard]] bool isLoaded(const QString& id) const;

    /// Test seam: cap the number of plugin files considered per scan. A scan
    /// that reaches the cap keeps every plugin it already knew rather than
    /// dropping the ones past the cap. Zero or negative restores the default.
    void setMaxFilesPerScanForTest(int cap);

Q_SIGNALS:
    /// A plugin id came on offer.
    void pluginAdded(const QString& id);
    /// A plugin id left the offer: its file is gone or now refused.
    void pluginRemoved(const QString& id);
    /// After every scan, whether or not anything changed.
    void scanCompleted();

private:
    class ScanStrategy;
    struct Loaded;

    QStringList performScan(const QStringList& directoriesLowestPriorityFirst);
    [[nodiscard]] bool shouldWarn(const QString& key);
    [[nodiscard]] PluginOrigin originOf(const QString& directory) const;

    PluginSeam m_seam;
    QList<PluginSearchPath> m_searchPaths;
    bool m_liveReload = true;
    bool m_registered = false;
    int m_maxFilesPerScan = 0;
    std::unique_ptr<ScanStrategy> m_strategy;
    std::unique_ptr<PhosphorFsLoader::WatchedDirectorySet> m_watcher;
    QHash<QString, PluginMetadata> m_plugins;
    QList<RefusedPlugin> m_refused;
    QHash<QString, std::shared_ptr<Loaded>> m_loaded;
    /// Warn-once keys ("<path>|<reason>"). A key is dropped when its file
    /// stops producing that warning, so a regression warns again.
    QSet<QString> m_warned;
};

} // namespace PhosphorRegistry
