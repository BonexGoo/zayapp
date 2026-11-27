#include <boss.hpp>
#include "glue_instrumentation.hpp"
#include <service/boss_zaywidget.hpp>
#include <chrono>

ZAY_DECLARE_GLUE_CLASS("instrumentation", GlueInstrumentation)

bool GlueInstrumentation::Register(ZaySon& NewZayson, const ZayGlue::Bindings& bindings)
{
    const auto Connect = bindings.host.Find<void(chars)>("scope.connect");
    const auto Validate = bindings.host.Find<bool()>("scope.validate");
    const auto Write = bindings.host.Find<void(chars)>("scope.write");
    const auto Query = bindings.host.Find<void(chars, String&)>("scope.query");
    if(!Connect || !Validate || !Write || !Query)
    {
        BOSS_TRACE("[instrumentation] Missing required typed host operations");
        return false;
    }

    // Monotonic microseconds. Integer ticks keep short render intervals precise in Zay.
    NewZayson.AddGlue("renderclock", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 0 && params.CanReturn())
        {
            using Clock = std::chrono::steady_clock;
            static const auto Origin = Clock::now();
            const auto Elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - Origin);
            params.Return(String::FromInteger(sint64(Elapsed.count())));
        }
    }, "", "Monotonic render clock (microseconds)");

    ////////////////////////////////////////////////////////////////////////////////
    // connectscope
    NewZayson.AddGlue("connectscope", ZAY_DECLARE_GLUE(params, Connect, Validate)
    {
        if(params.ParamCount() == 1)
        {
            auto ScopeId = params.Param(0).ToText();
            (*Connect)(ScopeId);
            (*Validate)();
        }
    }, "[ScopeId:'USB0::0x18D1::0x0007::410001140::1::INSTR']", "스코프에 연결하기");

    ////////////////////////////////////////////////////////////////////////////////
    // getscope
    NewZayson.AddGlue("getscope", ZAY_DECLARE_GLUE(params, Validate, Write, Query)
    {
        if(2 <= params.ParamCount())
        {
            // Measure = 'PKPK'     // Peak-to-Peak
            // Measure = 'AMPL'     // Amplitude (보통 PKPK와 유사하지만 정의 다름)
            // Measure = 'MAX'      // Maximum
            // Measure = 'MIN'      // Minimum
            // Measure = 'TOP'      // Top level
            // Measure = 'BASE'     // Base level
            // Measure = 'MEAN'     // Mean (DC average)
            // Measure = 'RMS'      // RMS
            auto Measure = params.Param(0).ToText();
            auto DomNameD1 = params.Param(1).ToText();
            auto DomNameD2 = (2 < params.ParamCount())? params.Param(2).ToText() : String();
            auto DomNameD3 = (3 < params.ParamCount())? params.Param(3).ToText() : String();
            auto DomNameD4 = (4 < params.ParamCount())? params.Param(4).ToText() : String();
            if((DomNameD1.Length() == 0 || DomNameD1.Left(2) == "d.") &&
                (DomNameD2.Length() == 0 || DomNameD2.Left(2) == "d.") &&
                (DomNameD3.Length() == 0 || DomNameD3.Left(2) == "d.") &&
                (DomNameD4.Length() == 0 || DomNameD4.Left(2) == "d."))
            {
                // 값 가져오기
                if((*Validate)())
                {
                    const String DomNamesD[4] = {DomNameD1, DomNameD2, DomNameD3, DomNameD4};
                    for(sint32 i = 0; i < 4; ++i)
                    {
                        if(0 < DomNamesD[i].Length())
                        {
                            String Value;
                            (*Write)((chars) String::Format(":MEASure:OPEN %s, CH%d", (chars) Measure, i + 1)); // 메뉴가 없다면 새로 생기게 됨
                            (*Query)((chars) String::Format(":MEASure:%s? CH%d", (chars) Measure, i + 1), Value); // 메뉴가 없을땐 "-"로 나옴
                            const String ErrorCheck = Value.Trim();
                            sint32 ReadOffset = 0;
                            Parser::GetFloat((chars) ErrorCheck, ErrorCheck.Length(), &ReadOffset);
                            if(ReadOffset == ErrorCheck.Length())
                                ZayWidgetDOM::SetValue(DomNamesD[i].Offset(2), (chars) Value);
                            else
                            {
                                ZayWidgetDOM::SetValue(DomNamesD[i].Offset(2), "'x'");
                                ZayWidgetDOM::SetValue(DomNamesD[i].Offset(2) + "_error", '\'' + ErrorCheck + '\'');
                            }
                        }
                    }
                }
                else
                {
                    if(0 < DomNameD1.Length())
                        ZayWidgetDOM::SetValue(DomNameD1.Offset(2), "'x'");
                    if(0 < DomNameD2.Length())
                        ZayWidgetDOM::SetValue(DomNameD2.Offset(2), "'x'");
                    if(0 < DomNameD3.Length())
                        ZayWidgetDOM::SetValue(DomNameD3.Offset(2), "'x'");
                    if(0 < DomNameD4.Length())
                        ZayWidgetDOM::SetValue(DomNameD4.Offset(2), "'x'");
                }
            }
        }
    }, "[Measure:'PKPK'][DomNameD1:'d.ch1_value']#[DomNameD2:'d.ch2_value'][DomNameD3:'d.ch3_value'][DomNameD4:'d.ch4_value']", "스코프의 현재값 얻기");
    return true;
}
