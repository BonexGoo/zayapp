#include <boss.hpp>
#include "glue_media.hpp"

ZAY_DECLARE_GLUE_CLASS("media", GlueMedia)

bool GlueMedia::Register(ZaySon& NewZayson, const ZayGlue::Bindings& context)
{
    const auto Sounds = context.host.Find<Map<id_sound>&()>("sound.handles");
    const auto SoundDeviceFocus = context.host.Find<sint32&()>("sound.device_focus");
    const auto SoundMuted = context.host.Find<bool&()>("sound.muted");
    const auto UpdateSoundDeviceList = context.host.Find<void(sint32)>("sound.select_device");
    const auto GetAPng = context.host.Find<id_animate(chars)>("animation.get_apng");
    if(!Sounds || !SoundDeviceFocus || !SoundMuted || !UpdateSoundDeviceList || !GetAPng)
    {
        BOSS_TRACE("[media] Missing required GLUE host callback/view");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // bindsounddevice
    NewZayson.AddGlue("bindsounddevice", ZAY_DECLARE_GLUE(params, Sounds, UpdateSoundDeviceList)
    {
        if(params.ParamCount() == 1)
        {
            auto DeviceIndex = params.Param(0).ToInteger();
            (*UpdateSoundDeviceList)(DeviceIndex);
            for(sint32 i = 0, iend = (*Sounds)().Count(); i < iend; ++i)
                if(auto CurSound = (*Sounds)().AccessByOrder(i))
                    Platform::Sound::SetDevice(*CurSound, DeviceIndex);
        }
    }, "[DeviceIndex:0]", "사운드장치선택");

    ////////////////////////////////////////////////////////////////////////////////
    // mutesound
    NewZayson.AddGlue("mutesound", ZAY_DECLARE_GLUE(params, SoundMuted, Sounds)
    {
        if(params.ParamCount() == 1)
        {
            auto MuteFlag = (params.Param(0).ToInteger() != 0);
            (*SoundMuted)() = MuteFlag;
            if((*SoundMuted)())
            for(sint32 i = 0, iend = (*Sounds)().Count(); i < iend; ++i)
                if(auto CurSound = (*Sounds)().AccessByOrder(i))
                    Platform::Sound::Stop(*CurSound);
        }
    }, "[MuteFlag:1]", "사운드무음설정");

    ////////////////////////////////////////////////////////////////////////////////
    // playsound
    NewZayson.AddGlue("playsound", ZAY_DECLARE_GLUE(params, Sounds, SoundDeviceFocus, SoundMuted)
    {
        if(1 <= params.ParamCount())
        {
            const String SoundAssetName = params.Param(0).ToText();
            auto Volume = (1 < params.ParamCount())? params.Param(1).ToFloat() : 1.0;
            if(!(*SoundMuted)())
            {
                if(!(*Sounds)().Access(SoundAssetName))
                {
                    #if BOSS_ANDROID | BOSS_IPHONE
                        buffer NewSoundData = Asset::ToBuffer(SoundAssetName);
                        (*Sounds)()(SoundAssetName) = Platform::Sound::OpenForData((bytes) NewSoundData,
                            Buffer::SizeOf(NewSoundData) * Buffer::CountOf(NewSoundData));
                        Buffer::Free(NewSoundData);
                    #elif BOSS_WASM
                        (*Sounds)()(SoundAssetName) = Platform::Sound::OpenForUrl(SoundAssetName, false);
                    #else
                        const String Path = Asset::GetFilePath(SoundAssetName);
                        if(Path.Length()) (*Sounds)()(SoundAssetName) = Platform::Sound::OpenForFile(Path, false);
                    #endif
                    if((*SoundDeviceFocus)() != -1 && (*Sounds)().Access(SoundAssetName) && (*Sounds)()(SoundAssetName))
                        Platform::Sound::SetDevice((*Sounds)()(SoundAssetName), (*SoundDeviceFocus)());
                }
                auto CurSoundID = (*Sounds)()(SoundAssetName);
                if(!CurSoundID) return;
                if(Platform::Sound::NowPlaying(CurSoundID))
                    Platform::Sound::Stop(CurSoundID);
                Platform::Sound::SetVolume(CurSoundID, Volume);
                Platform::Sound::Play(CurSoundID);
            }
        }
    }, "[SoundAssetName:'sound/sound1.mp3']#[Volume:1.0]", "사운드시작");

    ////////////////////////////////////////////////////////////////////////////////
    // stopsound
    NewZayson.AddGlue("stopsound", ZAY_DECLARE_GLUE(params, Sounds)
    {
        if(params.ParamCount() == 1)
        {
            const String SoundAssetName = params.Param(0).ToText();
            if(auto CurSoundID = (*Sounds)().Access(SoundAssetName))
            if(Platform::Sound::NowPlaying(*CurSoundID))
                Platform::Sound::Stop(*CurSoundID);
        }
    }, "[SoundAssetName:'sound/sound1.mp3']", "사운드중지");

    ////////////////////////////////////////////////////////////////////////////////
    // readyapng
    NewZayson.AddGlue("readyapng", ZAY_DECLARE_GLUE(params, GetAPng)
    {
        id_animate handle = nullptr;
        if(params.ParamCount() == 1)
            handle = (*GetAPng)(params.Param(0).ToText());
        if(params.CanReturn())
            params.Return(handle && Platform::Animate::GetFrameCount(handle) > 0 ? "1" : "0");
    }, "[APngAssetName:'image/animation.apng']", "APNG 사전로딩. 재생 가능한 프레임이 준비되면 1");
    return true;
}
