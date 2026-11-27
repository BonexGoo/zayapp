#pragma once
#include "zayglue.hpp"

class GlueTable : public BOSS::ZayGlue
{
public:
    bool Register(BOSS::ZaySon& zayson,
        const BOSS::ZayGlue::Bindings& bindings) override;
};
