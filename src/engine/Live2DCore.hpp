#pragma once
#include <Live2DCubismCore.h>
#include <QString>
#include <QByteArray>
#include <cstdint>
#include "common/Utils.hpp"

struct MocHolder {
    csmMoc* moc{nullptr};
    QByteArray mocStorage; // owning storage
    void* mocAlignedPtr{nullptr}; // aligned pointer into storage
    csmModel* model{nullptr};
    QByteArray modelStorage; // owning storage
    void* modelAlignedPtr{nullptr}; // aligned pointer into storage
};

class Live2DCore {
public:
    static QString versionString();
    static uint32_t latestMocVersion();
    static uint32_t mocVersion(const QByteArray& data);

    static MocHolder loadMoc(const QString& path);
};
