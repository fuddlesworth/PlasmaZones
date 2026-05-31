// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "ddccontroller.h"

#include <QLoggingCategory>

#include <ddcutil_c_api.h>

Q_LOGGING_CATEGORY(lcDdcController, "phosphor.service.brightness.ddc")

namespace {
constexpr DDCA_Vcp_Feature_Code kLuminanceFeature = 0x10;
} // namespace

namespace PhosphorServiceBrightness {

DdcController::DdcController(QObject* parent)
    : QObject(parent)
{
}

DdcController::~DdcController()
{
    if (m_displayList)
        ddca_free_display_info_list(static_cast<DDCA_Display_Info_List*>(m_displayList));
}

bool DdcController::ensureInitialized()
{
    if (m_initialized)
        return true;
    // ddca_init must run before other ddca calls; do it on the worker thread.
    // Quiet syslog, default config-file handling.
    const DDCA_Status status = ddca_init(nullptr, DDCA_SYSLOG_NEVER, DDCA_INIT_OPTIONS_NONE);
    if (status < 0) {
        qCDebug(lcDdcController) << "ddca_init failed:" << status;
        return false;
    }
    m_initialized = true;
    return true;
}

bool DdcController::readLuminance(void* handle, int& current, int& maxValue)
{
    DDCA_Non_Table_Vcp_Value valrec{};
    const DDCA_Status status =
        ddca_get_non_table_vcp_value(static_cast<DDCA_Display_Handle>(handle), kLuminanceFeature, &valrec);
    if (status != 0)
        return false;
    current = (valrec.sh << 8) | valrec.sl;
    maxValue = (valrec.mh << 8) | valrec.ml;
    return true;
}

void DdcController::enumerate()
{
    if (!ensureInitialized())
        return;

    DDCA_Display_Info_List* list = nullptr;
    const DDCA_Status status = ddca_get_display_info_list2(/*include_invalid=*/false, &list);
    if (status != 0 || !list) {
        qCDebug(lcDdcController) << "ddca_get_display_info_list2 failed:" << status;
        return;
    }
    // Keep the list alive: the dref handles inside it must outlive enumeration
    // so later get/set calls can reuse them.
    m_displayList = list;

    for (int i = 0; i < list->ct; ++i) {
        const DDCA_Display_Info& info = list->info[i];
        const QString id = QString::number(info.dispno);
        DDCA_Display_Handle handle = nullptr;
        if (ddca_open_display2(info.dref, /*wait=*/true, &handle) != 0 || !handle) {
            qCDebug(lcDdcController) << "open failed for display" << id;
            continue;
        }
        int current = 0;
        int maxValue = 0;
        const bool ok = readLuminance(handle, current, maxValue);
        ddca_close_display(handle);
        if (!ok) {
            qCDebug(lcDdcController) << "luminance read failed for display" << id;
            continue;
        }
        m_refs.insert(id, info.dref);
        Q_EMIT displayFound(id, QString::fromUtf8(info.model_name), current, maxValue);
    }
}

void DdcController::setBrightness(const QString& id, int value)
{
    const auto it = m_refs.constFind(id);
    if (it == m_refs.constEnd())
        return;
    DDCA_Display_Handle handle = nullptr;
    if (ddca_open_display2(it.value(), /*wait=*/true, &handle) != 0 || !handle) {
        qCDebug(lcDdcController) << "open failed for set on display" << id;
        return;
    }
    const DDCA_Status status = ddca_set_non_table_vcp_value(
        handle, kLuminanceFeature, static_cast<uint8_t>((value >> 8) & 0xff), static_cast<uint8_t>(value & 0xff));
    if (status != 0)
        qCDebug(lcDdcController) << "set luminance failed for display" << id << ":" << status;
    // Read back the applied value (DDC clamps to its own range) and report it.
    int current = 0;
    int maxValue = 0;
    if (readLuminance(handle, current, maxValue))
        Q_EMIT brightnessRead(id, current);
    ddca_close_display(handle);
}

void DdcController::refresh(const QString& id)
{
    const auto it = m_refs.constFind(id);
    if (it == m_refs.constEnd())
        return;
    DDCA_Display_Handle handle = nullptr;
    if (ddca_open_display2(it.value(), /*wait=*/true, &handle) != 0 || !handle)
        return;
    int current = 0;
    int maxValue = 0;
    if (readLuminance(handle, current, maxValue))
        Q_EMIT brightnessRead(id, current);
    ddca_close_display(handle);
}

} // namespace PhosphorServiceBrightness
