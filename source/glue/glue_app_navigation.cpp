#include <boss.hpp>
#include "glue_app_navigation.hpp"
#include <service/boss_zaywidget.hpp>

ZAY_DECLARE_GLUE_CLASS("app_navigation", GlueAppNavigation)

bool GlueAppNavigation::Register(ZaySon& NewZayson, const ZayGlue::Bindings& bindings)
{
    const auto CanChangeSite = bindings.host.Find<bool(const String&)>("app.can_change_site");
    const auto AddressInput = bindings.host.Find<String()>("app.address");
    const auto InitDeck = bindings.host.Find<void()>("app.init_deck");
    const auto SetAppUrl = bindings.host.Find<bool(const String&)>("app.set_url");
    const auto CreateShortcut = bindings.host.Find<sint32(const String&, const String&)>("app.create_shortcut");
    const auto OpenStandalone = bindings.host.Find<bool(const String&, const String&, const String&)>("app.open_standalone");
    auto* view = bindings.view;
    if(!CanChangeSite || !AddressInput || !InitDeck || !SetAppUrl ||
        !CreateShortcut || !OpenStandalone || !view)
    {
        BOSS_TRACE("[app_navigation] Missing required typed host operations/view");
        return false;
    }

    if((*CanChangeSite)(bindings.viewname))
    {
        NewZayson.AddGlue("initappdeck", ZAY_DECLARE_GLUE(params, InitDeck)
        {if(params.ParamCount()==0) (*InitDeck)();}, nullptr, "OnCreate에서 웹사이트 Deck 로딩 및 실행 상태 초기화");
        NewZayson.AddGlue("setappurl", ZAY_DECLARE_GLUE(params, AddressInput, SetAppUrl)
        {
            const String Url=(params.ParamCount()==0)? (*AddressInput)().Trim() :
                (params.ParamCount()==1)? params.Param(0).ToText() : String();
            if(params.ParamCount()==0 && !Url.Length())
            {
                ZayWidgetDOM::SetValue("program.appurl_ok","0");
                if(params.CanReturn()) params.Return("0");
                return;
            }
            const bool ok=params.ParamCount()<=1 && (*SetAppUrl)(Url);
            if(params.CanReturn()) params.Return(ok?"1":"0");
        }, "#[Url:'https://bonexgoo.github.io/zayapp/']", "웹사이트 Deck/Index 전환(인수 생략시 주소창 원문 사용)");
    }
    ////////////////////////////////////////////////////////////////////////////////
    // createappshortcut
    NewZayson.AddGlue("createappshortcut", ZAY_DECLARE_GLUE(params, CreateShortcut, view)
    {
        sint32 result = -1;
        if(params.ParamCount() == 1 || params.ParamCount() == 2)
        {
            const String url = (params.ParamCount() == 2)? params.Param(1).ToText() : String();
            result = (*CreateShortcut)(params.Param(0).ToText(), url);
        }
        ZayWidgetDOM::SetValue("program.shortcut_result", String::FromInteger(result));
        view->invalidate();
    }, "[AppId:'robo']#[Url:'']", "앱과 앱사이트 URL을 보존하는 단독 모드 바로가기(Url 생략시 현재 사이트)");

    ////////////////////////////////////////////////////////////////////////////////
    // openstandaloneapp
    NewZayson.AddGlue("openstandaloneapp", ZAY_DECLARE_GLUE(params, OpenStandalone, view)
    {
        bool opened = false;
        if(1 <= params.ParamCount() && params.ParamCount() <= 3)
        {
            const String id = params.Param(0).ToText();
            const String target = (2 <= params.ParamCount())? params.Param(1).ToText() : String("self");
            const String url = (params.ParamCount() == 3)? params.Param(2).ToText() : String();
            opened = (*OpenStandalone)(id, target, url);
        }
        ZayWidgetDOM::SetValue("program.shortcut_result", opened? "3" : "-1");
        view->invalidate();
    }, "[AppId:'robo']#[Target:'self']#[Url:'']", "현재 런타임에서 앱과 앱사이트 URL로 단독 모드 실행(Url 생략시 현재 사이트)");

    ////////////////////////////////////////////////////////////////////////////////
    // openwebpage
    NewZayson.AddGlue("openwebpage", ZAY_DECLARE_GLUE(params)
    {
        bool opened = false;
        if(params.ParamCount() == 1 || params.ParamCount() == 2)
        {
            const String url = params.Param(0).ToText();
            const String target = (params.ParamCount() == 2)? params.Param(1).ToText() : String("self");
            if(!String::CompareNoCase(url, "https://", 8) || !String::CompareNoCase(url, "http://", 7))
                opened = Platform::Utility::OpenWebPageWithQuery(url, target);
        }
        if(params.CanReturn()) params.Return(opened? "1" : "0");
    }, "[Url:'https://www.youtube.com/']#[Target:'self']", "웹 페이지 열기");
    return true;
}
