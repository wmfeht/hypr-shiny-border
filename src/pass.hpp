#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/helpers/Color.hpp>

class CShinyPassElement : public IPassElement {
  public:
    struct SData {
        CBox       box; // scaled, monitor-local, outer
        CHyprColor colA;
        CHyprColor colB;
        Vector2D   pointer = {}; // scaled, same space as box (pre-GL transform)
        float      angle   = 0.f;
        float      a       = 1.f;
        float      roundingPower = 2.f;
        float      time    = 0.f;
        float      pulseHz = 0.4f;
        float      lobe    = 0.18f;
        int        round      = 0;
        int        outerRound = 0;
        int        borderSize = 3;
    };

    CShinyPassElement(const SData& data);
    virtual ~CShinyPassElement() = default;

    virtual std::vector<UP<IPassElement>> draw() override;
    virtual bool                          needsLiveBlur() override;
    virtual bool                          needsPrecomputeBlur() override;
    virtual std::optional<CBox>           boundingBox() override;
    virtual CRegion                       opaqueRegion() override;
    virtual bool                          disableSimplification() override;

    virtual const char*                   passName() override {
        return "CShinyPassElement";
    }

    virtual ePassElementType type() override {
        return EK_CUSTOM;
    }

    SData m_data;
};

bool ensureShinyShader();
void destroyShinyShader();
