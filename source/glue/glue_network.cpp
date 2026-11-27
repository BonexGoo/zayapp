#include <boss.hpp>
#include "glue_network.hpp"

ZAY_DECLARE_GLUE_CLASS("network", GlueNetwork)

bool GlueNetwork::Register(ZaySon& NewZayson, const ZayGlue::Bindings& bindings)
{
    const auto host = bindings.host;
    const auto UpdateIPAddress = host.Find<void()>("network.update_ip_address");
    const auto RequestImage = host.Find<id_image_read(chars)>("resource.request_image");
    const auto DispatchCallback = host.Find<void(const String&, const String&)>("widget.dispatch_callback");
    if(!host.IsAlive() || !UpdateIPAddress || !RequestImage || !DispatchCallback)
    {
        BOSS_TRACE("[network] Missing required GLUE bindings");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // wificonnect
    NewZayson.AddGlue("wificonnect", ZAY_DECLARE_GLUE(params)
    {
        if(1 <= params.ParamCount())
        {
            auto Ssid = params.Param(0).ToText();
            auto Password = (1 < params.ParamCount())? params.Param(1).ToText() : String();
            const bool Result = Platform::Socket::ConnectWifi(Ssid, Password);
            if(params.CanReturn())
                params.Return((Result)? "1" : "0");
        }
    }, "[Ssid:'abc']#[Password:'123']", "와이파이 연결");

    ////////////////////////////////////////////////////////////////////////////////
    // wifidisconnect
    NewZayson.AddGlue("wifidisconnect", ZAY_DECLARE_GLUE(params)
    {
        Platform::Socket::DisconnectWifi();
    }, nullptr, "와이파이 연결해제");

    ////////////////////////////////////////////////////////////////////////////////
    // updateipaddress
    NewZayson.AddGlue("updateipaddress", ZAY_DECLARE_GLUE(params, UpdateIPAddress)
    {
        (*UpdateIPAddress)();
    }, nullptr, "IP주소를 알기");

    ////////////////////////////////////////////////////////////////////////////////
    // geturltext
    NewZayson.AddGlue("geturltext", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 1)
        {
            auto Text = params.Param(0).ToText();
            if(params.CanReturn())
                params.Return('\'' + Text.ToUrlString() + '\'');
        }
    }, "[Text:'abc']", "UrlText로 변환");

    // restapi
    NewZayson.AddGlue("restapi", ZAY_DECLARE_GLUE(params, host, RequestImage, DispatchCallback)
    {
        if(3 <= params.ParamCount())
        {
            auto DomNameD = params.Param(0).ToText();
            auto HostUrl = params.Param(1).ToText();
            auto Params = params.Param(2).ToText();
            auto CallBack = (3 < params.ParamCount())? params.Param(3).ToText() : String();
            auto ResName = (4 < params.ParamCount())? params.Param(4).ToText() : String();
            if(DomNameD.Left(2) == "d.")
            {
                auto Query = String::Format("%s?%s", (chars) HostUrl, (chars) Params);
                BOSS_TRACE("##### RestAPI(%s >> %s) Request: %s", params.UIName(), (chars) DomNameD, (chars) Query);

                struct Payload
                {
                    ZayGlue::Host mHost;
                    std::shared_ptr<std::function<void(const String&, const String&)>> mDispatch;
                    String mDomNameD;
                    String mCallBack;
                };
                auto* Data = new Payload({host, DispatchCallback, DomNameD, CallBack});
                auto Method = [](payload data, chars text)->void
                    {
                        std::unique_ptr<Payload> Data((Payload*) data);
                        if(!Data->mHost.IsAlive()) return;
                        const String ResultJson = text;
                        const Context Json(ST_Json, SO_OnlyReference, ResultJson);
                        ZayWidgetDOM::SetJson(Json, Data->mDomNameD.Offset(2) + '.');
                        BOSS_TRACE("##### RestAPI(%s) Response: %s...", (chars) Data->mDomNameD, (chars) ResultJson.Left(40).ToUrlString());

                        if(0 < Data->mCallBack.Length())
                            (*Data->mDispatch)(Data->mCallBack, Data->mDomNameD);
                        Platform::UpdateAllViews();
                    };

                if(ResName.Left(2) == "r.")
                {
                    auto RawHeader = (5 < params.ParamCount())? params.Param(5).ToText() : String();
                    Platform::Utility::SendImageRequest(Query, (*RequestImage)(ResName.Offset(2)), Method, Data, RawHeader);
                }
                else if(!ResName.Right(4).CompareNoCase(".csv"))
                    Platform::Utility::SendCsvRequest(Query, String::FromAsset(ResName), Method, Data);
                else Platform::Utility::SendRequest(Query, Method, Data);
            }
        }
    }, "[DomNameD:'d.return'][HostUrl:'localhost/member/login'][Params:'id=aaaa&key=bbbb']#[CallBack:'gate1'][ResName:'r.grab']", "REST-API를 사용하여 결과를 DOM으로 받기");
    return true;
}
