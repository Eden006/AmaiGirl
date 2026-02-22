#pragma once
#include <QString>
#include <QVector>
#include <QHash>
#include <QJsonObject>
#include <QJsonDocument>
#include "engine/Model.hpp"

struct ExpressionParam {
    enum Blend { Add, Multiply, Overwrite };
    QString id;
    float value{0.0f};
    Blend blend{Overwrite};
};

struct Expression {
    QString name;
    float fadeIn{0.0f};
    float fadeOut{0.0f};
    QVector<ExpressionParam> params;

    // Apply expression with weight w in [0,1]
    // baseValues: pointer to parameter values snapshot before any expression contribution this frame; when nullptr, current values are used as base.
    void apply(Model* model, float w, const float* baseValues = nullptr) const;
};

struct ExpressionLoader {
    static Expression load(const QString& path);
};
