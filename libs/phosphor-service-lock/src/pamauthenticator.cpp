// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "pamauthenticator.h"

#include <QtConcurrent>

#include <security/pam_appl.h>

#include <cstdlib>
#include <cstring>
#include <utility>

namespace PhosphorServiceLock {

namespace {

// Payload handed to the PAM conversation callback: a pointer to the (still
// owned, null-terminated) password bytes to answer the module's echo-off prompt
// with. Valid for the synchronous duration of pam_authenticate().
struct ConvData
{
    const char* password = nullptr;
};

void wipe(QByteArray& bytes)
{
    if (!bytes.isEmpty())
        explicit_bzero(bytes.data(), static_cast<size_t>(bytes.size()));
}

// PAM conversation callback. PAM drives authentication by sending prompts; for
// password auth it asks once with PAM_PROMPT_ECHO_OFF, which we answer with the
// supplied password. PAM takes ownership of (and frees) every `resp` string, so
// each must be a fresh malloc'd copy.
int pamConversation(int numMsg, const struct pam_message** msg, struct pam_response** resp, void* appdataPtr)
{
    if (numMsg <= 0 || numMsg > PAM_MAX_NUM_MSG || !msg || !resp || !appdataPtr)
        return PAM_CONV_ERR;

    auto* data = static_cast<ConvData*>(appdataPtr);
    auto* replies = static_cast<struct pam_response*>(calloc(static_cast<size_t>(numMsg), sizeof(struct pam_response)));
    if (!replies)
        return PAM_BUF_ERR;

    const auto unwind = [&](int upTo) {
        for (int j = 0; j < upTo; ++j) {
            if (replies[j].resp) {
                explicit_bzero(replies[j].resp, strlen(replies[j].resp));
                free(replies[j].resp);
            }
        }
        free(replies);
    };

    for (int i = 0; i < numMsg; ++i) {
        switch (msg[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF: {
            // The password prompt. strdup so PAM can free it; on OOM unwind.
            char* answer = strdup(data->password ? data->password : "");
            if (!answer) {
                unwind(i);
                return PAM_BUF_ERR;
            }
            replies[i].resp = answer;
            replies[i].resp_retcode = 0;
            break;
        }
        case PAM_PROMPT_ECHO_ON:
            // A visible prompt (e.g. a re-asked username). The user is already
            // fixed via pam_start, so answer empty.
            replies[i].resp = strdup("");
            replies[i].resp_retcode = 0;
            break;
        case PAM_ERROR_MSG:
        case PAM_TEXT_INFO:
            // Informational; no response. The module may log these itself.
            replies[i].resp = nullptr;
            replies[i].resp_retcode = 0;
            break;
        default:
            unwind(i + 1);
            return PAM_CONV_ERR;
        }
    }

    *resp = replies;
    return PAM_SUCCESS;
}

QString pamError(pam_handle_t* handle, int code)
{
    return QString::fromUtf8(pam_strerror(handle, code));
}

// Run one full PAM transaction. Takes sole ownership of @p password and wipes
// its buffer before returning. Never logs the password.
PamResult runPamTransaction(const QString& service, const QString& username, QByteArray password)
{
    ConvData convData{password.constData()}; // constData() does not detach: sole owner is preserved
    const struct pam_conv conv = {pamConversation, &convData};

    pam_handle_t* handle = nullptr;
    int ret = pam_start(service.toLocal8Bit().constData(), username.toLocal8Bit().constData(), &conv, &handle);
    if (ret != PAM_SUCCESS) {
        const QString reason = pamError(handle, ret);
        if (handle)
            pam_end(handle, ret);
        wipe(password);
        return {false, reason};
    }

    const int authRet = pam_authenticate(handle, 0);
    // Even with correct credentials the account may be unusable (expired,
    // locked, password-change required); gate success on acct_mgmt too.
    const int acctRet = (authRet == PAM_SUCCESS) ? pam_acct_mgmt(handle, 0) : authRet;

    const bool success = (authRet == PAM_SUCCESS && acctRet == PAM_SUCCESS);
    const QString reason = success ? QString() : pamError(handle, authRet != PAM_SUCCESS ? authRet : acctRet);

    pam_end(handle, success ? PAM_SUCCESS : authRet);
    wipe(password);
    return {success, reason};
}

} // namespace

PamAuthenticator::PamAuthenticator(QString service, QObject* parent)
    : IAuthenticator(parent)
    , m_service(std::move(service))
{
    // QFutureWatcher::finished is delivered on this (the GUI) thread, so reading
    // the result and emitting here is thread-safe.
    connect(&m_watcher, &QFutureWatcher<PamResult>::finished, this, [this] {
        if (m_watcher.future().resultCount() == 0)
            return; // cancelled / no result
        const PamResult result = m_watcher.result();
        if (result.success)
            Q_EMIT succeeded();
        else
            Q_EMIT failed(result.reason);
    });
}

PamAuthenticator::~PamAuthenticator() = default;

QString PamAuthenticator::service() const
{
    return m_service;
}

void PamAuthenticator::authenticate(const QString& username, const QString& password)
{
    if (m_watcher.isRunning()) {
        // One transaction at a time: never run two PAM stacks concurrently.
        Q_EMIT failed(QStringLiteral("authentication already in progress"));
        return;
    }
    if (username.isEmpty()) {
        Q_EMIT failed(QStringLiteral("no user to authenticate"));
        return;
    }

    const QString service = m_service;
    QByteArray pw = password.toUtf8();
    // The worker captures only value copies (service, user, and the moved-in
    // password buffer) — never `this` — so destroying the authenticator
    // mid-transaction cannot dangle: the watcher disconnects and the detached
    // task finishes harmlessly. The password buffer is moved through to
    // runPamTransaction, which is its sole owner and wipes it.
    m_watcher.setFuture(QtConcurrent::run([service, user = username, pw = std::move(pw)]() mutable -> PamResult {
        return runPamTransaction(service, user, std::move(pw));
    }));
}

} // namespace PhosphorServiceLock
