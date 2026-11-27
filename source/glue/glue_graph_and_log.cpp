#include <boss.hpp>
#include "glue_graph_and_log.hpp"

ZAY_DECLARE_GLUE_CLASS("graph_and_log", GlueGraphAndLog)

bool GlueGraphAndLog::Register(ZaySon& NewZayson, const ZayGlue::Bindings& context)
{
    const auto Graphs = context.host.Find<Map<doubles>&()>("graph.values");
    const auto LogHighlights = context.host.Find<Map<String>&()>("log.highlights");
    if(!Graphs || !LogHighlights)
    {
        BOSS_TRACE("[graph_and_log] Missing required GLUE host callback/view");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // addgraph
    NewZayson.AddGlue("addgraph", ZAY_DECLARE_GLUE(params, Graphs)
    {
        if(params.ParamCount() == 3)
        {
            auto GraphName = params.Param(0).ToText();
            auto Value = params.Param(1).ToFloat();
            auto MaxCount = params.Param(1).ToInteger();
            // 그래프에 값추가
            auto& CurGraph = (*Graphs)()(GraphName);
            if(0 < CurGraph.Count() - MaxCount)
                CurGraph.SubtractionSection(0, CurGraph.Count() - MaxCount);
            CurGraph.AtAdding() = Value;
        }
    }, "[GraphName:'temp'][Value:500][MaxCount:100]", "특정 그래프에 하나의 값을 추가");

    ////////////////////////////////////////////////////////////////////////////////
    // cleargraph
    NewZayson.AddGlue("cleargraph", ZAY_DECLARE_GLUE(params, Graphs)
    {
        if(params.ParamCount() == 1)
        {
            auto GraphName = params.Param(0).ToText();
            (*Graphs)().Remove(GraphName);
        }
    }, "[GraphName:'temp']", "특정 그래프를 제거");

    ////////////////////////////////////////////////////////////////////////////////
    // setloghighlight
    NewZayson.AddGlue("setloghighlight", ZAY_DECLARE_GLUE(params, LogHighlights)
    {
        if(params.ParamCount() == 2)
        {
            auto ColorCode = params.Param(0).ToText();
            auto MatchText = params.Param(1).ToText();
            if(0 < MatchText.Length())
                (*LogHighlights)()(ColorCode) = MatchText;
            else (*LogHighlights)().Remove(ColorCode);
        }
    }, "[ColorCode:'#ff00ff'][MatchText:'80 00']", "로그의 강조옵션");
    return true;
}
