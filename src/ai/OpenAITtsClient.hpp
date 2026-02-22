#pragma once

#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QString>
#include <QUrl>

// NOTE: Do NOT undefine Qt's 'slots' keyword macro here.
// If some 3rd-party code defines a conflicting macro named 'slots', we clean it up
// in our .cpp translation units before including Qt headers.

class OpenAITtsClient : public QObject
{
    Q_OBJECT
public:
    struct TtsConfig
    {
        QString baseUrl;  // user input like https://xxx/v1
        QString apiKey;
        QString model;
        QString voice;
    };

    explicit OpenAITtsClient(QObject* parent = nullptr);

    void setConfig(TtsConfig cfg);
    TtsConfig config() const { return m_cfg; }

    bool isBusy() const { return m_reply != nullptr; }

    // Writes audio to outPath (overwrites) and emits finished(outPath).
    void startSpeech(const QString& text, const QString& outPath);
    void cancel();

Q_SIGNALS:
    void started();
    void finished(const QString& outPath);
    void errorOccurred(const QString& message);

private Q_SLOTS:
    void onFinished();

private:
    static QString normalizeBaseUrlToV1(const QString& baseUrl);
    static QUrl makeSpeechUrl(const QString& baseUrlV1);

    void emitErrorAndCleanup(const QString& message);
    void cleanupReply();

    TtsConfig m_cfg;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;

    QString m_outPath;
};
