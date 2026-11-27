#pragma once
#include "zayglue.hpp"
#include <service/boss_zaywidget.hpp>
#include "glue_native_state.hpp"

class GlueNavigationAndDepth : public BOSS::ZayGlue
{
public:
    bool Register(BOSS::ZaySon& zayson, const BOSS::ZayGlue::Bindings& bindings) override;
};
