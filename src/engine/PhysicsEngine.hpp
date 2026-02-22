#pragma once
#include <QSharedPointer>
#include <QVector>
#include <QHash>
#include <QString>
#include <QSet>
#include <cmath>
#include <optional>
#include "engine/Model.hpp"

// A lightweight physics engine inspired by Cubism physics3.
class PhysicsEngine {
public:
    struct Options {
        Vec2 Gravity{0.0f, -1.0f};
        Vec2 Wind{0.0f, 0.0f};
    };

    PhysicsEngine() = default;
    ~PhysicsEngine() = default;

    // Parse physics3.json from model's FileReferences and build runtime. Safe to call repeatedly.
    void init(const QSharedPointer<Model>& model);

    // Update and apply physics-driven parameter deltas to the Core model.
    void update(double dt, const QSharedPointer<Model>& model);

    // Reset: clear runtime and restore parameters touched by physics to their default values.
    void reset(const QSharedPointer<Model>& model);

    // Stabilize internal particles based on current inputs so first Evaluate won't jerk.
    void stabilize(const QSharedPointer<Model>& model);

    bool isValid() const { return m_valid; }

    // CubismPhysics::Options equivalent accessors
    Options getOptions() const { return Options{ m_gravity, m_wind }; }
    void setOptions(const Options& opt) { m_gravity = opt.Gravity; m_wind = opt.Wind; }

    // Global gain to tame overall amplitude [0..1], default 0.7
    float globalGain() const { return m_globalGain; }
    void setGlobalGain(float g) { m_globalGain = std::clamp(g, 0.0f, 1.0f); }

private:
    struct InputDef {
        enum Type { InX, InY, InAngle } type{InX};
        QString paramId; // source parameter id
        float scale{1.0f};
        float weight{1.0f}; // assume percentage-like [0..1]
        bool reflect{false};
    };
    struct OutputDef {
        enum Type { OutX, OutY, OutAngle } type{OutAngle};
        QString paramId; // destination parameter id
        int vertexIndex{0};
        float scale{1.0f};
        float weight{1.0f};
        bool reflect{false};
    };
    struct Range { float min{0}, max{0}, def{0}; };
    struct Normalization { Range position, angle; };
    struct VertexCfg { Vec2 rest{0,0}; float mobility{1.0f}; float delay{0.2f}; float acceleration{1.0f}; float radius{10.0f}; };
    struct Particle { Vec2 pos{0,0}; Vec2 prev{0,0}; Vec2 vel{0,0}; };
    enum class GroupKind { Generic, Hair, Cloth, Accessory, Body };
    struct Group {
        QVector<InputDef> inputs;
        QVector<OutputDef> outputs;
        QVector<VertexCfg> verts;
        QVector<Particle> particles;
        Normalization norm;
        float friction{0.5f};
        // cached param indices
        QVector<int> inputIndex;
        QVector<int> outputIndex;
        // smoothed inputs
        float inX_s{0.0f}, inY_s{0.0f}, inA_s{0.0f};
        // heuristic kind
        GroupKind kind{GroupKind::Generic};
        // output interpolation buffers (size == outputs.size())
        QVector<float> prevOutputs; // physics result at previous fixed step
        QVector<float> curOutputs;  // physics result at current fixed step
    };

    // runtime
    bool m_valid{false};
    QVector<Group> m_groups;
    Vec2 m_gravity{0.0f, -1.0f};
    Vec2 m_wind{0.0f, 0.0f};
    float m_globalGain{0.58f};
    float m_targetHz{180.0f}; // substep frequency, may be derived from Meta.Fps
    float m_hairGravityScale{0.12f}; // stronger gravity for hair to tame swing amplitude
    double m_timeAccumulator{0.0};   // fixed-step accumulator to reduce frame jitter
    double m_frameDtClamped{0.0};    // current frame clamped dt for input interpolation

    // per-parameter smoothed delta (relative to default), and last touched set
    QVector<float> m_prevSmoothed;
    QSet<int> m_touched;
    // parameter caches for input interpolation
    QVector<float> m_paramCache;       // evolving towards current params within frame
    QVector<float> m_paramInputCache;  // last cache snapshot used as previous

    // helpers
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float normSigned(float v, const Range& r) {
        if (v > r.def) { float d = (r.max - r.def); return d != 0 ? (v - r.def)/d : 0.0f; }
        if (v < r.def) { float d = (r.def - r.min); return d != 0 ? (v - r.def)/d : 0.0f; }
        return 0.0f;
    }
    static Vec2 rotate(const Vec2& v, float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return { v.x * c - v.y * s, v.x * s + v.y * c };
    }
    static float length(const Vec2& v) { return std::sqrt(v.x*v.x + v.y*v.y); }
    static float normalizeWeight(float w) {
        // Treat values >2 as percentage (e.g. 100 -> 1.0), else use as-is.
        if (w > 2.0f) w *= 0.01f;
        return std::clamp(w, 0.0f, 1.0f);
    }
};
