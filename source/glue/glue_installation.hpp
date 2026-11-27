#pragma once
#include "zayglue.hpp"

class GlueInstallation : public BOSS::ZayGlue
{
public:
    bool Register(BOSS::ZaySon& zayson,
        const BOSS::ZayGlue::Bindings& bindings) override;
    static uint32 UpdateCrc(bytes data, sint32 datalen, uint32 oldcrc = 0xFFFFFFFF);
};
