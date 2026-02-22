#include "engine/Model.hpp"
#include <QJsonDocument>
#include <QFileInfo>
#include <cstdint>
#include <QDirIterator>

QSharedPointer<Model> ModelLoader::loadModel(const QString &model3JsonPath) {
    auto doc = jsonFromFile(model3JsonPath);
    auto obj = doc.object();
    auto m = QSharedPointer<Model>::create();
    m->rootDir = QFileInfo(model3JsonPath).absolutePath();
    m->modelJson = obj;

    auto fileRef = obj.value("FileReferences").toObject();
    QString mocPath = QDir(m->rootDir).filePath(fileRef.value("Moc").toString());
    m->moc = Live2DCore::loadMoc(mocPath);

    // textures: only record paths; defer actual GL texture creation to Renderer within a valid GL context
    auto texArr = fileRef.value("Textures").toArray();
    for (const auto& v : texArr) {
        QString rel = v.toString();
        QString abs = QDir(m->rootDir).filePath(rel);
        m->texturesPaths.push_back(abs);
    }

    // Build drawables (no GL calls here)
    int32_t count = (int32_t)csmGetDrawableCount(m->moc.model);
    auto cflags = reinterpret_cast<const uint8_t*>(csmGetDrawableConstantFlags(m->moc.model));
    auto dflags = reinterpret_cast<const uint8_t*>(csmGetDrawableDynamicFlags(m->moc.model));
    auto tIdxs  = reinterpret_cast<const int32_t*>(csmGetDrawableTextureIndices(m->moc.model));
    auto opac   = reinterpret_cast<const float*>(csmGetDrawableOpacities(m->moc.model));
    auto orders = reinterpret_cast<const int32_t*>(csmGetDrawableRenderOrders(m->moc.model));

    auto vCounts = reinterpret_cast<const int32_t*>(csmGetDrawableVertexCounts(m->moc.model));
    auto iCounts = reinterpret_cast<const int32_t*>(csmGetDrawableIndexCounts(m->moc.model));

    const csmVector2** positions = csmGetDrawableVertexPositions(m->moc.model);
    const csmVector2** uvs = csmGetDrawableVertexUvs(m->moc.model);
    const uint16_t** idxs = csmGetDrawableIndices(m->moc.model);

    const int32_t* mCounts = csmGetDrawableMaskCounts(m->moc.model);
    const int** masks = csmGetDrawableMasks(m->moc.model);

    // ids
    QStringList ids;
    const char** idsPtr = csmGetDrawableIds(m->moc.model);
    for (int i = 0; i < count; ++i) {
        ids << QString::fromUtf8(idsPtr[i]);
    }

    m->drawables.reserve(count);
    for (int i = 0; i < count; ++i) {
        Drawable d;
        d.id = ids[i];
        d.textureIndex = tIdxs[i];
        // d.texture will be assigned by Renderer after creating textures in GL context
        d.cflag = cflags[i];
        d.dflag = dflags[i];
        d.opacity = opac[i];
        d.order = orders[i];
        d.index = i; // record original index

        int vc = vCounts[i];
        int ic = iCounts[i];
        d.pos.resize(vc);
        d.uv.resize(vc);
        for (int v = 0; v < vc; ++v) {
            d.pos[v] = { positions[i][v].X, positions[i][v].Y };
            d.uv[v]  = { uvs[i][v].X, uvs[i][v].Y };
        }
        d.idx.resize(ic);
        for (int k = 0; k < ic; ++k) d.idx[k] = idxs[i][k];

        int mc = mCounts[i];
        for (int k = 0; k < mc; ++k) {
            int midx = masks[i][k];
            if (midx >= 0) d.masks.push_back(static_cast<uint32_t>(midx));
        }

        m->drawables.push_back(std::move(d));
    }

    // motions indexing
    auto motionsObj = fileRef.value("Motions").toObject();
    for (auto it = motionsObj.begin(); it != motionsObj.end(); ++it) {
        QVector<QString> files;
        auto arr = it.value().toArray();
        for (const auto mv : arr) {
            auto mo = mv.toObject();
            QString file = mo.value("File").toString();
            files.push_back(QDir(m->rootDir).filePath(file));
        }
        m->motions.insert(it.key(), files);
    }

    // expressions indexing
    if (fileRef.contains("Expressions")) {
        auto exArr = fileRef.value("Expressions").toArray();
        for (const auto &any : exArr) {
            auto e = any.toObject();
            QString name = e.value("Name").toString();
            QString file = e.value("File").toString();
            if (!file.isEmpty()) m->expressions.insert(name, QDir(m->rootDir).filePath(file));
        }
    }

    // pose3.json parsing (unchanged)
    if (fileRef.contains("Pose")) {
        QString poseRel = fileRef.value("Pose").toString();
        if (!poseRel.isEmpty()) {
            QString posePath = QDir(m->rootDir).filePath(poseRel);
            try {
                auto poseDoc = jsonFromFile(posePath);
                PoseDef pose;
                auto pobj = poseDoc.object();
                if (pobj.contains("FadeInTime")) pose.fadeInTime = (float)pobj.value("FadeInTime").toDouble(0.5);

                // 构建 PartId -> index 映射
                int partCount = csmGetPartCount(m->moc.model);
                QHash<QString,int> partIndexById;
                const char** partIds = csmGetPartIds(m->moc.model);
                for (int i=0;i<partCount;++i) partIndexById.insert(QString::fromUtf8(partIds[i]), i);
                // 构建 ParamId -> parameterIndex 映射（虚拟参数与 Part 同名）
                int paramCount = (int)csmGetParameterCount(m->moc.model);
                QHash<QString,int> paramIndexById;
                const char** paramIds = csmGetParameterIds(m->moc.model);
                for (int i=0;i<paramCount;++i) paramIndexById.insert(QString::fromUtf8(paramIds[i]), i);

                auto groups = pobj.value("Groups").toArray();
                for (const auto &gAny : groups) {
                    PoseGroup g;
                    auto arr = gAny.toArray();
                    for (const auto &entryAny : arr) {
                        auto eobj = entryAny.toObject();
                        QString pid = eobj.value("Id").toString();
                        PoseEntry pe;
                        pe.partIndex = partIndexById.value(pid, -1);
                        pe.parameterIndex = paramIndexById.value(pid, -1);
                        auto links = eobj.value("Link").toArray();
                        for (const auto &lAny : links) {
                            QString lid = lAny.toString();
                            int li = partIndexById.value(lid, -1);
                            if (li >= 0) pe.linkPartIndices.push_back(li);
                        }
                        g.entries.push_back(std::move(pe));
                    }
                    if (!g.entries.isEmpty()) pose.groups.push_back(std::move(g));
                }
                pose.valid = !pose.groups.isEmpty();
                if (pose.valid) m->pose = std::move(pose);
            } catch (...) {
                // ignore pose parse error, keep running
            }
        }
    }

    return m;
}
