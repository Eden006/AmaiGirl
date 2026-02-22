// Defensively undo third-party macro pollution that breaks Qt headers.
#ifdef slots
#  undef slots
#endif

#include "ai/OpenAITtsClient.hpp"

// Qt uses the keyword 'slots' in headers (unless QT_NO_KEYWORDS is set).
// Some third-party SDKs (and even old code) occasionally define a macro named 'slots'
// which breaks Qt headers badly (you'll see errors deep inside Qt about operators
// having the wrong number of parameters). Ensure it's not defined for this TU.
//#ifdef slots
//#  undef slots
//#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

namespace {
QString withErrorTag(const QString& message)
{
    if (message.startsWith(QStringLiteral("[Error]"))) return message;
    return QStringLiteral("[Error] %1").arg(message);
}
}

OpenAITtsClient::OpenAITtsClient(QObject* parent)
    : QObject(parent)
{
}

void OpenAITtsClient::setConfig(TtsConfig cfg)
{
    m_cfg = std::move(cfg);
}

QString OpenAITtsClient::normalizeBaseUrlToV1(const QString& baseUrl)
{
    QString u = baseUrl.trimmed();
    if (u.endsWith('/'))
    {
        u.chop(1);
    }

    // allow user to input https://host OR https://host/v1
    if (!u.endsWith("/v1"))
    {
        u += "/v1";
    }

    return u;
}

QUrl OpenAITtsClient::makeSpeechUrl(const QString& baseUrlV1)
{
    return QUrl(baseUrlV1 + "/audio/speech");
}

void OpenAITtsClient::startSpeech(const QString& text, const QString& outPath)
{
    if (isBusy())
    {
        emitErrorAndCleanup(tr("TTS 正在忙碌中"));
        return;
    }

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
    {
        emitErrorAndCleanup(tr("TTS 输入为空"));
        return;
    }

    const QString baseV1 = normalizeBaseUrlToV1(m_cfg.baseUrl);
    if (baseV1.isEmpty())
    {
        emitErrorAndCleanup(tr("TTS base_url 为空"));
        return;
    }

    m_outPath = outPath;

    QJsonObject body;
    body.insert("model", m_cfg.model.isEmpty() ? QStringLiteral("gpt-4o-mini-tts") : m_cfg.model);
    body.insert("voice", m_cfg.voice.isEmpty() ? QStringLiteral("alloy") : m_cfg.voice);
    body.insert("input", trimmed);

    // Prefer uncompressed (or lightly compressed) format for lip-sync.
    // OpenAI-compatible endpoints support `response_format`: wav / pcm / mp3.
    body.insert("response_format", QStringLiteral("wav"));

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(makeSpeechUrl(baseV1));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_cfg.apiKey.trimmed().isEmpty())
    {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + m_cfg.apiKey.trimmed().toUtf8());
    }

    m_reply = m_nam.post(req, payload);
    emit started();

    connect(m_reply, &QNetworkReply::finished, this, &OpenAITtsClient::onFinished);
}

void OpenAITtsClient::cancel()
{
    if (m_reply)
    {
        m_reply->abort();
    }
    cleanupReply();
    m_outPath.clear();
}

void OpenAITtsClient::onFinished()
{
    if (!m_reply)
    {
        return;
    }

    const auto httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int statusCode = httpStatus.isValid() ? httpStatus.toInt() : 0;

    const QByteArray data = m_reply->readAll();
    const QString errStr = m_reply->errorString();
    const bool ok = (m_reply->error() == QNetworkReply::NoError) && (statusCode >= 200 && statusCode < 300);

    cleanupReply();

    if (!ok)
    {
        QString details;
        if (!data.isEmpty())
        {
            details = QString::fromUtf8(data.left(2048));
        }
        emit errorOccurred(withErrorTag(tr("TTS 请求失败 (%1): %2 %3").arg(statusCode).arg(errStr, details)));
        return;
    }

    if (m_outPath.isEmpty())
    {
        emit errorOccurred(withErrorTag(tr("TTS 输出路径为空")));
        return;
    }

    QDir().mkpath(QFileInfo(m_outPath).absolutePath());

    // atomic write to avoid partial file
    QSaveFile sf(m_outPath);
    if (!sf.open(QIODevice::WriteOnly))
    {
        emit errorOccurred(withErrorTag(tr("无法打开缓存文件进行写入")));
        return;
    }
    sf.write(data);
    if (!sf.commit())
    {
        emit errorOccurred(withErrorTag(tr("无法提交缓存文件")));
        return;
    }

    emit finished(m_outPath);
}

void OpenAITtsClient::emitErrorAndCleanup(const QString& message)
{
    cancel();
    emit errorOccurred(withErrorTag(message));
}

void OpenAITtsClient::cleanupReply()
{
    if (!m_reply)
    {
        return;
    }

    m_reply->deleteLater();
    m_reply = nullptr;
}
