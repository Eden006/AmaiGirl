#include "engine/Expression.hpp"
#include "common/Utils.hpp"
#include <QJsonArray>
#include <algorithm>

static ExpressionParam::Blend parseBlend(const QString& s) {
    QString t = s.toLower();
    if (t == "add") return ExpressionParam::Add;
    if (t == "multiply") return ExpressionParam::Multiply;
    return ExpressionParam::Overwrite;
}

Expression ExpressionLoader::load(const QString &path) {
    auto doc = jsonFromFile(path);
    Expression e;
    auto obj = doc.object();
    if (obj.contains("Name")) e.name = obj.value("Name").toString();
    auto meta = obj.value("Meta").toObject();
    e.fadeIn = (float)meta.value("FadeInTime").toDouble(0.0);
    e.fadeOut = (float)meta.value("FadeOutTime").toDouble(0.0);

    auto arr = obj.value("Parameters").toArray();
    for (const auto &any : arr) {
        auto p = any.toObject();
        ExpressionParam ep;
        ep.id = p.value("Id").toString();
        ep.value = (float)p.value("Value").toDouble(0.0);
        ep.blend = parseBlend(p.value("Blend").toString("Overwrite"));
        e.params.push_back(std::move(ep));
    }
    return e;
}

void Expression::apply(Model *model, float w, const float* baseValues) const {
    if (!model || w <= 0.0f) return;
    auto* core = model->moc.model;
    int32_t pc = (int32_t)csmGetParameterCount(core);
    const char** ids = csmGetParameterIds(core);
    float* vals = const_cast<float*>(csmGetParameterValues(core));
    const float* minVals = csmGetParameterMinimumValues(core);
    const float* maxVals = csmGetParameterMaximumValues(core);

    auto findIdx = [&](const QString& id)->int{
        for (int i=0;i<pc;++i) if (QString::fromUtf8(ids[i]) == id) return i; return -1;
    };

    for (const auto &p : params) {
        int idx = findIdx(p.id);
        if (idx < 0) continue;
        float base = baseValues ? baseValues[idx] : vals[idx];
        float v = base;
        switch (p.blend) {
            case ExpressionParam::Add:
                // base + value*w
                v = base + p.value * w;
                break;
            case ExpressionParam::Multiply:
                // base * (1 + (value-1)*w)
                v = base * (1.0f + (p.value - 1.0f) * w);
                break;
            case ExpressionParam::Overwrite:
            default:
                // lerp(base, value, w)
                v = base + (p.value - base) * w;
                break;
        }
        vals[idx] = std::clamp(v, minVals[idx], maxVals[idx]);
    }
}
