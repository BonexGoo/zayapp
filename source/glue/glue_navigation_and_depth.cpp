#include <boss.hpp>
#include "glue_navigation_and_depth.hpp"

// BEGIN ZAY_GLUE_NATIVE_DOM_HELPERS
namespace
{
    bool ZayNativeDom(const String& text, String& base)
    {
        if(text.Left(2)!="d." || text.Length()<=2) return false;
        base=text.Offset(2);
        const char* p=base;
        for(int i=0; p[i]; ++i)
            if(!(p[i]>='a'&&p[i]<='z') && !(p[i]>='A'&&p[i]<='Z') &&
               !(p[i]>='0'&&p[i]<='9') && p[i]!='_' && p[i]!='.') return false;
        return p[0]!='.' && p[base.Length()-1]!='.';
    }
    bool ZayNativeNumber(const String& key, double& value)
    {
        if(!ZayWidgetDOM::ExistVariable(key)) return false;
        value=ZayWidgetDOM::GetValue(key).ToFloat();
        return std::isfinite(value);
    }
    void ZayNativePutNumber(const String& key, double value)
    {
        if(!ZayWidgetDOM::ExistVariable(key) || ZayWidgetDOM::GetValue(key).ToFloat()!=value)
            ZayWidgetDOM::SetValue(key,String::Format("%.17f",value)); // Solver-safe decimal, no exponent notation.
    }
    void ZayNativePutText(const String& key, const char* value)
    {
        if(!ZayWidgetDOM::ExistVariable(key) || ZayWidgetDOM::GetValue(key).ToText()!=value)
            ZayWidgetDOM::SetValue(key,String::Format("'%s'",value));
    }
    bool ZayNativeCount(const String& base, const char* field, int& count, int maxCount)
    {
        const String key=base+field+".count";
        if(!ZayWidgetDOM::ExistVariable(key)) { count=0; return true; }
        double n=0;
        if(!ZayNativeNumber(key,n) || n<0 || n>maxCount || std::floor(n)!=n) return false;
        count=int(n); return true;
    }
    bool ZayNativeGeometry(const String& base, double halfWidth, double halfHeight,
        ZayGlueNativeState::Geometry& g, std::string& error)
    {
        error="invalid_geometry";
        if(!ZayNativeNumber(base+".bounds.l",g.l)||!ZayNativeNumber(base+".bounds.t",g.t)||
           !ZayNativeNumber(base+".bounds.r",g.r)||!ZayNativeNumber(base+".bounds.b",g.b)) return false;
        g.halfWidth=halfWidth; g.halfHeight=halfHeight;
        g.polygonsPreexpanded=ZayWidgetDOM::ExistVariable(base+".polygons_preexpanded") &&
            ZayWidgetDOM::GetValue(base+".polygons_preexpanded").ToInteger()!=0;
        int rectangleCount=0, polygonCount=0;
        if(!ZayNativeCount(base,".rectangles",rectangleCount,1024)||!ZayNativeCount(base,".polygons",polygonCount,512)) return false;
        for(int i=0; i<rectangleCount; ++i)
        {
            const String p=base+String::Format(".rectangles.%d",i);
            double l=0,t=0,r=0,b=0;
            if(!ZayNativeNumber(p+".l",l)||!ZayNativeNumber(p+".t",t)||!ZayNativeNumber(p+".r",r)||!ZayNativeNumber(p+".b",b)) return false;
            g.rectangles.push_back(Rect(l,t,r,b));
        }
        int totalPoints=0;
        for(int i=0; i<polygonCount; ++i)
        {
            const String p=base+String::Format(".polygons.%d",i);
            int count=0;
            if(!ZayNativeCount(p,".points",count,2048)||count<3||(totalPoints+=count)>4096) return false;
            ZayGlueNativeState::Polygon polygon;
            polygon.subtract=ZayWidgetDOM::ExistVariable(p+".subtract") && ZayWidgetDOM::GetValue(p+".subtract").ToInteger()!=0;
            for(int j=0; j<count; ++j)
            {
                const String q=p+String::Format(".points.%d",j);
                double x=0,y=0;
                if(!ZayNativeNumber(q+".x",x)||!ZayNativeNumber(q+".y",y)) return false;
                polygon.points.push_back(Point(x,y));
            }
            g.polygons.push_back(std::move(polygon));
        }
        error.clear(); return true;
    }
    void ZayNativeStatus(const String& base, bool ok, const char* status)
    {
        ZayNativePutNumber(base+".ok",ok?1:0);
        ZayNativePutText(base+".status",status);
    }
    void ZayNativeRoute(const String& base, const ZayGlueNativeState::Route& route)
    {
        const Point pos=route.world?route.world->ToWorld(route.pos):route.pos;
        const Point goal=route.world?route.world->ToWorld(route.goal):route.goal;
        ZayNativeStatus(base,route.ok,route.status.c_str());
        ZayNativePutNumber(base+".x",pos.x); ZayNativePutNumber(base+".y",pos.y);
        ZayNativePutNumber(base+".target_x",goal.x); ZayNativePutNumber(base+".target_y",goal.y);
        ZayNativePutNumber(base+".moving",route.moving?1:0);
        ZayNativePutNumber(base+".arrived",route.arrived?1:0);
        ZayNativePutNumber(base+".blocked",route.blocked?1:0);
        ZayNativePutNumber(base+".moved",route.moved);
        ZayNativePutNumber(base+".point_count",route.path?route.path->Dots.Count():0);
        ZayNativePutText(base+".dir",route.dir.c_str());
    }
    void ZayNativeChangedRoutes(ZayGlueNativeState& state, const std::vector<ZayGlueNativeState::Key>& changed)
    {
        for(const auto& key : changed)
        {
            String base;
            if(ZayNativeDom(key.second.c_str(),base)) ZayNativeRoute(base,*state.routes.at(key));
        }
    }
    void ZayNativeDepth(const String& base, const ZayGlueNativeState::Depth& depth, int first, int last)
    {
        for(int i=first; i>=0 && i<=last; ++i)
            ZayWidgetDOM::SetValue(base+String::Format(".%d",i),depth.order[i].literal.c_str());
        ZayNativePutNumber(base+".count",double(depth.order.size()));
        ZayNativePutNumber(base+".revision",double(depth.revision));
    }
}
// END ZAY_GLUE_NATIVE_DOM_HELPERS

ZAY_DECLARE_GLUE_CLASS("navigation_and_depth", GlueNavigationAndDepth)

bool GlueNavigationAndDepth::Register(ZaySon& NewZayson, const ZayGlue::Bindings& bindings)
{
    const auto GetState = bindings.host.Find<std::shared_ptr<ZayGlueNativeState>()>("native.state");
    auto* view = bindings.view;
    const String viewname = bindings.viewname;
    if(!GetState || !view)
    {
        BOSS_TRACE("[navigation_and_depth] Missing required GLUE host callback/view");
        return false;
    }
    const auto state = (*GetState)();
    if(!state)
    {
        BOSS_TRACE("[navigation_and_depth] Host returned no native state");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Native navigation and depth ordering. Names are scoped to this widget view.
    NewZayson.AddGlue("twcreate", ZAY_DECLARE_GLUE(params, state, view, viewname)
    {
        bool ok=false;
        if(2<=params.ParamCount() && params.ParamCount()<=5)
        {
            const String name=params.Param(0).ToText(), source=params.Param(1).ToText();
            const String output=params.ParamCount()>4?params.Param(4).ToText():source+".build";
            String base, result;
            if(ZayNativeDom(output,result))
            {
                std::string error="invalid_arguments";
                if(name.Length()>0 && name.Left(2)!="d." && ZayNativeDom(source,base))
                {
                    const double halfWidth=params.ParamCount()>2?params.Param(2).ToFloat():0;
                    const double halfHeight=params.ParamCount()>3?params.Param(3).ToFloat():halfWidth;
                    ZayGlueNativeState::Geometry geometry;
                    if(ZayNativeGeometry(base,halfWidth,halfHeight,geometry,error))
                    {
                        const ZayGlueNativeState::Key key((chars)viewname,(chars)name);
                        std::vector<ZayGlueNativeState::Key> changed;
                        ok=state->Create(key,geometry,error,changed);
                        ZayNativeChangedRoutes(*state,changed);
                        if(ok)
                        {
                            const auto& world=*state->worlds.at(key);
                            ZayNativePutNumber(result+".vertices",world.map->Dots.Count());
                            ZayNativePutNumber(result+".walls",double(world.walls.size()));
                            ZayNativePutNumber(result+".half_width",halfWidth);
                            ZayNativePutNumber(result+".half_height",halfHeight);
                        }
                    }
                }
                ZayNativeStatus(result,ok,ok?"ready":error.c_str());
                view->invalidate();
            }
        }
        if(params.CanReturn()) params.Return(ok?"1":"0");
    }, "[World:'scube.small'][GeometryDomD:'d.scube.geometry']#[HalfWidth:8][HalfHeight:5][ResultDomD:'d.scube.build']", "체형별 트라이월드 맵 생성(허리 반폭/반높이)");

    NewZayson.AddGlue("twpath", ZAY_DECLARE_GLUE(params, state, view, viewname)
    {
        bool ok=false;
        if(params.ParamCount()==6)
        {
            const String name=params.Param(0).ToText(), output=params.Param(5).ToText();
            String base;
            if(ZayNativeDom(output,base))
            {
                const ZayGlueNativeState::Key worldKey((chars)viewname,(chars)name), routeKey((chars)viewname,(chars)output);
                ok=state->FindPath(worldKey,routeKey,
                    Point(params.Param(1).ToFloat(),params.Param(2).ToFloat()),
                    Point(params.Param(3).ToFloat(),params.Param(4).ToFloat()));
                ZayNativeRoute(base,*state->routes.at(routeKey));
                view->invalidate();
            }
        }
        if(params.CanReturn()) params.Return(ok?"1":"0");
    }, "[World:'scube.small'][StartX:100][StartY:100][GoalX:500][GoalY:300][RouteDomD:'d.scube.route.me']", "캐릭터별 경로 생성 및 이전 경로 교체");

    NewZayson.AddGlue("twstep", ZAY_DECLARE_GLUE(params, state, view, viewname)
    {
        bool ok=false;
        if(params.ParamCount()==3)
        {
            const String output=params.Param(0).ToText(); String base;
            if(ZayNativeDom(output,base))
            {
                const ZayGlueNativeState::Key key((chars)viewname,(chars)output);
                const auto found=state->routes.find(key);
                if(found!=state->routes.end())
                {
                    ok=state->Step(key,params.Param(1).ToFloat(),params.Param(2).ToFloat());
                    ZayNativeRoute(base,*found->second);
                    if(!ok && found->second->ok)
                    { ZayNativeStatus(base,false,"invalid_step"); ZayNativePutNumber(base+".moving",0); }
                }
                else
                {
                    ZayNativeStatus(base,false,"path_not_found");
                    ZayNativePutNumber(base+".moving",0); ZayNativePutNumber(base+".arrived",0);
                }
                view->invalidate();
            }
        }
        if(params.CanReturn()) params.Return(ok?"1":"0");
    }, "[RouteDomD:'d.scube.route.me'][Speed:85][DeltaSeconds:0.016]", "경로 추종/충돌 검사 후 좌표와 방향 갱신(초 단위)");

    NewZayson.AddGlue("twrelease", ZAY_DECLARE_GLUE(params, state, view, viewname)
    {
        bool ok=false;
        if(params.ParamCount()==1)
        {
            const String name=params.Param(0).ToText(); String base;
            const ZayGlueNativeState::Key key((chars)viewname,(chars)name);
            if(ZayNativeDom(name,base))
            {
                ok=state->routes.erase(key)!=0;
                ZayNativeStatus(base,false,"released");
                ZayNativePutNumber(base+".moving",0); ZayNativePutNumber(base+".arrived",0);
                ZayNativePutNumber(base+".blocked",0); ZayNativePutNumber(base+".point_count",0);
            }
            else
            {
                std::vector<ZayGlueNativeState::Key> changed;
                ok=state->ReleaseWorld(key,changed);
                ZayNativeChangedRoutes(*state,changed);
            }
            if(ok) view->invalidate();
        }
        if(params.CanReturn()) params.Return(ok?"1":"0");
    }, "[WorldOrRouteDomD:'scube.small']", "맵 또는 경로 해제(맵 해제 시 연결 경로 무효화)");

    // FootY stays third for backward compatibility; optional FootX enables front-edge tests.
    NewZayson.AddGlue("depthset", ZAY_DECLARE_GLUE(params, state, view, viewname)
    {
        bool ok=false;
        const int n=params.ParamCount();
        if(n==3 || n==4 || n==8)
        {
            const String output=params.Param(0).ToText(), literal=params.Param(1).ToText(true); String base;
            ZayGlueNativeState::DepthEntry entry;
            entry.id=(chars)literal; entry.literal=(chars)literal; entry.y=params.Param(2).ToFloat();
            if(n>=4) { entry.hasX=true; entry.x=params.Param(3).ToFloat(); }
            if(n==8)
            {
                entry.bounded=true; entry.l=params.Param(4).ToFloat(); entry.t=params.Param(5).ToFloat();
                entry.r=params.Param(6).ToFloat(); entry.b=params.Param(7).ToFloat();
            }
            if(ZayNativeDom(output,base) && ZayGlueNativeState::Depth::Valid(entry))
            {
                auto& depth=state->depths[{(chars)viewname,(chars)output}];
                int first=-1,last=-1; ok=depth.Put(entry,first,last);
                if(ok && first>=0) { ZayNativeDepth(base,depth,first,last); view->invalidate(); }
            }
        }
        if(params.CanReturn()) params.Return(ok?"1":"0");
    }, "[OrderDomD:'d.scube.depth'][ObjectID:0][FootY:350][FootX:300][Left:280][Top:250][Right:320][Bottom:355]", "개체 발밑 Y/X와 선택적 출력 사각형 갱신. 3/4/8개 인자 지원");

    NewZayson.AddGlue("depthfront", ZAY_DECLARE_GLUE(params, state, view, viewname)
    {
        bool ok=false;
        const int n=params.ParamCount();
        if(n==6 || n==10)
        {
            const String output=params.Param(0).ToText(), literal=params.Param(1).ToText(true); String base;
            ZayGlueNativeState::DepthEntry entry;
            entry.id=(chars)literal; entry.literal=(chars)literal; entry.hasX=entry.front=true;
            entry.x=params.Param(2).ToFloat(); entry.y=params.Param(3).ToFloat();
            entry.leftSlope=params.Param(4).ToFloat(); entry.rightSlope=params.Param(5).ToFloat();
            if(n==10)
            {
                entry.bounded=true; entry.l=params.Param(6).ToFloat(); entry.t=params.Param(7).ToFloat();
                entry.r=params.Param(8).ToFloat(); entry.b=params.Param(9).ToFloat();
            }
            if(ZayNativeDom(output,base) && ZayGlueNativeState::Depth::Valid(entry))
            {
                auto& depth=state->depths[{(chars)viewname,(chars)output}];
                int first=-1,last=-1; ok=depth.Put(entry,first,last);
                if(ok && first>=0) { ZayNativeDepth(base,depth,first,last); view->invalidate(); }
            }
        }
        if(params.CanReturn()) params.Return(ok?"1":"0");
    }, "[OrderDomD:'d.scube.depth'][ObjectID:0][CornerX:300][CornerY:350][LeftSlope:0.6][RightSlope:-1.1][Left:240][Top:200][Right:360][Bottom:355]", "건물의 앞 꼭짓점/좌우 바닥 경계선과 선택적 출력 사각형 등록. 6/10개 인자 지원");

    NewZayson.AddGlue("depthafterstatic", ZAY_DECLARE_GLUE(params, state, view, viewname)
    {
        int rank = -1;
        if(params.ParamCount() == 7)
        {
            const String name = params.Param(0).ToText();
            const auto group = state->depths.find({(chars) viewname, (chars) name});
            ZayGlueNativeState::DepthEntry actor;
            actor.hasX = actor.bounded = true;
            actor.y = params.Param(1).ToFloat(); actor.x = params.Param(2).ToFloat();
            actor.l = params.Param(3).ToFloat(); actor.t = params.Param(4).ToFloat();
            actor.r = params.Param(5).ToFloat(); actor.b = params.Param(6).ToFloat();
            if(group != state->depths.end() && ZayGlueNativeState::Depth::Valid(actor))
            {
                actor.serial = group->second.serial; // Equal Y draws the actor in front of static entries.
                rank = group->second.AfterStatic(actor);
            }
        }
        if(params.CanReturn()) params.Return(String::FromInteger(rank));
    }, "[StaticOrderDomD:'d.scene.static'][FootY:350][FootX:300][Left:280][Top:250][Right:320][Bottom:355]", "캐릭터 발 위치와 겹치는 건물의 전면 경계로 정적 장면 삽입 순번 계산(-1은 맨 뒤)");

    NewZayson.AddGlue("depthremove", ZAY_DECLARE_GLUE(params, state, view, viewname)
    {
        bool ok=false;
        if(params.ParamCount()==2)
        {
            const String output=params.Param(0).ToText(), literal=params.Param(1).ToText(true); String base;
            if(ZayNativeDom(output,base))
            {
                const ZayGlueNativeState::Key key((chars)viewname,(chars)output);
                const auto found=state->depths.find(key);
                int first=-1;
                if(found!=state->depths.end() && found->second.Remove((chars)literal,first))
                {
                    const auto& depth=found->second; ok=true;
                    ZayNativeDepth(base,depth,first,int(depth.order.size())-1);
                    // Clear the vacated slot without prefix-deleting IDs 1, 10, 100, ...
                    ZayWidgetDOM::SetValue(base+String::Format(".%d",int(depth.order.size())),"''");
                    view->invalidate();
                }
            }
        }
        if(params.CanReturn()) params.Return(ok?"1":"0");
    }, "[OrderDomD:'d.scube.depth'][ObjectID:0]", "개체를 깊이 정렬에서 제거");

    NewZayson.AddGlue("depthclear", ZAY_DECLARE_GLUE(params, state, view, viewname)
    {
        bool ok=false;
        if(params.ParamCount()==1)
        {
            const String output=params.Param(0).ToText(); String base;
            if(ZayNativeDom(output,base))
            {
                const ZayGlueNativeState::Key key((chars)viewname,(chars)output);
                auto found=state->depths.find(key);
                if(found!=state->depths.end())
                {
                    for(size_t i=0; i<found->second.order.size(); ++i)
                        ZayWidgetDOM::SetValue(base+String::Format(".%d",int(i)),"''");
                    state->depths.erase(found);
                }
                ZayNativePutNumber(base+".count",0); ZayNativePutNumber(base+".revision",0);
                ok=true; view->invalidate();
            }
        }
        if(params.CanReturn()) params.Return(ok?"1":"0");
    }, "[OrderDomD:'d.scube.depth']", "깊이 정렬 그룹 초기화");
    return true;
}
