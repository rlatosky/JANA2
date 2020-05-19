//
// Created by Nathan Brei on 5/19/20.
//

#ifndef JANA2_DSTEXAMPLE_DATAOBJECTS_H
#define JANA2_DSTEXAMPLE_DATAOBJECTS_H

#include <JANA/JObject.h>

/// Renderable is another base class. We strongly encourage users to
/// stick to _implementation_inheritance_, aka no member variables and all
/// member functions are pure virtual.
struct Renderable {
    virtual void Render() = 0;
    virtual ~Renderable() = default;
};

struct MyJObject : public JObject {
    int x;
    int y;
    double E;

    void Summarize(JObjectSummary& summary) const override {
        summary.add(x, NAME_OF(x), "%d", "Pixel coordinates centered around 0,0");
        summary.add(y, NAME_OF(y), "%d", "Pixel coordinates centered around 0,0");
        summary.add(E, NAME_OF(E), "%f", "Energy loss in GeV");
    }

    const std::string className() const override {
        return NAME_OF_THIS;
    }
};

struct MyRenderable : public Renderable {
    int x;
    int y;
    double E;

    void Render() override {
        LOG << "MyRenderable::Render " << x << "," << y << "," << E << LOG_END;
    }
};

struct MyRenderableJObject : public JObject, Renderable {
    int x;
    int y;
    double E;

    /// Renderable vfunctions

    void Render() override {
        LOG << "MyRenderableJObject::Render " << x << "," << y << "," << E << LOG_END;
    }

    /// JObject vfunctions

    void Summarize(JObjectSummary& summary) const override {
        summary.add(x, NAME_OF(x), "%d", "Pixel coordinates centered around 0,0");
        summary.add(y, NAME_OF(y), "%d", "Pixel coordinates centered around 0,0");
        summary.add(E, NAME_OF(E), "%f", "Energy loss in GeV");
    }

    const std::string className() const override {
        return NAME_OF_THIS;
    }
};

#endif //JANA2_DATAOBJECTS_H
