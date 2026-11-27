#pragma once
#include "zayglue.hpp"
#include <service/boss_zaywidget.hpp>
#include "glue_native_state.hpp"

class GlueDepthScene : public BOSS::ZayGlue
{
public:
    bool Register(BOSS::ZaySon& zayson, const BOSS::ZayGlue::Bindings& bindings) override;
private:
    typedef std::function<bool(chars, const Image*&, sint32&, sint32&)> ImageFinder;
    typedef std::function<id_animate(chars)> APngGetter;
    static bool RenderDepthResource(const std::shared_ptr<ZayGlueNativeState>& state,
        const std::shared_ptr<ImageFinder>& FindImage,
        const std::shared_ptr<APngGetter>& GetAPng, const ZayWidget* widget,
        const ZayExtend::Payload& params, bool sprite);
};
