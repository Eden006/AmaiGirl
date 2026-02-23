#include "common/Utils.hpp"
#include <QtGlobal>
#include <QCoreApplication>
#include <QFileInfo>
#include <cmath>

QByteArray readFileAll(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QString("Failed to open file: %1").arg(path).toStdString());
    }
    return f.readAll();
}

QJsonDocument jsonFromFile(const QString &path) {
    auto data = readFileAll(path);
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        throw std::runtime_error(QString("JSON parse error in %1: %2").arg(path, err.errorString()).toStdString());
    }
    return doc;
}

void ensure(bool cond, const QString &msg) {
    if (!cond) throw std::runtime_error(msg.toStdString());
}

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float easingSine(float rate) {
    if (rate < 0.0f) return 0.0f;
    if (rate > 1.0f) return 1.0f;
    const float pi = static_cast<float>(std::acos(-1.0));
    return 0.5f - 0.5f * std::cos(rate * pi);
}

QString appResourceRootPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();

#if defined(Q_OS_MACOS)
    const QString bundleRes = QDir::cleanPath(QDir(appDir).filePath("../Resources"));
    if (QFileInfo::exists(bundleRes) && QFileInfo(bundleRes).isDir()) {
        return bundleRes;
    }
#endif

    const QString localRes = QDir::cleanPath(QDir(appDir).filePath("res"));
    if (QFileInfo::exists(localRes) && QFileInfo(localRes).isDir()) {
        return localRes;
    }

#if defined(Q_OS_MACOS)
    return QDir::cleanPath(QDir(appDir).filePath("../Resources"));
#else
    return localRes;
#endif
}

QString appResourcePath(const QString& relativePath)
{
    return QDir(appResourceRootPath()).filePath(relativePath);
}
