#pragma once
#include "zayglue.hpp"
#include <service/boss_zaywidget.hpp>
#include <service/boss_tryworld.hpp>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <algorithm>
#include <cmath>
#include <utility>
#include <limits>

// Shared implementation state for the view and the rendering/navigation GLUEs.
// Bind one shared instance per host; global GLUE objects do not own host state.
// BEGIN ZAY_GLUE_NATIVE_NAVIGATION
// All calls run on the widget/UI thread (TryWorld has shared scratch state).
class ZayGlueNativeState
{
public:
    typedef std::pair<std::string, std::string> Key;
    struct Polygon { std::vector<Point> points; bool subtract = false; };
    struct Geometry
    {
        double l = 0, t = 0, r = 0, b = 0, halfWidth = 0, halfHeight = 0;
        bool polygonsPreexpanded = false;
        std::vector<Rect> rectangles;
        std::vector<Polygon> polygons;
    };
    struct Edge { Point a, b; };
    static double Cross(const Point& a, const Point& b, const Point& c)
    { return (double(b.x) - a.x) * (double(c.y) - a.y) - (double(b.y) - a.y) * (double(c.x) - a.x); }
    static double Distance(const Point& a, const Point& b)
    { return std::hypot(double(a.x) - b.x, double(a.y) - b.y); }
    static bool Finite(const Point& p) { return std::isfinite(p.x) && std::isfinite(p.y); }
    static double PointEdgeDistance(const Point& p, const Edge& e)
    {
        const double x = double(e.b.x) - e.a.x, y = double(e.b.y) - e.a.y;
        const double length2 = x*x + y*y;
        const double u = length2 > 0 ? std::max(0.0, std::min(1.0,
            ((p.x-e.a.x)*x + (p.y-e.a.y)*y) / length2)) : 0;
        return std::hypot(p.x-e.a.x-u*x, p.y-e.a.y-u*y);
    }
    static bool Touches(const Point& a, const Point& b, const Edge& e)
    {
        const double ab1 = Cross(a,b,e.a), ab2 = Cross(a,b,e.b);
        const double cd1 = Cross(e.a,e.b,a), cd2 = Cross(e.a,e.b,b);
        if(((ab1 < 0 && ab2 > 0) || (ab1 > 0 && ab2 < 0)) &&
           ((cd1 < 0 && cd2 > 0) || (cd1 > 0 && cd2 < 0))) return true;
        const Edge move {a,b};
        return PointEdgeDistance(a,e) <= 0.002 || PointEdgeDistance(b,e) <= 0.002 ||
            PointEdgeDistance(e.a,move) <= 0.002 || PointEdgeDistance(e.b,move) <= 0.002;
    }
    struct World
    {
        TryWorld::Hurdle* hurdle = nullptr;
        TryWorld::Map* map = nullptr;
        double ox = 0, oy = 0, scale = 1;
        float l = 0, t = 0, r = 0, b = 0;
        std::vector<Edge> walls;
        std::vector<Polygon> regions; // Original Boolean geometry; mesh wall lines need not form closed rings.
        ~World() { TryWorld::Map::Release(map); TryWorld::Hurdle::Release(hurdle); }
        Point ToLocal(double x, double y) const { return Point((x-ox)*scale, (y-oy)*scale); }
        Point ToWorld(const Point& p) const { return Point(p.x/scale+ox, p.y/scale+oy); }
        bool Walkable(const Point& p) const
        {
            if(!Finite(p) || p.x <= l+0.002 || p.x >= r-0.002 || p.y <= t+0.002 || p.y >= b-0.002) return false;
            bool blocked = false;
            for(const auto& region : regions)
            {
                bool inside=false;
                for(size_t i=0; i<region.points.size(); ++i)
                {
                    const auto& a=region.points[i]; const auto& b=region.points[(i+1)%region.points.size()];
                    if(PointEdgeDistance(p,Edge{a,b})<=0.002) { inside=true; break; }
                    if((a.y>p.y)!=(b.y>p.y) && p.x<a.x+(double(p.y)-a.y)*(double(b.x)-a.x)/(double(b.y)-a.y)) inside=!inside;
                }
                if(inside) blocked=!region.subtract;
            }
            return !blocked;
        }
        bool Clear(const Point& a, const Point& b) const
        {
            if(!Walkable(a) || !Walkable(b)) return false;
            for(const auto& edge : walls) if(Touches(a,b,edge)) return false;
            return true;
        }
    };
    struct Route
    {
        Key worldKey;
        World* world = nullptr;
        TryWorld::Path* path = nullptr;
        Point pos, goal;
        std::string status = "ready", dir = "se";
        bool ok = true, moving = false, arrived = false, blocked = false;
        double moved = 0;
        ~Route() { TryWorld::Path::Release(path); }
    };
    struct DepthEntry
    {
        std::string id, literal;
        double y = 0, x = 0, leftSlope = 0, rightSlope = 0;
        double l = 0, t = 0, r = 0, b = 0;
        bool hasX = false, front = false, bounded = false;
        unsigned long long serial = 0;
    };
    struct Depth
    {
        std::vector<DepthEntry> order, nodes;
        std::map<std::string, size_t> indices;
        // Cached pair constraints, indexed by registration slot, not draw rank.
        // -2/+2: building front boundary; -1/+1: ordinary Y; 0: no overlap.
        std::vector<std::vector<signed char>> edges;
        unsigned long long serial = 0, revision = 0, rebuilds = 0;
        unsigned int cycleBreaks = 0;
        static bool Less(const DepthEntry& a, const DepthEntry& b)
        { return a.y < b.y || (a.y == b.y && a.serial < b.serial); }
        static bool InFront(const DepthEntry& building, const DepthEntry& point)
        {
            const double slope = point.x <= building.x ? building.leftSlope : building.rightSlope;
            // Screen/world-map Y grows downwards. Equality belongs to the front.
            return point.y >= building.y + slope * (point.x - building.x) - 0.000001;
        }
        static signed char Relation(const DepthEntry& a, const DepthEntry& b)
        {
            if(a.bounded && b.bounded && (a.r <= b.l || b.r <= a.l || a.b <= b.t || b.b <= a.t))
                return 0; // Distant buildings must not constrain each other through infinite rays.
            if(a.hasX && b.hasX && a.front != b.front)
            {
                if(a.front) return InFront(a,b) ? -2 : 2;
                return InFront(b,a) ? 2 : -2;
            }
            return Less(a,b) ? -1 : 1;
        }
        // Pick a cut in the cached back-to-front static order. Moving the cut past
        // one item changes only that item's constraint. Prefer visible facade
        // boundaries, then other overlapping sprites, then ordinary foot Y.
        int AfterStatic(const DepthEntry& actor) const
        {
            int bestRank = -1, bestFront = 0, bestOverlap = 0, bestY = 0;
            int front = 0, overlap = 0, footY = 0;
            for(size_t rank = 0; rank < order.size(); ++rank)
            {
                const auto& item = order[rank];
                const int relation = Relation(item, actor);
                if(relation == -2) --front;
                else if(relation == 2) ++front;
                else overlap += relation;
                footY += Less(item, actor) ? -1 : 1;
                if(front < bestFront || (front == bestFront && (overlap < bestOverlap ||
                    (overlap == bestOverlap && footY < bestY))))
                {
                    bestRank = int(rank);
                    bestFront = front;
                    bestOverlap = overlap;
                    bestY = footY;
                }
            }
            return bestRank;
        }
        static bool Same(const DepthEntry& a, const DepthEntry& b)
        {
            return a.x==b.x && a.y==b.y && a.hasX==b.hasX && a.front==b.front &&
                a.leftSlope==b.leftSlope && a.rightSlope==b.rightSlope && a.bounded==b.bounded &&
                a.l==b.l && a.t==b.t && a.r==b.r && a.b==b.b && a.literal==b.literal;
        }
        static bool Valid(const DepthEntry& e)
        {
            return std::isfinite(e.y) && (!e.hasX || std::isfinite(e.x)) &&
                (!e.front || (e.hasX && std::isfinite(e.leftSlope) && std::isfinite(e.rightSlope) &&
                    e.leftSlope>=0 && e.rightSlope<=0)) &&
                (!e.bounded || (std::isfinite(e.l) && std::isfinite(e.t) && std::isfinite(e.r) &&
                    std::isfinite(e.b) && e.l<e.r && e.t<e.b));
        }
        void Reorder(int& first, int& last)
        {
            ++rebuilds; cycleBreaks=0;
            const size_t count=nodes.size();
            std::vector<unsigned int> incoming(count,0), strong(count,0);
            std::vector<bool> done(count,false);
            for(size_t i=0;i<count;++i) for(size_t j=0;j<count;++j)
                if(edges[i][j]<0) { ++incoming[j]; if(edges[i][j]==-2) ++strong[j]; }
            std::vector<DepthEntry> next; next.reserve(count);
            for(size_t step=0;step<count;++step)
            {
                size_t pick=count;
                for(size_t i=0;i<count;++i) if(!done[i] && incoming[i]==0 &&
                    (pick==count || Less(nodes[i],nodes[pick]))) pick=i;
                if(pick==count)
                {
                    // Approximate whole-sprite ordering can form a cycle. Break ordinary Y
                    // edges first; never feed this non-transitive relation to std::sort.
                    ++cycleBreaks;
                    for(size_t i=0;i<count;++i) if(!done[i] &&
                        (pick==count || strong[i]<strong[pick] ||
                        (strong[i]==strong[pick] && Less(nodes[i],nodes[pick])))) pick=i;
                }
                done[pick]=true; next.push_back(nodes[pick]);
                for(size_t j=0;j<count;++j) if(!done[j] && edges[pick][j]<0)
                { --incoming[j]; if(edges[pick][j]==-2) --strong[j]; }
            }
            first=last=-1;
            for(size_t i=0;i<next.size();++i)
                if(i>=order.size() || next[i].id!=order[i].id)
                { if(first<0) first=int(i); last=int(i); }
            if(next.size()!=order.size() && first<0) first=int(next.size());
            if(first>=0) ++revision;
            order.swap(next);
        }
        bool Put(DepthEntry entry, int& first, int& last)
        {
            first=last=-1;
            if(!Valid(entry)) return false;
            const auto found=indices.find(entry.id);
            bool changed=found==indices.end();
            size_t index;
            if(changed)
            {
                index=nodes.size(); entry.serial=serial++; indices[entry.id]=index;
                nodes.push_back(entry);
                for(auto& row:edges) row.push_back(0);
                edges.emplace_back(nodes.size(),0);
            }
            else
            {
                index=found->second; entry.serial=nodes[index].serial;
                if(Same(nodes[index],entry)) return true;
                nodes[index]=entry;
                for(auto& item:order) if(item.id==entry.id) { item=entry; break; }
            }
            // Moving one object only re-evaluates its row. Static pairs remain cached.
            for(size_t j=0;j<nodes.size();++j) if(j!=index)
            {
                const signed char relation=Relation(entry,nodes[j]);
                if(edges[index][j]!=relation) changed=true;
                edges[index][j]=relation; edges[j][index]=-relation;
            }
            if(changed) Reorder(first,last);
            return true;
        }
        // The original 3-argument GLUE remains a stable ascending-Y order.
        bool Set(const std::string& id, const std::string& literal, double y, int& first, int& last)
        {
            DepthEntry e; e.id=id; e.literal=literal; e.y=y;
            return Put(e,first,last);
        }
        bool Remove(const std::string& id, int& first)
        {
            const auto found=indices.find(id);
            if(found==indices.end()) return false;
            const size_t index=found->second; indices.erase(found);
            nodes.erase(nodes.begin()+index); edges.erase(edges.begin()+index);
            for(auto& row:edges) row.erase(row.begin()+index);
            for(size_t i=index;i<nodes.size();++i) indices[nodes[i].id]=i;
            int last; Reorder(first,last);
            return true;
        }
    };

    std::map<Key, std::unique_ptr<World>> worlds;
    std::map<Key, std::unique_ptr<Route>> routes;
    std::map<Key, Depth> depths;
    // The marker supplies coordinates for GLUE, which has no panel parameter of its own.
    struct SceneView
    {
        rect128 viewport {};
        Point origin;
        double scale = 0;
        Color color = Color::ColoringDefault;
        uint64 revision = 0;

        bool Same(const SceneView& rhs) const
        {
            return viewport.l == rhs.viewport.l && viewport.t == rhs.viewport.t &&
                viewport.r == rhs.viewport.r && viewport.b == rhs.viewport.b &&
                origin.x == rhs.origin.x && origin.y == rhs.origin.y && scale == rhs.scale &&
                color.argb == rhs.color.argb && revision == rhs.revision;
        }
    };
    struct Scene
    {
        id_depthscene handle = nullptr;
        SceneView built;
        bool ready = false;
        ~Scene() {Platform::Graphics::RemoveDepthScene(handle);}
    };
    using SceneKey = std::pair<const ZayWidget*, std::string>;
    std::map<SceneKey, Scene> scenes;
    SceneKey paintKey {};
    SceneView paintView;
    bool painting = false;
    uint64 sceneRevision = 1;
    bool sceneSmooth = true;

    void MarkViewport(const ZayWidget* widget, const String& name, const ZayPanel& panel)
    {
        paintKey = {};
        if(!painting || !name.Length() || panel.zoom().orientation != OR_Normal) return;
        SceneView view;
        view.scale = panel.zoom().scale * Platform::Graphics::GetDevicePixelRatio();
        if(!(view.scale > 0) || !std::isfinite(view.scale)) return;
        view.origin = panel.toview(0, 0);
        const auto clip = panel.scissor();
        // Keep the untrimmed origin for all sprites, even when the viewport is clipped on the left/top.
        const double left = std::floor(std::max(view.origin.x, clip.l) * view.scale);
        const double top = std::floor(std::max(view.origin.y, clip.t) * view.scale);
        const double right = std::ceil(std::min(view.origin.x + panel.w(), clip.r) * view.scale);
        const double bottom = std::ceil(std::min(view.origin.y + panel.h(), clip.b) * view.scale);
        if(!(left < right && top < bottom && left >= std::numeric_limits<sint32>::min() &&
            top >= std::numeric_limits<sint32>::min() && right <= std::numeric_limits<sint32>::max() &&
            bottom <= std::numeric_limits<sint32>::max())) return;
        view.viewport = {sint32(left), sint32(top), sint32(right), sint32(bottom)};
        view.color = panel.color();
        view.revision = sceneRevision;
        paintView = view;
        paintKey = {widget, (chars) name};
    }
    Scene* PaintScene(const ZayWidget* widget, const String& name)
    {
        if(!painting || paintKey.first != widget || paintKey.second != (chars) name) return nullptr;
        const auto it = scenes.find(paintKey);
        return it != scenes.end() ? &it->second : nullptr;
    }

    struct ZoomState
    {
        int pressX=0, pressY=0;
        double zoomX=0, zoomY=0;
        bool pressed=false, dragged=false;
        void Press(int x,int y,double ox,double oy)
        { pressX=x;pressY=y;zoomX=ox;zoomY=oy;pressed=true;dragged=false; }
        bool Drag(int x,int y)
        { if(pressed && (std::abs(x-pressX)>6 || std::abs(y-pressY)>6)) dragged=true;return pressed&&dragged; }
        bool Release(int x,int y,bool inside)
        { Drag(x,y);bool click=pressed&&!dragged&&inside;pressed=false;return click; }
    };
    std::map<std::string,ZoomState> zooms;

    static void AddRect(World& w, double l, double t, double r, double b)
    {
        if(l>=r || t>=b) return;
        TryWorld::DotList p;
        p.AtAdding()=TryWorld::Dot(l,t); p.AtAdding()=TryWorld::Dot(l,b);
        p.AtAdding()=TryWorld::Dot(r,b); p.AtAdding()=TryWorld::Dot(r,t);
        Polygon region; region.points={Point(l,t),Point(l,b),Point(r,b),Point(r,t)};
        w.regions.push_back(std::move(region));
        w.hurdle->Add(p,true);
    }
    static bool ValidPolygon(const std::vector<Point>& p)
    {
        if(p.size()<3) return false;
        double area=0;
        for(size_t i=0; i<p.size(); ++i)
        {
            const size_t j=(i+1)%p.size();
            if(!Finite(p[i]) || Distance(p[i],p[j])<0.01) return false;
            area+=double(p[i].x)*p[j].y-double(p[j].x)*p[i].y;
            for(size_t k=i+1; k<p.size(); ++k)
            {
                const size_t n=(k+1)%p.size();
                if(k==j || n==i) continue;
                if(Touches(p[i],p[j],Edge{p[k],p[n]})) return false;
            }
        }
        return std::abs(area)>0.01;
    }
    // Candidate build is transactional: failures leave the old world/routes intact.
    bool Create(const Key& key, const Geometry& g, std::string& error, std::vector<Key>& invalidated)
    {
        if(!std::isfinite(g.l)||!std::isfinite(g.t)||!std::isfinite(g.r)||!std::isfinite(g.b)||
           !std::isfinite(g.halfWidth)||!std::isfinite(g.halfHeight)||
           g.halfWidth<0||g.halfHeight<0||g.r-g.l<=2*g.halfWidth||g.b-g.t<=2*g.halfHeight)
        { error="invalid_geometry"; return false; }
        if(!g.polygons.empty() && (g.halfWidth>0 || g.halfHeight>0) && !g.polygonsPreexpanded)
        { error="polygon_profile_required"; return false; }
        std::unique_ptr<World> w(new World());
        w->ox=g.l; w->oy=g.t;
        w->scale=1024.0/std::max(g.r-g.l,g.b-g.t);
        if(!std::isfinite(w->scale) || w->scale<=0) { error="invalid_geometry"; return false; }
        const Point lower=w->ToLocal(g.l+g.halfWidth,g.t+g.halfHeight);
        const Point upper=w->ToLocal(g.r-g.halfWidth,g.b-g.halfHeight);
        w->l=lower.x; w->t=lower.y; w->r=upper.x; w->b=upper.y;
        if(w->r-w->l<0.1 || w->b-w->t<0.1) { error="invalid_geometry"; return false; }
        w->hurdle=TryWorld::Hurdle::Create();
        // A closed obstacle frame prevents routes from escaping logical map bounds.
        const double ol=-8, ot=-8, ore=(g.r-g.l)*w->scale+8, ob=(g.b-g.t)*w->scale+8;
        AddRect(*w,ol,ot,ore,w->t); AddRect(*w,ol,w->b,ore,ob);
        AddRect(*w,ol,w->t,w->l,w->b); AddRect(*w,w->r,w->t,ore,w->b);
        for(const auto& rect : g.rectangles)
        {
            if(!std::isfinite(rect.l)||!std::isfinite(rect.t)||!std::isfinite(rect.r)||!std::isfinite(rect.b)||rect.l>=rect.r||rect.t>=rect.b)
            { error="invalid_rectangle"; return false; }
            Point a=w->ToLocal(rect.l-g.halfWidth,rect.t-g.halfHeight);
            Point b=w->ToLocal(rect.r+g.halfWidth,rect.b+g.halfHeight);
            AddRect(*w,std::max(double(w->l),double(a.x)),std::max(double(w->t),double(a.y)),
                std::min(double(w->r),double(b.x)),std::min(double(w->b),double(b.y)));
        }
        for(const auto& polygon : g.polygons)
        {
            std::vector<Point> local;
            for(const auto& p : polygon.points)
            {
                if(!Finite(p)||p.x<g.l||p.x>g.r||p.y<g.t||p.y>g.b)
                { error="polygon_out_of_bounds"; return false; }
                local.push_back(w->ToLocal(p.x,p.y));
            }
            if(local.size()>1 && Distance(local.front(),local.back())<0.002) local.pop_back();
            if(!ValidPolygon(local)) { error="invalid_polygon"; return false; }
            // Subtractions must stay inside the logical boundary, preserving its frame.
            if(polygon.subtract) for(const auto& p : local)
                if(p.x<=w->l||p.x>=w->r||p.y<=w->t||p.y>=w->b)
                { error="subtraction_touches_bounds"; return false; }
            TryWorld::DotList dots;
            for(const auto& p : local) dots.AtAdding()=TryWorld::Dot(p);
            Polygon region; region.points=local; region.subtract=polygon.subtract;
            w->regions.push_back(std::move(region));
            w->hurdle->Add(dots,!polygon.subtract);
        }
        w->map=w->hurdle->BuildMap(Rect(ol-8,ot-8,ore+8,ob+8));
        if(!w->map) { error="build_failed"; return false; }
        for(int i=0; i<w->map->Lines.Count(); ++i)
        {
            const auto& line=w->map->Lines[i];
            if(line.Type!=TryWorld::linetype_wall) continue;
            const auto& a=w->map->Dots[line.DotA]; const auto& b=w->map->Dots[line.DotB];
            if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(b.x)||!std::isfinite(b.y))
            { error="build_failed"; return false; }
            w->walls.push_back(Edge{Point(a.x,a.y),Point(b.x,b.y)});
        }
        Invalidate(key,"map_rebuilt",invalidated);
        worlds[key]=std::move(w);
        error.clear(); return true;
    }
    void Invalidate(const Key& worldKey, const char* status, std::vector<Key>& changed)
    {
        for(auto& item : routes) if(item.second->worldKey==worldKey && item.second->world)
        {
            auto& route=*item.second;
            // Keep world-space final coordinates after releasing the world.
            route.pos=route.world->ToWorld(route.pos); route.goal=route.world->ToWorld(route.goal);
            route.world=nullptr; TryWorld::Path::Release(route.path);
            route.status=status; route.ok=false; route.moving=false; route.arrived=false; route.blocked=false; route.moved=0;
            changed.push_back(item.first);
        }
    }
    bool FindPath(const Key& worldKey, const Key& routeKey, Point start, Point goal)
    {
        std::unique_ptr<Route> route(new Route());
        route->worldKey=worldKey; route->pos=Finite(start)?start:Point(0,0); route->goal=Finite(goal)?goal:route->pos;
        const auto wi=worlds.find(worldKey);
        if(wi==worlds.end()) { route->ok=false; route->status="world_not_found"; }
        else if(!Finite(start)||!Finite(goal)) { route->ok=false; route->status="invalid_position"; }
        else
        {
            route->world=wi->second.get(); World& w=*route->world;
            route->pos=w.ToLocal(start.x,start.y); route->goal=w.ToLocal(goal.x,goal.y);
            if(!Finite(route->pos)||!Finite(route->goal))
            { route->world=nullptr; route->pos=start; route->goal=goal; route->ok=false; route->status="invalid_position"; }
            else if(!w.Walkable(route->pos)) { route->ok=false; route->status="invalid_start"; }
            else if(!w.Walkable(route->goal)) { route->ok=false; route->status="invalid_goal"; }
            else if(Distance(route->pos,route->goal)<=0.002)
            { route->arrived=true; route->status="arrived"; }
            else
            {
                // TryWorld's raw mesh path may detour even when the target is visible.
                if(w.Clear(route->pos,route->goal))
                {
                    route->path=w.map->CreatePath(5);
                    route->path->Dots.AtAdding()=TryWorld::Dot(route->goal);
                    route->path->Dots.AtAdding()=TryWorld::Dot(route->pos);
                }
                else route->path=w.map->BuildPath(TryWorld::Dot(route->pos),TryWorld::Dot(route->goal),5);
                if(!route->path) { route->ok=false; route->status="no_path"; }
                else { route->moving=true; route->status="moving"; }
            }
        }
        const bool ok=route->ok;
        routes[routeKey]=std::move(route); // A new destination cancels this actor's previous path.
        return ok;
    }
    bool Step(const Key& routeKey, double speed, double seconds)
    {
        auto it=routes.find(routeKey); if(it==routes.end()) return false;
        Route& route=*it->second; route.moved=0;
        if(!std::isfinite(speed)||!std::isfinite(seconds)||speed<0||seconds<0||seconds>1)
            return false; // Caller may split long updates; never teleport after a pause.
        if(!route.ok || !route.world || route.arrived || !route.path) return route.ok;
        if(speed==0 || seconds==0) { route.moving=false; route.status="paused"; return true; }
        World& w=*route.world;
        double budget=speed*seconds*w.scale;
        if(!std::isfinite(budget) || budget>4096) return false;
        route.moving=true; route.blocked=false; route.status="moving";
        for(int iteration=0; budget>0 && iteration<512; ++iteration)
        {
            Point target;
            bool haveTarget=false;
            if(w.Clear(route.pos,route.goal)) { target=route.goal; haveTarget=true; }
            else
            {
                const int oldFocus=route.path->DotFocus;
                if(TryWorld::GetPosition::SubTarget(w.hurdle,route.path,route.pos,target) &&
                   Distance(route.pos,target)>0.002 && w.Clear(route.pos,target)) haveTarget=true;
                else
                {
                    route.path->DotFocus=oldFocus;
                    // SubTarget advances focus before its waypoint is reached. If a corner
                    // loses visibility after float rounding, allow a visible earlier
                    // waypoint instead of treating that provisional focus as a wall.
                    for(int i=0; i<route.path->Dots.Count(); ++i)
                    {
                        const auto& dot=route.path->Dots[i]; const Point candidate(dot.x,dot.y);
                        if(Distance(route.pos,candidate)>0.002 && w.Clear(route.pos,candidate))
                        { target=candidate; route.path->DotFocus=route.path->Dots.Count()-1-i; haveTarget=true; break; }
                    }
                }
            }
            if(!haveTarget)
            { route.status="blocked"; route.blocked=true; route.moving=false; break; }
            const double distance=Distance(route.pos,target), move=std::min(budget,distance);
            const Point next(route.pos.x+(target.x-route.pos.x)*move/distance,
                             route.pos.y+(target.y-route.pos.y)*move/distance);
            Point result=next, reflect=next;
            const auto hitEdge=TryWorld::GetPosition::GetValidNext(w.hurdle,route.pos,next,result,reflect);
            // TryWorld's integer orientation can report a near-corner hit for a short
            // step that does not actually cross that edge. Confirm its segment in double precision.
            const bool hit=hitEdge && Touches(route.pos,next,Edge{
                Point(hitEdge[0]->x,hitEdge[0]->y),Point(hitEdge[1]->x,hitEdge[1]->y)});
            if(hit || !w.Clear(route.pos,next))
            { route.status="blocked"; route.blocked=true; route.moving=false; break; }
            const double dx=double(next.x)-route.pos.x, dy=double(next.y)-route.pos.y;
            route.dir=dy>=0 ? (dx>=0?"se":"sw") : (dx>=0?"ne":"nw");
            route.pos=next; budget-=move; route.moved+=move/w.scale;
            if(Distance(route.pos,route.goal)<=0.002)
            { route.pos=route.goal; route.arrived=true; route.moving=false; route.status="arrived"; break; }
        }
        return true;
    }
    bool ReleaseWorld(const Key& key, std::vector<Key>& changed)
    {
        if(worlds.find(key)==worlds.end()) return false;
        Invalidate(key,"world_released",changed); worlds.erase(key); return true;
    }
};
// END ZAY_GLUE_NATIVE_NAVIGATION
