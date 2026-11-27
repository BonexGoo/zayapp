#pragma once
#include "zayglue.hpp"
#include <service/boss_zaywidget.hpp>

class GlueDom : public BOSS::ZayGlue
{
public:
    bool Register(BOSS::ZaySon& zayson,
        const BOSS::ZayGlue::Bindings& context) override;
};
