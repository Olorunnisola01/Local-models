#include "RemoteFishSpeechEngine.h"

#include <stdexcept>
#include <thread>

#include <QByteArray>
#include <QEventLoop>
#include <QJsonArray>
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

// Fish Audio S2 Pro can be slow on first inference (model warm-up + large
// reference audio encoding).  Use a generous timeout.
constexpr int kSynthesizeTimeoutMs = 180000; // 3 min
constexpr int kEncodeTimeoutMs = 60000;      // 1 min
constexpr int kShutdownTimeoutMs = 10000;

// cloudflared tunnels occasionally drop TLS connections under concurrent load.
// Retry a few times with backoff before giving up.
constexpr int kMaxAttempts = 4;
constexpr int kRetryDelayMs = 750;

std::string stripTrailingSlash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

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

// Shared POST helper: sends `body` to `url`, waits up to `timeoutMs`, returns
// the raw response bytes on HTTP 200.  Throws on error (prefixes "RETRYABLE:"
// for transient failures so the retry loop can distinguish them).
QByteArray postRequest(const QString& url, const QByteArray& bodyBytes,
                        const QString& contentType, int timeoutMs) {
    QNetworkAccessManager manager;
    const QUrl qurl{url};
    QNetworkRequest request{qurl};
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);

    QNetworkReply* reply = manager.post(request, bodyBytes);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        throw std::runtime_error("RETRYABLE:RemoteFishSpeechEngine: request timed out");
    }

    const QByteArray data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    const std::string errStr = reply->errorString().toStdString();
    reply->deleteLater();

    if (err != QNetworkReply::NoError || status != 200) {
        std::string msg = "RemoteFishSpeechEngine: request failed";
        if (status != 0) msg += " (HTTP " + std::to_string(status) + ")";
        const QJsonDocument errJson = QJsonDocument::fromJson(data);
        if (errJson.isObject() && errJson.object().contains("detail")) {
            msg += ": " + errJson.object().value("detail").toString().toStdString();
        } else if (errJson.isObject() && errJson.object().contains("error")) {
            msg += ": " + errJson.object().value("error").toString().toStdString();
        } else if (err != QNetworkReply::NoError) {
            msg += ": " + errStr;
        }
        if ((err != QNetworkReply::NoError && isRetryableError(err)) || status >= 500) {
            throw std::runtime_error("RETRYABLE:" + msg);
        }
        throw std::runtime_error(msg);
    }
    return data;
}

AudioBuffer postTtsOnce(const std::string& baseUrl, const QJsonObject& body) {
    const QByteArray data = postRequest(
        QString::fromStdString(baseUrl) + "/v1/tts",
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        "application/json",
        kSynthesizeTimeoutMs);

    AudioBuffer out;
    if (!readWavFromMemory(reinterpret_cast<const uint8_t*>(data.constData()),
                           static_cast<size_t>(data.size()), &out)) {
        throw std::runtime_error(
            "RETRYABLE:RemoteFishSpeechEngine: failed to parse WAV response");
    }
    return out;
}

AudioBuffer postTtsWithRetry(const std::string& baseUrl, const QJsonObject& body) {
    for (int attempt = 1;; ++attempt) {
        try {
            return postTtsOnce(baseUrl, body);
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

RemoteFishSpeechEngine::RemoteFishSpeechEngine(std::string baseUrl)
    : baseUrl_(stripTrailingSlash(std::move(baseUrl))) {}

AudioBuffer RemoteFishSpeechEngine::synthesize(const std::string& text,
                                                const std::string& refAudioB64,
                                                const std::string& refText,
                                                float speed) const {
    QJsonObject body;
    body["text"]              = QString::fromStdString(text);
    body["format"]            = QString("wav");
    body["streaming"]         = false;
    body["speed"]             = static_cast<double>(speed);
    body["top_p"]             = 0.7;
    body["temperature"]       = 0.7;
    body["repetition_penalty"]= 1.2;
    body["max_new_tokens"]    = 1024;
    body["chunk_length"]      = 200;

    if (!refAudioB64.empty()) {
        QJsonObject ref;
        ref["audio"] = QString::fromStdString(refAudioB64);
        if (!refText.empty()) {
            ref["text"] = QString::fromStdString(refText);
        }
        body["references"] = QJsonArray{ref};
    }

    return postTtsWithRetry(baseUrl_, body);
}

AudioBuffer RemoteFishSpeechEngine::synthesizeWithReferenceJson(const std::string& text,
                                                                 const std::string& referenceJson,
                                                                 float speed) const {
    QJsonObject body;
    body["text"] = QString::fromStdString(text);
    body["format"] = QString("wav");
    body["streaming"] = false;
    body["speed"] = static_cast<double>(speed);
    body["top_p"] = 0.7;
    body["temperature"] = 0.7;
    body["repetition_penalty"] = 1.2;
    body["max_new_tokens"] = 1024;
    body["chunk_length"] = 200;

    const QJsonDocument refDoc = QJsonDocument::fromJson(QByteArray::fromStdString(referenceJson));
    if (refDoc.isObject()) {
        body["references"] = QJsonArray{refDoc.object()};
    }

    return postTtsWithRetry(baseUrl_, body);
}

std::string RemoteFishSpeechEngine::extractTokens(const std::string& refAudioB64) const {
    QJsonObject body;
    body["audio"] = QString::fromStdString(refAudioB64);

    const QByteArray data = postRequest(
        QString::fromStdString(baseUrl_) + "/v1/models/vqgan/encode",
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        "application/json",
        kEncodeTimeoutMs);

    return data.toStdString();
}

bool RemoteFishSpeechEngine::stopSession(const std::string& baseUrl) {
    QNetworkAccessManager manager;
    QNetworkRequest request(
        QUrl(QString::fromStdString(stripTrailingSlash(baseUrl)) + "/shutdown"));

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
