#include <boss.hpp>
#include "glue_runtime.hpp"

ZAY_DECLARE_GLUE_CLASS("runtime", GlueRuntime)

bool GlueRuntime::Register(ZaySon& NewZayson, const ZayGlue::Bindings& context)
{
    const auto SetLocale = context.host.Find<void(const String&)>("locale.set");
    const auto TurnLanguage = context.host.Find<void()>("language.turn");
    auto* view = context.view;
    if(!SetLocale || !TurnLanguage || !view)
    {
        BOSS_TRACE("[runtime] Missing required GLUE host callback/view");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // sleep
    NewZayson.AddGlue("sleep", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 1)
        {
            auto Msec = sint32(params.Param(0).ToFloat() * 1000);
            Platform::Utility::Sleep(Msec, true, true, false);
        }
    }, "[Msec]", "Msec동안 슬립처리");

    ////////////////////////////////////////////////////////////////////////////////
    // print
    NewZayson.AddGlue("print", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 2)
        {
            auto Title = params.Param(0).ToText();
            auto Text = params.Param(1).ToText();
            BOSS_TRACE("[%s] %s (%s)", (chars) Title, (chars) Text, params.UIName());
        }
    }, "[Title][Text]", "출력창에 로그남기기");

    ////////////////////////////////////////////////////////////////////////////////
    // setlocale
    NewZayson.AddGlue("setlocale", ZAY_DECLARE_GLUE(params, SetLocale)
    {
        if(params.ParamCount() == 1)
        {
            const String Locale = params.Param(0).ToText();
            (*SetLocale)(Locale);
            ZayWidgetDOM::SetValue("program.locale", '\'' + Locale + '\'');
        }
    }, "[Locale:'ko-KR']", "언어코드를 설정");

    ////////////////////////////////////////////////////////////////////////////////
    // langturn
    NewZayson.AddGlue("langturn", ZAY_DECLARE_GLUE(params, TurnLanguage, view)
    {
        (*TurnLanguage)();
        view->invalidate();
    }, nullptr, "현재 언어를 전환");

    ////////////////////////////////////////////////////////////////////////////////
    // makeuid
    NewZayson.AddGlue("makeuid", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 6)
        {
            auto Num1 = (params.Param(0).ToInteger() & 0xFFFF);
            auto Num2 = (params.Param(1).ToInteger() & 0xFFFF);
            auto Num3 = (params.Param(2).ToInteger() & 0xFFFF);
            auto Num4 = (params.Param(3).ToInteger() & 0xFFFF);
            auto Num5 = (params.Param(4).ToInteger() & 0xFFFF);
            auto Num6 = (params.Param(5).ToInteger() & 0xFFFF);
            if(params.CanReturn())
                params.Return(String::Format("'%04X-%04X-%04X-%04X-%04X-%04X'", Num1, Num2, Num3, Num4, Num5, Num6));
        }
    }, "[Num1:0xFFFF][Num2:0xFFFF][Num3:0xFFFF][Num4:0xFFFF][Num5:0xFFFF][Num6:0xFFFF]", "UID로 변환");

    ////////////////////////////////////////////////////////////////////////////////
    // maketimetag
    NewZayson.AddGlue("maketimetag", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 1)
        {
            auto Format = params.Param(0).ToText();
            String TimeTag;
            if(id_clock NewClock = Platform::Clock::CreateAsCurrent())
            {
                sint32 Year = 0, Month = 0, Day = 0, Hour = 0, Min = 0, Sec = 0;
                Platform::Clock::GetDetail(NewClock, nullptr, &Sec, &Min, &Hour, &Day, &Month, &Year);
                if(Format == "YYYYMMDD_HHMMSS")
                    TimeTag = String::Format("%04d%02d%02d_%02d%02d%02d", Year, Month, Day, Hour, Min, Sec);
                else if(Format == "YYYY-MM-DD")
                    TimeTag = String::Format("%04d-%02d-%02d", Year, Month, Day);
                else if(Format == "HH:MM:SS")
                    TimeTag = String::Format("%02d:%02d:%02d", Hour, Min, Sec);
                Platform::Clock::Release(NewClock);
            }
            if(params.CanReturn())
                params.Return('\'' + TimeTag + '\'');
        }
    }, "[Format:'YYYYMMDD_HHMMSS']", "현재시간의 TimeTag를 반환");
    return true;
}
