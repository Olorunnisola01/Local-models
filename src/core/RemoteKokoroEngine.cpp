#include "RemoteKokoroEngine.h"

#include <stdexcept>
#include <thread>

#include <QByteArray>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "../dsp/WavReader.h"

namespace tts {

namespace {

// Long timeout: the Kaggle server may need to warm up CUDA on its first
// request.
constexpr int kSynthesizeTimeoutMs = 120000;
constexpr int kShutdownTimeoutMs = 10000;

// cloudflared quick tunnels occasionally drop/fail individual TLS handshakes
// under concurrent load (multiple chunks synthesized in parallel each open
// their own connection). These errors are transient, so retry a few times
// with a short backoff before giving up.
constexpr int kMaxAttempts = 4;
constexpr int kRetryDelayMs = 750;

std::string stripTrailingSlash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

// Network-level errors (connection drops, SSL handshake failures, timeouts)
// are worth retrying. HTTP-level errors (4xx/5xx with a JSON {"error": ...}
// body from the server) are deterministic and retrying won't help.
bool isRetryableError(QNetworkReply::NetworkError err) {
    switch (err) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::SslHandshakeFailedError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::UnknownNetworkError:
            return true;
        default:
            return false;
    }
}

AudioBuffer postSynthesizeOnce(const std::string& baseUrl, const QJsonObject& body) {
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QString::fromStdString(baseUrl) + "/synthesize"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(kSynthesizeTimeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        throw std::runtime_error("RemoteKokoroEngine: request timed out");
    }

    const QByteArray data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    const std::string errString = reply->errorString().toStdString();
    reply->deleteLater();

    if (err != QNetworkReply::NoError || status != 200) {
        std::string msg = "RemoteKokoroEngine: server request failed";
        if (status != 0) msg += " (HTTP " + std::to_string(status) + ")";
        const QJsonDocument errJson = QJsonDocument::fromJson(data);
        if (errJson.isObject() && errJson.object().contains("error")) {
            msg += ": " + errJson.object().value("error").toString().toStdString();
        } else if (err != QNetworkReply::NoError) {
            msg += ": " + errString;
        }
        // HTTP 5xx (e.g. a transient CUDA out-of-memory error from the
        // Kaggle server) is worth retrying after a short delay, unlike 4xx
        // client errors (bad request, unknown voice, ...) which are
        // deterministic.
        if ((err != QNetworkReply::NoError && isRetryableError(err)) || status >= 500) {
            throw std::runtime_error("RETRYABLE:" + msg);
        }
        throw std::runtime_error(msg);
    }

    AudioBuffer out;
    if (!readWavFromMemory(reinterpret_cast<const uint8_t*>(data.constData()), static_cast<size_t>(data.size()),
                           &out)) {
        // A 200 response with a malformed/truncated WAV body is most likely
        // the same tunnel corruption that causes connection-level errors, so
        // treat it as retryable too.
        throw std::runtime_error("RETRYABLE:RemoteKokoroEngine: failed to parse WAV response from server");
    }
    return out;
}

AudioBuffer postSynthesize(const std::string& baseUrl, const QJsonObject& body) {
    for (int attempt = 1;; ++attempt) {
        try {
            return postSynthesizeOnce(baseUrl, body);
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            const std::string prefix = "RETRYABLE:";
            if (msg.rfind(prefix, 0) != 0 || attempt >= kMaxAttempts) {
                if (msg.rfind(prefix, 0) == 0) msg = msg.substr(prefix.size());
                throw std::runtime_error(msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryDelayMs * attempt));
        }
    }
}

} // namespace

RemoteKokoroEngine::RemoteKokoroEngine(std::string baseUrl) : baseUrl_(stripTrailingSlash(std::move(baseUrl))) {}

AudioBuffer RemoteKokoroEngine::synthesize(const std::string& text, const std::string& voiceShortName,
                                            const std::string& lang, float speed) const {
    QJsonObject body;
    body["text"] = QString::fromStdString(text);
    body["voice"] = QString::fromStdString(voiceShortName);
    body["lang"] = QString::fromStdString(lang);
    body["speed"] = speed;
    return postSynthesize(baseUrl_, body);
}

AudioBuffer RemoteKokoroEngine::synthesizeMixed(const std::string& text, const std::string& voiceAShortName,
                                                 const std::string& voiceBShortName, int pctA,
                                                 const std::string& lang, float speed) const {
    QJsonObject body;
    body["text"] = QString::fromStdString(text);
    body["voice"] = QString::fromStdString(voiceAShortName);
    body["voice_b"] = QString::fromStdString(voiceBShortName);
    body["pct_a"] = pctA;
    body["lang"] = QString::fromStdString(lang);
    body["speed"] = speed;
    return postSynthesize(baseUrl_, body);
}

bool RemoteKokoroEngine::stopSession(const std::string& baseUrl) {
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QString::fromStdString(stripTrailingSlash(baseUrl)) + "/shutdown"));

    QNetworkReply* reply = manager.post(request, QByteArray());

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(kShutdownTimeoutMs);
    loop.exec();

    const bool ok = reply->isFinished() && reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

} // namespace tts
