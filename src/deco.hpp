#pragma once

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>

class CShinyBorder : public IHyprWindowDecoration {
  public:
    CShinyBorder(PHLWINDOW window);
    virtual ~CShinyBorder();

    virtual SDecorationPositioningInfo getPositioningInfo() override;
    virtual void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
    virtual void                       draw(PHLMONITOR pMonitor, float const& a) override;
    virtual eDecorationType            getDecorationType() override;
    virtual void                       updateWindow(PHLWINDOW pWindow) override;
    virtual void                       damageEntire() override;
    virtual eDecorationLayer           getDecorationLayer() override;
    virtual uint64_t                   getDecorationFlags() override;
    virtual std::string                getDisplayName() override;

    void                               setAngle(float radians);
    float                              angle() const;
    void                               syncPulse();

  private:
    PHLWINDOWREF        m_window;
    CBox                m_assignedGeometry = {};
    SBoxExtents         m_extents          = {};
    Vector2D            m_lastPos;
    Vector2D            m_lastSize;
    float               m_angle     = 0.f; // radians, math convention: 0 = +x
    int                 m_lastSizeB = -1;
    SP<CEventLoopTimer> m_pulseTimer;

    int                 borderSize() const;
    CBox                assignedBoxGlobal();
    bool                pulseWanted() const;
    void                startPulse();
    void                stopPulse();
    void                onPulseTick(SP<CEventLoopTimer> self);
};
