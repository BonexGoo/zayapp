#include <boss.hpp>
#include "glue_input.hpp"

ZAY_DECLARE_GLUE_CLASS("input", GlueInput)

bool GlueInput::Register(ZaySon& NewZayson, const ZayGlue::Bindings& context)
{
    const auto KeepUpdatingUntil = context.host.Find<void(uint64)>("view.update_until");
    const auto SetHotKey = context.host.Find<void(sint32, const String&, const String&)>("input.set_hotkey");
    const auto ClearHotKeys = context.host.Find<void()>("input.clear_hotkeys");
    auto* view = context.view;
    if(!KeepUpdatingUntil || !SetHotKey || !ClearHotKeys || !view)
    {
        BOSS_TRACE("[input] Missing required GLUE host callback/view");
        return false;
    }
    const String viewname = context.viewname;

    ////////////////////////////////////////////////////////////////////////////////
    // ascii
    NewZayson.AddGlue("ascii", ZAY_DECLARE_GLUE(params, view)
    {
        if(params.ParamCount() == 1)
        {
            auto AsciiCode = params.Param(0).ToInteger();
            char AsciiText[2] = {char(AsciiCode & 0xFF), '\0'};
            Platform::SendKeyEvent(view->view(), 0, AsciiText, true);
            Platform::SendKeyEvent(view->view(), 0, AsciiText, false);
        }
    }, "[AsciiCode:65]", "아스키를 입력");

    ////////////////////////////////////////////////////////////////////////////////
    // keypad
    NewZayson.AddGlue("keypad", ZAY_DECLARE_GLUE(params, view)
    {
        if(params.ParamCount() == 2)
        {
            auto KeyCode = params.Param(0).ToInteger();
            auto KeyText = params.Param(1).ToText();
            Platform::SendKeyEvent(view->view(), KeyCode, KeyText, true);
            Platform::SendKeyEvent(view->view(), KeyCode, KeyText, false);
        }
    }, "[KeyCode:65][KeyText:'A']", "키패드를 입력");

    ////////////////////////////////////////////////////////////////////////////////
    // clearcapture
    NewZayson.AddGlue("clearcapture", ZAY_DECLARE_GLUE(params, view)
    {
        view->clearCapture();
        view->invalidate();
    }, nullptr, "Capture를 없애기");

    ////////////////////////////////////////////////////////////////////////////////
    // setcapture
    NewZayson.AddGlue("setcapture", ZAY_DECLARE_GLUE(params, view, viewname)
    {
        if(params.ParamCount() == 1)
        {
            auto UIName = viewname + '.' + params.Param(0).ToText();
            view->setCapture(UIName);
            view->invalidate();
        }
    }, "[UIName:'ui_editbox']", "Capture를 설정하기");

    ////////////////////////////////////////////////////////////////////////////////
    // sethotkey
    NewZayson.AddGlue("sethotkey", ZAY_DECLARE_GLUE(params, SetHotKey)
    {
        if(2 <= params.ParamCount())
        {
            auto Key = params.Param(0).ToText();
            auto Gate = params.Param(1).ToText();
            auto NeedPress = (2 < params.ParamCount())? (params.Param(2).ToInteger() != 0) : false;
            sint32 KeyCode = 0;
            branch;
            jump(!Key.CompareNoCase("ESC")) KeyCode = 27;
            jump(!Key.CompareNoCase("ENTER")) KeyCode = 13;
            jump(!Key.CompareNoCase("SPACE")) KeyCode = 32;
            jump(!Key.CompareNoCase("BACKSPACE")) KeyCode = 8;
            jump(!Key.CompareNoCase("TAB")) KeyCode = 9;
            jump(!Key.CompareNoCase("INSERT")) KeyCode = 45;
            jump(!Key.CompareNoCase("DELETE")) KeyCode = 46;
            jump(!Key.CompareNoCase("HOME")) KeyCode = 36;
            jump(!Key.CompareNoCase("END")) KeyCode = 35;
            jump(!Key.CompareNoCase("PAGEUP")) KeyCode = 33;
            jump(!Key.CompareNoCase("PAGEDOWN")) KeyCode = 34;
            jump(!Key.CompareNoCase("LEFT")) KeyCode = 37;
            jump(!Key.CompareNoCase("UP")) KeyCode = 38;
            jump(!Key.CompareNoCase("RIGHT")) KeyCode = 39;
            jump(!Key.CompareNoCase("DOWN")) KeyCode = 40;
            if(KeyCode != 0)
                (*SetHotKey)(KeyCode, NeedPress ? Gate + "_press" : String(), Gate);
        }
    }, "[Key:'Enter'][Gate:'aaa']#[NeedPress:0]", "핫키추가");

    ////////////////////////////////////////////////////////////////////////////////
    // clearhotkey
    NewZayson.AddGlue("clearhotkey", ZAY_DECLARE_GLUE(params, ClearHotKeys)
    {
        (*ClearHotKeys)();
    }, nullptr, "핫키초기화");

    ////////////////////////////////////////////////////////////////////////////////
    // movescrollv
    NewZayson.AddGlue("movescrollv", ZAY_DECLARE_GLUE(params, view)
    {
        if(params.ParamCount() == 2)
        {
            auto UIName = params.Param(0).ToText();
            auto ScrollTarget = params.Param(1).ToInteger();
            const sint32 ScrollPos = view->scrollpos(UIName).y;
            view->moveScroll(UIName, 0, ScrollPos, 0, ScrollTarget, 0.5, false);
        }
    }, "[UIName:'ui_scroll'][ScrollTarget:1000]", "수직스크롤바를 이동");

    ////////////////////////////////////////////////////////////////////////////////
    // slideonce
    NewZayson.AddGlue("slideonce", ZAY_DECLARE_GLUE(params, KeepUpdatingUntil, view)
    {
        if(params.ParamCount() == 4)
        {
            const String DomNameD = params.Param(0).ToText();
            const sint32 Min = params.Param(1).ToInteger();
            const sint32 Max = params.Param(2).ToInteger();
            const sint32 Step = params.Param(3).ToInteger();
            if(DomNameD.Left(2) == "d." && Min < Max && Step != 0)
            {
                const String DomName = DomNameD.Offset(2);
                const float OldValue = ZayWidgetDOM::ExistVariable(DomName + ".ani")?
                    ZayWidgetDOM::GetValue(DomName + ".ani").ToFloat() : ZayWidgetDOM::GetValue(DomName).ToFloat();
                const sint32 AbsStep = Math::Abs(Step);
                const sint32 NewValue = Min + sint32(Math::Round((OldValue - Min) / AbsStep)) * AbsStep + Step;
                const sint32 ClampValue = Math::Clamp(NewValue, Min, Max);
                ZayWidgetDOM::SetValue(DomName, String::FromInteger(ClampValue));
                ZayWidgetDOM::SetValue(DomName + ".ani", String::Format("%f ~ %d : 0.1", OldValue, ClampValue));
                const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + 100;
                (*KeepUpdatingUntil)(NewUpdateMsec);
                view->clearCapture();
                view->invalidate(2);
            }
        }
    }, "[DomNameD:'d.value'][Step:-10][Limit:1]", "슬라이드를 한번 이동");

    ////////////////////////////////////////////////////////////////////////////////
    // pickeronce
    NewZayson.AddGlue("pickeronce", ZAY_DECLARE_GLUE(params, KeepUpdatingUntil, view)
    {
        if(params.ParamCount() == 6)
        {
            const String DomNameD = params.Param(0).ToText();
            const sint32 Min = params.Param(1).ToInteger();
            const sint32 Max = params.Param(2).ToInteger();
            const sint32 Step = params.Param(3).ToInteger();
            const sint32 Def = params.Param(4).ToInteger();
            const bool IsRight = (params.Param(5).ToInteger() != 0);
            if(DomNameD.Left(2) == "d." && Min < Max && 0 < Step)
            {
                const String DomName = DomNameD.Offset(2);
                auto CurDom = String(DomName) + ".value";
                auto AniDom = String(DomName) + ".ani";
                const float AniValue = (ZayWidgetDOM::ExistVariable(AniDom))? ZayWidgetDOM::GetValue(AniDom).ToFloat() : Def;
                const float NewValue = Math::Clamp((Math::Round(AniValue / Step) + ((IsRight)? 1 : -1)) * Step, Min, Max);
                ZayWidgetDOM::SetValue(CurDom, String::FromInteger((sint32) Math::Round(NewValue)));
                ZayWidgetDOM::SetValue(AniDom, String::Format("%f ~ %f : %f", AniValue, NewValue, 0.2));
                const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + 200;
                (*KeepUpdatingUntil)(NewUpdateMsec);
                view->clearCapture();
                view->invalidate(2);
            }
        }
    }, "[DomNameD:'d.value'][Min:10][Max:100][Step:5][IsRight:0]", "픽커를 한번 이동");
    return true;
}
