#pragma once
#include <QString>
#include <QJsonObject>
#include <QSharedPointer>
#include <vector>
#include "engine/Live2DCore.hpp"
#include "common/Utils.hpp"

// Mirror of Go code motion parsing and evaluation

enum CurveType { Linear=0, Bezier=1, Stepped=2, InverseStepped=3 };

struct MotionCurveSegmentPoint { double time{0}, value{0}; };

struct MotionSegment {
    int type{0};
    double value{0}; // for stepped types
    std::vector<MotionCurveSegmentPoint> pts;
};

struct MotionCurve {
    QString target; // PartOpacity/Parameter/Model
    QString id;
    double fadeIn{-1};
    double fadeOut{-1};
    std::vector<MotionSegment> segments;
};

struct MotionMeta { double duration{0}; double fps{30}; bool loop{false}; };

struct Motion {
    MotionMeta meta;
    double fadeIn{0};
    double fadeOut{0};
    QString sound;
    std::vector<MotionCurve> curves;
};

class MotionLoader {
public:
    static Motion load(const QString& path);
};

// Evaluator applies motion to csmModel
class MotionPlayer {
public:
    explicit MotionPlayer(QSharedPointer<struct Model> model);
    void playRandom(const QString& group, bool loop);
    void stop();
    void update(double dt);

private:
    QSharedPointer<struct Model> m_model;
    Motion m_motion;
    bool m_hasMotion{false};
    bool m_loop{false};
    double m_timer{0};
};
