// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <QStringList>

namespace PlasmaZones {

class Settings;

/**
 * @brief Exclusions sub-KCM — Window filtering, excluded apps and window classes
 */
class KCMExclusions : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(QStringList excludedApplications READ excludedApplications WRITE setExcludedApplications NOTIFY
                   excludedApplicationsChanged)
    Q_PROPERTY(QStringList excludedWindowClasses READ excludedWindowClasses WRITE setExcludedWindowClasses NOTIFY
                   excludedWindowClassesChanged)
    Q_PROPERTY(bool excludeTransientWindows READ excludeTransientWindows WRITE setExcludeTransientWindows NOTIFY
                   excludeTransientWindowsChanged)
    Q_PROPERTY(
        int minimumWindowWidth READ minimumWindowWidth WRITE setMinimumWindowWidth NOTIFY minimumWindowWidthChanged)
    Q_PROPERTY(
        int minimumWindowHeight READ minimumWindowHeight WRITE setMinimumWindowHeight NOTIFY minimumWindowHeightChanged)

public:
    KCMExclusions(QObject* parent, const KPluginMetaData& data);
    ~KCMExclusions() override = default;

    QStringList excludedApplications() const;
    QStringList excludedWindowClasses() const;
    bool excludeTransientWindows() const;
    int minimumWindowWidth() const;
    int minimumWindowHeight() const;

    void setExcludedApplications(const QStringList& apps);
    void setExcludedWindowClasses(const QStringList& classes);
    void setExcludeTransientWindows(bool exclude);
    void setMinimumWindowWidth(int width);
    void setMinimumWindowHeight(int height);

    Q_INVOKABLE void addExcludedApp(const QString& app);
    Q_INVOKABLE void removeExcludedApp(int index);
    Q_INVOKABLE void addExcludedWindowClass(const QString& windowClass);
    Q_INVOKABLE void removeExcludedWindowClass(int index);
    Q_INVOKABLE QVariantList getRunningWindows();

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;
    void onExternalSettingsChanged();

Q_SIGNALS:
    void excludedApplicationsChanged();
    void excludedWindowClassesChanged();
    void excludeTransientWindowsChanged();
    void minimumWindowWidthChanged();
    void minimumWindowHeightChanged();

private:
    void emitAllChanged();

    Settings* m_settings = nullptr;
    bool m_saving = false;
};

} // namespace PlasmaZones
