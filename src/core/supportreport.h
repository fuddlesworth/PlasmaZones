// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>

namespace PlasmaZones {

class ScreenManager;
class LayoutManager;
class AutotileEngine;

/**
 * @brief Generates a redacted support report for bug reports and discussions
 *
 * Collects config, screen topology, autotile state, layout info, and recent
 * journal logs into a Markdown-formatted report suitable for pasting into
 * GitHub Discussions or Issues.
 *
 * Privacy:
 * - Home directory paths replaced with ~
 * - No network calls — report is generated locally
 */
class PLASMAZONES_EXPORT SupportReport
{
public:
    /**
     * @brief Generate a complete support report
     * @param screenManager ScreenManager instance (nullable)
     * @param layoutManager LayoutManager instance (nullable)
     * @param autotileEngine AutotileEngine instance (nullable)
     * @param sinceMinutes How many minutes of journal logs to include (default 30, capped at 120)
     * @return Markdown-formatted support report
     */
    static QString generate(ScreenManager* screenManager, LayoutManager* layoutManager, AutotileEngine* autotileEngine,
                            int sinceMinutes = 30);

    /**
     * @brief Redact home directory paths from a string
     * @param input String that may contain the user's home path
     * @return String with home path replaced by ~
     */
    static QString redactHomePath(const QString& input);

private:
    SupportReport() = delete;

    static QString sectionVersion();
    static QString sectionEnvironment();
    static QString sectionScreens(ScreenManager* screenManager);
    static QString readAndRedactFile(const QString& path, const QString& label,
                                     const QString& lang = QStringLiteral("json"));
    static QString sectionConfig();
    static QString sectionLayouts(LayoutManager* layoutManager);
    static QString sectionAutotile(AutotileEngine* autotileEngine);
    static QString sectionSession();
    static QString sectionLogs(int sinceMinutes);
};

} // namespace PlasmaZones
