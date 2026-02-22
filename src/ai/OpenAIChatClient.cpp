#include "ai/OpenAIChatClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr int kHttpTimeoutMs = 120000;

QString withErrorTag(const QString& message)
{
    if (message.startsWith(QStringLiteral("[Error]"))) return message;
    return QStringLiteral("[Error] %1").arg(message);
}
}

OpenAIChatClient::OpenAIChatClient(QObject* parent)
    : QObject(parent)
{
}

void OpenAIChatClient::setConfig(ChatConfig cfg)
{
    m_cfg = std::move(cfg);
}

QString OpenAIChatClient::normalizeBaseUrl(const QString& baseUrl)
{
    QString u = baseUrl.trimmed();
    while (u.endsWith('/')) u.chop(1);
    // user will input https://xxx/v1
    if (!u.endsWith("/v1"))
    {
        // if they accidentally passed https://xxx, keep consistent with requirement: only autocomplete /chat/completions
        // so we don't alter to /v1.
        return u;
    }
    return u;
}

QUrl OpenAIChatClient::makeCompletionsUrl(const QString& baseUrl)
{
    QString u = normalizeBaseUrl(baseUrl);
    // Requirement: only autocomplete /chat/completions
    if (!u.endsWith("/chat/completions"))
        u += "/chat/completions";
    return QUrl(u);
}

void OpenAIChatClient::startChat(const QByteArray& messagesJson)
{
    if (isBusy())
    {
        emitErrorAndCleanup(QStringLiteral("Request already in progress"));
        return;
    }

    if (m_cfg.baseUrl.trimmed().isEmpty())
    {
        emit errorOccurred(withErrorTag(tr("base_url 为空")));
        return;
    }

    // api_key is allowed to be empty (some providers use IP allow-list or other auth).

    QJsonParseError parseErr;
    const auto msgDoc = QJsonDocument::fromJson(messagesJson, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !msgDoc.isArray())
    {
        emit errorOccurred(withErrorTag(tr("消息体格式错误")));
        return;
    }

    QJsonArray messages = msgDoc.array();
    if (!m_cfg.systemPrompt.trimmed().isEmpty())
    {
        QJsonObject sys;
        sys["role"] = "system";
        sys["content"] = m_cfg.systemPrompt;
        QJsonArray withSys;
        withSys.append(sys);
        for (const auto& v : messages) withSys.append(v);
        messages = withSys;
    }

    QJsonObject body;
    body["model"] = m_cfg.model.isEmpty() ? QStringLiteral("gpt-4o-mini") : m_cfg.model;
    body["messages"] = messages;
    body["stream"] = m_cfg.stream;

    QNetworkRequest req(makeCompletionsUrl(m_cfg.baseUrl));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_cfg.apiKey.trimmed().isEmpty())
    {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + m_cfg.apiKey.trimmed().toUtf8());
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    req.setTransferTimeout(kHttpTimeoutMs);
#endif

    m_accumulated.clear();
    m_streamBuffer.clear();
    m_nonStreamBuffer.clear();

    m_reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    if (!m_reply)
    {
        emit errorOccurred(withErrorTag(tr("无法发起请求")));
        return;
    }

    m_reply->setReadBufferSize(0);

    connect(m_reply, &QNetworkReply::readyRead, this, &OpenAIChatClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OpenAIChatClient::onFinished);

    emit started();
}

void OpenAIChatClient::cancel()
{
    if (!m_reply) return;
    m_reply->abort();
    cleanupReply();
}

void OpenAIChatClient::onReadyRead()
{
    if (!m_reply) return;

    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty()) return;

    if (m_cfg.stream)
    {
        consumeSseLines(chunk);
    }
    else
    {
        // Non-stream: buffer raw payload; QNetworkReply::finished happens after last readyRead,
        // but relying on readAll() in finished can be empty if already drained.
        m_nonStreamBuffer += chunk;
    }
}

void OpenAIChatClient::onFinished()
{
    if (!m_reply) return;

    if (m_reply->error() != QNetworkReply::NoError)
    {
        const QString err = m_reply->errorString();
        cleanupReply();
        emit errorOccurred(withErrorTag(err));
        return;
    }

    // Non-streaming response
    if (!m_cfg.stream)
    {
        // Use buffered body (see onReadyRead)
        const QByteArray payload = m_nonStreamBuffer.isEmpty() ? m_reply->readAll() : m_nonStreamBuffer;
        QJsonParseError e;
        auto doc = QJsonDocument::fromJson(payload, &e);
        if (e.error != QJsonParseError::NoError)
        {
            cleanupReply();
            emit errorOccurred(withErrorTag(tr("响应 JSON 解析失败")));
            return;
        }
        auto o = doc.object();
        auto choices = o.value("choices").toArray();
        if (!choices.isEmpty())
        {
            auto msg = choices.first().toObject().value("message").toObject();
            m_accumulated = msg.value("content").toString();
        }
    }

    const QString out = m_accumulated;
    cleanupReply();
    emit finished(out);
}

void OpenAIChatClient::consumeSseLines(const QByteArray& chunk)
{
    // SSE is event-based: events are separated by a blank line ("\n\n").
    // Each event can contain multiple lines like:
    //   data: {...}\n
    //   data: [DONE]\n\n
    // Parsing line-by-line can split JSON across reads and cause token duplication.
    m_streamBuffer += chunk;

    while (true)
    {
        const int sep = m_streamBuffer.indexOf("\n\n");
        if (sep < 0)
            break;

        const QByteArray event = m_streamBuffer.left(sep);
        m_streamBuffer.remove(0, sep + 2);

        // Collect all `data:` lines and join with \n (per SSE spec).
        QByteArray dataJoined;
        const QList<QByteArray> lines = event.split('\n');
        for (QByteArray line : lines)
        {
            if (!line.isEmpty() && line.endsWith('\r'))
                line.chop(1);
            const QByteArray prefix = "data:";
            if (!line.startsWith(prefix))
                continue;

            QByteArray d = line.mid(prefix.size()).trimmed();
            if (dataJoined.isEmpty())
                dataJoined = d;
            else
                dataJoined += "\n" + d;
        }

        if (dataJoined.isEmpty())
            continue;

        if (dataJoined == "[DONE]")
            continue;

        // For OpenAI-compatible streaming, each event is a standalone JSON object.
        QJsonParseError e;
        const auto doc = QJsonDocument::fromJson(dataJoined, &e);
        if (e.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const auto o = doc.object();
        const auto choices = o.value("choices").toArray();
        if (choices.isEmpty())
            continue;

        const auto delta = choices.first().toObject().value("delta").toObject();
        const QString t = delta.value("content").toString();
        if (t.isEmpty())
            continue;

        m_accumulated += t;
        emit tokenReceived(t);
    }
}

void OpenAIChatClient::handleSseLine(const QByteArray& line)
{
    // Legacy line-based SSE parsing is intentionally unused.
    Q_UNUSED(line);
}

void OpenAIChatClient::emitErrorAndCleanup(const QString& message)
{
    cleanupReply();
    emit errorOccurred(withErrorTag(message));
}

void OpenAIChatClient::cleanupReply()
{
    if (!m_reply) return;
    m_reply->disconnect(this);
    m_reply->deleteLater();
    m_reply = nullptr;
}
