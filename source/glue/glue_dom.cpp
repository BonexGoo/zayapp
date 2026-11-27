#include <boss.hpp>
#include "glue_dom.hpp"

ZAY_DECLARE_GLUE_CLASS("dom", GlueDom)

bool GlueDom::Register(ZaySon& NewZayson, const ZayGlue::Bindings& context)
{
    auto* view = context.view;
    if(!view)
    {
        BOSS_TRACE("[dom] Missing required GLUE host callback/view");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // setvalue
    NewZayson.AddGlue("setvalue", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 2)
        {
            auto DomNameD = params.Param(0).ToText();
            auto Value = params.Param(1).ToText(true);
            if(DomNameD.Left(2) == "d.")
                ZayWidgetDOM::SetValue(DomNameD.Offset(2), Value);
        }
    }, "[DomNameD:'d.aaa.bbb'][Value]", "DOM에 값을 설정");

    ////////////////////////////////////////////////////////////////////////////////
    // calcvalue
    NewZayson.AddGlue("calcvalue", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 2)
        {
            auto DomNameD = params.Param(0).ToText();
            auto Value = params.Param(1).ToText(); // 스트링을 연산식으로 다시 평가
            if(DomNameD.Left(2) == "d.")
                ZayWidgetDOM::SetValue(DomNameD.Offset(2), Value);
        }
    }, "[DomNameD:'d.aaa.bbb'][Value]", "DOM에 값 또는 연산식을 설정");

    ////////////////////////////////////////////////////////////////////////////////
    // clearvalue
    NewZayson.AddGlue("clearvalue", ZAY_DECLARE_GLUE(params, view)
    {
        if(params.ParamCount() == 1)
        {
            auto DomNameD = params.Param(0).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                ZayWidgetDOM::RemoveVariables(DomNameD.Offset(2));
                view->invalidate();
            }
        }
    }, "[DomNameD:'d.aaa.bbb']", "DomName이 포함된 모든 DOM을 제거");

    ////////////////////////////////////////////////////////////////////////////////
    // getcomment
    NewZayson.AddGlue("getcomment", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 1)
        {
            auto DomNameD = params.Param(0).ToText();
            if(DomNameD.Left(2) == "d." && params.CanReturn())
            {
                auto Value = ZayWidgetDOM::GetComment(DomNameD.Offset(2));
                params.Return(Value);
            }
        }
    }, "[DomNameD:'d.aaa.bbb']", "Comment타입 DOM의 값을 반환");

    ////////////////////////////////////////////////////////////////////////////////
    // setcomment
    NewZayson.AddGlue("setcomment", ZAY_DECLARE_GLUE(params, view)
    {
        if(params.ParamCount() == 2)
        {
            auto DomNameD = params.Param(0).ToText();
            auto Value = params.Param(1).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                ZayWidgetDOM::SetComment(DomNameD.Offset(2), Value);
                ZayWidgetDOM::SetValue(DomNameD.Offset(2) + ".text", '\'' + Value + '\'');
                view->clearCapture();
                view->invalidate();
            }
        }
    }, "[DomNameD:'d.aaa.bbb'][Value]", "Comment타입 DOM에 값을 설정");

    ////////////////////////////////////////////////////////////////////////////////
    // setdom
    NewZayson.AddGlue("setdom", ZAY_DECLARE_GLUE(params, view)
    {
        if(2 <= params.ParamCount())
        {
            auto DomNameD = params.Param(0).ToText();
            auto FilePath = params.Param(1).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                String DomHeader = DomNameD.Offset(2) + '.';
                ZayWidgetDOM::RemoveVariables(DomHeader);
                if(Platform::File::Exist(FilePath))
                {
                    const BOSS::Context Json(ST_Json, SO_NeedCopy, String::FromFile(FilePath));
                    ZayWidgetDOM::SetJson(Json, DomHeader);
                }
                else if(2 < params.ParamCount())
                {
                    const String AssetPath = params.Param(2).ToText();
                    const BOSS::Context Json(ST_Json, SO_NeedCopy, String::FromAsset(AssetPath));
                    ZayWidgetDOM::SetJson(Json, DomHeader);
                }
                view->invalidate();
            }
        }
    }, "[DomNameD:'d.aaa.bbb'][FilePath:'C:/aaa/bbb.json']", "DomName의 아래에 FilePath의 json을 로딩");
    return true;
}
