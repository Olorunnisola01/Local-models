#include "EdgeTtsEngine.h"

#include "TextMarkup.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QBuffer>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMap>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <QUuid>

#ifdef Q_OS_WIN
#include <QtCore/private/qeventdispatcher_win_p.h>
#endif

namespace tts {

namespace {

constexpr const char* kHost = "speech.platform.bing.com";
constexpr const char* kPathBase = "/consumer/speech/synthesize/readaloud/edge/v1";
constexpr const char* kTrustedClientToken = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";
constexpr const char* kSecMsGecVersion = "1-143.0.3650.75";
constexpr const char* kChromeUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0";

// WebSocket opcodes (RFC6455).
constexpr quint8 kOpContinuation = 0x0;
constexpr quint8 kOpText = 0x1;
constexpr quint8 kOpBinary = 0x2;
constexpr quint8 kOpClose = 0x8;
constexpr quint8 kOpPing = 0x9;
constexpr quint8 kOpPong = 0xA;

// Returns a UUID with no dashes, as used by edge-tts for X-RequestId/ConnectionId.
QString connectId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'));
}

// Ported from edge-tts drm.py: DRM.generate_sec_ms_gec().
QString generateSecMsGec() {
    constexpr qint64 kWinEpoch = 11644473600LL; // seconds between 1601-01-01 and 1970-01-01
    qint64 ticks = QDateTime::currentSecsSinceEpoch() + kWinEpoch;
    ticks -= ticks % 300; // round down to nearest 5 minutes
    ticks *= 10000000LL;  // convert seconds to 100-ns Windows filetime ticks

    const QString strToHash = QString::number(ticks) + QLatin1String(kTrustedClientToken);
    const QByteArray hash = QCryptographicHash::hash(strToHash.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex()).toUpper();
}

// Ported from edge-tts drm.py: DRM.generate_muid() -- random 16 bytes, hex, uppercase.
QString generateMuid() {
    QByteArray bytes(16, '\0');
    for (char& b : bytes) {
        b = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return QString::fromLatin1(bytes.toHex()).toUpper();
}

// Ported from edge-tts communicate.py: date_to_string() -- JS-style date string.
QString dateToString() {
    static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDate d = now.date();
    const QTime t = now.time();
    return QString("%1 %2 %3 %4 %5:%6:%7 GMT+0000 (Coordinated Universal Time)")
        .arg(kDays[d.dayOfWeek() % 7])
        .arg(kMonths[d.month() - 1])
        .arg(d.day(), 2, 10, QLatin1Char('0'))
        .arg(d.year())
        .arg(t.hour(), 2, 10, QLatin1Char('0'))
        .arg(t.minute(), 2, 10, QLatin1Char('0'))
        .arg(t.second(), 2, 10, QLatin1Char('0'));
}

QString escapeXml(const QString& s) {
    QString out = s;
    out.replace(QLatin1String("&"), QLatin1String("&amp;"));
    out.replace(QLatin1String("<"), QLatin1String("&lt;"));
    out.replace(QLatin1String(">"), QLatin1String("&gt;"));
    return out;
}

// Maps a 0.5..2.0 speed multiplier onto an SSML <prosody rate='+N%'> value.
QString rateFromSpeed(float speed) {
    const int pct = static_cast<int>(std::lround((speed - 1.0f) * 100.0f));
    return (pct >= 0 ? QLatin1String("+") : QLatin1String("")) + QString::number(pct) + QLatin1String("%");
}

// Builds the SSML body (the content of <voice>...</voice>) for `text`.
// Always uses a single flat <prosody> span — the server rejects nested or
// repeated spans, so any naturalisation is handled by the DSP post-pass
// in applyHumanizer() rather than via SSML.
QString buildSsmlBody(const QString& text, float speed) {
    return QString("<prosody pitch='+0Hz' rate='%1' volume='+0%'>%2</prosody>")
        .arg(rateFromSpeed(speed), escapeXml(text));
}

// Encodes a client->server WebSocket frame. Per RFC6455, client frames must be masked.
QByteArray encodeFrame(const QByteArray& payload, quint8 opcode) {
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode)); // FIN=1, opcode

    const quint64 len = static_cast<quint64>(payload.size());
    if (len <= 125) {
        frame.append(static_cast<char>(0x80 | len));
    } else if (len <= 0xFFFF) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i) {
            frame.append(static_cast<char>((len >> (8 * i)) & 0xFF));
        }
    }

    quint8 mask[4];
    for (quint8& m : mask) {
        m = static_cast<quint8>(QRandomGenerator::global()->bounded(256));
    }
    frame.append(reinterpret_cast<const char*>(mask), 4);

    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i) {
        masked[i] = static_cast<char>(static_cast<quint8>(masked[i]) ^ mask[i % 4]);
    }
    frame.append(masked);
    return frame;
}

struct WsFrame {
    quint8 opcode = 0;
    bool fin = true;
    QByteArray payload;
};

// Attempts to parse one (unmasked) server->client frame from `buf`, removing
// the consumed bytes on success. Returns false if more data is needed.
bool tryParseFrame(QByteArray& buf, WsFrame& out) {
    if (buf.size() < 2) return false;

    const quint8 b0 = static_cast<quint8>(buf[0]);
    const quint8 b1 = static_cast<quint8>(buf[1]);
    const quint8 opcode = b0 & 0x0F;
    const bool fin = (b0 & 0x80) != 0;
    const bool masked = (b1 & 0x80) != 0;
    quint64 len = b1 & 0x7F;
    int pos = 2;

    if (len == 126) {
        if (buf.size() < 4) return false;
        len = (static_cast<quint8>(buf[2]) << 8) | static_cast<quint8>(buf[3]);
        pos = 4;
    } else if (len == 127) {
        if (buf.size() < 10) return false;
        len = 0;
        for (int i = 0; i < 8; ++i) {
            len = (len << 8) | static_cast<quint8>(buf[2 + i]);
        }
        pos = 10;
    }

    const int maskLen = masked ? 4 : 0;
    if (static_cast<quint64>(buf.size()) < static_cast<quint64>(pos) + maskLen + len) {
        return false;
    }

    QByteArray payload = buf.mid(pos + maskLen, static_cast<int>(len));
    if (masked) {
        quint8 mask[4];
        for (int i = 0; i < 4; ++i) mask[i] = static_cast<quint8>(buf[pos + i]);
        for (int i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(static_cast<quint8>(payload[i]) ^ mask[i % 4]);
        }
    }

    out.opcode = opcode;
    out.fin = fin;
    out.payload = payload;
    buf.remove(0, pos + maskLen + static_cast<int>(len));
    return true;
}

QMap<QByteArray, QByteArray> parseHeaderBlock(const QByteArray& block) {
    QMap<QByteArray, QByteArray> headers;
    for (const QByteArray& rawLine : block.split('\n')) {
        QByteArray line = rawLine;
        if (line.endsWith('\r')) line.chop(1);
        const int idx = line.indexOf(':');
        if (idx < 0) continue;
        headers.insert(line.left(idx), line.mid(idx + 1));
    }
    return headers;
}

// Edge rejects several control characters; replace them with spaces (edge-tts behaviour).
std::string sanitizeForEdgeTts(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if ((c <= 8) || (c >= 11 && c <= 12) || (c >= 14 && c <= 31)) {
            out.push_back(' ');
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

void ensureEventDispatcher() {
    if (QThread::currentThread()->eventDispatcher() != nullptr) {
        return;
    }
#ifdef Q_OS_WIN
    QThread::currentThread()->setEventDispatcher(new QEventDispatcherWin32);
#endif
}

void throwIfCancelled(const std::atomic<bool>* cancelFlag, QSslSocket* socket = nullptr) {
    if (cancelFlag != nullptr && cancelFlag->load()) {
        if (socket != nullptr) {
            socket->abort();
        }
        throw std::runtime_error("Synthesis cancelled.");
    }
}

} // namespace

AudioBuffer EdgeTtsEngine::synthesize(const std::string& text, const std::string& voiceShortName, float speed,
                                       bool humanize, int streamTimeoutMs,
                                       const std::atomic<bool>* cancelFlag) const {
    AudioBuffer result;
    result.sampleRate = 24000;

    throwIfCancelled(cancelFlag);

    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyNone);
    QObject::connect(&socket, &QSslSocket::sslErrors, [&](const QList<QSslError>&) {
        socket.ignoreSslErrors();
    });

    socket.connectToHostEncrypted(QString::fromLatin1(kHost), 443);
    if (!socket.waitForEncrypted(15000)) {
        throw std::runtime_error("EdgeTtsEngine: TLS connection failed: " + socket.errorString().toStdString());
    }
    throwIfCancelled(cancelFlag, &socket);

    // --- HTTP Upgrade handshake -------------------------------------------------
    QByteArray keyBytes(16, '\0');
    for (char& b : keyBytes) b = static_cast<char>(QRandomGenerator::global()->bounded(256));
    const QString wsKey = QString::fromLatin1(keyBytes.toBase64());

    const QString path = QString("%1?TrustedClientToken=%2&ConnectionId=%3&Sec-MS-GEC=%4&Sec-MS-GEC-Version=%5")
                              .arg(QLatin1String(kPathBase), QLatin1String(kTrustedClientToken), connectId(),
                                   generateSecMsGec(), QLatin1String(kSecMsGecVersion));

    QString request;
    request += "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + QString::fromLatin1(kHost) + "\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Pragma: no-cache\r\n";
    request += "Cache-Control: no-cache\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Origin: chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += QString("User-Agent: %1\r\n").arg(QLatin1String(kChromeUserAgent));
    request += "Accept-Encoding: gzip, deflate, br, zstd\r\n";
    request += "Accept-Language: en-US,en;q=0.9\r\n";
    request += "Sec-WebSocket-Key: " + wsKey + "\r\n";
    request += "Cookie: muid=" + generateMuid() + ";\r\n";
    request += "\r\n";

    socket.write(request.toUtf8());

    QByteArray buf;
    {
        QElapsedTimer handshakeTimer;
        handshakeTimer.start();
        while (!buf.contains("\r\n\r\n") && handshakeTimer.elapsed() < 15000) {
            throwIfCancelled(cancelFlag, &socket);
            if (socket.waitForReadyRead(200)) {
                buf += socket.readAll();
            } else if (socket.error() != QAbstractSocket::UnknownSocketError &&
                       socket.error() != QAbstractSocket::SocketTimeoutError) {
                break;
            }
        }
    }
    throwIfCancelled(cancelFlag, &socket);

    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        throw std::runtime_error("EdgeTtsEngine: no response to WebSocket handshake");
    }
    const QByteArray statusLine = buf.left(buf.indexOf("\r\n"));
    if (!statusLine.contains(" 101")) {
        throw std::runtime_error("EdgeTtsEngine: WebSocket handshake rejected: " + statusLine.toStdString());
    }
    buf.remove(0, headerEnd + 4); // discard HTTP headers; keep any leftover WS frame bytes

    // --- Send speech.config + ssml messages -------------------------------------
    const QString speechConfig =
        QString("X-Timestamp:%1\r\n"
                "Content-Type:application/json; charset=utf-8\r\n"
                "Path:speech.config\r\n\r\n"
                "{\"context\":{\"synthesis\":{\"audio\":{\"metadataoptions\":{"
                "\"sentenceBoundaryEnabled\":\"false\",\"wordBoundaryEnabled\":\"false\"},"
                "\"outputFormat\":\"audio-24khz-48kbitrate-mono-mp3\"}}}}\r\n")
            .arg(dateToString());
    socket.write(encodeFrame(speechConfig.toUtf8(), kOpText));

    // Markup tags like [emph]/*...*/ inside <prosody> can hang or break the service.
    // Pause markers are already split into separate chunks by the UI; strip the rest.
    const std::string speakable = sanitizeForEdgeTts(stripMarkup(text));
    const QString voiceBody = buildSsmlBody(QString::fromStdString(speakable), speed);
    const QString ssml = QString("<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' "
                                  "xml:lang='en-US'><voice name='%1'>%2</voice></speak>")
                              .arg(QString::fromStdString(voiceShortName), voiceBody);
    const QString ssmlMessage = QString("X-RequestId:%1\r\n"
                                         "Content-Type:application/ssml+xml\r\n"
                                         "X-Timestamp:%2Z\r\n"
                                         "Path:ssml\r\n\r\n%3")
                                     .arg(connectId(), dateToString(), ssml);
    socket.write(encodeFrame(ssmlMessage.toUtf8(), kOpText));

    // --- Receive audio frames until turn.end -------------------------------------
    QByteArray mp3Data;
    bool turnEnded = false;
    QString streamError;

    quint8 fragOpcode = 0xFF;
    QByteArray fragBuf;

    auto handleMessage = [&](quint8 opcode, const QByteArray& payload) {
        if (opcode == kOpText) {
            const int sep = payload.indexOf("\r\n\r\n");
            const QByteArray headerBlock = sep >= 0 ? payload.left(sep) : payload;
            const auto headers = parseHeaderBlock(headerBlock);
            const QByteArray path = headers.value("Path").trimmed();
            if (path == "turn.end") {
                turnEnded = true;
            } else if (path != "response" && path != "turn.start" && path != "audio.metadata") {
                // Unknown text message; ignore.
            }
        } else if (opcode == kOpBinary) {
            if (payload.size() < 2) return;
            const int headerLen = (static_cast<quint8>(payload[0]) << 8) | static_cast<quint8>(payload[1]);
            if (headerLen > payload.size() - 2) return;
            const QByteArray headerBlock = payload.mid(2, headerLen);
            const QByteArray data = payload.mid(2 + headerLen);
            const auto headers = parseHeaderBlock(headerBlock);
            if (headers.value("Path").trimmed() == "audio" && !data.isEmpty()) {
                mp3Data += data;
            }
        } else if (opcode == kOpPing) {
            socket.write(encodeFrame(payload, kOpPong));
        } else if (opcode == kOpClose) {
            if (payload.size() >= 2) {
                const quint16 closeCode =
                    (static_cast<quint8>(payload[0]) << 8) | static_cast<quint8>(payload[1]);
                const QByteArray reason = payload.mid(2);
                if (!turnEnded && streamError.isEmpty()) {
                    streamError = QString("server closed connection (code %1): %2")
                                       .arg(closeCode)
                                       .arg(QString::fromUtf8(reason));
                }
            }
        }
    };

    auto processBuf = [&]() {
        WsFrame frame;
        while (tryParseFrame(buf, frame)) {
            if (frame.fin) {
                const quint8 opcode = (frame.opcode == kOpContinuation) ? fragOpcode : frame.opcode;
                QByteArray payload = fragBuf + frame.payload;
                fragBuf.clear();
                fragOpcode = 0xFF;
                handleMessage(opcode, payload);
                if (turnEnded) break;
            } else {
                if (frame.opcode != kOpContinuation) fragOpcode = frame.opcode;
                fragBuf += frame.payload;
            }
        }
    };

    processBuf();

    QElapsedTimer streamTimer;
    streamTimer.start();
    while (!turnEnded && streamError.isEmpty() && streamTimer.elapsed() < streamTimeoutMs) {
        throwIfCancelled(cancelFlag, &socket);
        if (socket.state() != QAbstractSocket::ConnectedState) {
            if (streamError.isEmpty()) {
                streamError = "connection closed before turn.end";
            }
            break;
        }
        if (socket.waitForReadyRead(200)) {
            buf += socket.readAll();
            processBuf();
        } else if (socket.error() != QAbstractSocket::UnknownSocketError &&
                   socket.error() != QAbstractSocket::SocketTimeoutError) {
            streamError = socket.errorString();
            break;
        }
    }
    if (!turnEnded && streamError.isEmpty() && streamTimer.elapsed() >= streamTimeoutMs) {
        streamError = "timed out waiting for audio";
    }
    throwIfCancelled(cancelFlag, &socket);

    socket.write(encodeFrame(QByteArray(), kOpClose));
    socket.flush();
    socket.disconnectFromHost();

    if (!turnEnded) {
        throw std::runtime_error("EdgeTtsEngine: " +
                                  (streamError.isEmpty() ? std::string("stream ended before turn.end")
                                                          : streamError.toStdString()));
    }
    if (mp3Data.isEmpty()) {
        throw std::runtime_error("EdgeTtsEngine: no audio data received");
    }

    // --- Decode MP3 -> mono float32 PCM ------------------------------------------
    QBuffer mp3Buffer(&mp3Data);
    mp3Buffer.open(QIODevice::ReadOnly);

    QAudioDecoder decoder;
    decoder.setSourceDevice(&mp3Buffer);

    std::vector<float> samples;
    int outSampleRate = 24000;
    QString decodeError;

    auto appendBuffer = [&](const QAudioBuffer& audioBuffer) {
        const QAudioFormat fmt = audioBuffer.format();
        outSampleRate = fmt.sampleRate();
        const int frameCount = audioBuffer.frameCount();
        const int channels = std::max(1, fmt.channelCount());

        if (fmt.sampleFormat() == QAudioFormat::Float) {
            const float* data = audioBuffer.constData<float>();
            for (int i = 0; i < frameCount; ++i) {
                float sum = 0.0f;
                for (int c = 0; c < channels; ++c) sum += data[i * channels + c];
                samples.push_back(sum / static_cast<float>(channels));
            }
        } else if (fmt.sampleFormat() == QAudioFormat::Int16) {
            const qint16* data = audioBuffer.constData<qint16>();
            for (int i = 0; i < frameCount; ++i) {
                int sum = 0;
                for (int c = 0; c < channels; ++c) sum += data[i * channels + c];
                samples.push_back((sum / static_cast<float>(channels)) / 32768.0f);
            }
        } else if (fmt.sampleFormat() == QAudioFormat::Int32) {
            const qint32* data = audioBuffer.constData<qint32>();
            for (int i = 0; i < frameCount; ++i) {
                qint64 sum = 0;
                for (int c = 0; c < channels; ++c) sum += data[i * channels + c];
                samples.push_back(static_cast<float>(sum / channels) / 2147483648.0f);
            }
        } else if (fmt.sampleFormat() == QAudioFormat::UInt8) {
            const quint8* data = audioBuffer.constData<quint8>();
            for (int i = 0; i < frameCount; ++i) {
                int sum = 0;
                for (int c = 0; c < channels; ++c) sum += static_cast<int>(data[i * channels + c]) - 128;
                samples.push_back((sum / static_cast<float>(channels)) / 128.0f);
            }
        }
    };

    ensureEventDispatcher();

    QEventLoop decodeLoop;
    QTimer decodeTimeout;
    decodeTimeout.setSingleShot(true);
    QObject::connect(&decodeTimeout, &QTimer::timeout, [&]() {
        decodeError = "MP3 decode timed out";
        decoder.stop();
        decodeLoop.quit();
    });
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, [&]() {
        while (decoder.bufferAvailable()) {
            appendBuffer(decoder.read());
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &decodeLoop, &QEventLoop::quit);
    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), [&](QAudioDecoder::Error) {
        decodeError = decoder.errorString();
        decodeLoop.quit();
    });

    decodeTimeout.start(30000);
    decoder.start();
    decodeLoop.exec();
    while (decoder.bufferAvailable()) {
        appendBuffer(decoder.read());
    }
    throwIfCancelled(cancelFlag);

    if (samples.empty()) {
        throw std::runtime_error("EdgeTtsEngine: MP3 decode failed: " + decodeError.toStdString());
    }

    result.samples = std::move(samples);
    result.sampleRate = outSampleRate;
    return result;
}

EdgeTtsEngine::ConnectionTestResult EdgeTtsEngine::testConnection(const std::string& voiceShortName) const {
    ConnectionTestResult result;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        synthesize("Test.", voiceShortName, 1.0f, false, 15000);
        const auto t1 = std::chrono::steady_clock::now();
        result.ok = true;
        result.latencyMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        result.message = "Connected (" + std::to_string(static_cast<int>(result.latencyMs)) + " ms)";
    } catch (const std::exception& e) {
        result.ok = false;
        result.message = e.what();
    }
    return result;
}

} // namespace tts
