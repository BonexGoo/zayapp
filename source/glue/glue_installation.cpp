#include <boss.hpp>
#include "glue_installation.hpp"
#include <service/boss_zaywidget.hpp>

ZAY_DECLARE_GLUE_CLASS("installation", GlueInstallation)

bool GlueInstallation::Register(ZaySon& NewZayson, const ZayGlue::Bindings& bindings)
{
    const auto BeginInstall = bindings.host.Find<void(chars, chars)>("install.begin");
    const auto InstallPath = bindings.host.Find<String()>("install.path");
    const auto SendStatus = bindings.host.Find<void(chars)>("install.status");
    if(!BeginInstall || !InstallPath || !SendStatus)
    {
        BOSS_TRACE("[installation] Missing required typed host operations");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // installwpk
    NewZayson.AddGlue("installwpk", ZAY_DECLARE_GLUE(params, BeginInstall)
    {
        if(params.ParamCount() == 3)
        {
            auto WpkPath = params.Param(0).ToText();
            auto DomNameD = params.Param(1).ToText();
            auto Exclusions = params.Param(2).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                ZayWidgetDOM::SetValue(DomNameD.Offset(2) + ".status", "'run'");
                (*BeginInstall)(WpkPath, Exclusions);
            }
        }
    }, "[WpkPath:'E:/hxz_1.wpk'][DomNameD:'d.install']#[Exclusions:'app_win|fw_main|fw_pixel']", "WPK파일로 재설치하기");

    ////////////////////////////////////////////////////////////////////////////////
    // installsector
    NewZayson.AddGlue("installsector", ZAY_DECLARE_GLUE(params, InstallPath)
    {
        if(params.ParamCount() == 3)
        {
            auto FileOffset = params.Param(0).ToInteger();
            auto DataCount = params.Param(1).ToInteger();
            auto DataUnit = Math::Clamp(params.Param(2).ToInteger(), 1, 8);
            if(auto FilePath = (*InstallPath)())
            if(auto OldFile = Platform::File::OpenForRead(FilePath))
            {
                Platform::File::Seek(OldFile, FileOffset);
                uint32 SectorCrc = 0xFFFFFFFF;
                for(sint32 i = 0; i < DataCount; ++i)
                {
                    uint64 DataPool = oxFFFFFFFFFFFFFFFF;
                    Platform::File::Read(OldFile, (uint08*) &DataPool, DataUnit);
                    ZayWidgetDOM::SetValue(String::Format("sector.%d", i), String::FromInteger(DataPool));
                    SectorCrc = UpdateCrc((bytes) &DataPool, DataUnit, SectorCrc);
                }
                Platform::File::Close(OldFile);
                ZayWidgetDOM::SetValue("sector.unit", String::FromInteger(DataUnit));
                ZayWidgetDOM::SetValue("sector.count", String::FromInteger(DataCount));
                if(params.CanReturn())
                    params.Return(String::FromInteger(SectorCrc & 0xFFFF));
            }
        }
    }, "[FileOffset:30][DataCount:100][DataUnit:2]", "WPK파일 재설치중 포커싱된 파일의 일부를 DOM에 전개");

    ////////////////////////////////////////////////////////////////////////////////
    // installstatus
    NewZayson.AddGlue("installstatus", ZAY_DECLARE_GLUE(params, SendStatus)
    {
        if(params.ParamCount() == 1)
        {
            auto Status = params.Param(0).ToText();
            (*SendStatus)(Status);
        }
    }, "[Status:'BootLoaderEnd']", "WPK파일 재설치중 상태업데이트");
    return true;
}

uint32 GlueInstallation::UpdateCrc(bytes data, sint32 datalen, uint32 oldcrc)
{
    static uint32 CrcTable[256] = {0};
    if(CrcTable[255] == 0)
    for(uint32 i = 0; i < 256; ++i)
    {
        uint32 crc = i;
        for(sint32 j = 0; j < 8; ++j)
        {
            if(crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
        CrcTable[i] = crc;
    }
    uint32 crc = oldcrc;
    for(sint32 i = 0; i < datalen; ++i)
        crc = CrcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}
