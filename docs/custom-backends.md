# Writing Custom Backends

PlasmaZones uses pluggable backend interfaces for config, shortcuts,
and wallpaper.  This guide explains how to implement custom backends
for integrating with other shells and environments (Quickshell,
niri, etc.).

## IConfigBackend — Configuration storage

**Header:** `src/config/configbackend.h`
**Default:** `QSettingsConfigBackend` (INI file at `~/.config/plasmazonesrc`)

### Interface

```cpp
class IConfigBackend {
public:
    virtual ~IConfigBackend() = default;
    virtual std::unique_ptr<ConfigGroup> group(const QString& name) = 0;
    virtual void reparseConfiguration() = 0;
    virtual void sync() = 0;
    virtual void deleteGroup(const QString& name) = 0;
    virtual QStringList groupList() const = 0;
};

class ConfigGroup {
public:
    virtual ~ConfigGroup() = default;
    virtual QString readString(const QString& key, const QString& defaultValue = {}) const = 0;
    virtual int readInt(const QString& key, int defaultValue = 0) const = 0;
    virtual bool readBool(const QString& key, bool defaultValue = false) const = 0;
    virtual double readDouble(const QString& key, double defaultValue = 0.0) const = 0;
    virtual QColor readColor(const QString& key, const QColor& defaultValue = {}) const = 0;
    virtual void writeString(const QString& key, const QString& value) = 0;
    virtual void writeInt(const QString& key, int value) = 0;
    virtual void writeBool(const QString& key, bool value) = 0;
    virtual void writeDouble(const QString& key, double value) = 0;
    virtual void writeColor(const QString& key, const QColor& value) = 0;
    virtual bool hasKey(const QString& key) const = 0;
};
```

### Example: JSON config backend

```cpp
#include "configbackend.h"
#include <QJsonDocument>
#include <QJsonObject>

class JsonConfigBackend : public IConfigBackend {
    QJsonObject m_root;
    QString m_filePath;

public:
    explicit JsonConfigBackend(const QString& path) : m_filePath(path) {
        reparseConfiguration();
    }

    std::unique_ptr<ConfigGroup> group(const QString& name) override {
        return std::make_unique<JsonConfigGroup>(&m_root, name);
    }

    void reparseConfiguration() override {
        QFile f(m_filePath);
        if (f.open(QIODevice::ReadOnly))
            m_root = QJsonDocument::fromJson(f.readAll()).object();
    }

    void sync() override {
        QFile f(m_filePath);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(m_root).toJson());
    }

    void deleteGroup(const QString& name) override {
        m_root.remove(name);
    }

    QStringList groupList() const override {
        return m_root.keys();
    }
};
```

### Wiring it up

The `Settings` class accepts an `IConfigBackend*` via constructor:

```cpp
auto backend = std::make_unique<JsonConfigBackend>("/path/to/config.json");
auto settings = new Settings(std::move(backend), parent);
```

Or replace the factory in `configbackend_qsettings.cpp`:

```cpp
std::unique_ptr<IConfigBackend> createDefaultConfigBackend() {
    return std::make_unique<YourCustomBackend>(...);
}
```

---

## IShortcutBackend — Global keyboard shortcuts

**Header:** `src/daemon/shortcutbackend.h`
**Default:** Auto-detected (KGlobalAccel → Portal → D-Bus trigger)

### Interface

```cpp
class IShortcutBackend : public QObject {
    Q_OBJECT
public:
    virtual void setDefaultShortcut(QAction* action, const QKeySequence& defaultShortcut) = 0;
    virtual void setShortcut(QAction* action, const QKeySequence& shortcut) = 0;
    virtual void setGlobalShortcut(QAction* action, const QKeySequence& shortcut) = 0;
    virtual void removeAllShortcuts(QAction* action) = 0;
    virtual void flush() = 0;

Q_SIGNALS:
    void shortcutsReady();
};
```

### Example: Compositor IPC backend

For compositors with their own shortcut API (like niri or Quickshell):

```cpp
class NiriShortcutBackend : public IShortcutBackend {
    Q_OBJECT
    QHash<QString, QAction*> m_actions;

public:
    explicit NiriShortcutBackend(QObject* parent) : IShortcutBackend(parent) {
        // Connect to niri's IPC socket
        connectToNiriSocket();
    }

    void setGlobalShortcut(QAction* action, const QKeySequence& shortcut) override {
        m_actions.insert(action->objectName(), action);
        // Send bind command via niri IPC
        sendNiriCommand("bind", action->objectName(), shortcut.toString());
    }

    void removeAllShortcuts(QAction* action) override {
        m_actions.remove(action->objectName());
        sendNiriCommand("unbind", action->objectName());
    }

    void flush() override { Q_EMIT shortcutsReady(); }

private:
    void onShortcutActivated(const QString& id) {
        if (auto* action = m_actions.value(id))
            action->trigger();
    }
};
```

### Wiring it up

Replace the factory in `shortcutbackend.cpp`:

```cpp
std::unique_ptr<IShortcutBackend> createShortcutBackend(QObject* parent) {
    // Add detection for your compositor before the fallback
    if (isNiriRunning())
        return std::make_unique<NiriShortcutBackend>(parent);

    // ... existing portal/trigger detection
}
```

---

## IWallpaperProvider — Desktop wallpaper path

**Header:** `src/core/wallpaperprovider.h`
**Default:** Auto-detected (Plasma → Hyprland → Sway → GNOME → null)

### Interface

```cpp
class IWallpaperProvider {
public:
    virtual ~IWallpaperProvider() = default;
    virtual QString wallpaperPath() = 0;
};
```

### Example: Quickshell wallpaper provider

```cpp
class QuickshellWallpaperProvider : public IWallpaperProvider {
public:
    QString wallpaperPath() override {
        // Read from Quickshell's config or IPC
        QSettings qs("~/.config/quickshell/config.ini", QSettings::IniFormat);
        return qs.value("wallpaper/path").toString();
    }
};
```

### Wiring it up

Add to the factory in `wallpaperprovider.cpp`:

```cpp
std::unique_ptr<IWallpaperProvider> createWallpaperProvider() {
    if (isQuickshellRunning())
        return std::make_unique<QuickshellWallpaperProvider>();
    // ... existing detection
}
```

---

## Adding a new compositor

To add full support for a new compositor:

1. **Shortcuts:** Add detection in `createShortcutBackend()` factory, or
   document the D-Bus trigger method for users.

2. **Config:** The default INI backend works everywhere. Only implement a
   custom backend if the compositor has its own config system you want
   to integrate with.

3. **Wallpaper:** Add a provider to `createWallpaperProvider()` if the
   compositor has a queryable wallpaper path.

4. **Autostart:** Document the compositor's autostart mechanism.

5. **Testing:** Test drag-to-snap with LayerShell overlays, shortcut
   activation, and config persistence.

Submit a PR with the new backend(s) and documentation!
