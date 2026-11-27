#include <boss.hpp>
#include "glue_device_channel.hpp"
#include <service/boss_zaywidget.hpp>

ZAY_DECLARE_GLUE_CLASS("device_channel", GlueDeviceChannel)

bool GlueDeviceChannel::Register(ZaySon& NewZayson, const ZayGlue::Bindings& bindings)
{
    const auto ConnectSerial = bindings.host.Find<bool(chars, sint32)>("channel.connect_serial");
    const auto CloseChannel = bindings.host.Find<void(chars)>("channel.close");
    const auto BuildPacket = bindings.host.Find<void(chars, uint08s&)>("channel.build_packet");
    const auto Write = bindings.host.Find<bool(chars, bytes, sint32)>("channel.write");
    const auto LogPacket = bindings.host.Find<void(chars, chars, bytes, sint32, chars)>("channel.log_packet");
    const auto Devices = bindings.host.Find<Map<String>&()>("device.bindings");
    const auto DevicesChanged = bindings.host.Find<void()>("device.changed");
    auto* view = bindings.view;
    if(!ConnectSerial || !CloseChannel || !BuildPacket || !Write ||
        !LogPacket || !Devices || !DevicesChanged || !view)
    {
        BOSS_TRACE("[device_channel] Missing required typed host operations/view");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // attachserialonce
    NewZayson.AddGlue("attachserialonce", ZAY_DECLARE_GLUE(params)
    {
        Platform::Serial::AttachOnce();
    }, nullptr, "시리얼포트 권한추가(WASM전용)");

    ////////////////////////////////////////////////////////////////////////////////
    // connectserial
    NewZayson.AddGlue("connectserial", ZAY_DECLARE_GLUE(params, ConnectSerial, view)
    {
        if(1 <= params.ParamCount())
        {
            auto Port = params.Param(0).ToText();
            auto Baudrate = (1 < params.ParamCount())? params.Param(1).ToInteger() : -1;
            (*ConnectSerial)(Port, Baudrate);
            view->invalidate();
        }
    }, "[Port:'COM1']#[Baudrate:115200]", "시리얼포트 연결");

    ////////////////////////////////////////////////////////////////////////////////
    // closechannel
    NewZayson.AddGlue("closechannel", ZAY_DECLARE_GLUE(params, CloseChannel, view)
    {
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            (*CloseChannel)(Ch);
            view->invalidate();
        }
    }, "[Ch:'COM1']", "채널 닫기");

    ////////////////////////////////////////////////////////////////////////////////
    // sendchannel
    NewZayson.AddGlue("sendchannel", ZAY_DECLARE_GLUE(params, BuildPacket, Write, LogPacket, view)
    {
        view->clearCapture();
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            uint08s NewPacket;
            (*BuildPacket)(Ch, NewPacket);
            if(0 < NewPacket.Count())
            {
                if((*Write)(Ch, (bytes) &NewPacket[0], NewPacket.Count()))
                    (*LogPacket)(Ch, "(send CMD)", &NewPacket[0], NewPacket.Count(), "#800080");
                else (*LogPacket)(Ch, "(send-failed CMD)", &NewPacket[0], NewPacket.Count(), "#800000");
            }
            view->invalidate();
        }
    }, "[Ch:'COM1']", "채널로 패킷송신");

    ////////////////////////////////////////////////////////////////////////////////
    // adddevice
    NewZayson.AddGlue("adddevice", ZAY_DECLARE_GLUE(params, Devices, DevicesChanged, view)
    {
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            if(Ch != "x")
            {
                String FindedDeviceName;
                for(sint32 i = 1; i < 1000; ++i)
                {
                    const String CurDeviceName = String::Format("%03d", i);
                    if(auto CurCh = (*Devices)().Access(CurDeviceName))
                    {
                        if(*CurCh == Ch)
                        {
                            FindedDeviceName = CurDeviceName;
                            break;
                        }
                    }
                    else if(FindedDeviceName.Length() == 0)
                        FindedDeviceName = CurDeviceName;
                }
                if(0 < FindedDeviceName.Length())
                {
                    (*Devices)()(FindedDeviceName) = Ch;
                    (*DevicesChanged)();
                }
                view->invalidate();
            }
        }
    }, "[Ch:'COM1']", "채널로 장치추가");

    ////////////////////////////////////////////////////////////////////////////////
    // subdevice
    NewZayson.AddGlue("subdevice", ZAY_DECLARE_GLUE(params, Devices, DevicesChanged, view)
    {
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            for(sint32 i = 1; i < 1000; ++i)
            {
                const String CurDeviceName = String::Format("%03d", i);
                if(auto CurCh = (*Devices)().Access(CurDeviceName))
                if(*CurCh == Ch)
                {
                    (*Devices)().Remove(CurDeviceName);
                    (*DevicesChanged)();
                    break;
                }
            }
            view->invalidate();
        }
    }, "[Ch:'COM1']", "채널로 장치제거");
    return true;
}
