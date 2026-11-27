#include <boss.hpp>
#include "glue_vds.hpp"
#include <service/boss_zaywidget.hpp>

ZAY_DECLARE_GLUE_CLASS("vds", GlueVds)

bool GlueVds::Register(ZaySon& NewZayson, const ZayGlue::Bindings& bindings)
{
    const auto ActiveProject = bindings.host.Find<String()>("vds.active_project");
    const auto HasProject = bindings.host.Find<bool(chars)>("vds.has_project");
    const auto SetQuest = bindings.host.Find<void(chars, chars, chars, sint32)>("vds.set_quest");
    const auto RemoveQuest = bindings.host.Find<void(chars, chars, chars)>("vds.remove_quest");
    const auto RunEntry = bindings.host.Find<bool(chars, chars, sint32, chars)>("vds.run_entry");
    const auto StopEntry = bindings.host.Find<bool(chars, chars)>("vds.stop_entry");
    const auto SaveStore = bindings.host.Find<void(chars, chars)>("vds.save_store");
    const auto SetMonitorFocus = bindings.host.Find<void(chars)>("vds.set_monitor_focus");
    const auto Log = bindings.host.Find<void(chars, chars)>("vds.log");
    const auto KeepUpdatingUntil = bindings.host.Find<void(uint64)>("view.update_until");
    auto* view = bindings.view;
    if(!ActiveProject || !HasProject || !SetQuest || !RemoveQuest ||
        !RunEntry || !StopEntry || !SaveStore || !SetMonitorFocus ||
        !Log || !KeepUpdatingUntil || !view)
    {
        BOSS_TRACE("[vds] Missing required typed host operations/view");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // vdsentry
    NewZayson.AddGlue("vdsentry", ZAY_DECLARE_GLUE(params, ActiveProject, HasProject, SetQuest, RemoveQuest, RunEntry, Log, view)
    {
        if(3 <= params.ParamCount())
        {
            auto Ch = params.Param(0).ToText();
            auto QuestIdx = params.Param(1).ToInteger();
            auto ScriptField = params.Param(2).ToText();
            auto StepIdx = (3 < params.ParamCount())? params.Param(3).ToInteger() : -1;
            if((*HasProject)((*ActiveProject)()))
            {
                // 라스트퀘스트 확인
                const String CacheLastQuestDom = (*ActiveProject)() + String::Format(".%s.cache.lastquest", (chars) Ch);
                const sint32 OldQuestIdx = (ZayWidgetDOM::ExistVariable(CacheLastQuestDom))? ZayWidgetDOM::GetValue(CacheLastQuestDom).ToInteger() : -1;
                const sint32 NewQuestIdx = (QuestIdx != -1)? QuestIdx : OldQuestIdx;
                if(NewQuestIdx != -1)
                {
                    const String QuestCode = ZayWidgetDOM::GetValue((*ActiveProject)() + String::Format(".data.quest.%d.code", NewQuestIdx)).ToText();
                    if(ScriptField == "script_start")
                    {
                        (*SetQuest)((*ActiveProject)(), Ch, QuestCode, NewQuestIdx);
                        // 라스트퀘스트 기억
                        ZayWidgetDOM::SetValue(CacheLastQuestDom, String::FromInteger(NewQuestIdx));
                        // 스텝포커스 저장 및 받아오기
                        const String CacheDom = (*ActiveProject)() + String::Format(".%s.cache.quest.%d", (chars) Ch, NewQuestIdx);
                        if(StepIdx == -1) StepIdx = ZayWidgetDOM::GetValue(CacheDom + ".focus").ToInteger();
                        else ZayWidgetDOM::SetValue(CacheDom + ".focus", String::FromInteger(StepIdx));
                        // 스텝별 스크립트가 있으면 쌓아둠
                        if((*RunEntry)((*ActiveProject)(), Ch, NewQuestIdx, String::Format("step.%d.script", StepIdx)))
                            view->invalidate();
                        // 퀘스트진입 스크립트가 있으면 쌓아둠
                        if((*RunEntry)((*ActiveProject)(), Ch, NewQuestIdx, "script_start"))
                            view->invalidate();
                    }
                    else if(ScriptField == "script_stop")
                    {
                        (*RemoveQuest)((*ActiveProject)(), Ch, QuestCode);
                        // 라스트퀘스트 제거
                        if(QuestIdx == -1)
                            ZayWidgetDOM::RemoveVariables(CacheLastQuestDom);
                        // 퀘스트퇴장 스크립트가 있으면 쌓아둠
                        if((*RunEntry)((*ActiveProject)(), Ch, NewQuestIdx, "script_stop"))
                            view->invalidate();
                    }
                }
            }
        }
        else (*Log)((*ActiveProject)(), "vdsentry의 파라미터 수량에러");
    }, "[Ch:'채널명'][QuestIdx:'퀘스트Idx'][ScriptField:'스크립트필드명']#[StepIdx:'스텝Idx']", "퀘스트로 VDS스크립트를 시작/종료");

    ////////////////////////////////////////////////////////////////////////////////
    // vdsreset
    NewZayson.AddGlue("vdsreset", ZAY_DECLARE_GLUE(params, ActiveProject, HasProject, Log, view)
    {
        if(params.ParamCount() == 2)
        {
            auto Ch = params.Param(0).ToText();
            auto QuestIdx = params.Param(1).ToInteger();
            if((*HasProject)((*ActiveProject)()))
            {
                const String CacheDom = (*ActiveProject)() + String::Format(".%s.cache.quest.%d", (chars) Ch, QuestIdx);
                ZayWidgetDOM::RemoveVariables(CacheDom + ".result.");
            }
            view->invalidate();
        }
        else (*Log)((*ActiveProject)(), "vdsreset의 파라미터 수량에러");
    }, "[Ch:'채널명'][QuestIdx:'퀘스트Idx']", "VDS스크립트의 결과를 초기화");

    ////////////////////////////////////////////////////////////////////////////////
    // vdsstop
    NewZayson.AddGlue("vdsstop", ZAY_DECLARE_GLUE(params, ActiveProject, StopEntry, Log, view)
    {
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            if((*StopEntry)((*ActiveProject)(), Ch))
                view->invalidate();
        }
        else (*Log)((*ActiveProject)(), "vdsstop의 파라미터 수량에러");
    }, "[Ch:'채널명']", "VDS스크립트의 통신을 중지");

    ////////////////////////////////////////////////////////////////////////////////
    // vdsseek
    NewZayson.AddGlue("vdsseek", ZAY_DECLARE_GLUE(params, ActiveProject, HasProject, Log, view)
    {
        if(params.ParamCount() == 3)
        {
            auto Ch = params.Param(0).ToText();
            auto QuestIdx = params.Param(1).ToInteger();
            auto StepIdx = params.Param(2).ToInteger();
            if((*HasProject)((*ActiveProject)()))
            {
                const String CacheDom = (*ActiveProject)() + String::Format(".%s.cache.quest.%d", (chars) Ch, QuestIdx);
                const sint32 StepFocus = ZayWidgetDOM::GetValue(CacheDom + ".focus").ToInteger();
                const String CacheStepDom = CacheDom + ".result.step" + String::FromInteger(StepFocus);
                ZayWidgetDOM::SetValue(CacheDom + ".next_step", String::FromInteger(StepIdx));
                ZayWidgetDOM::SetValue(CacheDom + ".next_wait", "0");
                ZayWidgetDOM::SetValue(CacheStepDom, "0"); // 실패
                ZayWidgetDOM::SetValue(CacheDom + ".result.errored", "1"); // 에러있음
                view->invalidate();
            }
        }
        else (*Log)((*ActiveProject)(), "vdsseek의 파라미터 수량에러");
    }, "[Ch:'채널명'][QuestIdx:'퀘스트Idx'][StepIdx:'스텝Idx']", "VDS스크립트를 포커스이동");

    ////////////////////////////////////////////////////////////////////////////////
    // vdscheck
    NewZayson.AddGlue("vdscheck", ZAY_DECLARE_GLUE(params, ActiveProject, HasProject, Log, KeepUpdatingUntil, view)
    {
        if(params.ParamCount() == 3)
        {
            auto Ch = params.Param(0).ToText();
            auto QuestIdx = params.Param(1).ToInteger();
            auto UserFlag = params.Param(2).ToText();
            if((*HasProject)((*ActiveProject)()))
            {
                const String CacheDom = (*ActiveProject)() + String::Format(".%s.cache.quest.%d", (chars) Ch, QuestIdx);
                // 다음 스텝으로 넘어가는 중이 아닐때만
                if(!ZayWidgetDOM::ExistVariable(CacheDom + ".next_step"))
                {
                    const sint32 StepFocus = ZayWidgetDOM::GetValue(CacheDom + ".focus").ToInteger();
                    const String CacheStepDom = CacheDom + ".result.step" + String::FromInteger(StepFocus);
                    sint32 GoNext = -1;
                    if(UserFlag == "ok") GoNext = 1;
                    else if(UserFlag == "skip") GoNext = 0;
                    else if(UserFlag == "all")
                    {
                        for(sint32 i = 1; i <= 8; ++i)
                            ZayWidgetDOM::SetValue(CacheStepDom + "_hp" + String::FromInteger(i), "1");
                        GoNext = 1;
                    }
                    else if(UserFlag.Left(2) == "hp")
                    {
                        auto OldFlag = ZayWidgetDOM::GetValue(CacheStepDom + "_hp" + UserFlag.Offset(2)).ToInteger();
                        ZayWidgetDOM::SetValue(CacheStepDom + "_hp" + UserFlag.Offset(2), (OldFlag != 0)? "0" : "1");
                        sint32 SumFalg = 0;
                        for(sint32 i = 1; i <= 8; ++i)
                            SumFalg += (ZayWidgetDOM::GetValue(CacheStepDom + "_hp" + String::FromInteger(i)).ToInteger() != 0);
                        if(SumFalg == 8)
                            GoNext = 1;
                    }
                    // 다음 스텝으로 넘어감
                    if(GoNext != -1)
                    {
                        const String StepDom = (*ActiveProject)() + String::Format(".data.quest.%d.step.%d", QuestIdx, StepFocus);
                        const sint32 NextStep = ZayWidgetDOM::GetValue(StepDom + ".next_step").ToInteger();
                        ZayWidgetDOM::SetValue(CacheDom + ".next_step", String::FromInteger(NextStep));
                        if(GoNext == 1)
                        {
                            ZayWidgetDOM::SetValue(CacheDom + ".next_wait", "1 ~ 0 : 0.5"); // 0.5초 기다림
                            const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + 500;
                            (*KeepUpdatingUntil)(NewUpdateMsec);
                            ZayWidgetDOM::SetValue(CacheStepDom, "1"); // 성공
                        }
                        else
                        {
                            ZayWidgetDOM::SetValue(CacheDom + ".next_wait", "0");
                            ZayWidgetDOM::SetValue(CacheStepDom, "0"); // 실패
                            ZayWidgetDOM::SetValue(CacheDom + ".result.errored", "1"); // 에러있음
                        }
                    }
                }
                view->invalidate();
            }
        }
        else (*Log)((*ActiveProject)(), "vdscheck의 파라미터 수량에러");
    }, "[Ch:'채널명'][QuestIdx:'퀘스트Idx'][UserFlag:ok|skip|seek_n]", "VDS스크립트에 사용자체크");

    ////////////////////////////////////////////////////////////////////////////////
    // vdssavestore
    NewZayson.AddGlue("vdssavestore", ZAY_DECLARE_GLUE(params, ActiveProject, SaveStore, Log)
    {
        if(params.ParamCount() == 2)
        {
            auto Ch = params.Param(0).ToText();
            auto AssetPath = params.Param(1).ToText();
            (*SaveStore)(Ch, AssetPath);
        }
        else (*Log)((*ActiveProject)(), "vdssavestore의 파라미터 수량에러");
    }, "[Ch:'채널명'][AssetPath:'log/a.txt']", "VDS채널의 읽기버퍼 파일저장");

    ////////////////////////////////////////////////////////////////////////////////
    // vdssetmonitor
    NewZayson.AddGlue("vdssetmonitorfocus", ZAY_DECLARE_GLUE(params, ActiveProject, SetMonitorFocus, Log)
    {
        if(params.ParamCount() == 2)
        {
            auto Ch = params.Param(0).ToText();
            auto MonitorFocus = params.Param(1).ToText();
            (*SetMonitorFocus)(MonitorFocus);
        }
        else (*Log)((*ActiveProject)(), "vdssetmonitorfocus의 파라미터 수량에러");
    }, "[Ch:'채널명'][MonitorFocus:'d.hxz.data.x00']", "VDS채널의 패킷모니터기능");
    return true;
}
