#include "engine/Motion.hpp"
#include "engine/Model.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <random>
#include <cstdint>

static MotionCurve parseCurve(const QJsonObject& o) {
    MotionCurve c;
    c.target = o.value("Target").toString();
    c.id = o.value("Id").toString();
    if (o.contains("FadeInTime")) c.fadeIn = o.value("FadeInTime").toDouble(-1);
    if (o.contains("FadeOutTime")) c.fadeOut = o.value("FadeOutTime").toDouble(-1);
    auto seg = o.value("Segments").toArray();
    if (seg.size() < 2) return c;
    MotionCurveSegmentPoint last{ seg[0].toDouble(), seg[1].toDouble() };
    int i = 2;
    while (i < seg.size()) {
        int type = seg[i++].toInt();
        MotionSegment s; s.type = type;
        if (type == Linear) {
            MotionCurveSegmentPoint next{ seg[i].toDouble(), seg[i+1].toDouble() };
            s.pts = { last, next };
            last = next; i += 2;
        } else if (type == Bezier) {
            MotionCurveSegmentPoint p1{ seg[i].toDouble(), seg[i+1].toDouble() };
            MotionCurveSegmentPoint p2{ seg[i+2].toDouble(), seg[i+3].toDouble() };
            MotionCurveSegmentPoint next{ seg[i+4].toDouble(), seg[i+5].toDouble() };
            s.pts = { last, p1, p2, next };
            last = next; i += 6;
        } else if (type == Stepped) {
            MotionCurveSegmentPoint next{ seg[i].toDouble(), seg[i+1].toDouble() };
            s.pts = { last }; s.value = next.time; i += 2;
            last = next;
        } else if (type == InverseStepped) {
            MotionCurveSegmentPoint next{ seg[i].toDouble(), seg[i+1].toDouble() };
            s.pts = { last }; s.value = last.time; i += 2;
            last = next;
        }
        c.segments.push_back(std::move(s));
    }
    return c;
}

Motion MotionLoader::load(const QString &path) {
    auto doc = jsonFromFile(path);
    Motion m;
    auto meta = doc.object().value("Meta").toObject();
    m.meta.duration = meta.value("Duration").toDouble();
    m.meta.fps = meta.value("Fps").toDouble(30);
    m.meta.loop = meta.value("Loop").toBool(false);

    auto arr = doc.object().value("Curves").toArray();
    for (const auto cv : arr) {
        m.curves.push_back(parseCurve(cv.toObject()));
    }
    return m;
}

static double lerp(double a, double b, double t){ return a + (b-a)*t; }
static MotionCurveSegmentPoint lerpPt(const MotionCurveSegmentPoint &p1, const MotionCurveSegmentPoint &p2, double t){ return { lerp(p1.time,p2.time,t), lerp(p1.value,p2.value,t)}; }

static const MotionSegment* findSegment(const std::vector<MotionSegment>& segs, double t) {
    for (auto &s : segs) {
        if (s.type == Linear) {
            if (s.pts[0].time <= t && s.pts[1].time >= t) return &s;
        } else if (s.type == Bezier) {
            if (s.pts[0].time <= t && s.pts[3].time >= t) return &s;
        } else if (s.type == Stepped) {
            if (s.pts[0].time <= t && s.value >= t) return &s;
        } else if (s.type == InverseStepped) {
            if (s.value <= t && s.pts[0].time >= t) return &s;
        }
    }
    return nullptr;
}

static double segmentValue(const MotionSegment* s, double t) {
    if (!s) return 0.0;
    if (s->type == Linear) {
        double r = std::max((t - s->pts[0].time) / (s->pts[1].time - s->pts[0].time), 0.0);
        return s->pts[0].value + r * (s->pts[1].value - s->pts[0].value);
    } else if (s->type == Bezier) {
        double r = std::max((t - s->pts[0].time) / (s->pts[3].time - s->pts[0].time), 0.0);
        auto p01 = lerpPt(s->pts[0], s->pts[1], r);
        auto p12 = lerpPt(s->pts[1], s->pts[2], r);
        auto p23 = lerpPt(s->pts[2], s->pts[3], r);
        auto p02 = lerpPt(p01, p12, r);
        auto p13 = lerpPt(p12, p23, r);
        return lerpPt(p02, p13, r).value;
    } else {
        return s->pts[0].value;
    }
}

MotionPlayer::MotionPlayer(QSharedPointer<Model> model) : m_model(std::move(model)) {}

void MotionPlayer::playRandom(const QString &group, bool loop) {
    auto files = m_model->motions.value(group);
    ensure(!files.isEmpty(), QString("No motions for group: %1").arg(group));
    std::random_device rd; std::mt19937 gen(rd()); std::uniform_int_distribution<> dis(0, files.size()-1);
    QString path = files[dis(gen)];
    m_motion = MotionLoader::load(path);
    m_hasMotion = true;
    m_loop = loop;
    m_timer = 0;
}

void MotionPlayer::stop() { m_hasMotion = false; }

void MotionPlayer::update(double dt) {
    if (!m_hasMotion) return;
    m_timer += dt;
    if (m_motion.meta.duration > 0 && m_timer > m_motion.meta.duration) {
        if (!m_loop) { m_hasMotion = false; return; }
        m_timer = 0;
    }

    for (auto &c : m_motion.curves) {
        const MotionSegment* seg = findSegment(c.segments, m_timer);
        if (!seg) continue;
        double value = segmentValue(seg, m_timer);
        if (c.target == "PartOpacity") {
            int32_t count = (int32_t)csmGetPartCount(m_model->moc.model);
            const char** ids = csmGetPartIds(m_model->moc.model);
            int idx = -1;
            for (int i = 0; i < count; ++i) if (QString::fromUtf8(ids[i]) == c.id) { idx = i; break; }
            if (idx >= 0) {
                float* opas = const_cast<float*>(csmGetPartOpacities(m_model->moc.model));
                opas[idx] = (float)value;
            }
        } else if (c.target == "Parameter") {
            int32_t count = (int32_t)csmGetParameterCount(m_model->moc.model);
            const char** ids = csmGetParameterIds(m_model->moc.model);
            int idx = -1;
            for (int i = 0; i < count; ++i) if (QString::fromUtf8(ids[i]) == c.id) { idx = i; break; }
            if (idx >= 0) {
                float* vals = const_cast<float*>(csmGetParameterValues(m_model->moc.model));
                float old = vals[idx];
                float fin = c.fadeIn > 0 ? easingSine((float)(m_timer / c.fadeIn)) : 1.0f;
                float fout = c.fadeOut > 0 && m_motion.meta.duration > 0 ? easingSine((float)((m_motion.meta.duration - m_timer) / c.fadeOut)) : 1.0f;
                float nv = old + (float)(fin * fout) * (float(value) - old);
                vals[idx] = nv;
            }
        }
    }

    csmResetDrawableDynamicFlags(m_model->moc.model);
    csmUpdateModel(m_model->moc.model);

    // sync dynamic fields
    int32_t count = (int32_t)csmGetDrawableCount(m_model->moc.model);
    const uint8_t* dflags = csmGetDrawableDynamicFlags(m_model->moc.model);
    const int32_t* orders = csmGetDrawableRenderOrders(m_model->moc.model);
    const float* opacities = csmGetDrawableOpacities(m_model->moc.model);
    const csmVector2** positions = csmGetDrawableVertexPositions(m_model->moc.model);

    for (int i = 0; i < count; ++i) {
        auto &d = m_model->drawables[i];
        d.dflag = dflags[i];
        d.order = orders[i];
        d.opacity = opacities[i];
        if (d.dflag & (1 << 5)) { // VertexPositionChange
            int vc = (int)csmGetDrawableVertexCounts(m_model->moc.model)[i];
            d.pos.resize(vc);
            for (int v = 0; v < vc; ++v) d.pos[v] = { positions[i][v].X, positions[i][v].Y };
        }
    }
}
