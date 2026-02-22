#include "engine/PhysicsEngine.hpp"
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>

static QJsonDocument jsonFromFileQ(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("open physics json failed");
    QJsonParseError err; auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) throw std::runtime_error("parse physics json failed");
    return doc;
}

void PhysicsEngine::init(const QSharedPointer<Model> &model) {
    m_groups.clear(); m_valid = false; m_prevSmoothed.clear(); m_touched.clear();
    // reset options to defaults before loading
    m_gravity = {0.0f, -1.0f};
    m_wind = {0.0f, 0.0f};
    if (!model) return;

    // read physics3.json path from model json
    auto fr = model->modelJson.value("FileReferences").toObject();
    if (!fr.contains("Physics")) return;
    QString rel = fr.value("Physics").toString();
    if (rel.isEmpty()) return;
    QString abs = QDir(model->rootDir).filePath(rel);

    QJsonDocument doc; try { doc = jsonFromFileQ(abs); } catch(...) { return; }
    auto root = doc.object();

    if (root.contains("Meta")) {
        auto meta = root.value("Meta").toObject();
        if (meta.contains("EffectiveForces")) {
            auto ef = meta.value("EffectiveForces").toObject();
            if (ef.contains("Gravity")) { auto g = ef.value("Gravity").toObject(); m_gravity = { (float)g.value("X").toDouble(0.0), (float)g.value("Y").toDouble(-1.0) }; }
            // Wind from file is intentionally ignored for stability; set to zero to avoid drift
            m_wind = {0.0f, 0.0f};
        }
        if (meta.contains("Fps")) {
            double fps = meta.value("Fps").toDouble(60.0);
            if (std::isfinite(fps) && fps >= 24.0 && fps <= 240.0) {
                // use higher internal substep to stabilize chains on high-mobility settings
                m_targetHz = std::clamp((float)(fps * 4.0), 120.0f, 420.0f);
            }
        }
    }

    QJsonArray settings;
    if (root.contains("PhysicsSettings")) settings = root.value("PhysicsSettings").toArray();
    else if (root.contains("Physics"))   settings = root.value("Physics").toArray();
    if (settings.isEmpty()) return;

    // prepare param index array size
    int pcnt = (int)csmGetParameterCount(model->moc.model);
    m_prevSmoothed.resize(pcnt); for (int i=0;i<pcnt;++i) m_prevSmoothed[i] = 0.0f;

    const char** pids = csmGetParameterIds(model->moc.model);

    auto findParam = [&](const QString& id)->int{
        for (int i=0;i<pcnt;++i) if (id.compare(QString::fromUtf8(pids[i]), Qt::CaseInsensitive)==0) return i; return -1; };

    // removed mirror pair mapping: rely on physical chain coherence

    for (const auto& any : settings) {
        auto s = any.toObject();
        Group g; g.friction = (float)s.value("Friction").toDouble(0.6); // slightly higher default friction
        if (s.contains("Normalization")) {
            auto no = s.value("Normalization").toObject();
            auto pos = no.value("Position").toObject();
            g.norm.position.min = (float)pos.value("Minimum").toDouble(0.0);
            g.norm.position.max = (float)pos.value("Maximum").toDouble(0.0);
            g.norm.position.def = (float)pos.value("Default").toDouble(0.0);
            auto ang = no.value("Angle").toObject();
            g.norm.angle.min = (float)ang.value("Minimum").toDouble(0.0);
            g.norm.angle.max = (float)ang.value("Maximum").toDouble(0.0);
            g.norm.angle.def = (float)ang.value("Default").toDouble(0.0);
        }
        // inputs
        QJsonArray inArr = s.contains("Inputs") ? s.value("Inputs").toArray() : s.value("Input").toArray();
        for (const auto& inAny : inArr) {
            auto io = inAny.toObject(); InputDef in;
            auto t = io.value("Type").toString();
            if (t.contains("Angle", Qt::CaseInsensitive)) in.type = InputDef::InAngle; else if (t.contains("X")) in.type = InputDef::InX; else in.type = InputDef::InY;
            auto so = io.value("Source").toObject();
            in.paramId = so.value("Id").toString();
            in.scale = (float)io.value("Scale").toDouble(1.0);
            in.weight = (float)io.value("Weight").toDouble(1.0);
            in.reflect = io.value("Reflect").toBool(false);
            g.inputs.push_back(in);
        }
        // outputs
        QJsonArray outArr = s.contains("Outputs") ? s.value("Outputs").toArray() : s.value("Output").toArray();
        for (const auto& outAny : outArr) {
            auto oo = outAny.toObject(); OutputDef out;
            auto t = oo.value("Type").toString();
            if (t.contains("Angle", Qt::CaseInsensitive)) out.type = OutputDef::OutAngle; else if (t.contains("X")) out.type = OutputDef::OutX; else out.type = OutputDef::OutY;
            auto dst = oo.value("Destination").toObject();
            out.paramId = dst.value("Id").toString();
            out.vertexIndex = oo.value("VertexIndex").toInt(0);
            out.scale = (float)oo.value("Scale").toDouble(1.0);
            out.weight = (float)oo.value("Weight").toDouble(1.0);
            out.reflect = oo.value("Reflect").toBool(false);

            // Filter: avoid writing to primary head/body orientation parameters to prevent conflicts
            QString lid = out.paramId.toLower();
            bool isPrimaryAngle = lid.startsWith("paramanglex") || lid.startsWith("paramangley") || lid.startsWith("paramanglez");
            bool isPrimaryBody  = lid.startsWith("parambodyanglex") || lid.startsWith("parambodyangley") || lid.startsWith("parambodyanglez");
            if (isPrimaryAngle || isPrimaryBody) {
                // skip this output to let procedural systems (gaze/breath/motion) own these params
                continue;
            }
            g.outputs.push_back(out);
        }
        // vertices
        QJsonArray vArr = s.value("Vertices").toArray();
        if (vArr.isEmpty()) continue;
        g.verts.resize(vArr.size()); g.particles.resize(vArr.size());
        for (int i=0;i<vArr.size(); ++i) {
            auto vo = vArr[i].toObject();
            g.verts[i].rest.x = (float)vo.value("Position").toObject().value("X").toDouble(0.0);
            g.verts[i].rest.y = (float)vo.value("Position").toObject().value("Y").toDouble(0.0);
            g.verts[i].mobility = (float)vo.value("Mobility").toDouble(1.0);
            g.verts[i].delay = (float)vo.value("Delay").toDouble(0.25); // a bit slower to respond
            g.verts[i].acceleration = (float)vo.value("Acceleration").toDouble(1.0);
            g.verts[i].radius = (float)vo.value("Radius").toDouble(10.0);
            g.particles[i].pos = g.verts[i].rest; g.particles[i].prev = g.verts[i].rest;
        }
        // cache param indices
        g.inputIndex.resize(g.inputs.size());
        for (int i=0;i<g.inputs.size(); ++i) g.inputIndex[i] = findParam(g.inputs[i].paramId);
        g.outputIndex.resize(g.outputs.size());
        for (int i=0;i<g.outputs.size(); ++i) g.outputIndex[i] = findParam(g.outputs[i].paramId);
        // heuristic kind based on output paramIds
        auto isHairParam = [](const QString& id){
            QString lid = id.toLower();
            return lid.contains("hair") || lid.contains("syhair") || lid.contains("bang") || lid.contains("fringe");
        };
        auto isClothParam = [](const QString& id){
            QString lid = id.toLower();
            return lid.contains("cloth") || lid.contains("clothes") || lid.contains("skirt") || lid.contains("dress");
        };
        auto isAccessoryParam = [](const QString& id){
            QString lid = id.toLower();
            return lid.contains("ear") || lid.contains("ribbon") || lid.contains("access") || lid.contains("pendant");
        };
        int hairVotes=0, clothVotes=0, accVotes=0;
        for (const auto& o : g.outputs) {
            if (isHairParam(o.paramId)) ++hairVotes;
            else if (isClothParam(o.paramId)) ++clothVotes;
            else if (isAccessoryParam(o.paramId)) ++accVotes;
        }
        if (hairVotes > clothVotes && hairVotes > accVotes) g.kind = GroupKind::Hair;
        else if (clothVotes > accVotes) g.kind = GroupKind::Cloth;
        else if (accVotes > 0) g.kind = GroupKind::Accessory;
        else g.kind = GroupKind::Generic;

        // init output interpolation buffers
        g.prevOutputs.resize(g.outputs.size());
        g.curOutputs.resize(g.outputs.size());
        std::fill(g.prevOutputs.begin(), g.prevOutputs.end(), 0.0f);
        std::fill(g.curOutputs.begin(), g.curOutputs.end(), 0.0f);

        m_groups.push_back(std::move(g));
    }
    m_valid = !m_groups.isEmpty();

    // init parameter caches
    if (m_valid) {
        m_paramCache.resize(pcnt);
        m_paramInputCache.resize(pcnt);
        auto params0 = csmGetParameterValues(model->moc.model);
        for (int i=0;i<pcnt;++i) {
            m_paramCache[i] = params0[i];
            m_paramInputCache[i] = params0[i];
        }
    }
}

void PhysicsEngine::update(double dt, const QSharedPointer<Model> &model) {
    if (!m_valid || !model) return;

    int pcnt = (int)csmGetParameterCount(model->moc.model);
    auto params = const_cast<float*>(csmGetParameterValues(model->moc.model));
    auto minVals = csmGetParameterMinimumValues(model->moc.model);
    auto maxVals = csmGetParameterMaximumValues(model->moc.model);
    auto defVals = csmGetParameterDefaultValues(model->moc.model);
    if (m_prevSmoothed.size() != pcnt) { m_prevSmoothed.resize(pcnt); for (int i=0;i<pcnt;++i) m_prevSmoothed[i] = 0.0f; }
    if (m_paramCache.size() != pcnt) { m_paramCache.resize(pcnt); for (int i=0;i<pcnt;++i) m_paramCache[i] = params[i]; }
    if (m_paramInputCache.size() != pcnt) { m_paramInputCache.resize(pcnt); for (int i=0;i<pcnt;++i) m_paramInputCache[i] = params[i]; }

    QVector<float> accum(pcnt); std::fill(accum.begin(), accum.end(), 0.0f);
    m_touched.clear();

    // Fixed-step accumulator
    const double fixedStep = 1.0 / std::max(30.0, (double)m_targetHz);
    m_frameDtClamped = std::clamp(dt, 0.0, 1.0/10.0);
    m_timeAccumulator += m_frameDtClamped;

    while (m_timeAccumulator >= fixedStep) {
        // 线性插值输入参数：从上一次缓存向新帧参数推进
        float w = (float)(fixedStep / std::max(1e-6, m_frameDtClamped));
        if (w > 1.0f) w = 1.0f;
        for (int i=0;i<pcnt; ++i) {
            m_paramCache[i] = m_paramCache[i] * (1.0f - w) + params[i] * w;
        }

        // 对每个组进行一步物理，并写入 curOutputs（保留 prevOutputs 以备帧尾插值）
        for (int gi=0; gi<m_groups.size(); ++gi) {
            auto &g = m_groups[gi];

            float inX=0.0f, inY=0.0f, inA=0.0f;
            for (int i=0;i<g.inputs.size(); ++i) {
                int pidx = g.inputIndex[i]; if (pidx < 0) continue;
                float raw = m_paramCache[pidx];
                float n = (g.inputs[i].type == InputDef::InAngle)
                          ? normSigned(raw, g.norm.angle)
                          : normSigned(raw, g.norm.position);
                float wgt = normalizeWeight(g.inputs[i].weight);
                float v = n * g.inputs[i].scale * wgt * (g.inputs[i].reflect ? -1.0f : 1.0f);
                v *= (0.82f);
                switch (g.inputs[i].type) {
                    case InputDef::InX: inX += v; break;
                    case InputDef::InY: inY += v; break;
                    case InputDef::InAngle: inA += v; break;
                }
            }
            float alphaIn = 1.0f - std::exp(-(float)fixedStep * 12.0f);
            g.inX_s = g.inX_s + (inX - g.inX_s) * alphaIn;
            g.inY_s = g.inY_s + (inY - g.inY_s) * alphaIn;
            g.inA_s = g.inA_s + (inA - g.inA_s) * alphaIn;
            inX = g.inX_s; inY = g.inY_s; inA = g.inA_s;

            // 计算锚点（加冲击吸收）
            float avgR = 0.0f; for (int i=1;i<g.verts.size();++i) avgR += std::max(1e-3f, g.verts[i].radius);
            if (g.verts.size() > 1) avgR /= (float)(g.verts.size()-1);
            if (avgR <= 1e-3f) avgR = 10.0f;
            auto rangeAbs = [](const Range& r){ return std::max(std::fabs(r.max - r.def), std::fabs(r.def - r.min)); };
            float posAmp = rangeAbs(g.norm.position); if (posAmp <= 1e-4f) posAmp = avgR;
            float angDeg = rangeAbs(g.norm.angle); if (angDeg <= 1e-4f) angDeg = 10.0f;
            const float deg2rad = 0.01745329252f;

            Vec2 anchorRest = g.verts[0].rest;
            Vec2 desiredAnchor = { anchorRest.x + inX * posAmp, anchorRest.y + inY * posAmp };
            if (!g.particles.isEmpty()) {
                // clamp anchor displacement per substep
                Vec2 prevAnchor = g.particles[0].pos;
                Vec2 d{ desiredAnchor.x - prevAnchor.x, desiredAnchor.y - prevAnchor.y };
                float dlen = length(d);
                float maxStep = avgR * 0.6f; // 每子步锚点位移不超过 0.6 段长
                if (dlen > maxStep) { d.x *= (maxStep / dlen); d.y *= (maxStep / dlen); }
                desiredAnchor = { prevAnchor.x + d.x, prevAnchor.y + d.y };
            }

            float aRadBase = inA * angDeg * deg2rad;
            float aRad = (g.kind == GroupKind::Hair) ? (aRadBase * 0.55f) : aRadBase;

            auto computeOmegaZeta = [&](const VertexCfg& cfg, GroupKind kind){
                float mob = std::clamp(cfg.mobility, 0.0f, 1.0f);
                float baseF = std::clamp(1.1f + cfg.acceleration * 4.2f, 0.8f, 6.0f);
                float delay = std::max(0.10f, cfg.delay);
                float f = std::clamp(baseF / delay, 0.8f, 8.5f);
                float omega = 2.0f * 3.1415926535f * f;
                float zeta;
                if (kind == GroupKind::Hair) zeta = std::clamp(1.08f + (1.0f - mob) * 0.12f, 1.02f, 1.22f);
                else if (kind == GroupKind::Cloth) zeta = std::clamp(0.95f + (1.0f - mob) * 0.10f, 0.90f, 1.10f);
                else zeta = std::clamp(0.82f + (1.0f - mob) * 0.10f, 0.76f, 1.02f);
                return std::pair<float,float>(omega, zeta);
            };
            auto bendLimitRadFor = [&](GroupKind kind){
                if (kind == GroupKind::Hair) return 22.0f * deg2rad;
                if (kind == GroupKind::Cloth) return 25.0f * deg2rad;
                return 20.0f * deg2rad;
            };

            const float friction = std::clamp(g.friction, 0.0f, 1.0f);
            const float velDampK = -std::log(std::max(1e-4f, 1.0f - (0.45f + 0.45f*friction)));
            const float velDampPerStep = std::exp(-velDampK * (float)fixedStep);

            if (!g.particles.isEmpty()) {
                Vec2 prev = g.particles[0].pos;
                g.particles[0].pos = desiredAnchor;
                g.particles[0].vel = { (g.particles[0].pos.x - prev.x) / (float)fixedStep,
                                       (g.particles[0].pos.y - prev.y) / (float)fixedStep };
                g.particles[0].vel.x *= velDampPerStep;
                g.particles[0].vel.y *= velDampPerStep;
            }

            // 主积分与一次长度约束（逐段）
            for (int vi=1; vi<g.particles.size(); ++vi) {
                auto &p = g.particles[vi];
                const auto &cfg = g.verts[vi];
                const Vec2 prevPos = g.particles[vi-1].pos;

                Vec2 restSeg{ g.verts[vi].rest.x - g.verts[vi-1].rest.x,
                              g.verts[vi].rest.y - g.verts[vi-1].rest.y };
                float restLen = length(restSeg);
                Vec2 baseDir = restLen > 1e-5f ? Vec2{ restSeg.x / restLen, restSeg.y / restLen } : Vec2{1.0f, 0.0f};
                Vec2 dir = rotate(baseDir, aRad);
                float L = (restLen > 1e-3f) ? restLen : std::max(1e-3f, cfg.radius);
                Vec2 target{ prevPos.x + dir.x * L, prevPos.y + dir.y * L };

                auto [omega, zeta] = computeOmegaZeta(cfg, g.kind);

                Vec2 e{ p.pos.x - target.x, p.pos.y - target.y };
                Vec2 acc;
                acc.x = -2.0f * zeta * omega * p.vel.x - (omega * omega) * e.x;
                acc.y = -2.0f * zeta * omega * p.vel.y - (omega * omega) * e.y;
                float gScale = (g.kind == GroupKind::Hair ? 0.32f : 0.10f);
                acc.x += (m_gravity.x + m_wind.x) * gScale;
                acc.y += (m_gravity.y + m_wind.y) * gScale;

                p.vel.x += acc.x * (float)fixedStep;
                p.vel.y += acc.y * (float)fixedStep;
                p.vel.x *= velDampPerStep;
                p.vel.y *= velDampPerStep;
                p.pos.x += p.vel.x * (float)fixedStep;
                p.pos.y += p.vel.y * (float)fixedStep;

                Vec2 link{ p.pos.x - prevPos.x, p.pos.y - prevPos.y };
                float linkLen = length(link);
                if (linkLen > 1e-6f) {
                    float corr = (L - linkLen);
                    p.pos.x += (link.x / linkLen) * corr;
                    p.pos.y += (link.y / linkLen) * corr;
                }

                Vec2 curDir{ p.pos.x - prevPos.x, p.pos.y - prevPos.y };
                float curLen = length(curDir);
                if (curLen > 1e-6f) {
                    curDir.x /= curLen; curDir.y /= curLen;
                    float bend = std::atan2(curDir.x*dir.y - curDir.y*dir.x, curDir.x*dir.x + curDir.y*dir.y);
                    float limit = bendLimitRadFor(g.kind);
                    if (bend > limit || bend < -limit) {
                        float clamped = std::clamp(bend, -limit, limit);
                        Vec2 nd = rotate(dir, clamped);
                        p.pos.x = prevPos.x + nd.x * L;
                        p.pos.y = prevPos.y + nd.y * L;
                        p.vel.x *= 0.80f;
                        p.vel.y *= 0.80f;
                    }
                }
            }

            // 附加两轮长度约束迭代，提升收敛与稳定
            for (int iter=0; iter<2; ++iter) {
                for (int vi=1; vi<g.particles.size(); ++vi) {
                    const Vec2 prevPos = g.particles[vi-1].pos;
                    const auto &cfg = g.verts[vi];
                    Vec2 restSeg{ g.verts[vi].rest.x - g.verts[vi-1].rest.x,
                                  g.verts[vi].rest.y - g.verts[vi-1].rest.y };
                    float restLen = length(restSeg);
                    float L = (restLen > 1e-3f) ? restLen : std::max(1e-3f, cfg.radius);
                    Vec2 link{ g.particles[vi].pos.x - prevPos.x, g.particles[vi].pos.y - prevPos.y };
                    float linkLen = length(link);
                    if (linkLen > 1e-6f) {
                        float corr = (L - linkLen);
                        g.particles[vi].pos.x += (link.x / linkLen) * corr;
                        g.particles[vi].pos.y += (link.y / linkLen) * corr;
                    }
                }
            }

            // 计算本步 outputs（写入 curOutputs）
            for (int oi=0; oi<g.outputs.size(); ++oi) {
                int vi = g.outputs[oi].vertexIndex; if (vi < 0) vi = 0; if (vi >= g.particles.size()) vi = g.particles.size()-1;
                Vec2 basePos = g.particles[0].pos;
                Vec2 curPos = g.particles[vi].pos;
                Vec2 restBase = g.verts[0].rest;
                Vec2 restPos = g.verts[vi].rest;
                Vec2 delta{ curPos.x - basePos.x, curPos.y - basePos.y };
                Vec2 rest{ restPos.x - restBase.x, restPos.y - restBase.y };
                auto dot = [](const Vec2&a,const Vec2&b){ return a.x*b.x + a.y*b.y; };
                auto cross = [](const Vec2&a,const Vec2&b){ return a.x*b.y - a.y*b.x; };
                float valueNorm = 0.0f;
                bool isAngle = (g.outputs[oi].type == OutputDef::OutAngle);
                if (isAngle) {
                    float ang = std::atan2(cross(delta, rest), dot(delta, rest));
                    valueNorm = std::clamp(ang / (0.5f * 3.1415926535f), -1.0f, 1.0f);
                } else {
                    float restLen = length(rest);
                    float radRef = std::max(1e-3f, g.verts[vi].radius);
                    float denom = std::max(restLen, radRef);
                    if (g.outputs[oi].type == OutputDef::OutX)
                        valueNorm = std::clamp((delta.x - rest.x) / denom, -1.0f, 1.0f);
                    else
                        valueNorm = std::clamp((delta.y - rest.y) / denom, -1.0f, 1.0f);
                }
                auto mapDelta = [](float v, const Range& r)->float{
                    if (!std::isfinite(v)) return 0.0f;
                    bool deg = (std::fabs(r.max - r.def) < 1e-6f) && (std::fabs(r.def - r.min) < 1e-6f);
                    if (deg) return v;
                    if (v >= 0.0f) return v * (r.max - r.def);
                    else           return v * (r.def - r.min);
                };
                float mapped = mapDelta(valueNorm, isAngle ? g.norm.angle : g.norm.position);
                float wgt = normalizeWeight(g.outputs[oi].weight);
                float s = g.outputs[oi].scale;
                float val = mapped * s * wgt * (g.outputs[oi].reflect ? -1.0f : 1.0f);
                val *= m_globalGain;
                if (g.kind == GroupKind::Hair) val *= 0.85f;
                g.curOutputs[oi] = val;
            }
        }

        // 完成本固定步：为下一固定步准备 prevOutputs
        for (auto &g : m_groups) {
            if (g.prevOutputs.size() == g.curOutputs.size())
                std::copy(g.curOutputs.begin(), g.curOutputs.end(), g.prevOutputs.begin());
        }

        m_timeAccumulator -= fixedStep;
    }

    // 帧尾：按 m_timeAccumulator 与 fixedStep 的比重在 prev/cur 之间插值，得到本帧输出
    float alpha = (float)(m_timeAccumulator / std::max(1e-9, fixedStep));
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    // 参数加权平均融合，避免多输出“碰撞”
    QVector<float> accumW(pcnt); accumW.fill(0.0f);
    for (int gi=0; gi<m_groups.size(); ++gi) {
        auto &g = m_groups[gi];
        for (int oi=0; oi<g.outputs.size(); ++oi) {
            int pidx = g.outputIndex[oi]; if (pidx < 0) continue;
            float blended = g.prevOutputs[oi] * (1.0f - alpha) + g.curOutputs[oi] * alpha;
            float wOut = normalizeWeight(g.outputs[oi].weight);
            accum[pidx] += blended * wOut;
            accumW[pidx] += wOut;
            m_touched.insert(pidx);
        }
    }

    // 平滑 + 软夹 + 应用（使用加权平均）
    float k = 1.0f - std::exp(-(float)std::min(std::max(dt, 0.0), 1.0/15.0) * 16.0f);
    for (int p : m_touched) {
        float denom = std::max(1e-6f, accumW[p]);
        float desired = accum[p] / denom;
        float prev = m_prevSmoothed[p];
        float smoothed = prev + (desired - prev) * k;
        float maxAbs = std::max(0.025f, (maxVals[p] - minVals[p]) * 0.12f);
        smoothed = std::clamp(smoothed, -maxAbs, maxAbs);
        float cur = params[p];
        float next = clampf(cur + (smoothed - prev), minVals[p], maxVals[p]);
        params[p] = next;
        m_prevSmoothed[p] = smoothed;
    }
}

void PhysicsEngine::reset(const QSharedPointer<Model> &model) {
    if (!model) { m_groups.clear(); m_valid = false; m_prevSmoothed.clear(); m_touched.clear(); m_timeAccumulator = 0.0; return; }
    int pcnt = (int)csmGetParameterCount(model->moc.model);
    auto params = const_cast<float*>(csmGetParameterValues(model->moc.model));
    auto minVals = csmGetParameterMinimumValues(model->moc.model);
    auto maxVals = csmGetParameterMaximumValues(model->moc.model);
    auto defVals = csmGetParameterDefaultValues(model->moc.model);

    // Reset all parameters that this physics can touch (union of outputs)
    QSet<int> outputs;
    for (const auto &g : m_groups) for (int oi=0; oi<g.outputs.size(); ++oi) {
        int pidx = g.outputIndex[oi]; if (pidx >= 0) outputs.insert(pidx);
    }
    for (int pidx : outputs) {
        params[pidx] = clampf(defVals[pidx], minVals[pidx], maxVals[pidx]);
    }
    // clear smoothing and accumulator
    m_prevSmoothed.fill(0.0f, pcnt);
    m_timeAccumulator = 0.0;
}

void PhysicsEngine::stabilize(const QSharedPointer<Model> &model) {
    if (!m_valid || !model) return;
    auto params = const_cast<float*>(csmGetParameterValues(model->moc.model));

    for (auto &g : m_groups) {
        float inX=0.0f, inY=0.0f, inA=0.0f;
        for (int i=0;i<g.inputs.size(); ++i) {
            int pidx = g.inputIndex[i]; if (pidx < 0) continue;
            float raw = params[pidx];
            float n = (g.inputs[i].type == InputDef::InAngle)
                      ? normSigned(raw, g.norm.angle)
                      : normSigned(raw, g.norm.position);
            float w = normalizeWeight(g.inputs[i].weight);
            float v = n * g.inputs[i].scale * w * (g.inputs[i].reflect ? -1.0f : 1.0f);
            switch (g.inputs[i].type) {
                case InputDef::InX: inX += v; break;
                case InputDef::InY: inY += v; break;
                case InputDef::InAngle: inA += v; break;
            }
        }
        auto rangeAbs = [](const Range& r){ return std::max(std::fabs(r.max - r.def), std::fabs(r.def - r.min)); };
        float posAmp = rangeAbs(g.norm.position); if (posAmp <= 1e-4f) posAmp = 10.0f;
        float angDeg = rangeAbs(g.norm.angle); if (angDeg <= 1e-4f) angDeg = 10.0f;
        const float deg2rad = 0.01745329252f;

        Vec2 anchorRest = g.verts[0].rest;
        Vec2 anchorPos { anchorRest.x + inX * posAmp, anchorRest.y + inY * posAmp };
        float aRadBase = inA * angDeg * deg2rad;
        float aRad = (g.kind == GroupKind::Hair) ? (aRadBase * 0.55f) : aRadBase; // keep consistent with update()

        if (g.particles.isEmpty()) continue;
        g.particles[0].pos = anchorPos;
        g.particles[0].prev = anchorPos;
        for (int vi=1; vi<g.particles.size(); ++vi) {
            Vec2 restSeg{ g.verts[vi].rest.x - g.verts[vi-1].rest.x,
                          g.verts[vi].rest.y - g.verts[vi-1].rest.y };
            float rlen = length(restSeg);
            Vec2 baseDir = rlen > 1e-5f ? Vec2{ restSeg.x / rlen, restSeg.y / rlen } : Vec2{1.0f, 0.0f};
            Vec2 dir = rotate(baseDir, aRad);
            float L = (rlen > 1e-3f) ? rlen : std::max(1e-3f, g.verts[vi].radius);
            Vec2 p { g.particles[vi-1].pos.x + dir.x * L,
                     g.particles[vi-1].pos.y + dir.y * L };
            g.particles[vi].pos = p;
            g.particles[vi].prev = p;
        }
    }
}
