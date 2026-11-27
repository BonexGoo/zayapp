#include <boss.hpp>
#include "glue_rendering.hpp"

ZAY_DECLARE_GLUE_CLASS("rendering", GlueRendering)

bool GlueRendering::Register(ZaySon& NewZayson, const ZayGlue::Bindings& context)
{
    const auto KeepUpdatingUntil = context.host.Find<void(uint64)>("view.update_until");
    const auto RenderHintChanged = context.host.Find<void(bool)>("render.hint_changed");
    const auto CaptureWindow = context.host.Find<void(chars, sint32, sint32, sint32, sint32, double, bool)>("resource.capture_window");
    auto* view = context.view;
    if(!KeepUpdatingUntil || !RenderHintChanged || !CaptureWindow || !view)
    {
        BOSS_TRACE("[rendering] Missing required GLUE host callback/view");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // update
    NewZayson.AddGlue("update", ZAY_DECLARE_GLUE(params, KeepUpdatingUntil) // 업데이트를 위한 글루함수를 추가
    {
        if(params.ParamCount() == 1)
        {
            auto Msec = sint32(params.Param(0).ToFloat() * 1000);
            const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + Msec;
            (*KeepUpdatingUntil)(NewUpdateMsec);
        }
    }, "[Msec]", "Msec동안 화면갱신유지");

    ////////////////////////////////////////////////////////////////////////////////
    // renderhint
    NewZayson.AddGlue("renderhint", ZAY_DECLARE_GLUE(params, RenderHintChanged, view)
    {
        if(params.ParamCount() == 2)
        {
            auto Smooth = (params.Param(0).ToInteger() != 0);
            auto Antialiasing = (params.Param(1).ToInteger() != 0);
            Platform::Graphics::SetRenderHint(Smooth, Antialiasing);
            (*RenderHintChanged)(Smooth);
            ZayWidgetDOM::SetValue("program.smooth", (Smooth)? "1" : "0");
            ZayWidgetDOM::SetValue("program.antialiasing", (Antialiasing)? "1" : "0");
            view->invalidate();
        }
    }, "[Smooth][Antialiasing]", "랜더링품질 설정");

    ////////////////////////////////////////////////////////////////////////////////
    // setrenderingtype: CPU/GPU 전환은 플랫폼이 다음 프레임 시작 시 반영합니다.
    NewZayson.AddGlue("setrenderingtype", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 1)
        {
            const String Type = params.Param(0).ToText();
            if(!Type.CompareNoCase("CPU"))
                Platform::Graphics::SetRenderingType(RT_CPU);
            else if(!Type.CompareNoCase("GPU"))
                Platform::Graphics::SetRenderingType(RT_GPU);
        }
    }, "[Type:'GPU']", "CPU 또는 GPU 렌더링을 다음 프레임에 적용");

    ////////////////////////////////////////////////////////////////////////////////
    // windowgrap
    NewZayson.AddGlue("windowgrab", ZAY_DECLARE_GLUE(params, CaptureWindow)
    {
        if(6 <= params.ParamCount())
        {
            auto ResName = params.Param(0).ToText();
            auto X = params.Param(1).ToInteger();
            auto Y = params.Param(2).ToInteger();
            auto Width = params.Param(3).ToInteger();
            auto Height = params.Param(4).ToInteger();
            auto Blur = params.Param(5).ToFloat();
            auto UseRepaint = (6 < params.ParamCount())? (params.Param(6).ToInteger() != 0) : true;
            if(ResName.Left(2) == "r.")
                (*CaptureWindow)(ResName.Offset(2), X, Y, Width, Height, Blur, UseRepaint);
        }
    }, "[ResName:'r.grab'][X:0][Y:0][Width:100][Height:100][Blur:5.0]#[UseRepaint:1]", "해당 윈도우영역을 리소스화");
    return true;
}
