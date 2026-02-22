#pragma once
#include <QString>
#include <QImage>
#include <QOpenGLTexture>
#include <QVector>
#include <QHash>
#include <QSharedPointer>
#include <QRect>
#include <QJsonObject>
#include <QJsonArray>
#include <cstdint>
#include <optional>
#include "engine/Live2DCore.hpp"
#include "common/Utils.hpp"

struct Drawable {
    QString id;
    int textureIndex{0};
    QSharedPointer<QOpenGLTexture> texture;
    std::vector<Vec2> uv;
    std::vector<Vec2> pos;
    std::vector<uint16_t> idx;
    uint8_t cflag{0};
    uint8_t dflag{0};
    float opacity{1.0f};
    int32_t order{0};
    std::vector<uint32_t> masks;
    // 新增：原始 drawable 索引（用于查询父 Part 链）
    int index{-1};
};

// pose3 支持的简单数据结构
struct PoseEntry {
    int partIndex{-1};
    int parameterIndex{-1}; // 虚拟参数索引（与部件ID同名的参数），用于依据动态判断显示部件
    QVector<int> linkPartIndices; // 与该部件联动的其它部件
};
struct PoseGroup {
    QVector<PoseEntry> entries; // 同组互斥的部件
};
struct PoseDef {
    QVector<PoseGroup> groups;
    float fadeInTime{0.5f};
    bool valid{false};
};

struct Model {
    QString rootDir;
    QJsonObject modelJson;
    MocHolder moc;
    std::vector<Drawable> drawables;
    QVector<QString> texturesPaths;
    QHash<QString, QVector<QString>> motions; // group -> files
    // 可选 pose 定义
    std::optional<PoseDef> pose;
    // 表情：名称 -> 文件路径
    QHash<QString, QString> expressions;
};

class ModelLoader {
public:
    static QSharedPointer<Model> loadModel(const QString& model3JsonPath);
};
