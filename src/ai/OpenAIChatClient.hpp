#pragma once

#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QString>

class OpenAIChatClient : public QObject
{
    Q_OBJECT
public:
    struct ChatConfig
    {
        QString baseUrl;      // like https://xxx/v1
        QString apiKey;
        QString model;
        QString systemPrompt;
        bool stream{true};
    };

    explicit OpenAIChatClient(QObject* parent = nullptr);

    void setConfig(ChatConfig cfg);
    ChatConfig config() const { return m_cfg; }

    bool isBusy() const { return m_reply != nullptr; }

    // messagesJson: already in simplified format we use in chat persistence.
    // [{"role":"user","content":"..."}, ...]
    void startChat(const QByteArray& messagesJson);
    void cancel();

Q_SIGNALS:
    void started();
    void tokenReceived(const QString& text);
    void finished(const QString& fullText);
    void errorOccurred(const QString& message);

private Q_SLOTS:
    void onReadyRead();
    void onFinished();

private:
    static QString normalizeBaseUrl(const QString& baseUrl); // ensure ends with /v1
    static QUrl makeCompletionsUrl(const QString& baseUrl);

    ChatConfig m_cfg;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;

    QByteArray m_streamBuffer;
    QByteArray m_nonStreamBuffer;
    QString m_accumulated;

    void emitErrorAndCleanup(const QString& message);
    void cleanupReply();

    void consumeSseLines(const QByteArray& chunk);
    void handleSseLine(const QByteArray& line);
};
