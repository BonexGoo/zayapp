#include <boss.hpp>

#include <resource.hpp>
#include <platform/boss_platform.hpp>
#include <service/boss_zay.hpp>

extern String gLastLocale;
extern sint32 gMinWindowWidth;
extern sint32 gMinWindowHeight;

bool PlatformInit()
{
    #if BOSS_WINDOWS & !BOSS_NDEBUG
        if(Asset::RebuildForEmbedded())
            return false;
    #endif

    // 윈도우 불러오기
    String WindowInfoString = String::FromAsset("windowinfo.json");
    Context WindowInfo(ST_Json, SO_OnlyReference, WindowInfoString, WindowInfoString.Length());
    gLastLocale = WindowInfo("locale").GetText(Platform::Utility::GetLocaleBCP47());
    const sint32 ScreenID = WindowInfo("screen").GetInt(0);
    rect128 ScreenRect = {};
    Platform::Utility::GetScreenRect(ScreenRect, ScreenID);
    const sint32 ScreenWidth = ScreenRect.r - ScreenRect.l;
    const sint32 ScreenHeight = ScreenRect.b - ScreenRect.t;
    const sint32 WindowWidth = WindowInfo("w").GetInt(1600);
    const sint32 WindowHeight = WindowInfo("h").GetInt(980);
    const sint32 WindowX = Math::Clamp(WindowInfo("x").GetInt((ScreenWidth - WindowWidth) / 2), 0, ScreenWidth - WindowWidth);
    const sint32 WindowY = Math::Clamp(WindowInfo("y").GetInt((ScreenHeight - WindowHeight) / 2), 0, ScreenHeight - WindowHeight);
    Platform::SetWindowRect(ScreenRect.l + WindowX, ScreenRect.t + WindowY, WindowWidth, WindowHeight);

    // 아틀라스 자동업데이트
    R::SetAtlasDir("image");
    Platform::AddProcedure(PE_100MSEC,
        [](payload data)->void
        {
            if(R::IsAtlasUpdated())
            {
                R::RebuildAll();
                Context AtlasInfo;
                R::SaveAtlas(AtlasInfo);
                AtlasInfo.SaveJson().ToAsset("atlasinfo.json", true);
                Platform::UpdateAllViews();
            }
        });

    // 윈도우 설정
    Platform::InitForMDI(true);
    Platform::SetViewCreator(ZayView::Creator);
    Platform::SetWindowName("ZayApp");
    Platform::SetWindowView("ZayAppView");
    return true;
}

void PlatformQuit()
{
    // 윈도우 저장하기
    const rect128 WindowRect = Platform::GetWindowRect(true);
    const sint32 ScreenID = Platform::Utility::GetScreenID(
        {(WindowRect.l + WindowRect.r) / 2, (WindowRect.t + WindowRect.b) / 2});
    rect128 ScreenRect = {};
    Platform::Utility::GetScreenRect(ScreenRect, ScreenID);
    Context WindowInfo;
    WindowInfo.At("locale").Set(gLastLocale);
    WindowInfo.At("screen").Set(String::FromInteger(ScreenID));
    WindowInfo.At("x").Set(String::FromInteger(WindowRect.l - ScreenRect.l));
    WindowInfo.At("y").Set(String::FromInteger(WindowRect.t - ScreenRect.t));
    WindowInfo.At("w").Set(String::FromInteger(WindowRect.r - WindowRect.l));
    WindowInfo.At("h").Set(String::FromInteger(WindowRect.b - WindowRect.t));
    WindowInfo.SaveJson().ToAsset("windowinfo.json", true);
}

void PlatformFree()
{
}
