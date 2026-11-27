#include <boss.hpp>
#include "zayapp.hpp"
#include "glue/zayglue.hpp"

#include <resource.hpp>
#include <format/boss_bmp.hpp>
#include <string>

ZAY_DECLARE_VIEW_CLASS("ZayAppView", ZayAppData)

String gLastLocale;
sint32 gMinWindowWidth {600};
sint32 gMinWindowHeight {600};

ZAY_VIEW_API OnCommand(CommandType type, id_share in, id_cloned_share* out)
{
    const uint64 CurMsec = Platform::Utility::CurrentTimeMsec();
    if(type == CT_Tick)
    {
        const uint64 TickBeginMsec = Platform::Utility::CurrentTimeMsec();
        {
            // 상태 업데이트
            static sint32 gOSScale = 0; const sint32 OSScale = Platform::Utility::GetOSScale();
            if(gOSScale != OSScale) ZayWidgetDOM::SetValue("program.osscale", String::FromInteger(gOSScale = OSScale));
            static sint32 gPhysicalDpi = 0; const sint32 PhysicalDpi = Platform::Utility::GetPhysicalDpi();
            if(gPhysicalDpi != PhysicalDpi) ZayWidgetDOM::SetValue("program.screendpi", String::FromInteger(gPhysicalDpi = PhysicalDpi));
            static bool gCapturing = false; const bool Capturing = m->isCapturing();
            if(gCapturing != Capturing) ZayWidgetDOM::SetValue("program.capture", (gCapturing = Capturing)? "1" : "0");

            // 제이에디터에 틱전달
            if(m->mMainWidget.TickOnce())
                m->invalidate();
            m->mSubWidgets.AccessByCallback(
                [](const MapPath* path, ZayWidget* widget, payload data)->void
                {
                    if(widget->TickOnce())
                        m->invalidate();
                }, nullptr);
            // 갱신 미션: 만료 시에도 마지막 갱신을 한 번 요청하고 종료
            if(0 < m->mUpdateMsec)
            {
                if(m->mUpdateMsec < CurMsec)
                    m->mUpdateMsec = 0;
                m->invalidate(2);
            }

            // 바통진행
            const uint64 BatonBeginMsec = Platform::Utility::CurrentTimeMsec();
            if(m->mChannel.BatonProcess(200))
                m->invalidate();
            const uint64 BatonEndMsec = Platform::Utility::CurrentTimeMsec();
            ZayWidgetDOM::SetValue("program.time.baton", String::FromInteger(sint64(BatonEndMsec - BatonBeginMsec)));

            // 퀘스트 진행
            for(sint32 i = 0, iend = m->mProjects.Count(); i < iend; ++i)
            {
                chararray ProjCode;
                if(auto CurProject = m->mProjects.AccessByOrder(i, &ProjCode))
                {
                    // 퀘스트
                    for(sint32 j = 0, jend = CurProject->mChQuests.Count(); j < jend; ++j)
                    {
                        chararray Ch;
                        if(auto CurQuests = CurProject->mChQuests.AccessByOrder(j, &Ch))
                        for(sint32 k = CurQuests->Count() - 1; 0 <= k; --k)
                        {
                            chararray QuestCode;
                            const sint32 CurQuestIdx = *CurQuests->AccessByOrder(k, &QuestCode);
                            const String CacheDom = &ProjCode[0] + String::Format(".%s.cache.quest.%d", &Ch[0], CurQuestIdx);
                            // 다음 스텝으로 넘어가는 중
                            if(ZayWidgetDOM::ExistVariable(CacheDom + ".next_step"))
                            {
                                if(ZayWidgetDOM::GetValue(CacheDom + ".next_wait").ToFloat() == 0) // 기다리는 시간이 지나면
                                {
                                    const sint32 NextStep = ZayWidgetDOM::GetValue(CacheDom + ".next_step").ToInteger();
                                    ZayWidgetDOM::RemoveVariables(CacheDom + ".next_");
                                    ZayWidgetDOM::SetValue(CacheDom + ".focus", String::FromInteger(NextStep));
                                    if(NextStep != -1)
                                    {
                                        // 스텝별 스크립트가 있으면 쌓아둠
                                        m->RunEntry(&ProjCode[0], &Ch[0], CurQuestIdx, String::Format("step.%d.script", NextStep));
                                    }
                                    else // 전부 클리어시
                                    {
                                        ZayWidgetDOM::SetValue(CacheDom + ".enabled", "0");
                                        ZayWidgetDOM::SetValue(CacheDom + ".enable_ani", "0");
                                        // 결과
                                        if(ZayWidgetDOM::GetValue(CacheDom + ".result.errored").ToInteger() != 0)
                                            ZayWidgetDOM::SetValue(CacheDom + ".result.total", "'FAILED'");
                                        else ZayWidgetDOM::SetValue(CacheDom + ".result.total", "'SUCCEED'");
                                        CurProject->mChQuests(&Ch[0]).Remove(&QuestCode[0]);
                                        // 퀘스트퇴장 스크립트가 있으면 쌓아둠
                                        m->RunEntry(&ProjCode[0], &Ch[0], CurQuestIdx, "script_stop");
                                    }
                                    // 다음으로 넘어가는 이벤트 발생
                                    if(auto CurWidgetName = m->mLastWidgetName.Access(&ProjCode[0]))
                                    if(auto CurSubWidget = m->mSubWidgets.Access(*CurWidgetName))
                                    {
                                        const String CacheEventDom = String::Format("%s.event", &ProjCode[0]);
                                        ZayWidgetDOM::SetValue(CacheEventDom + ".ch", String::Format("'%s'", &Ch[0]));
                                        CurSubWidget->JumpCall("event_next", &ProjCode[0]);
                                    }
                                    m->invalidate();
                                }
                            }
                            // 현재 스텝을 평가하는 중
                            else
                            {
                                const sint32 StepFocus = ZayWidgetDOM::GetValue(CacheDom + ".focus").ToInteger();
                                const String StepDom = &ProjCode[0] + String::Format(".data.quest.%d.step.%d", CurQuestIdx, StepFocus);
                                const String NextPass = ZayWidgetDOM::GetValue(StepDom + ".next_pass").ToText().Replace("#vds#", String::Format("vds.%s.", &Ch[0]));
                                const sint32 NextStep = ZayWidgetDOM::GetValue(StepDom + ".next_step").ToInteger();
                                // PASS조건에 맞으면 다음 스텝을 결정
                                ZayWidgetDOM::SetValue(CacheDom + ".pass", NextPass);
                                if(ZayWidgetDOM::GetValue(CacheDom + ".pass").ToInteger() != 0)
                                {
                                    ZayWidgetDOM::SetValue(CacheDom + ".result.step" + String::FromInteger(StepFocus), "1"); // 성공
                                    ZayWidgetDOM::SetValue(CacheDom + ".next_step", String::FromInteger(NextStep));
                                    ZayWidgetDOM::SetValue(CacheDom + ".next_wait", "1 ~ 0 : 0.5"); // 0.5초 기다림
                                    const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + 500;
                                    if(m->mUpdateMsec < NewUpdateMsec)
                                        m->mUpdateMsec = NewUpdateMsec;
                                    m->invalidate();
                                }
                            }
                        }
                    }
                }
            }

            // 패키지리스트 업데이트
            if(m->mPackageList)
            {
                if(auto NewAnswer = Tasking::GetAnswer(m->mPackageList))
                {
                    const String AnswerText = String::FromBuffer(NewAnswer);
                    if(AnswerText == "PackageSearchBegin")
                        ZayWidgetDOM::SetValue("program.tasking.package", "1");
                    else if(AnswerText == "PackageSearchEnd")
                        ZayWidgetDOM::SetValue("program.tasking.package", "0");
                    else
                    {
                        const Context WpkJson(ST_Json, SO_OnlyReference, AnswerText);
                        ZayWidgetDOM::RemoveVariables("package.");
                        ZayWidgetDOM::SetJson(WpkJson, "package.");
                    }
                    m->invalidate();
                }
                else if(!Tasking::IsAlive(m->mPackageList))
                {
                    ZayWidgetDOM::SetValue("program.tasking.package", "0");
                    Tasking::Release(m->mPackageList, true);
                    m->mPackageList = nullptr;
                }
            }

            // IP주소 알기
            if(m->mIPAddress)
            {
                if(!Tasking::IsAlive(m->mIPAddress))
                {
                    Tasking::Release(m->mIPAddress, true);
                    m->mIPAddress = nullptr;
                    m->invalidate();
                }
            }

            // WPK인스톨 업데이트
            if(m->mInstallWpk)
            {
                if(auto NewAnswer = Tasking::GetAnswer(m->mInstallWpk))
                {
                    const String AnswerText = String::FromBuffer(NewAnswer);
                    if(!String::Compare(AnswerText, strpair("BootLoaderStart:")))
                    {
                        auto Args = String::Split(AnswerText, ':');
                        if(Args.Count() == 7)
                        {
                            ZayWidgetDOM::SetValue("program.tasking.install", "1");
                            ZayWidgetDOM::SetValue("program.tasking.install.step", '\'' + Args[1] + '\'');
                            ZayWidgetDOM::SetValue("program.tasking.install.path", '\'' + Args[2] + '\'');
                            ZayWidgetDOM::SetValue("program.tasking.install.size", Args[3]);
                            ZayWidgetDOM::SetValue("program.tasking.install.crc", Args[4]);
                            ZayWidgetDOM::SetValue("program.tasking.install.cmd", Args[5]);
                            ZayWidgetDOM::SetValue("program.tasking.install.ack", Args[6]);
                            if(auto CurWidgetName = m->mLastWidgetName.Access(m->mLastProjCode))
                            if(auto CurSubWidget = m->mSubWidgets.Access(*CurWidgetName))
                                CurSubWidget->JumpCall("bootloader_install", m->mLastProjCode);
                        }
                    }
                }
                else if(!Tasking::IsAlive(m->mInstallWpk))
                {
                    ZayWidgetDOM::SetValue("program.tasking.install", "0");
                    Tasking::Release(m->mInstallWpk, true);
                    m->mInstallWpk = nullptr;
                }
            }
        }
        const uint64 TickEndMsec = Platform::Utility::CurrentTimeMsec();
        ZayWidgetDOM::SetValue("program.time.tick", String::FromInteger(sint64(TickEndMsec - TickBeginMsec)));
    }
    else if(type == CT_Create)
    {
        // 메인위젯 구성: 뷰 콜백이 연결된 이후 OnCreate를 실행한다.
        ZayWidgetDOM::SetValue("program.standalone", "0");
        m->CollectModifiedDoms("main", "");
        m->UpdateModifiedDoms();
        m->InitWidget(&m->mMainWidget, "main");
        m->mMainWidget.Reload("main/widget/main.zay");
        m->mMainWidget.UpdateAtlas(R::PrintUpdatedAtlas(true));
        Platform::File::EnumStorage();
        m->UpdateChannelList();
        m->UpdateSoundDeviceList();
        m->UpdatePackageList();
    }
    else if(type == CT_DeviceArrival) // USB장치를 꼽거나 뺄때
    {
        m->UpdateChannelList();
        m->invalidate();
    }
    else if(type == CT_StorageMounted) // USB저장장치를 꼽을때
    {
        const Strings Path_Free_Total = String::Split(String(in), '|');
        auto& NewStorage = m->mStorages(Path_Free_Total[0]);
        NewStorage.mFree = Parser::GetInt<sint64>(Path_Free_Total[1]);
        NewStorage.mTotal = Parser::GetInt<sint64>(Path_Free_Total[2]);
        m->UpdateStorageList();
        m->UpdatePackageList();
        m->invalidate();
    }
    else if(type == CT_StorageUnmounted) // USB저장장치를 뺄때
    {
        const String Path(in);
        m->mStorages.Remove(Path);
        m->UpdateStorageList();
        m->UpdatePackageList();
        m->invalidate();
    }
    else if(type == CT_NetworkChanged) // 네트워크가 바뀔때
    {
        const Strings Lines = String::Split(String(in), '\n');
        for(sint32 i = 0, iend = Lines.Count(); i < iend; ++i)
        {
            const Strings State = String::Split(Lines[i], '|');
            if(4 <= State.Count())
            {
                auto& CurWifi = m->mWifis(State[1]);
                CurWifi.mConnected = (Parser::GetInt(State[0]) != 0);
                CurWifi.mSignal = Parser::GetInt(State[2]);
                CurWifi.mSecurity = State[3];
                CurWifi.mIp = (4 < State.Count())? State[4] : String();
                m->UpdateNetworkList();
                m->invalidate();
            }
        }
    }
    else if(type == CT_Activate && !boolo(in).ConstValue())
        m->clearCapture();
}

ZAY_VIEW_API OnNotify(NotifyType type, chars topic, id_share in, id_cloned_share* out)
{
    if(type == NT_Normal)
    {
        if(!String::Compare(topic, "UrlImageLoaded"))
        {
            const String Url(in);
            if(auto CurUrlImage = m->mUrlImages.Access(Url))
            {
                chars ExtNames[3] = {".png", ".jpg", ".bmp"};
                for(sint32 i = 0; i < 3; ++i)
                {
                    if(Asset::Exist(CurUrlImage->mFileTitle + ExtNames[i]))
                    {
                        CurUrlImage->mLoadedMsec = Platform::Utility::CurrentTimeMsec();
                        CurUrlImage->mImage.SetName(CurUrlImage->mFileTitle + ExtNames[i]).Load();
                        break;
                    }
                }
                if(0 < CurUrlImage->mLoadedMsec)
                {
                    const uint64 NewUpdateMsec = CurUrlImage->mLoadedMsec + CurUrlImage->mFadeMsec;
                    if(m->mUpdateMsec < NewUpdateMsec)
                        m->mUpdateMsec = NewUpdateMsec;
                    m->invalidate(2);
                }
            }
        }
    }
    else if(type == NT_ZayWidget)
    {
        if(!String::Compare(topic, "SetCursor"))
        {
            auto Cursor = (CursorRole) sint32o(in).Value();
            m->SetCursor(Cursor);
        }
        else if(!String::Compare(topic, "LangPressing"))
        {
            auto Lang = String(in);
            ZayWidgetDOM::SetValue("program.lang", Lang);
        }
        else if(!String::Compare(topic, "EnterPressing"))
        {
            auto DomName = String(in);
            if(DomName == "main_browser.url" || DomName == "data.glue_filter")
            {
                m->mMainWidget.JumpCall("editenter_" + DomName, String::Format("EditEnter:%s", (chars) DomName));
                m->invalidate();
            }
            else if(auto CurWidgetName = m->mLastWidgetName.Access(m->mLastProjCode))
            if(auto CurSubWidget = m->mSubWidgets.Access(*CurWidgetName))
                CurSubWidget->JumpCall("editenter_" + DomName, String::Format("EditEnter:%s", (chars) DomName));
        }
    }
    else if(type == NT_WindowWeb)
    {
        if(!String::Compare(topic, "PythonLog"))
        {
            const String Text(in);
            m->mMainWidget.SendLog(Text);
        }
        else if(!String::Compare(topic, "PythonSet"))
        {
            const Strings Args(in);
            const String Key = Args[0];
            const String Value = Args[1];
            if(Key.Left(2) == "d.")
            {
                ZayWidgetDOM::SetValue(Key.Offset(2), Value);
                m->invalidate();
            }
        }
        else if(!String::Compare(topic, "PythonSetJson"))
        {
            const Strings Args(in);
            const String KeyHead = Args[0];
            const String JsonText = Args[1];
            if(KeyHead.Left(2) == "d.")
            {
                const Context Json(ST_Json, SO_OnlyReference, JsonText);
                ZayWidgetDOM::SetJson(Json, KeyHead.Offset(2));
                m->invalidate();
            }
        }
        else if(!String::Compare(topic, "PythonGet"))
        {
            const String Key(in);
            if(Key.Left(2) == "d.")
            {
                const String Value = ZayWidgetDOM::GetValue(Key.Offset(2)).ToText(true);
                if(out) *out = Value;
            }
        }
        else if(!String::Compare(topic, "PythonRem"))
        {
            const String KeyHead(in);
            if(KeyHead.Left(2) == "d.")
            {
                ZayWidgetDOM::RemoveVariables(KeyHead.Offset(2));
                m->invalidate();
            }
        }
        else if(!String::Compare(topic, "PythonGlueCall"))
        {
            const Strings Args(in);
            const String Name = Args[0];
            Strings Params;
            for(sint32 i = 1, iend = Args.Count(); i < iend; ++i)
                Params.AtAdding() = Args[i];
            if(m->mMainWidget.GlueCall(Name, Params))
                m->invalidate();
        }
        else if(!String::Compare(topic, "PythonJumpCall"))
        {
            const String Name(in);
            m->mMainWidget.JumpCall(Name, topic);
            m->invalidate();
        }
    }
    else if(type == NT_SocketReceive)
    {
        bool PeerUpdated = false;
        if(m->mChannel.RecvServerOnce(&PeerUpdated))
            m->invalidate();
        if(PeerUpdated)
        {
            m->UpdateChannelList();
            m->invalidate();
        }
    }
    else if(type == NT_Serial)
    {
        if(!String::Compare(topic, "message"))
        {
            const String Port = String(in);
            auto Ch = m->mChannel.SerialPortToCh(Port);
            if(m->mChannel.Read(Ch))
                m->invalidate();
        }
        else if(!String::Compare(topic, "error"))
        {
            const String PortAndErrorText = String(in);
            const sint32 ColonPos = PortAndErrorText.Find(0, ":");
            if(ColonPos != -1)
            {
                m->AddLog(PortAndErrorText.Left(ColonPos), "(error) " + PortAndErrorText.Offset(ColonPos + 1), "#800000");
                m->invalidate();
            }
        }
    }
    else if(type == NT_KeyPress || type == NT_KeyRelease)
    {
        const sint32 KeyCode = sint32o(in).ConstValue();
        if(auto CurProject = m->mProjects.Access(m->mLastProjCode))
        for(sint32 i = 0, iend = CurProject->mHotKeys.Count(); i < iend; ++i)
        {
            chararray Key;
            if(auto CurKey = CurProject->mHotKeys.AccessByOrder(i, &Key))
            {
                const sint32 CurKeyCode = MapPath::ToInt(Key);
                if(CurKeyCode == KeyCode)
                if(auto CurWidgetName = m->mLastWidgetName.Access(m->mLastProjCode))
                if(auto CurSubWidget = m->mSubWidgets.Access(*CurWidgetName))
                {
                    const String GateName = (type == NT_KeyPress)? CurKey->mPressGate : CurKey->mReleaseGate;
                    if(GateName.Length())
                    {
                        CurSubWidget->JumpCall(GateName, (type == NT_KeyPress)?
                            String::Format("HotKeyPress:%d", KeyCode) : String::Format("HotKeyRelease:%d", KeyCode));
                        m->invalidate();
                    }
                    break;
                }
            }
        }
    }
}

ZAY_VIEW_API OnGesture(GestureType type, sint32 x, sint32 y)
{
    static point64 OldCursorPos;
    static rect128 OldWindowRect;
    static uint64 ReleaseMsec = 0;

    if(type == GT_Moving)
        m->SetCursor(CR_Arrow);
    else if(type == GT_Pressed || type == GT_ExtendPressed || type == GT_WheelPressed)
    {
        Platform::Utility::GetCursorPos(OldCursorPos);
        OldWindowRect = Platform::GetWindowRect();
        m->clearCapture();
    }
    else if(type == GT_InDragging || type == GT_OutDragging || type == GT_ExtendDragging || type == GT_WheelDragging)
    {
        #if !BOSS_NEED_FULLSCREEN
            point64 CurCursorPos;
            Platform::Utility::GetCursorPos(CurCursorPos);
            if(!m->IsFullScreen())
            {
                rect128 CurWindowRect;
                CurWindowRect.l = OldWindowRect.l + CurCursorPos.x - OldCursorPos.x;
                CurWindowRect.t = OldWindowRect.t + CurCursorPos.y - OldCursorPos.y;
                CurWindowRect.r = OldWindowRect.r + CurCursorPos.x - OldCursorPos.x;
                CurWindowRect.b = OldWindowRect.b + CurCursorPos.y - OldCursorPos.y;
                Platform::SetWindowRect(CurWindowRect.l, CurWindowRect.t,
                    CurWindowRect.r - CurWindowRect.l, CurWindowRect.b - CurWindowRect.t);
            }
        #endif
        ReleaseMsec = 0;
        m->invalidate();
    }
    else if(type == GT_InReleased || type == GT_OutReleased || type == GT_CancelReleased || type == GT_ExtendRelease || type == GT_WheelRelease)
    {
        const uint64 CurReleaseMsec = Platform::Utility::CurrentTimeMsec();
        const bool HasDoubleClick = (CurReleaseMsec < ReleaseMsec + 300);
        if(HasDoubleClick)
        {
            if(m->IsFullScreen())
                m->SetNormalWindow();
            else m->SetFullScreen();
        }
        else ReleaseMsec = CurReleaseMsec;
    }
    else if(type == GT_LongPressed && !ZayWidgetDOM::GetValue("program.standalone").ToInteger())
    {
        m->mMainWidget.JumpCall("bg_longpressed", "bg");
        m->invalidate();
    }
}

ZAY_VIEW_API OnRender(ZayPanel& panel)
{
    m->mNativeState->painting = true;
    m->mMainWidget.Render(panel);
    m->mNativeState->painting = false;
    m->mNativeState->paintKey = {};
    #if !BOSS_NEED_FULLSCREEN
        if(!m->IsFullScreen())
            m->RenderWindowOutline(panel);
    #endif
}

ZayAppData::ZayAppData()
{
    // BUILD
    String DateText = __DATE__;
    String TimeText = __TIME__;
    DateText.Replace("Jan", "01"); DateText.Replace("Feb", "02"); DateText.Replace("Mar", "03");
    DateText.Replace("Apr", "04"); DateText.Replace("May", "05"); DateText.Replace("Jun", "06");
    DateText.Replace("Jul", "07"); DateText.Replace("Aug", "08"); DateText.Replace("Sep", "09");
    DateText.Replace("Oct", "10"); DateText.Replace("Nov", "11"); DateText.Replace("Dec", "12");
    const String Date = String::Format("%02d", Parser::GetInt(DateText.Middle(2, DateText.Length() - 6).Trim()));
    DateText = DateText.Right(4) + "/" + DateText.Left(2) + "/" + Date;
    ZayWidgetDOM::SetValue("program.buildtime", '\'' + DateText + '_' + TimeText.Left(2) + "H'");
    // OS
    ZayWidgetDOM::SetValue("program.osname", String::Format("'%s'", Platform::Utility::GetOSName()));
    ZayWidgetDOM::SetValue("program.locale", '\'' + gLastLocale + '\'');
    ZayWidgetDOM::SetValue("program.osscale", String::FromInteger(Platform::Utility::GetOSScale()));
    ZayWidgetDOM::SetValue("program.screendpi", String::FromInteger(Platform::Utility::GetPhysicalDpi()));
    ZayWidgetDOM::SetValue("program.capture", "0");
    ZayWidgetDOM::SetValue("program.lang", "English");
    #if BOSS_NEED_FULLSCREEN
        mIsFullScreen = true;
        ZayWidgetDOM::SetValue("program.fullscreen", "1");
    #else
        mIsFullScreen = false;
        ZayWidgetDOM::SetValue("program.fullscreen", "0");
    #endif
    // RENDER
    Platform::Graphics::SetRenderHint(true, true);
    ZayWidgetDOM::SetValue("program.smooth", "1");
    ZayWidgetDOM::SetValue("program.antialiasing", "1");
    // TASKING
    ZayWidgetDOM::SetValue("program.tasking.package", "0");
    // NETWORK
    ZayWidgetDOM::SetValue("program.ip.local", "'0.0.0.0'");
    ZayWidgetDOM::SetValue("program.ip.public", "'0.0.0.0'");
    // RTC
    sint32 Second = 0, Minute = 0, Hour = 0, Day = 1, Month = 1, Year = 0, Weekday = 0;
    chars WeekdayText[8] = {"UNK", "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
    const String Battery = Platform::Utility::GetTimeRTC("/dev/rtc0", &Second, &Minute, &Hour, &Day, &Month, &Year, &Weekday);
    ZayWidgetDOM::SetValue("program.rtctime", String::Format("'%04d-%02d-%02d(%s) %02d:%02d:%02d'",
        Year, Month, Day, WeekdayText[Weekday], Hour, Minute, Second));
    ZayWidgetDOM::SetValue("program.rtcinfo", '\'' + Battery + '\'');

    // 스크립트 필터
    ZayWidgetDOM::SetVariableFilter("edittext", [this](const String& formula, const SolverValue& value, float reliable)->bool
    {
        mEditText.Parse(((chars) formula) + 1); // ?
        return true;
    });

    // 채널 셋팅
    mChannel.SetGetter([this](chars ch, chars key)->const SolverValue
        {
            return ZayWidgetDOM::GetValue(String::Format("vds.%s.%s", ch, key));
        });
    mChannel.SetSetter([this](chars ch, chars key, chars value)->void
        {
            ZayWidgetDOM::SetValue(String::Format("vds.%s.%s", ch, key), value);
        });
    mChannel.SetActor([this](chars ch, chars action)->void
        {
            const String Action(action);
            sint32 ColonPos = Action.Find(0, ":");
            if(ColonPos != -1)
            {
                static String LocalCh = String::Format("'%s'", ch);
                const String ProjCodeAndGate[2] = {Action.Left(ColonPos).Lower(), Action.Offset(ColonPos + 1)};
                if(auto CurWidgetName = mLastWidgetName.Access(ProjCodeAndGate[0]))
                if(auto CurSubWidget = mSubWidgets.Access(*CurWidgetName))
                {
                    Solvers LocalSolvers;
                    LocalSolvers.AtAdding().Link(CurSubWidget->viewname(), "LocalCh").Parse(LocalCh).Execute();
                    CurSubWidget->JumpCallDirectly(ProjCodeAndGate[1], ProjCodeAndGate[0]);
                }
            }
        });
    mChannel.SetLogger([this](chars ch, chars type, bytes data, sint32 length)->void
        {
            if(!String::Compare(type, "write:", 6))
                AddHexLog(ch, type + 6, data, length, "#800080");
            else if(!String::Compare(type, "read:", 5))
            {
                if(ZayWidgetDOM::GetValue("log.read_hidden").ToInteger() == 0)
                    AddHexLog(ch, type + 5, data, length, "#008000");
            }
            else if(!String::Compare(type, "normal:", 7))
            {
                if(ZayWidgetDOM::GetValue("log.normal_hidden").ToInteger() == 0)
                    AddHexLog(ch, type + 7, data, length, "#000080");
            }
            else if(!String::Compare(type, "error:", 6))
                AddHexLog(ch, type + 6, data, length, "#800000");
            else if(!String::Compare(type, "info:", 5))
                AddLog(ch, type + 5, "#804000");
        });
    const bool Binded = mChannel.BindServer(10787);
    ZayWidgetDOM::SetValue("vds.server.port", "10787");
    ZayWidgetDOM::SetValue("vds.server.binded", (Binded)? "1" : "0");

    // 업데이트 체크
    Platform::AddProcedure(PE_100MSEC,
        [](payload data)->void
        {
            auto Self = (ZayAppData*) data;
            Self->CollectModifiedDoms("main", "");
            Self->mSubWidgets.AccessByCallback(
                [](const MapPath* path, ZayWidget* widget, payload data)->void
                {
                    auto Self = (ZayAppData*) data;
                    Self->CollectModifiedDoms(String(path->GetPath()), widget->viewname());
                }, data);
            if(Self->UpdateModifiedDoms())
                Self->invalidate();
            if(Self->UpdateModifiedVds())
                Self->invalidate();
        }, this);
}

ZayAppData::~ZayAppData()
{
    mGlueHost.Close();
    mNativeState.reset();
    if(mPackageList)
    {
        Tasking::Release(mPackageList, false);
        mPackageList = nullptr;
    }
    if(mInstallWpk)
    {
        Tasking::Release(mInstallWpk, false);
        mInstallWpk = nullptr;
    }
    if(mIPAddress)
    {
        Tasking::Release(mIPAddress, false);
        mIPAddress = nullptr;
    }
    if(mOscilloScope)
    {
        mOscilloScope->Close();
        delete mOscilloScope;
        mOscilloScope = nullptr;
    }
    for(sint32 i = 0, iend = mUrlImages.Count(); i < iend; ++i)
        if(auto CurUrlImage = mUrlImages.AccessByOrder(i))
            Tasking::Release(CurUrlImage->mTasking, true);
}

void ZayAppData::ForcedRemove(chars itemname, payload data)
{
    if(Platform::File::ExistForDir(itemname))
    {
        Platform::File::Search(itemname, ForcedRemove, nullptr, true);
        Platform::File::RemoveDir(WString::FromChars(itemname), true);
    }
    else Platform::File::Remove(WString::FromChars(itemname));
}

uint32 ZayAppData::UpdateCrc(bytes data, sint32 datalen, uint32 oldcrc)
{
    static uint32 CrcTable[256] = {0};
    if(CrcTable[255] == 0)
    for(uint32 i = 0; i < 256; ++i)
    {
        uint32 crc = i;
        for(sint32 j = 0; j < 8; ++j)
        {
            if(crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
        CrcTable[i] = crc;
    }
    uint32 crc = oldcrc;
    for(sint32 i = 0; i < datalen; ++i)
        crc = CrcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

bool ZayAppData::IsFullScreen()
{
    return mIsFullScreen;
}

void ZayAppData::SetFullScreen()
{
    if(!mIsFullScreen)
    {
        mIsFullScreen = true;
        mSavedNormalRect = Platform::GetWindowRect();
        ZayWidgetDOM::SetValue("program.fullscreen", "1");

        #if !BOSS_NEED_FULLSCREEN
            point64 CursorPos;
            Platform::Utility::GetCursorPos(CursorPos);
            sint32 ScreenID = Platform::Utility::GetScreenID(CursorPos);
            rect128 FullScreenRect;
            Platform::Utility::GetScreenRect(FullScreenRect, ScreenID, true);
            Platform::SetWindowRect(FullScreenRect.l, FullScreenRect.t,
                FullScreenRect.r - FullScreenRect.l, FullScreenRect.b - FullScreenRect.t + 1);
            Platform::SetWindowRaise();
        #endif
    }
}

void ZayAppData::SetNormalWindow()
{
    if(mIsFullScreen)
    {
        mIsFullScreen = false;
        ZayWidgetDOM::SetValue("program.fullscreen", "0");

        #if !BOSS_NEED_FULLSCREEN
            Platform::SetWindowRect(mSavedNormalRect.l, mSavedNormalRect.t,
                mSavedNormalRect.r - mSavedNormalRect.l, mSavedNormalRect.b - mSavedNormalRect.t);
            Platform::SetWindowRaise();
        #endif
    }
}

void ZayAppData::SetCursor(CursorRole role)
{
    if(mNowCursor != role)
    {
        mNowCursor = role;
        Platform::Utility::SetCursor(role);
        if(mNowCursor != CR_SizeVer && mNowCursor != CR_SizeHor && mNowCursor != CR_SizeBDiag && mNowCursor != CR_SizeFDiag && mNowCursor != CR_SizeAll)
        {
            mNcLeftBorder = false;
            mNcTopBorder = false;
            mNcRightBorder = false;
            mNcBottomBorder = false;
        }
    }
}

sint32 ZayAppData::MoveNcLeft(const rect128& rect, sint32 addx)
{
    const sint32 NewLeft = rect.l + addx;
    return rect.r - Math::Max(gMinWindowWidth, rect.r - NewLeft);
}

sint32 ZayAppData::MoveNcTop(const rect128& rect, sint32 addy)
{
    const sint32 NewTop = rect.t + addy;
    return rect.b - Math::Max(gMinWindowHeight, rect.b - NewTop);
}

sint32 ZayAppData::MoveNcRight(const rect128& rect, sint32 addx)
{
    const sint32 NewRight = rect.r + addx;
    return rect.l + Math::Max(gMinWindowWidth, NewRight - rect.l);
}

sint32 ZayAppData::MoveNcBottom(const rect128& rect, sint32 addy)
{
    const sint32 NewBottom = rect.b + addy;
    return rect.t + Math::Max(gMinWindowHeight, NewBottom - rect.t);
}

void ZayAppData::CollectModifiedDoms(chars widgetname, chars projcode, bool forced)
{
    // 위젯의 기본DOM
    uint64 DataModifyMsec = 0;
    const String WidgetDir = (!String::Compare(widgetname, "main"))? "main/widget" : "widget";
    const String WidgetFile = String::Format("%s.json", widgetname);
    const String AssetName = WidgetDir + '/' + WidgetFile;
    if(Asset::Exist(AssetName, nullptr, nullptr, nullptr, nullptr, &DataModifyMsec))
    if(forced || !mDomJsons.Access(AssetName) || mDomJsons(AssetName).mModifiedMsec < DataModifyMsec)
    {
        hook(mDomJsons(AssetName))
        {
            fish.mAssetName = AssetName;
            if(!String::Compare(widgetname, "main"))
                fish.mHeaderName = "data.";
            else fish.mHeaderName = String::Format("%s.data.", projcode);
            fish.mModifiedMsec = DataModifyMsec;
            fish.mUpdated = true;
        }
    }

    // 위젯의 확장DOM들
    static String CurWidgetName;
    static String CurWidgetDir;
    static String CurProjCode;
    static bool CurForced;
    auto DomFinder = [](chars filepath, chars itemname, payload data)->void
    {
        auto Self = (ZayAppData*) data;
        const String FileName = itemname;
        const sint32 HeadLength = CurWidgetName.Length() + 1;
        if(!FileName.Left(HeadLength).CompareNoCase(CurWidgetName + "_"))
        if(!FileName.Right(5).CompareNoCase(".json"))
        {
            const String AssetName = CurWidgetDir + '/' + FileName;
            uint64 DataModifyMsec = 0;
            if(Asset::Exist(AssetName, nullptr, nullptr, nullptr, nullptr, &DataModifyMsec))
            if(CurForced || !Self->mDomJsons.Access(AssetName) || Self->mDomJsons(AssetName).mModifiedMsec < DataModifyMsec)
            {
                hook(Self->mDomJsons(AssetName))
                {
                    const String DomPath = FileName.Middle(HeadLength, FileName.Length() - (HeadLength + 5));
                    fish.mAssetName = AssetName;
                    if(!CurWidgetName.CompareNoCase("main"))
                        fish.mHeaderName = DomPath + ".";
                    else fish.mHeaderName = CurProjCode + "." + DomPath + ".";
                    fish.mModifiedMsec = DataModifyMsec;
                    fish.mUpdated = true;
                }
            }
        }
    };
    CurWidgetName = widgetname;
    CurWidgetDir = WidgetDir;
    CurProjCode = projcode;
    CurForced = forced;
    id_assetpath SearchPath = AssetPath::Create();
    AssetPath::AddPath(SearchPath, WidgetDir);
    AssetPath::Search(SearchPath, DomFinder, this);
    AssetPath::Release(SearchPath);
}

bool ZayAppData::UpdateModifiedDoms()
{
    bool NeedRedraw = false;
    for(sint32 i = 0, iend = mDomJsons.Count(); i < iend; ++i)
    {
        if(auto CurDomJson = mDomJsons.AccessByOrder(i))
        {
            if(!CurDomJson->mUpdated) continue;
            const String DataJson = String::FromAsset(CurDomJson->mAssetName);
            const bool IsMain = (CurDomJson->mAssetName == "main/widget/main.json");
            Context Data(ST_Json, SO_OnlyReference, DataJson, DataJson.Length());
            ZayWidgetDOM::RemoveVariables(CurDomJson->mHeaderName);
            ZayWidgetDOM::SetJson(Data, CurDomJson->mHeaderName);
            CurDomJson->mUpdated = false;
            NeedRedraw = true;

            if(CurDomJson->mHeaderName.Right(5) == "data.") // 기본DOM
            {
                if(mNativeState) ++mNativeState->sceneRevision;
                if(auto AtlasCount = ZayWidgetDOM::GetValue(CurDomJson->mHeaderName + "atlas.count").ToInteger())
                {
                    const String AtlasInfoJson = String::FromAsset("main/atlasinfo.json");
                    Context AtlasInfo(ST_Json, SO_OnlyReference, AtlasInfoJson, AtlasInfoJson.Length());
                    for(sint32 j = 0; j < AtlasCount; ++j)
                    {
                        const String CurHeader = String::Format(CurDomJson->mHeaderName + "atlas.%d.", j);
                        const String CurPath = ZayWidgetDOM::GetValue(CurHeader + "path").ToText();
                        R::AddAtlas("ui_atlaskey2.png", CurPath, AtlasInfo, 2, IsMain);
                    }
                    if(R::IsAtlasUpdated())
                    {
                        R::RebuildAll();
                        R::SaveAtlas(AtlasInfo);
                        AtlasInfo.SaveJson().ToAsset("main/atlasinfo.json", true);
                    }
                }
                LoadWidgetFonts(CurDomJson->mHeaderName);
            }
        }
    }
    return NeedRedraw;
}

void ZayAppData::LoadWidgetFonts(chars domheader)
{
    const String DomHeader = domheader;
    if(auto FontsCount = ZayWidgetDOM::GetValue(DomHeader + "fonts.count").ToInteger())
    {
        Strings Collector;
        for(sint32 j = 0; j < FontsCount; ++j)
        {
            const String CurHeader = String::Format(DomHeader + "fonts.%d.", j);
            const String CurPath = ZayWidgetDOM::GetValue(CurHeader + "path").ToText();
            if(0 < CurPath.Length())
            {
                buffer NewFontData = Asset::ToBuffer(CurPath);
                if(!NewFontData) continue;
                auto FontFamilies = Platform::Utility::CreateSystemFont((bytes) NewFontData, Buffer::CountOf(NewFontData));
                for(sint32 k = 0, kend = FontFamilies.Count(); k < kend; ++k)
                    Collector.AtAdding() = FontFamilies[k];
                Buffer::Free(NewFontData);
            }
        }
        for(sint32 j = 0, jend = Collector.Count(); j < jend; ++j)
        {
            auto FontStyles = Platform::Utility::EnumSystemFontStyles(Collector[j]);
            const String KeyName = Collector[j].Lower().Replace(" ", "_");
            if(FontStyles.Count() < 2)
                ZayWidgetDOM::SetValue(String::Format("program.fonts.%s", (chars) KeyName), '\'' + Collector[j] + '\'');
            else for(sint32 k = 0, kend = FontStyles.Count(); k < kend; ++k)
                ZayWidgetDOM::SetValue(String::Format("program.fonts.%s.%d", (chars) KeyName, k), '\'' + Collector[j] + '.' + FontStyles[k] + '\'');
        }
    }
}

bool ZayAppData::UpdateModifiedVds()
{
    bool NeedRedraw = false;
    for(sint32 i = 0, iend = mProjects.Count(); i < iend; ++i)
    {
        chararray ProjCode;
        if(auto CurProject = mProjects.AccessByOrder(i, &ProjCode))
        {
            uint64 LastVdsModifyMsec = 0;
            if(auto CurVdsModifyMsec = mLastVdsModifyMsec.Access(&ProjCode[0]))
                LastVdsModifyMsec = *CurVdsModifyMsec;
            uint64 NowVdsModifyMsec = 0;
            const String VdsAssetName = String::Format("script/%s_vds.json", &ProjCode[0]);
            if(Asset::Exist(VdsAssetName, nullptr, nullptr, nullptr, nullptr, &NowVdsModifyMsec))
            if(LastVdsModifyMsec < NowVdsModifyMsec)
            {
                mLastVdsModifyMsec(&ProjCode[0]) = NowVdsModifyMsec;
                mChannel.ClearBatonAll(&CurProject->mScript);
                CurProject->mScript.Clear();
                CurProject->mScript.LoadMore(String::FromAsset(VdsAssetName));
                CurProject->mChQuests.Reset();
                NeedRedraw = true;
            }
        }
    }
    return NeedRedraw;
}

static sint32 FindAppIndex(const String& id)
{
    if(!id.Length()) return -1;
    const sint32 count = ZayWidgetDOM::GetValue("deck.list.count").ToInteger();
    for(sint32 i = 0; i < count; ++i)
        if(id == ZayWidgetDOM::GetValue(String::Format("deck.list.%d.dom", i)).ToText()) return i;
    return -1;
}

static String AppShortcutTitle(sint32 index, const String& id)
{
    const String title = ZayWidgetDOM::GetValue(String::Format("deck.list.%d.title", index)).ToText();
    return title.Length()? title : id;
}

static String AppShortcutIcon(const String& id)
{
    return "image/icon/" + id + ".png";
}

static sint32 CreateAppShortcut(const String& id)
{
    const sint32 index = FindAppIndex(id);
    if(index < 0) return -1;
    #if !BOSS_WASM
        if(*Asset::GetWebFilesUrl()) return -1; // Existing shortcut stores only AppId, not site URL.
    #endif
    const String title = AppShortcutTitle(index, id);
    #if BOSS_WASM
        const String language = ZayWidgetDOM::GetValue("settings.language").ToText();
        const String prefix = "data.i18n." + language + ".settings.";
        return Platform::Utility::ShowWebShortcutDialog("app", id, title,
            ZayWidgetDOM::GetValue(prefix + "shortcut_web_help").ToText(),
            ZayWidgetDOM::GetValue(prefix + "shortcut_open").ToText(),
            ZayWidgetDOM::GetValue(prefix + "close").ToText())? 2 : -1;
    #else
        return Platform::Utility::CreateDesktopShortcut(title, "app", id, AppShortcutIcon(id))? 1 : -1;
    #endif
}

void ZayAppData::InitAppDeck()
{
    // 이전 버전의 로컬/원격 선택값은 더 이상 사용하거나 저장하지 않는다.
    ZayWidgetDOM::RemoveVariables("settings.app_url_remote");
    #if BOSS_WASM
        ZayWidgetDOM::RemoveVariables("deck.");
        ZayWidgetDOM::SetValue("deck.list.count","0");
        ZayWidgetDOM::SetValue("deck.focus","-1");
        const String DeckText=String::FromAsset("zayapp_deck.json");
        const Context Deck(ST_Json, SO_NeedCopy, DeckText);
        ZayWidgetDOM::SetJson(Deck,"deck.");
        ZayWidgetDOM::SetValue("program.remote_site","0");
    #else
        if(!*Asset::GetWebFilesUrl())
        {
            ZayWidgetDOM::RemoveVariables("deck.");
            ZayWidgetDOM::SetValue("deck.list.count","0");
            ZayWidgetDOM::SetValue("deck.focus","-1");
        }
        String Url=ZayWidgetDOM::ExistVariable("settings.app_url")?
            ZayWidgetDOM::GetValue("settings.app_url").ToText().Trim() : String();
        if(!Url.Length()) Url="https://bonexgoo.github.io/zayapp/";
        ZayWidgetDOM::SetValue("program.remote_site","1");
        SetAppUrl(Url);
    #endif
    const String Id=Platform::Utility::GetLaunchParameter("app");
    const sint32 Index=FindAppIndex(Id);
    ZayWidgetDOM::SetValue("deck.focus",String::FromInteger(Index));
    ZayWidgetDOM::SetValue("program.standalone",Index>=0?"1":"0");
    #if BOSS_WASM
        if(Index>=0) Platform::Utility::SetWebAppIdentity(AppShortcutTitle(Index,Id),AppShortcutIcon(Id));
    #endif
}

void ZayAppData::PrepareGlueHost()
{
    if(mGluePrepared) return;
    if(!mNativeState) mNativeState = std::make_shared<ZayGlueNativeState>();

    // Publish application operations once. Concrete GLUE modules choose their
    // dependencies; this function has no module types or registration list.
    mGlueHost.Bind<std::shared_ptr<ZayGlueNativeState>()>("native.state",
        [this]()->std::shared_ptr<ZayGlueNativeState> { return mNativeState; });
    mGlueHost.Bind<void(uint64)>("view.update_until",
        [this](uint64 deadline)->void { if(mUpdateMsec < deadline) mUpdateMsec = deadline; });

    mGlueHost.Bind<void(bool)>("render.hint_changed",
        [this](bool smooth)->void
        {
            auto& state = mNativeState;
            if(state && state->sceneSmooth != smooth)
            {
                state->sceneSmooth = smooth;
                ++state->sceneRevision;
            }
        });

    mGlueHost.Bind<void(chars, sint32, sint32, sint32, sint32, double, bool)>("resource.capture_window",
        [](chars name, sint32 x, sint32 y, sint32 width, sint32 height, double blur, bool repaint)->void { R::MakeGrapImage(name, x, y, width, height, blur, repaint); });

    mGlueHost.Bind<bool(chars, const Image*&, sint32&, sint32&)>("resource.find_image",
        [](chars name, const Image*& image, sint32& width, sint32& height)->bool
        {
            image = nullptr;
            width = height = 0;
            const R resource(name);
            if(!resource.exist()) return false;
            image = &((const Image&) resource);
            width = resource.w();
            height = resource.h();
            return true;
        });

    mGlueHost.Bind<id_animate(chars)>("animation.get_apng",
        [this](chars assetname)->id_animate { return GetAPng(assetname); });

    mGlueHost.Bind<void(const String&)>("locale.set",
        [](const String& locale)->void { gLastLocale = locale; });

    mGlueHost.Bind<void()>("language.turn",
        [this]()->void { mMainWidget.LangTurn(); });

    mGlueHost.Bind<void(sint32, const String&, const String&)>("input.set_hotkey",
        [this](sint32 key, const String& pressGate, const String& releaseGate)->void
        {
            if(auto project = mProjects.Access(mLastProjCode))
            {
                auto& hotkey = project->mHotKeys[key];
                hotkey.mPressGate = pressGate;
                hotkey.mReleaseGate = releaseGate;
            }
        });

    mGlueHost.Bind<void()>("input.clear_hotkeys",
        [this]()->void
        {
            if(auto project = mProjects.Access(mLastProjCode))
                project->mHotKeys.Reset();
        });

    mGlueHost.Bind<bool(const String&)>("app.can_change_site",
        [](const String& viewname)->bool { return viewname == "main"; });

    mGlueHost.Bind<String()>("app.address",
        []()->String { return ZayWidgetDOM::GetComment("main_browser.url"); });

    mGlueHost.Bind<void()>("app.init_deck",
        [this]()->void { InitAppDeck(); });

    mGlueHost.Bind<bool(const String&)>("app.set_url",
        [this](const String& url)->bool { return SetAppUrl(url); });

    mGlueHost.Bind<sint32(const String&)>("app.create_shortcut",
        [](const String& appId)->sint32 { return CreateAppShortcut(appId); });

    mGlueHost.Bind<bool(const String&, const String&)>("app.open_standalone",
        [](const String& appId, const String& target)->bool
        {
            #if BOSS_WASM
                if(FindAppIndex(appId) >= 0)
                {
                    const String page = Platform::Utility::GetThisWebPageUrl();
                    if(page.Length())
                        return Platform::Utility::OpenWebPageWithQuery(page + "?app=" + appId.ToUrlString(), target);
                }
            #else
                (void) appId;
                (void) target;
            #endif
            return false;
        });

    mGlueHost.Bind<Map<bool>&()>("table.loaded_files",
        [this]()->Map<bool>& { return mLoadedTableFiles; });

    mGlueHost.Bind<void(chars, const String&)>("table.parse",
        [this](chars group, const String& csv)->void { mDependencyTable.Parse(group, csv); });

    mGlueHost.Bind<void()>("table.clear_focus",
        [this]()->void { mFocusedTable = nullptr; });

    mGlueHost.Bind<bool(chars, chars, chars)>("table.focus",
        [this](chars group, chars table, chars dependency)->bool
        {
            mFocusedTable = dependency
                ? mDependencyTable.Table(group, table, dependency)
                : mDependencyTable.Table(group, table);
            return mFocusedTable != nullptr;
        });

    mGlueHost.Bind<bool()>("table.has_focus",
        [this]()->bool { return mFocusedTable != nullptr; });

    mGlueHost.Bind<sint32()>("table.index",
        [this]()->sint32 { return mFocusedTable->TableIndex(); });

    mGlueHost.Bind<bool(chars, chars, String&)>("table.cell.named",
        [this](chars column, chars row, String& out)->bool
        {
            if(auto cell = mFocusedTable->Cell(column, row))
            {
                out = *cell;
                return true;
            }
            return false;
        });

    mGlueHost.Bind<bool(sint32, sint32, String&)>("table.cell.indexed",
        [this](sint32 x, sint32 y, String& out)->bool
        {
            if(auto cell = mFocusedTable->Cell(x, y))
            {
                out = *cell;
                return true;
            }
            return false;
        });

    mGlueHost.Bind<sint32()>("table.columns",
        [this]()->sint32 { return mFocusedTable->ColumnCount(); });

    mGlueHost.Bind<sint32()>("table.rows",
        [this]()->sint32 { return mFocusedTable->RowCount(); });

    mGlueHost.Bind<String(sint32)>("table.column_name",
        [this](sint32 index)->String { return mFocusedTable->ColumnName(index); });

    mGlueHost.Bind<String(sint32)>("table.row_name",
        [this](sint32 index)->String { return mFocusedTable->RowName(index); });

    mGlueHost.Bind<Map<id_sound>&()>("sound.handles",
        [this]()->Map<id_sound>& { return mSounds; });

    mGlueHost.Bind<sint32&()>("sound.device_focus",
        [this]()->sint32& { return mSoundDeviceFocus; });

    mGlueHost.Bind<bool&()>("sound.muted",
        [this]()->bool& { return mSoundMuted; });

    mGlueHost.Bind<void(sint32)>("sound.select_device",
        [this](sint32 focus)->void { UpdateSoundDeviceList(focus); });

    mGlueHost.Bind<void()>("network.update_ip_address",
        [this]()->void { UpdateIPAddress(); });

    mGlueHost.Bind<id_image_read(chars)>("resource.request_image",
        [](chars name)->id_image_read { return R(name)->GetImage(); });

    mGlueHost.Bind<void(const String&, const String&)>("widget.dispatch_callback",
        [this](const String& gate, const String& domNameD)->void
        {
            if(auto widgetName = mLastWidgetName.Access(mLastProjCode))
            if(auto widget = mSubWidgets.Access(*widgetName))
            {
                const auto success = widget->JumpCallDirectly(gate, domNameD);
                BOSS_TRACE("##### RestAPI(%s) JumpCall: %s (%s)", (chars) domNameD,
                    (chars) gate, success? "success" : "failure");
            }
        });

    mGlueHost.Bind<Map<doubles>&()>("graph.values",
        [this]()->Map<doubles>& { return mGraphs; });

    mGlueHost.Bind<Map<String>&()>("log.highlights",
        [this]()->Map<String>& { return mLogHighlights; });

    mGlueHost.Bind<bool(chars, sint32)>("channel.connect_serial",
        [this](chars port, sint32 baudrate)->bool
        {
            const bool connected = mChannel.ConnectSerial(port, baudrate);
            if(connected) UpdateChannelList();
            return connected;
        });

    mGlueHost.Bind<void(chars)>("channel.close",
        [this](chars channel)->void
        {
            mChannel.Delete(channel);
            UpdateChannelList();
            if(mProjects.Access(mLastVdsProjCode))
                ZayWidgetDOM::RemoveVariables(mLastVdsProjCode + String::Format(".%s.", channel));
        });

    mGlueHost.Bind<void(chars, uint08s&)>("channel.build_packet",
        [this](chars channel, uint08s& out)->void
        {
            mEditText.WritePacket(out, [](chars ch, chars key)->const SolverValue
                {return ZayWidgetDOM::GetValue(String::Format("vds.%s", key));}, channel);
        });

    mGlueHost.Bind<bool(chars, bytes, sint32)>("channel.write",
        [this](chars channel, bytes data, sint32 size)->bool { return mChannel.Write(channel, data, size); });

    mGlueHost.Bind<void(chars, chars, bytes, sint32, chars)>("channel.log_packet",
        [this](chars channel, chars tag, bytes data, sint32 size, chars color)->void { AddHexLog(channel, tag, data, size, color); });

    mGlueHost.Bind<Map<String>&()>("device.bindings",
        [this]()->Map<String>& { return mDevices; });

    mGlueHost.Bind<void()>("device.changed",
        [this]()->void { UpdateDeviceList(); });

    mGlueHost.Bind<void(chars)>("scope.connect",
        [this](chars resource)->void
        {
            if(!mOscilloScope) mOscilloScope = VisaApi::Create();
            if(mOscilloScope)
            {
                if(mOscilloScope->IsOpen()) mOscilloScope->Close();
                mOscilloScope->SetResource(resource);
            }
        });

    mGlueHost.Bind<bool()>("scope.validate",
        [this]()->bool { return ValidScope(); });

    mGlueHost.Bind<void(chars)>("scope.write",
        [this](chars command)->void { mOscilloScope->Write(command); });

    mGlueHost.Bind<void(chars, String&)>("scope.query",
        [this](chars command, String& result)->void
        {
            std::string value;
            mOscilloScope->Query(command, value);
            result = value.c_str();
        });

    mGlueHost.Bind<void(chars, chars)>("install.begin",
        [this](chars wpkpath, chars exclusions)->void { RunInstallWpk(wpkpath, exclusions); });

    mGlueHost.Bind<String()>("install.path",
        []()->String
        {
            if(auto path = ZayWidgetDOM::GetValue("program.tasking.install.path").ToText())
                return Platform::File::RootForAssetsRem() + "../" + path;
            return String();
        });

    mGlueHost.Bind<void(chars)>("install.status",
        [this](chars status)->void
        {
            if(mInstallWpk)
                Tasking::SendQuery(mInstallWpk, String(status).ToBuffer());
        });

    mGlueHost.Bind<String()>("vds.active_project",
        [this]()->String { return mLastVdsProjCode; });

    mGlueHost.Bind<bool(chars)>("vds.has_project",
        [this](chars project)->bool { return mProjects.Access(project) != nullptr; });

    mGlueHost.Bind<void(chars, chars, chars, sint32)>("vds.set_quest",
        [this](chars project, chars channel, chars quest, sint32 index)->void { if(auto entry = mProjects.Access(project)) entry->mChQuests(channel)(quest) = index; });

    mGlueHost.Bind<void(chars, chars, chars)>("vds.remove_quest",
        [this](chars project, chars channel, chars quest)->void { if(auto entry = mProjects.Access(project)) entry->mChQuests(channel).Remove(quest); });

    mGlueHost.Bind<bool(chars, chars, sint32, chars)>("vds.run_entry",
        [this](chars project, chars channel, sint32 questIndex, chars script)->bool { return RunEntry(project, channel, questIndex, script); });

    mGlueHost.Bind<bool(chars, chars)>("vds.stop_entry",
        [this](chars project, chars channel)->bool { return StopEntry(project, channel); });

    mGlueHost.Bind<void(chars, chars)>("vds.save_store",
        [this](chars channel, chars assetPath)->void { mChannel.SaveStore(channel, assetPath); });

    mGlueHost.Bind<void(chars)>("vds.set_monitor_focus",
        [this](chars domName)->void { mChannel.SetMonitorFocus(domName); });

    mGlueHost.Bind<void(chars, chars)>("vds.log",
        [this](chars project, chars text)->void { SendWidgetLog(project, text); });

    mGluePrepared = true;
}

void ZayAppData::InitWidget(ZayWidget* widget, const String viewname)
{
    PrepareGlueHost();

    auto MainResource = [](chars name)->const Image* {
        const bool SiteIcon = String(name).Left(5) == "icon.";
        return &((const Image&) R(name,!SiteIcon));
    };
    auto AppResource = [](chars name)->const Image* {return &((const Image&) R(name));};
    auto& NewZayson = widget->Init(viewname, nullptr, (viewname == "main")? +MainResource : +AppResource);

    // Only the names are static. Each widget (including reinitialized widgets)
    // receives its own view, widget, name, and shared host bindings.
    static const chars GlueClasses[] = {
        "rendering",
        "depth_scene",
        "runtime",
        "input",
        "app_navigation",
        "table",
        "media",
        "dom",
        "file",
        "network",
        "navigation_and_depth",
        "graph_and_log",
        "device_channel",
        "instrumentation",
        "installation",
        "vds",
    };
    const ZayGlue::Bindings GlueBindings(this, widget, viewname, mGlueHost);
    for(chars GlueClass : GlueClasses)
        ZayGlue::Add(GlueClass, NewZayson, GlueBindings);

    ////////////////////////////////////////////////////////////////////////////////
    // user_content
    NewZayson.AddComponent(ZayExtend::ComponentType::ContentWithParameter, "user_content", ZAY_DECLARE_COMPONENT(panel, params, this, widget)
    {
        if(params.ParamCount() < 1)
            return panel._push_pass();
        const String Type = params.Param(0).ToText();
        bool HasRender = false;

        branch;
        jump(!Type.Compare("depthviewport"))
        {
            if(params.ParamCount() == 2)
            {
                mNativeState->MarkViewport(widget, params.Param(1).ToText(), panel);
                HasRender = true;
            }
        }
        jump(!Type.Compare("subwidget"))
        {
            if(params.ParamCount() == 3)
            {
                const uint64 RenderBeginMsec = Platform::Utility::CurrentTimeMsec();
                {
                    const String WidgetFile = params.Param(1).ToText();
                    const String ProjCode = params.Param(2).ToText();
                    if(!WidgetFile.Right(4).CompareNoCase(".zay"))
                    {
                        auto CurWidgetName = WidgetFile.Left(WidgetFile.Length() - 4).Lower();
                        auto CurSubWidget = mSubWidgets.Access(CurWidgetName);
                        bool NeedEntry = false;
                        // Vds를 사용하는 마지막 ProjCode를 기록
                        if(mLastProjCode != ProjCode || mLastWidgetName(ProjCode) != CurWidgetName)
                        {
                            mLastProjCode = ProjCode;
                            NeedEntry = true;
                            const String VdsAssetName = String::Format("script/%s_vds.json", (chars) ProjCode);
                            if(Asset::Exist(VdsAssetName, nullptr, nullptr, nullptr, nullptr, nullptr))
                            {
                                mLastVdsProjCode = ProjCode;
                                mProjects(ProjCode); // 프로젝트 생성보장
                                ZayWidgetDOM::SetValue("vds.projcode", '\'' + ProjCode + '\'');
                                ZayWidgetDOM::SetValue(String::Format("vds.%s.widget", (chars) ProjCode), '\'' + CurWidgetName + '\'');
                            }
                        }
                        // 위젯생성
                        if(!CurSubWidget)
                        {
                            CurSubWidget = &mSubWidgets(CurWidgetName);
                            mLastWidgetName(ProjCode) = CurWidgetName;
                            // A sibling widget may have indexed this JSON as an extension. Rebind it before oncreate.
                            CollectModifiedDoms(CurWidgetName, ProjCode, true);
                            UpdateModifiedDoms();
                            UpdateModifiedVds();
                            InitWidget(CurSubWidget, ProjCode);
                            CurSubWidget->Reload("widget/" + WidgetFile);
                            // 빌드코드화
                            auto CurSaveTime = CurSubWidget->SaveTime();
                            if(CurSaveTime.Length() == 0)
                                ZayWidgetDOM::SetValue("program.buildcodes." + CurWidgetName, "'00A00A'");
                            else
                            {
                                const sint32 Year  = Parser::GetInt(CurSaveTime.Offset(2), 2);
                                const sint32 Month = Parser::GetInt(CurSaveTime.Offset(5), 2);
                                const sint32 Day   = Parser::GetInt(CurSaveTime.Offset(8), 2);
                                const sint32 Hour  = Parser::GetInt(CurSaveTime.Offset(11), 2);
                                const char MonthCode = (1 <= Month && Month <= 12)? 'A' + Month : 'A';
                                const char HourCode = (0 <= Hour && Hour <= 23)? 'B' + Hour : 'A';
                                ZayWidgetDOM::SetValue("program.buildcodes." + CurWidgetName, String::Format("'%02d%c%02d%c'", Year, MonthCode, Day, HourCode));
                            }
                        }
                        // ProjCode별 마지막 위젯명이 다른 경우 강제로 DOM업데이트
                        else if(mLastWidgetName(ProjCode) != CurWidgetName)
                        {
                            mLastWidgetName(ProjCode) = CurWidgetName;
                            CollectModifiedDoms(CurWidgetName, ProjCode, true);
                            UpdateModifiedDoms();
                        }
                        if(NeedEntry) // 위젯의 첫진입/재진입시 entry게이트를 호출
                            CurSubWidget->JumpCallDirectly("entry", params.UIName(), nullptr);
                        HasRender = CurSubWidget->Render(panel);
                    }
                    else
                    {
                        mMainWidget.JumpCallDirectly("blankwidget", params.UIName(), &panel);
                        HasRender = true;
                    }
                }
                const uint64 RenderEndMsec = Platform::Utility::CurrentTimeMsec();
                ZayWidgetDOM::SetValue("program.time.render", String::FromInteger(sint64(RenderEndMsec - RenderBeginMsec)));
            }
        }
        jump(!Type.Compare("lottie"))
        {
            if(params.ParamCount() == 3)
            {
                const String AssetName = params.Param(1).ToText();
                const double Sec = params.Param(2).ToFloat();
                HasRender = RenderUC_Lottie(panel, AssetName, Sec);
            }
        }
        jump(!Type.Compare("movie"))
        {
            if(params.ParamCount() == 3)
            {
                const String AssetName = params.Param(1).ToText();
                const double Sec = params.Param(2).ToFloat();
                HasRender = RenderUC_Movie(panel, AssetName, Sec);
            }
        }
        jump(!Type.Compare("apng"))
        {
            if(params.ParamCount() == 3)
            {
                const String AssetName = params.Param(1).ToText();
                const double Sec = params.Param(2).ToFloat();
                HasRender = RenderUC_APng(panel, AssetName, Sec);
            }
        }
        jump(!Type.Compare("urlimage"))
        {
            if(params.ParamCount() == 3)
            {
                const double FadeSec = params.Param(1).ToFloat();
                const String Url = params.Param(2).ToText();
                HasRender = RenderUC_UrlImage(panel, FadeSec, Url);
            }
        }
        jump(!Type.Compare("graph"))
        {
            if(params.ParamCount() == 2)
            {
                const String GraphName = params.Param(1).ToText();
                HasRender = RenderUC_Graph(panel, GraphName);
            }
        }
        jump(!Type.Compare("tag") && params.ParamCount() == 8)
        {
            const String PosDom = params.Param(1).ToText();
            const String Text = params.Param(2).ToText();
            const sint32 LineGap = params.Param(3).ToInteger();
            const sint32 LastGap = params.Param(4).ToInteger();
            const sint32 Border = params.Param(5).ToInteger();
            const String Color = params.Param(6).ToText();
            const bool AntiCut = (params.Param(7).ToInteger() != 0);
            if(PosDom.Left(2) == "d.")
                HasRender = RenderUC_Tag(panel, PosDom.Offset(2), Text, LineGap, LastGap, Border, Color, AntiCut);
        }
        jump(!Type.Compare("script"))
        {
            mEditText.Render(panel, 5);
            HasRender = true;
        }
        jump(!Type.Compare("log"))
        {
            if(params.ParamCount() == 3)
            {
                const String Ch = params.Param(1).ToText();
                const sint32 Index = params.Param(2).ToInteger();
                HasRender = RenderUC_Log(panel, Ch, Index);
            }
        }
        jump(!Type.Compare("slider"))
        {
            if(params.ParamCount() == 5)
            {
                const String DomName = params.Param(1).ToText();
                const sint32 Min = params.Param(2).ToInteger();
                const sint32 Max = params.Param(3).ToInteger();
                const sint32 Unit = params.Param(4).ToInteger();
                HasRender = RenderUC_Slider(panel, DomName, Min, Max, Unit);
            }
        }
        jump(!Type.Compare("slidevh"))
        {
            if(8 <= params.ParamCount())
            {
                const String DomNameD = params.Param(1).ToText();
                const sint32 Min = params.Param(2).ToInteger();
                const sint32 Limit = params.Param(3).ToInteger();
                const sint32 Max = params.Param(4).ToInteger();
                const sint32 Step = params.Param(5).ToInteger();
                const bool Vertical = (params.Param(6).ToInteger() != 0);
                const sint32 Border = params.Param(7).ToInteger();
                const String GateName = (8 < params.ParamCount())? params.Param(8).ToText() : String();
                if(DomNameD.Left(2) == "d.")
                    HasRender = RenderUC_SlideVH(panel, DomNameD.Offset(2), Min, Limit, Max, Step, Vertical, Border, GateName);
            }
        }
        jump(!Type.Compare("picker"))
        {
            if(params.ParamCount() == 10)
            {
                const String DomNameD = params.Param(1).ToText();
                const sint32 Min = params.Param(2).ToInteger();
                const sint32 Max = params.Param(3).ToInteger();
                const sint32 Step = params.Param(4).ToInteger();
                const sint32 Def = params.Param(5).ToInteger();
                const sint32 Trunc = params.Param(6).ToInteger();
                const String Unit = params.Param(7).ToText();
                const sint32 Side = params.Param(8).ToInteger();
                const String ImageNameR = params.Param(9).ToText();
                if(DomNameD.Left(2) == "d." && ImageNameR.Left(2) == "r.")
                    HasRender = RenderUC_Picker(panel, DomNameD.Offset(2), Min, Max, Step, Def, Trunc, Unit, Side, ImageNameR.Offset(2));
            }
        }
        jump(!Type.Compare("touchcircle"))
        {
            if(7 <= params.ParamCount())
            {
                const sint32 X = params.Param(1).ToInteger();
                const sint32 Y = params.Param(2).ToInteger();
                const sint32 W = params.Param(3).ToInteger();
                const sint32 H = params.Param(4).ToInteger();
                const String DomNameD = params.Param(5).ToText();
                const bool Show = (params.Param(6).ToInteger() != 0);
                if(DomNameD.Left(2) == "d.")
                {
                    Points Dots;
                    for(sint32 i = 7, iend = params.ParamCount(); i + 1 < iend; i += 2)
                    {
                        auto& NewDot = Dots.AtAdding();
                        NewDot.x = params.Param(i).ToInteger();
                        NewDot.y = params.Param(i + 1).ToInteger();
                    }
                    HasRender = RenderUC_TouchCircle(panel, X, Y, W, H, DomNameD.Offset(2), Show, Dots);
                }
            }
        }
        jump(!Type.Compare("zoomarea"))
        {
            if(params.ParamCount() == 2)
            {
                const String DomNameD = params.Param(1).ToText();
                if(DomNameD.Left(2) == "d.")
                    HasRender = RenderUC_ZoomArea(panel, DomNameD.Offset(2));
            }
        }
        jump(!Type.Compare("dragarea"))
        {
            if(params.ParamCount() == 2)
            {
                const String DomNameD = params.Param(1).ToText();
                if(DomNameD.Left(2) == "d.")
                    HasRender = RenderUC_DragArea(panel, DomNameD.Offset(2));
            }
        }

        // 그외 처리
        if(!HasRender)
        ZAY_INNER_SCISSOR(panel, 0)
        {
            ZAY_RGBA(panel, 255, 0, 0, 128)
            panel.fill();
            for(sint32 i = 0; i < 5; ++i)
            {
                ZAY_INNER(panel, 1 + i)
                ZAY_RGBA(panel, 255, 0, 0, 128 - 16 * i)
                panel.rect(1);
            }
            ZAY_FONT(panel, 1.2 * Math::MinF(Math::MinF(panel.w(), panel.h()) / 40, 1))
            ZAY_RGB(panel, 255, 0, 0)
            panel.text(Type, UIFA_CenterMiddle, UIFE_Right);
        }
        return panel._push_pass();
    });
}

// The main widget owns the site selection. Do not destroy the executing subwidget.
void ZayAppData::ClearSiteState()
{
    clearCapture();
    mSubWidgets.Reset();
    // Disconnect script references before destroying project state.
    for(sint32 i=0;i<mProjects.Count();++i)
    {
        chararray ProjCode;
        if(auto Project=mProjects.AccessByOrder(i, &ProjCode))
        {
            mChannel.ClearBatonAll(&Project->mScript);
            ZayWidgetDOM::RemoveVariables(String::Format("vds.%s.widget", &ProjCode[0]));
        }
    }
    mProjects.Reset(); mLastVdsModifyMsec.Reset(); mLastWidgetName.Reset();
    mLastProjCode.Empty(); mLastVdsProjCode.Empty();
    for(sint32 i=mDomJsons.Count()-1;0<=i;--i)
        if(auto Dom=mDomJsons.AccessByOrder(i))
            if(Dom->mAssetName.Left(5)!="main/")
            {
                const String AssetName=Dom->mAssetName;
                ZayWidgetDOM::RemoveVariables(Dom->mHeaderName);
                mDomJsons.Remove(AssetName);
            }
    // Clear entire old app DOM namespaces, not just their JSON defaults.
    const sint32 Count=ZayWidgetDOM::GetValue("deck.list.count").ToInteger();
    for(sint32 i=0;i<Count;++i)
    {
        const String Id=ZayWidgetDOM::GetValue(String::Format("deck.list.%d.dom",i)).ToText();
        if(Id.Length() && Id!="main" && Id!="data" && Id!="settings" && Id!="program" && Id!="deck")
            ZayWidgetDOM::RemoveVariables(Id+".");
    }
    ZayWidgetDOM::RemoveVariables("program.buildcodes.");
    ZayWidgetDOM::RemoveVariables("vds.projcode");
    for(sint32 i=0;i<mSounds.Count();++i) if(auto H=mSounds.AccessByOrder(i)) if(*H) Platform::Sound::Close(*H);
    mSounds.Reset();
    for(sint32 i=0;i<mUrlImages.Count();++i)
        if(auto Image=mUrlImages.AccessByOrder(i)) if(Image->mTasking) Tasking::Release(Image->mTasking, true);
    mUrlImages.Reset();
    for(auto M : {&mLotties,&mMovies,&mAPngs})
    {
        for(sint32 i=0;i<M->Count();++i) if(auto H=M->AccessByOrder(i)) if(*H) Platform::Animate::Close(*H);
        M->Reset();
    }
    mFocusedTable=nullptr; mLoadedTableFiles.Reset();
    mDependencyTable=DependencyTable();
    mGraphs.Reset(); mLogHighlights.Reset();
    if(mNativeState)
    {
        mNativeState->routes.clear(); mNativeState->worlds.clear(); mNativeState->depths.clear();
        mNativeState->scenes.clear(); mNativeState->zooms.clear(); mNativeState->paintKey={};
        ++mNativeState->sceneRevision;
    }
    // 메인 DOM과 설정, 아틀라스는 유지하고 이전 사이트의 캐시와 폰트는 해제한다.
    R::ClearAll(false, true);
    Platform::Utility::RemoveSystemFontAll();
    ZayWidgetDOM::RemoveVariables("program.fonts.");
    LoadWidgetFonts("data.");
}

bool ZayAppData::SetAppUrl(const String& url)
{
    ZayWidgetDOM::SetValue("program.appurl_ok","0");
    // 작업중인 이미지가 이전 사이트의 캐시에 기록을 마친 뒤 기준 URL을 변경한다.
    for(sint32 i=0;i<mUrlImages.Count();++i)
        if(auto Image=mUrlImages.AccessByOrder(i))
            if(Image->mTasking)
            {
                Tasking::Release(Image->mTasking, true);
                Image->mTasking=nullptr;
                if(!Image->mImage.HasBitmap()) Image->mFileTitle.Empty();
            }
    const chars DeckJson=Asset::TrySetWebFilesUrl(url);
    if(!DeckJson)
    {
        ClearSiteState();
        Asset::SetWebFilesUrl();
        ZayWidgetDOM::RemoveVariables("deck.");
        ZayWidgetDOM::SetValue("deck.list.count","0");
        ZayWidgetDOM::SetValue("deck.focus","-1");
        ZayWidgetDOM::SetValue("program.standalone","0");
        ZayWidgetDOM::SetValue("program.appurl_error","'index_or_deck_failed'");
        BOSS_TRACE("[setappurl] Cannot open site: %s", (chars)url);
        invalidate(2);
        return false;
    }
    const String DeckText=DeckJson;
    ClearSiteState();
    const Context Deck(ST_Json, SO_NeedCopy, DeckText);
    ZayWidgetDOM::RemoveVariables("deck."); ZayWidgetDOM::SetJson(Deck,"deck.");
    ZayWidgetDOM::SetValue("deck.focus","-1");
    ZayWidgetDOM::SetValue("program.standalone","0");
    ZayWidgetDOM::SetValue("program.remote_site","1");
    const String Url=Asset::GetWebFilesUrl();
    String Literal=Url; Literal.Replace("\\","\\\\"); Literal.Replace("'","\\'");
    ZayWidgetDOM::SetValue("settings.app_url", '\''+Literal+'\'');
    // Persist only after a validated site transition; the normal settings Gate can still save other options.
    Context Settings;
    ZayWidgetDOM::GetJson(Settings,"settings.");
    Settings.SaveJson().ToAsset("main/widget/main_settings.json",true);
    ZayWidgetDOM::SetComment("main_browser.url",Url);
    ZayWidgetDOM::SetValue("main_browser.url.text", '\''+Literal+'\'');
    ZayWidgetDOM::SetValue("program.appurl_error","''"); ZayWidgetDOM::SetValue("program.appurl_ok","1");
    invalidate(2); return true;
}

void ZayAppData::RenderWindowOutline(ZayPanel& panel)
{
    ZAY_INNER(panel, 1)
    ZAY_RGBA(panel, 0, 0, 0, 32)
        panel.rect(1);

    // 리사이징바
    ZAY_RGBA(panel, 0, 0, 0, 64 + 128 * Math::Abs(((frame() * 2) % 100) - 50) / 50)
    {
        if(mNcLeftBorder)
        {
            for(sint32 i = 0; i < 5; ++i)
            ZAY_RGBA(panel, 128, 128, 128, 128 - i * 25)
            ZAY_XYWH(panel, i, 0, 1, panel.h())
                panel.fill();
            invalidate(2);
        }
        if(mNcTopBorder)
        {
            for(sint32 i = 0; i < 5; ++i)
            ZAY_RGBA(panel, 128, 128, 128, 128 - i * 25)
            ZAY_XYWH(panel, 0, i, panel.w(), 1)
                panel.fill();
            invalidate(2);
        }
        if(mNcRightBorder)
        {
            for(sint32 i = 0; i < 5; ++i)
            ZAY_RGBA(panel, 128, 128, 128, 128 - i * 25)
            ZAY_XYWH(panel, panel.w() - 1 - i, 0, 1, panel.h())
                panel.fill();
            invalidate(2);
        }
        if(mNcBottomBorder)
        {
            for(sint32 i = 0; i < 5; ++i)
            ZAY_RGBA(panel, 128, 128, 128, 128 - i * 25)
            ZAY_XYWH(panel, 0, panel.h() - 1 - i, panel.w(), 1)
                panel.fill();
            invalidate(2);
        }
    }

    // 윈도우 리사이징 모듈
    #define RESIZING_MODULE(C, L, T, R, B, BORDER) do {\
        static point64 OldMousePos; \
        static rect128 OldWindowRect; \
        static bool DraggingRaised = false; \
        if(t == GT_Moving) \
        { \
            SetCursor(C); \
            mNcLeftBorder = false; \
            mNcTopBorder = false; \
            mNcRightBorder = false; \
            mNcBottomBorder = false; \
            BORDER = true; \
            invalidate(); \
        } \
        else if(t == GT_MovingLosed) \
        { \
            SetCursor(CR_Arrow); \
        } \
        else if(t == GT_Pressed) \
        { \
            Platform::Utility::GetCursorPos(OldMousePos); \
            OldWindowRect = Platform::GetWindowRect(); \
            DraggingRaised = false; \
        } \
        else if(t == GT_InDragging || t == GT_OutDragging || t == GT_InReleased || t == GT_OutReleased || t == GT_CancelReleased) \
        { \
            point64 NewMousePos; \
            Platform::Utility::GetCursorPos(NewMousePos); \
            const rect128 NewWindowRect = {L, T, R, B}; \
            Platform::SetWindowRect(NewWindowRect.l, NewWindowRect.t, \
                NewWindowRect.r - NewWindowRect.l, NewWindowRect.b - NewWindowRect.t); \
            if(!DraggingRaised) \
            { \
                DraggingRaised = true; \
                Platform::SetWindowRaise(); \
            } \
        }} while(false);

    // 윈도우 리사이징 꼭지점
    const sint32 SizeBorder = 15;
    ZAY_LTRB_UI(panel, 0, 0, SizeBorder, SizeBorder, "NcLeftTop",
        ZAY_GESTURE_T(t, this)
        {
            RESIZING_MODULE(CR_SizeFDiag,
                MoveNcLeft(OldWindowRect, NewMousePos.x - OldMousePos.x),
                MoveNcTop(OldWindowRect, NewMousePos.y - OldMousePos.y),
                OldWindowRect.r,
                OldWindowRect.b,
                mNcLeftBorder = mNcTopBorder);
        });
    ZAY_LTRB_UI(panel, panel.w() - SizeBorder, 0, panel.w(), SizeBorder, "NcRightTop",
        ZAY_GESTURE_T(t, this)
        {
            RESIZING_MODULE(CR_SizeBDiag,
                OldWindowRect.l,
                MoveNcTop(OldWindowRect, NewMousePos.y - OldMousePos.y),
                MoveNcRight(OldWindowRect, NewMousePos.x - OldMousePos.x),
                OldWindowRect.b,
                mNcRightBorder = mNcTopBorder);
        });
    ZAY_LTRB_UI(panel, 0, panel.h() - SizeBorder, SizeBorder, panel.h(), "NcLeftBottom",
        ZAY_GESTURE_T(t, this)
        {
            RESIZING_MODULE(CR_SizeBDiag,
                MoveNcLeft(OldWindowRect, NewMousePos.x - OldMousePos.x),
                OldWindowRect.t,
                OldWindowRect.r,
                MoveNcBottom(OldWindowRect, NewMousePos.y - OldMousePos.y),
                mNcLeftBorder = mNcBottomBorder);
        });
    ZAY_LTRB_UI(panel, panel.w() - SizeBorder, panel.h() - SizeBorder, panel.w(), panel.h(), "NcRightBottom",
        ZAY_GESTURE_T(t, this)
        {
            RESIZING_MODULE(CR_SizeFDiag,
                OldWindowRect.l,
                OldWindowRect.t,
                MoveNcRight(OldWindowRect, NewMousePos.x - OldMousePos.x),
                MoveNcBottom(OldWindowRect, NewMousePos.y - OldMousePos.y),
                mNcRightBorder = mNcBottomBorder);
        });

    // 윈도우 리사이징 모서리
    ZAY_LTRB_UI(panel, 0, SizeBorder, SizeBorder, panel.h() - SizeBorder, "NcLeft",
        ZAY_GESTURE_T(t, this)
        {
            RESIZING_MODULE(CR_SizeHor,
                MoveNcLeft(OldWindowRect, NewMousePos.x - OldMousePos.x),
                OldWindowRect.t,
                OldWindowRect.r,
                OldWindowRect.b,
                mNcLeftBorder);
        });
    ZAY_LTRB_UI(panel, SizeBorder, 0, panel.w() - SizeBorder, SizeBorder, "NcTop",
        ZAY_GESTURE_T(t, this)
        {
            RESIZING_MODULE(CR_SizeVer,
                OldWindowRect.l,
                MoveNcTop(OldWindowRect, NewMousePos.y - OldMousePos.y),
                OldWindowRect.r,
                OldWindowRect.b,
                mNcTopBorder);
        });
    ZAY_LTRB_UI(panel, panel.w() - SizeBorder, SizeBorder, panel.w(), panel.h() - SizeBorder, "NcRight",
        ZAY_GESTURE_T(t, this)
        {
            RESIZING_MODULE(CR_SizeHor,
                OldWindowRect.l,
                OldWindowRect.t,
                MoveNcRight(OldWindowRect, NewMousePos.x - OldMousePos.x),
                OldWindowRect.b,
                mNcRightBorder);
        });
    ZAY_LTRB_UI(panel, SizeBorder, panel.h() - SizeBorder, panel.w() - SizeBorder, panel.h(), "NcBottom",
        ZAY_GESTURE_T(t, this)
        {
            RESIZING_MODULE(CR_SizeVer,
                OldWindowRect.l,
                OldWindowRect.t,
                OldWindowRect.r,
                MoveNcBottom(OldWindowRect, NewMousePos.y - OldMousePos.y),
                mNcBottomBorder);
        });
}

bool ZayAppData::RenderUC_UrlImage(ZayPanel& panel, double fadesec, chars url)
{
    // 최초시 로딩처리
    auto& CurUrlImage = mUrlImages(url);
    if(CurUrlImage.mFileTitle.Length() == 0)
    {
        const String Url = url;
        CurUrlImage.mFileTitle = String::Format("urlimage/%s", AddOn::Ssl::ToSHA256((bytes)(chars) Url, Url.Length(), false));
        CurUrlImage.mFadeMsec = Math::Max(0, 1000 * fadesec);
        if(Asset::Exist(CurUrlImage.mFileTitle + ".png"))
            CurUrlImage.mImage.SetName(CurUrlImage.mFileTitle + ".png").Load();
        else if(Asset::Exist(CurUrlImage.mFileTitle + ".jpg"))
            CurUrlImage.mImage.SetName(CurUrlImage.mFileTitle + ".jpg").Load();
        else if(Asset::Exist(CurUrlImage.mFileTitle + ".bmp"))
            CurUrlImage.mImage.SetName(CurUrlImage.mFileTitle + ".bmp").Load();
        else CurUrlImage.mTasking = Tasking::Create(
            [](buffer& self, Queue<buffer>& query, Queue<buffer>& answer, id_common common)->sint32
            {
                const String Args = String::FromBuffer(self);
                self = nullptr;
                auto ColonPos = Args.Find(0, ":");
                String FileName = Args.Left(ColonPos);
                const String Url = Args.Offset(ColonPos + 1);
                if(id_curl NewCurl = AddOn::Curl::Create())
                {
                    sint32 FileSize = 0;
                    bytes FileData = AddOn::Curl::GetBytes(NewCurl, Url, &FileSize);
                    if(FileData && 4 <= FileSize && FileData[0] == 0x89 && FileData[1] == 0x50 && FileData[2] == 0x4E && FileData[3] == 0x47)
                        FileName += ".png";
                    else if(FileData && 3 <= FileSize && FileData[0] == 0xFF && FileData[1] == 0xD8 && FileData[2] == 0xFF)
                        FileName += ".jpg";
                    else if(FileData && 2 <= FileSize && FileData[0] == 0x42 && FileData[1] == 0x4D)
                        FileName += ".bmp";
                    else
                    {
                        AddOn::Curl::Release(NewCurl);
                        return -1;
                    }
                    if(auto NewAsset = Asset::OpenForWrite(FileName, true))
                    {
                        Asset::Write(NewAsset, FileData, FileSize);
                        Asset::Close(NewAsset);
                        Platform::BroadcastNotify("UrlImageLoaded", Url);
                    }
                    AddOn::Curl::Release(NewCurl);
                }
                return -1;
            }, (CurUrlImage.mFileTitle + ':' + Url).ToBuffer());
    }

    // 랜더링
    if(CurUrlImage.mImage.HasBitmap())
    {
        if(0 < CurUrlImage.mLoadedMsec)
        {
            const sint32 Opacity = (CurUrlImage.mFadeMsec == 0)? 255 :
                Math::Min(255 * (Platform::Utility::CurrentTimeMsec() - CurUrlImage.mLoadedMsec) / CurUrlImage.mFadeMsec, 255);
            ZAY_RGBA(panel, 128, 128, 128, Opacity)
                panel.stretch(CurUrlImage.mImage, Image::Build::Force);
            if(Opacity == 255)
                CurUrlImage.mLoadedMsec = 0;
        }
        else panel.stretch(CurUrlImage.mImage, Image::Build::Force);
    }
    return true;
}

bool ZayAppData::RenderUC_Lottie(ZayPanel& panel, chars assetname, double sec)
{
    if(!mLotties.Access(assetname))
        mLotties(assetname) = Platform::Animate::OpenForLottieJson(String::FromAsset(assetname), assetname);
    if(auto CurLottie = mLotties.Access(assetname))
    if(*CurLottie)
    {
        Platform::Animate::Seek(*CurLottie, sec, true);
        auto Pos = panel.toview(0, 0);
        Platform::Animate::Draw(*CurLottie, Pos.x, Pos.y, panel.w(), panel.h(), 0);
    }
    return true;
}

bool ZayAppData::RenderUC_Movie(ZayPanel& panel, chars assetname, double sec)
{
    if(!mMovies.Access(assetname))
    {
        mMovies(assetname) = nullptr;
        #if BOSS_WASM
            mMovies(assetname) = Platform::Animate::OpenForMovieFile(Platform::File::RootForAssets() + assetname);
        #else
            const String Path=Asset::GetFilePath(assetname);
            if(Path.Length()) mMovies(assetname)=Platform::Animate::OpenForMovieFile(Path);
        #endif
    }
    if(auto CurMovie = mMovies.Access(assetname))
    if(*CurMovie)
    {
        Platform::Animate::Next(*CurMovie, true);
        auto Pos = panel.toview(0, 0);
        Platform::Animate::Draw(*CurMovie, Pos.x, Pos.y, panel.w(), panel.h(), 0);
    }
    return true;
}

id_animate ZayAppData::GetAPng(chars assetname)
{
    if(!mAPngs.Access(assetname))
    {
        #if BOSS_WASM
            // 웹어셋을 동기식으로 읽고 첫 프레임까지 준비한 뒤 핸들을 캐싱한다.
            // 비동기 URL로딩은 그리기 시점에 빈 영역을 노출할 수 있다.
            auto& NewAnimate = mAPngs(assetname);
            NewAnimate = nullptr;
            if(buffer NewAPngData = Asset::ToBuffer(assetname))
            {
                NewAnimate = Platform::Animate::OpenForAPngForData((bytes) NewAPngData,
                    Buffer::SizeOf(NewAPngData) * Buffer::CountOf(NewAPngData));
                Buffer::Free(NewAPngData);
            }
            // 실패한 어셋도 기록하여 매 프레임 동기식 다운로드가 반복되지 않게 한다.
            if(!NewAnimate)
                BOSS_TRACE("GetAPng(%s) failed - web asset load or decode", assetname);
        #elif BOSS_ANDROID | BOSS_IPHONE
            buffer NewAPngData = Asset::ToBuffer(assetname);
            if(NewAPngData)
            {
                mAPngs(assetname) = Platform::Animate::OpenForAPngForData((bytes) NewAPngData,
                    Buffer::SizeOf(NewAPngData) * Buffer::CountOf(NewAPngData));
                Buffer::Free(NewAPngData);
            }
        #else
            // 실패도 기록하여 매 프레임 같은 파일의 다운로드를 반복하지 않는다.
            auto& NewAnimate = mAPngs(assetname);
            NewAnimate = nullptr;
            const String Path=Asset::GetFilePath(assetname);
            if(Path.Length()) NewAnimate=Platform::Animate::OpenForAPngForFile(Path);
        #endif
    }
    const auto handle = mAPngs.Access(assetname);
    return handle ? *handle : nullptr;
}

bool ZayAppData::RenderUC_APng(ZayPanel& panel, chars assetname, double sec)
{
    if(auto animate = GetAPng(assetname))
    {
        if(0 < Platform::Animate::GetFrameCount(animate))
        {
            Platform::Animate::Seek(animate, sec, true);
            auto Pos = panel.toview(0, 0);
            Platform::Animate::Draw(animate, Pos.x, Pos.y, panel.w(), panel.h(), 0);
            return true;
        }
    }
    // 로딩/디코딩 실패시 user_content의 기존 오류표시를 사용한다.
    return false;
}

bool ZayAppData::RenderUC_Graph(ZayPanel& panel, chars graphname)
{
    if(auto CurGraph = mGraphs.Access(graphname))
    if(auto GraphCount = CurGraph->Count())
    {
        // 사전조사
        float Min = 0, Max = 0;
        for(sint32 i = 0; i < GraphCount; ++i)
        {
            const float CurValue = (*CurGraph)[i];
            const double LogValue = Math::Log10(CurValue); // 3.5, -1.7
            const double FloorValue = Math::Floor(LogValue); // 3.0, -2.0
            const double PowValue = Math::Pow(10, FloorValue); // 1000.0, 0.01
            const double MinValue = Math::Floor(CurValue / PowValue) * PowValue;
            const double MaxValue = Math::Floor(CurValue / PowValue + 1) * PowValue;
            Min = (Min > MinValue || i == 0)? MinValue : Min;
            Max = (Max < MaxValue || i == 0)? MaxValue : Max;
        }
        // 수집
        if(Min < Max)
        {
            Points Dots;
            Dots.AtAdding() = Point(1 * panel.w(), 1 * panel.h());
            Dots.AtAdding() = Point(1 * panel.w(), 1 * panel.h()); // 꺽임
            Dots.AtAdding() = Point(0 * panel.w(), 1 * panel.h());
            Dots.AtAdding() = Point(0 * panel.w(), 1 * panel.h()); // 꺽임
            for(sint32 i = 0, iend = Math::Max(2, GraphCount); i < iend; ++i) // GraphCount가 1이었을 경우 2로
            {
                const float CurValue = (*CurGraph)[Math::Min(i, GraphCount - 1)]; // GraphCount가 1이었을 경우 대응
                Dots.AtAdding() = Point(i * panel.w() / (iend - 1), panel.h() - (CurValue - Min) * panel.h() / (Max - Min));
                if(i == 0 || i == iend - 1)
                    Dots.AtAdding() = Dots[-1]; // 꺽임
            }
            ZAY_INNER_SCISSOR(panel, 0)
                panel.fillbezier(Dots);
            // 최대치와 최소치
            panel.text(String::Format(" %lf", Max), UIFA_LeftTop, UIFE_Right);
            ZAY_RGB(panel, 255, 255, 255)
                panel.text(String::Format(" %lf", Min), UIFA_LeftBottom, UIFE_Right);
        }
    }
    return true;
}

bool ZayAppData::RenderUC_Tag(ZayPanel& panel, chars posdom, chars text, sint32 linegap, sint32 lastgap, sint32 border, chars color, bool anticut)
{
    const String DomX = String::Format("%s.x", posdom);
    const String DomY = String::Format("%s.y", posdom);
    sint32 PosX = ZayWidgetDOM::GetValue(DomX).ToInteger();
    sint32 PosY = ZayWidgetDOM::GetValue(DomY).ToInteger();
    chars Text = text;
    const sint32 TextHeight = Platform::Graphics::GetStringHeight();
    while(true)
    {
        // 텍스트 자르기
        const sint32 ClippingWidth = Math::Max(0, panel.w() - PosX);
        sint32 TextLength = Platform::Graphics::GetLengthOfString(true, ClippingWidth, Text);
        if(TextLength == 0 && PosX == 0) // 들여쓴 첫줄이 아니면 최소글자수 보장
            TextLength = String::GetLengthOfFirstLetter(Text);

        // 회피조건
        const bool IsFirstRender = (Text == text);
        const bool IsLastRender = (Text[TextLength] == '\0');
        if(TextLength == 0 // 글자수가 없거나
        || (anticut && IsFirstRender && !IsLastRender && 0 < PosX)) // 첫줄 짤림방지
        {
            PosX = 0;
            PosY += TextHeight + linegap;
            continue;
        }
        const sint32 TextWidth = Platform::Graphics::GetStringWidth(Text, TextLength);

        // 배경
        ZAY_LTRB(panel, PosX - ((IsFirstRender)? border : 0), PosY - border, PosX + TextWidth + ((IsLastRender)? border : 0), PosY + TextHeight + border)
        {
            ZAY_LTRB(panel, (IsFirstRender)? 0 : 1, 0, panel.w() - ((IsLastRender)? 0 : 1), panel.h())
                panel.rect(1);
            ZAY_COLOR_CLEAR(panel)
            ZAY_COLOR(panel, color)
            {
                panel.fill();
                if(!IsFirstRender)
                ZAY_LTRB_SCISSOR(panel, -border, 0, 0, panel.h())
                ZAY_LTRB(panel, 0, 0, border * 2, panel.h())
                    panel.circle();
                if(!IsLastRender)
                ZAY_XYWH_SCISSOR(panel, panel.w(), 0, border, panel.h())
                ZAY_XYWH(panel, -border, 0, border * 2, panel.h())
                    panel.circle();
            }
        }

        // 텍스트
        panel.text(PosX, PosY, Text, TextLength, UIFA_LeftTop);
        if(IsLastRender)
        {
            PosX += TextWidth + lastgap;
            break;
        }
        else
        {
            PosX = 0;
            PosY += TextHeight + linegap;
            Text += TextLength;
        }
    }
    ZayWidgetDOM::SetValue(DomX, String::FromInteger(PosX));
    ZayWidgetDOM::SetValue(DomY, String::FromInteger(PosY));
    return true;
}

bool ZayAppData::RenderUC_Log(ZayPanel& panel, chars ch, sint32 index)
{
    auto& LogState = mLogStates(ch);
    if(0 <= index && index < LogState::MAX)
    {
        const String& CurText = LogState.mTexts[index];
        for(sint32 i = 0, iend = mLogHighlights.Count(); i < iend; ++i)
        {
            chararray GetColorCode;
            if(auto CurMatchText = mLogHighlights.AccessByOrder(i, &GetColorCode))
            {
                const sint32 FindPos = CurText.Find(0, *CurMatchText);
                if(FindPos != -1)
                {
                    const sint32 PosBegin = Platform::Graphics::GetStringWidth(CurText, FindPos);
                    const sint32 PosEnd = Platform::Graphics::GetStringWidth(CurText, FindPos + CurMatchText->Length());
                    const sint32 Height = panel.h();
                    ZAY_LTRB(panel, PosBegin, 0, PosEnd, Height)
                    ZAY_COLOR(panel, &GetColorCode[0], 0.25)
                    {
                        ZAY_LTRB_SCISSOR(panel, -Height / 2, 0, 0, Height)
                        ZAY_XYWH(panel, 0, 0, Height, Height)
                            panel.circle();
                        ZAY_XYWH_SCISSOR(panel, panel.w(), 0, Height / 2, Height)
                        ZAY_XYWH(panel, -Height / 2, 0, Height, Height)
                            panel.circle();
                        panel.fill();
                    }
                }
            }
        }
        ZAY_COLOR(panel, LogState.mColors[index])
            panel.text(CurText, UIFA_LeftMiddle, UIFE_Right);
        return true;
    }
    return false;
}

bool ZayAppData::RenderUC_Slider(ZayPanel& panel, chars domname, sint32 min, sint32 max, sint32 unit)
{
    if(min < max && 0 < unit)
    {
        const String DomName = domname;
        const String UIName = "ui_slider_" + DomName;
        ZAY_INNER_UI(panel, 0, UIName,
            ZAY_GESTURE_VNTXY(v, n, t, x, y, DomName, min, max, unit)
            {
                if(t == GT_Pressed || t == GT_InDragging || t == GT_OutDragging)
                {
                    auto UIRect = v->rect(n);
                    const String TextName = DomName + ".text";
                    sint32 ClampValue = Math::Clamp(min + (x - UIRect.l) * (max - min) / (UIRect.r - UIRect.l), min, max);
                    if(t == GT_Pressed && ZayWidgetDOM::ExistVariable(TextName) && 0 < ZayWidgetDOM::GetValue(TextName).ToText().Length())
                    {
                        const sint32 OldValue = ZayWidgetDOM::GetValue(TextName).ToFloat() * unit + 0.5;
                        if(OldValue < ClampValue) ClampValue = OldValue + 1;
                        else ClampValue = OldValue - 1;
                    }
                    String NewValue;
                    if(unit == 1)
                        NewValue = String::FromInteger(ClampValue);
                    else
                    {
                        sint32 DotNumber = ClampValue % unit;
                        while(0 < DotNumber && DotNumber % 10 == 0)
                            DotNumber /= 10;
                        if(DotNumber == 0) NewValue = String::FromInteger(ClampValue / unit);
                        else NewValue = String::Format("%d.%d", ClampValue / unit, DotNumber);
                    }
                    ZayWidgetDOM::SetComment(DomName, NewValue);
                    ZayWidgetDOM::SetValue(TextName, '\'' + NewValue + '\'');
                    v->clearCapture();
                    v->invalidate();
                }
            });
        return true;
    }
    return false;
}

bool ZayAppData::RenderUC_SlideVH(ZayPanel& panel, chars domname, sint32 min, sint32 limit, sint32 max, sint32 step, bool vertical, sint32 border, const String& gatename)
{
    if(min < max && 0 < step)
    {
        const String DomName = domname;
        const String UIName = "ui_slidevh_" + DomName;
        ZAY_INNER_UI(panel, 0, UIName,
            ZAY_GESTURE_VNTXY(v, n, t, x, y, this, DomName, min, limit, max, step, vertical, border, gatename)
            {
                if(t == GT_Pressed || t == GT_InDragging || t == GT_OutDragging)
                {
                    auto UIRect = v->rect(n);
                    const float OldValue = ZayWidgetDOM::ExistVariable(DomName + ".ani")?
                        ZayWidgetDOM::GetValue(DomName + ".ani").ToFloat() : ZayWidgetDOM::GetValue(DomName).ToFloat();
                    const sint32 NewValue = (vertical)? Math::Round((UIRect.b - border - y) * (max - min) / (float) (UIRect.b - UIRect.t - border * 2)) :
                        Math::Round((x - UIRect.l - border) * (max - min) / (float) (UIRect.r - UIRect.l - border * 2));
                    const sint32 ClampValue = Math::Clamp(min + (NewValue + step / 2) / step * step, limit, max);
                    ZayWidgetDOM::SetValue(DomName, String::FromInteger(ClampValue));
                    ZayWidgetDOM::SetValue(DomName + ".ani", String::Format("%f ~ %d : %f", OldValue, ClampValue, (t == GT_Pressed)? 0.3 : 0.1));
                    const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + ((t == GT_Pressed)? 300 : 100);
                    if(mUpdateMsec < NewUpdateMsec)
                        mUpdateMsec = NewUpdateMsec;
                    clearCapture();
                    invalidate(2);
                }
                else if(t == GT_InReleased || t == GT_OutReleased)
                {
                    if(gatename.Left(5) == "main:")
                        mMainWidget.JumpCallDirectly(gatename.Offset(5), n, nullptr);
                    else if(auto CurWidgetName = mLastWidgetName.Access(mLastProjCode))
                    if(auto CurSubWidget = mSubWidgets.Access(*CurWidgetName))
                        CurSubWidget->JumpCall(gatename, mLastProjCode);
                }
            });
        return true;
    }
    return false;
}

bool ZayAppData::RenderUC_Picker(ZayPanel& panel, chars domname, sint32 min, sint32 max, sint32 step, sint32 def, sint32 trunc, chars unit, sint32 side, chars imagename)
{
    if(max <= min || step <= 0 || side < 0)
        return false;
    auto CurDom = String(domname) + ".value";
    auto AniDom = String(domname) + ".ani";
    const float AniValue = (ZayWidgetDOM::ExistVariable(AniDom))? ZayWidgetDOM::GetValue(AniDom).ToFloat() : def;
    ZAY_INNER_UI(panel, 0, String::Format("ui_%s", domname),
        ZAY_GESTURE_VNTXY(v, n, t, x, y, this, CurDom, AniDom, AniValue, min, max, step)
        {
            static bool HasDrag;
            static sint32 PressX;
            float NewValue;
            sint32 NewAniTime = 0;
            if(t == GT_Pressed)
            {
                HasDrag = false;
                PressX = x;
            }
            else if(t == GT_InDragging || t == GT_OutDragging)
            {
                HasDrag = true;
                NewValue = Math::ClampF(AniValue + (PressX - x) * step, min - 2, max + 2);
                NewAniTime = 200;
                PressX = x;
            }
            else if(t == GT_InReleased || t == GT_OutReleased)
            {
                if(HasDrag)
                {
                    NewValue = Math::Clamp(Math::Round(AniValue / step) * step, min, max);
                    NewAniTime = 100;
                }
                else
                {
                    auto CurRect = v->rect(n);
                    if(PressX < (CurRect.l + CurRect.r) / 2)
                        NewValue = Math::Clamp((Math::Round(AniValue / step) - 1) * step, min, max);
                    else NewValue = Math::Clamp((Math::Round(AniValue / step) + 1) * step, min, max);
                    NewAniTime = 200;
                }
            }
            else if(t == GT_WheelUp || t == GT_WheelDown)
            {
                NewValue = Math::Clamp((Math::Round(AniValue / step) + ((t == GT_WheelUp)? -5 : 5)) * step, min, max);
                NewAniTime = 300;
            }
            if(0 < NewAniTime)
            {
                ZayWidgetDOM::SetValue(CurDom, String::FromInteger((sint32) Math::Round(NewValue)));
                ZayWidgetDOM::SetValue(AniDom, String::Format("%f ~ %f : %f", AniValue, NewValue, NewAniTime * 0.001));
                const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + NewAniTime;
                if(mUpdateMsec < NewUpdateMsec)
                    mUpdateMsec = NewUpdateMsec;
                clearCapture();
                invalidate(2);
            }
        })
    {
        const sint32 Middle = (sint32) Math::Round(AniValue / step);
        const sint32 SideMin = Math::Max(2, side);
        for(sint32 i = 0, iend = 1 + side * 2; i < iend; ++i)
        {
            const sint32 CurIndex = (i % 2)? Middle + (side - i / 2) : Middle - (side - i / 2);
            if(CurIndex * step < min || max < CurIndex * step)
                continue;
            const float CurPos = CurIndex - AniValue / step;
            const float AniScale = Math::Pow(Math::AbsF(CurPos / SideMin), 0.7);
            const float AniPos = AniScale * ((CurPos < 0)? -SideMin : SideMin);
            const float AniFont = Math::Pow(1 - AniScale, 0.5);
            const sint32 AniOpacity = Math::Max(0, 255 - 300 * AniScale);
            String CurText = String::FromInteger(CurIndex * step);
            if(trunc != 0)
            {
                const bool ZeroTrim = (trunc < 0)? false : true;
                const sint32 DotPos = Math::Log10(Math::Abs(trunc));
                const sint32 Value = CurIndex * step;
                String ValueText = String::FromInteger(Value);
                while(ValueText.Length() < DotPos + 1) ValueText = '0' + ValueText;
                String Text = ValueText.Left(ValueText.Length() - DotPos) + '.' + ValueText.Right(DotPos);
                if(ZeroTrim)
                {
                    while(Text[-2] == '0') Text.SubTail(1);
                    if(Text[-2] == '.') Text.SubTail(1);
                }
                CurText = Text;
            }
            ZAY_FONT(panel, AniFont)
            {
                const sint32 TextWidth = Platform::Graphics::GetStringWidth(CurText) + 20;
                const sint32 TextHeight = Platform::Graphics::GetStringHeight();
                ZAY_XYRR(panel, panel.w() * (SideMin + AniPos) / (SideMin * 2), panel.h() / 2, TextWidth / 2, TextHeight / 2)
                {
                    if(i == side * 2)
                    {
                        if(unit && unit[0] != '\0')
                        {
                            ZAY_MOVE(panel, 0, -10)
                            {
                                panel.ninepatch(R(imagename));
                                panel.text(CurText, UIFA_CenterMiddle);
                                ZAY_FONT(panel, 0.5)
                                ZAY_XYWH(panel, -100, panel.h(), panel.w() + 200, 40)
                                ZAY_RGBA(panel, 128, 128, 128, 64)
                                    panel.text(unit, UIFA_CenterTop);
                            }
                        }
                        else
                        {
                            panel.ninepatch(R(imagename));
                            panel.text(CurText, UIFA_CenterMiddle);
                        }
                    }
                    else ZAY_RGBA(panel, 128, 128, 128, AniOpacity)
                        panel.text(CurText, UIFA_CenterMiddle);
                }
            }
        }
    }
    return true;
}

bool ZayAppData::RenderUC_TouchCircle(ZayPanel& panel, sint32 x, sint32 y, sint32 width, sint32 height, chars domname, bool show, const Points& dots)
{
    static auto IsInsideCircle = [](sint32 x, sint32 y, sint32 width, sint32 height, sint32 ox, sint32 oy)->bool
        {
            double cx = x + width * 0.5;
            double cy = y + height * 0.5;
            double rx = width * 0.5;
            double ry = height * 0.5;
            double dx = ox - cx;
            double dy = oy - cy;
            double value = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
            return (value <= 1.0);
        };
    static auto FindNearestDot = [](const Points& dots, double ox, double oy)->sint32
        {
            int index = -1;
            double bestSq = 0;
            for(int i = 0, iend = dots.Count(); i < iend; ++i)
            {
                double dx = dots[i].x - ox;
                double dy = dots[i].y - oy;
                double distSq = dx * dx + dy * dy;
                if(i == 0 || bestSq > distSq)
                {
                    index = i;
                    bestSq = distSq;
                }
            }
            return index;
        };

    if(show)
    {
        const Color Colors[] = {
            Color(255,   0,   0, 64), //  1 Red
            Color(255, 128,   0, 64), //  2 Orange
            Color(255, 215,   0, 64), //  3 Gold
            Color(128, 200,   0, 64), //  4 Lime
            Color(  0, 180,   0, 64), //  5 Green
            Color(  0, 200, 120, 64), //  6 Emerald
            Color(  0, 200, 200, 64), //  7 Cyan
            Color(  0, 128, 255, 64), //  8 Sky Blue
            Color(  0,   0, 255, 64), //  9 Blue
            Color(100,   0, 255, 64), // 10 Indigo
            Color(180,   0, 255, 64), // 11 Purple
            Color(255,   0, 255, 64), // 12 Magenta
            Color(255,   0, 128, 64), // 13 Pink
            Color(160,  60,  20, 64), // 14 Brown
            Color(255, 100, 100, 64), // 15 Coral
            Color(180, 140,  60, 64), // 16 Ochre
            Color(100, 180, 100, 64), // 17 Soft Green
            Color( 80, 140, 180, 64), // 18 Steel Blue
            Color(130, 100, 200, 64), // 19 Lavender
            Color(180,  80, 140, 64)}; // 20 Rose
        for(sint32 i = y, iend = y + height; i < iend; i += 6)
        for(sint32 j = x, jend = x + width; j < jend; j += 6)
        {
            if(IsInsideCircle(x, y, width, height, j, i))
            {
                const sint32 Index = Math::Clamp(FindNearestDot(dots, j, i), 0,
                    sizeof(Colors) / sizeof(Colors[0]) - 1);
                ZAY_XYRR(panel, j, i, 3, 3)
                ZAY_COLOR(panel, Colors[Index])
                    panel.circle();
            }
        }
    }

    const String DomName = domname;
    ZAY_XYWH_UI(panel, x, y, width, height, "ui_touchcircle_" + DomName,
        ZAY_GESTURE_VNTXY(v, n, t, px, py, x, y, dots, DomName)
        {
            if(t == GT_Pressed || t == GT_InDragging)
            {
                auto CurRect = v->rect(n);
                if(IsInsideCircle(CurRect.l, CurRect.t, CurRect.r - CurRect.l, CurRect.b - CurRect.t, px, py))
                {
                    const String OldNumber = ZayWidgetDOM::GetValue(DomName).ToText();
                    const String NewNumber = String::FromInteger(FindNearestDot(dots, x + px - CurRect.l, y + py - CurRect.t));
                    if(OldNumber != NewNumber)
                    {
                        ZayWidgetDOM::SetValue(DomName, NewNumber);
                        v->invalidate();
                    }
                }
            }
        });
    return true;
}

bool ZayAppData::RenderUC_ZoomArea(ZayPanel& panel, chars domname)
{
    const String DomName = domname;
    ZAY_INNER_UI(panel, 0, "ui_zoomarea_" + DomName,
        ZAY_GESTURE_VNTXY(v, n, t, x, y, this, DomName)
        {
            auto& State = mNativeState->zooms[(chars) DomName];
            // Gesture coordinates are in view pixels; camera DOM uses local panel units.
            const float InputZoom = v->zoom(n);
            if(t == GT_Pressed)
            {
                State.Press(x, y, ZayWidgetDOM::GetValue(DomName + ".x").ToFloat(),
                    ZayWidgetDOM::GetValue(DomName + ".y").ToFloat());
                v->clearCapture();
            }
            else if(t == GT_InDragging || t == GT_OutDragging)
            {
                if(State.Drag(x,y))
                {
                    ZayWidgetDOM::SetValue(DomName + ".x", String::FromFloat(State.zoomX + (x - State.pressX) / InputZoom));
                    ZayWidgetDOM::SetValue(DomName + ".y", String::FromFloat(State.zoomY + (y - State.pressY) / InputZoom));
                    v->invalidate();
                }
            }
            else if(t == GT_InReleased || t == GT_OutReleased || t == GT_CancelReleased)
            {
                if(State.Release(x,y,t == GT_InReleased))
                {
                    const auto Area = v->rect(n);
                    ZayWidgetDOM::SetValue(DomName + ".click.x", String::FromFloat((x - Area.l) / InputZoom));
                    ZayWidgetDOM::SetValue(DomName + ".click.y", String::FromFloat((y - Area.t) / InputZoom));
                    ZayWidgetDOM::SetValue(DomName + ".click.seq", String::FromInteger(
                        ZayWidgetDOM::GetValue(DomName + ".click.seq").ToInteger() + 1));
                    v->invalidate();
                }
            }
            else if(t == GT_WheelUp || t == GT_WheelDown)
            {
                State.pressed=false;
                const float Speed = 0.05;
                const auto CurRect = v->rect(n);
                const float CursorX = (x - (CurRect.l + CurRect.r) / 2) / InputZoom;
                const float CursorY = (y - (CurRect.t + CurRect.b) / 2) / InputZoom;
                const float CurZoomX = ZayWidgetDOM::GetValue(DomName + ".x").ToFloat();
                const float CurZoomY = ZayWidgetDOM::GetValue(DomName + ".y").ToFloat();
                const sint32 OldZoomW = ZayWidgetDOM::GetValue(DomName + ".w").ToInteger();
                sint32 NewZoomW = OldZoomW + ((t == GT_WheelUp)? -1 : 1);
                if(ZayWidgetDOM::ExistVariable(DomName + ".min_w"))
                    NewZoomW = Math::Max(NewZoomW, ZayWidgetDOM::GetValue(DomName + ".min_w").ToInteger());
                if(ZayWidgetDOM::ExistVariable(DomName + ".max_w"))
                    NewZoomW = Math::Min(NewZoomW, ZayWidgetDOM::GetValue(DomName + ".max_w").ToInteger());
                const float OldZoomV = (0 < OldZoomW)? 100 * Math::Pow(1.0 + Speed, OldZoomW) : 100 / (1.0 - Speed * OldZoomW);
                const float NewZoomV = (0 < NewZoomW)? 100 * Math::Pow(1.0 + Speed, NewZoomW) : 100 / (1.0 - Speed * NewZoomW);
                ZayWidgetDOM::SetValue(DomName + ".x", String::FromFloat(CursorX + (CurZoomX - CursorX) * NewZoomV / OldZoomV));
                ZayWidgetDOM::SetValue(DomName + ".y", String::FromFloat(CursorY + (CurZoomY - CursorY) * NewZoomV / OldZoomV));
                ZayWidgetDOM::SetValue(DomName + ".w", String::FromInteger(NewZoomW));
                ZayWidgetDOM::SetValue(DomName + ".v", String::FromFloat(NewZoomV));
                v->invalidate();
            }
        });
    return true;
}

bool ZayAppData::RenderUC_DragArea(ZayPanel& panel, chars domname)
{
    const String DomName = domname;
    ZAY_INNER_UI(panel, 0, "ui_dragarea_" + DomName,
        ZAY_GESTURE_VNTXY(v, n, t, x, y, this, DomName)
        {
            static bool HasDrag;
            static sint32 PressX, PressY;
            if(t == GT_Pressed)
            {
                HasDrag = false;
                PressX = x;
                PressY = y;
                ZayWidgetDOM::SetValue(DomName + ".state", "'clicked'");
                ZayWidgetDOM::SetValue(DomName + ".click.x", String::FromInteger(x));
                ZayWidgetDOM::SetValue(DomName + ".click.y", String::FromInteger(y));
                v->clearCapture();
            }
            else if(t == GT_InDragging || t == GT_OutDragging)
            {
                if(HasDrag || 20 < Math::Distance(PressX, PressY, x, y))
                {
                    HasDrag = true;
                    ZayWidgetDOM::SetValue(DomName + ".state", "'dragging'");
                    ZayWidgetDOM::SetValue(DomName + ".drag.x", String::FromInteger(x));
                    ZayWidgetDOM::SetValue(DomName + ".drag.y", String::FromInteger(y));
                    v->invalidate();
                }
            }
            else if(t == GT_InReleased || t == GT_OutReleased || t == GT_CancelReleased)
            {
                if(HasDrag)
                {
                    HasDrag = false;
                    if(t == GT_OutReleased)
                        ZayWidgetDOM::SetValue(DomName + ".state", "'dropped'");
                    else ZayWidgetDOM::SetValue(DomName + ".state", "'dropcanceled'");
                }
            }
        });
    return true;
}

void ZayAppData::UpdateChannelList()
{
    const Context& ChJson = mChannel.EnumCh();
    ZayWidgetDOM::RemoveVariables("channel.");
    ZayWidgetDOM::SetJson(ChJson, "channel.");
}

void ZayAppData::UpdateDeviceList()
{
    ZayWidgetDOM::RemoveVariables("device.");
    for(sint32 i = 0, iend = mDevices.Count(); i < iend; ++i)
    {
        chararray GetName;
        if(auto CurCh = mDevices.AccessByOrder(i, &GetName))
        {
            ZayWidgetDOM::SetValue(String::Format("device.%d.name", i), String::Format("'%s'", &GetName[0]));
            ZayWidgetDOM::SetValue(String::Format("device.%d.ch", i), '\'' + *CurCh + '\'');
        }
    }
    ZayWidgetDOM::SetValue("device.count", String::FromInteger(mDevices.Count()));
}

void ZayAppData::UpdateStorageList()
{
    ZayWidgetDOM::RemoveVariables("storage.");
    for(sint32 i = 0, iend = mStorages.Count(); i < iend; ++i)
    {
        chararray GetName;
        if(auto CurSpace = mStorages.AccessByOrder(i, &GetName))
        {
            ZayWidgetDOM::SetValue(String::Format("storage.%d.path", i), String::Format("'%s'", &GetName[0]));
            ZayWidgetDOM::SetValue(String::Format("storage.%d.free", i), String::FromInteger(CurSpace->mFree));
            ZayWidgetDOM::SetValue(String::Format("storage.%d.total", i), String::FromInteger(CurSpace->mTotal));
        }
    }
    ZayWidgetDOM::SetValue("storage.count", String::FromInteger(mStorages.Count()));
}

void ZayAppData::UpdateNetworkList()
{
    ZayWidgetDOM::RemoveVariables("wifi.");
    for(sint32 i = 0, iend = mWifis.Count(); i < iend; ++i)
    {
        chararray GetName;
        if(auto CurWifi = mWifis.AccessByOrder(i, &GetName))
        {
            ZayWidgetDOM::SetValue(String::Format("wifi.%d.ssid", i), String::Format("'%s'", &GetName[0]));
            ZayWidgetDOM::SetValue(String::Format("wifi.%d.connected", i), (CurWifi->mConnected)? "1" : "0");
            ZayWidgetDOM::SetValue(String::Format("wifi.%d.signal", i), String::FromInteger(CurWifi->mSignal));
            ZayWidgetDOM::SetValue(String::Format("wifi.%d.security", i), '\'' + CurWifi->mSecurity + '\'');
            ZayWidgetDOM::SetValue(String::Format("wifi.%d.ip", i), '\'' + CurWifi->mIp + '\'');
        }
    }
    ZayWidgetDOM::SetValue("wifi.count", String::FromInteger(mWifis.Count()));
}

void ZayAppData::UpdateSoundDeviceList(sint32 focus)
{
    String SoundSpec;
    Platform::Sound::GetAllNames(&SoundSpec);
    Context SoundJson(ST_Json, SO_OnlyReference, SoundSpec);
    if(focus != -1)
    {
        mSoundDeviceFocus = focus;
        for(sint32 i = 0, iend = SoundJson.LengthOfIndexable(); i < iend; ++i)
            SoundJson.At(i).At("is_default").Set((i == focus)? "1" : "0");
    }
    ZayWidgetDOM::RemoveVariables("sounddevice.");
    ZayWidgetDOM::SetJson(SoundJson, "sounddevice.");
}

void ZayAppData::UpdatePackageList()
{
    if(mPackageList)
    {
        Tasking::Release(mPackageList, true);
        mPackageList = nullptr;
    }
    Strings DataDirs;
    DataDirs.AtAdding() = Platform::File::RootForData() + "update/";
    for(sint32 i = 0, iend = ZayWidgetDOM::GetValue("storage.count").ToInteger(); i < iend; ++i)
        DataDirs.AtAdding() = ZayWidgetDOM::GetValue(String::Format("storage.%d.path", i)).ToText();

    mPackageList = Tasking::Create([](buffer& self, Queue<buffer>& query, Queue<buffer>& answer, id_common common)->sint32
    {
        static Strings DataDirs;
        DataDirs = Strings((id_cloned_share_read) Share::Create(self));
        self = nullptr;
        static Queue<buffer>* PtrAnswer;
        PtrAnswer = &answer;
        static id_common* PtrCommon;
        PtrCommon = &common;
        PtrAnswer->Enqueue(String("PackageSearchBegin").ToBuffer());

        Context WpkCollector;
        for(sint32 i = 0, iend = DataDirs.Count(); i < iend; ++i)
        {
            static sint32 DataRootFocus;
            DataRootFocus = i;
            Platform::File::Search(DataDirs[i], [](chars itemname, payload data)->void
            {
                if(Tasking::IsReleased(*PtrCommon))
                    return;
                const String ItemName(itemname);
                if(!ItemName.Right(4).CompareNoCase(".wpk"))
                {
                    if(auto OldFile = Platform::File::OpenForRead(DataDirs[DataRootFocus] + ItemName))
                    {
                        // AddOn::Zip::OpenForFD방식은 라즈베리에서 실패함
                        auto FileSize = Platform::File::Size(OldFile);
                        buffer NewBuffer = Buffer::Alloc(BOSS_DBG FileSize);
                        Platform::File::Read(OldFile, (uint08*) NewBuffer, FileSize);
                        Platform::File::Close(OldFile);

                        auto& WpkCollector = *((Context*) data);
                        sint32 FileCount = 0;
                        if(id_zip NewZip = AddOn::Zip::OpenForBuffer((bytes) NewBuffer, Buffer::CountOf(NewBuffer), &FileCount, "03417"))
                        {
                            // 매니페스트
                            String IconSrcName, IconDstName;
                            for(sint32 i = 0; i < FileCount && !Tasking::IsReleased(*PtrCommon); ++i)
                            {
                                const String FileName = AddOn::Zip::GetFileInfo(NewZip, i);
                                if(!FileName.CompareNoCase("manifest.json"))
                                {
                                    if(buffer ManifestBuffer = AddOn::Zip::ToBuffer(NewZip, i))
                                    {
                                        Context ManifestJson(ST_Json, SO_OnlyReference, (chars) ManifestBuffer);
                                        if(auto CurDate = ManifestJson("date").GetText())
                                        {
                                            auto& NewWpk = WpkCollector.At(CurDate);
                                            NewWpk.At("title").Set(ManifestJson("title").GetText());
                                            NewWpk.At("date").Set(CurDate);
                                            NewWpk.At("path").Set(DataDirs[DataRootFocus] + ItemName);
                                            // 대표버전
                                            String CurVersion = ManifestJson("version").GetText();
                                            if(!CurVersion) CurVersion = ManifestJson("contents")("app_res")("version").GetText();
                                            NewWpk.At("version").Set(CurVersion);
                                            // 아이콘
                                            if(IconSrcName = ManifestJson("icon").GetText())
                                            {
                                                // 리소스명
                                                const String IconPkgName = "package/icon_" + CurDate;
                                                NewWpk.At("icon").Set("r." + String(IconPkgName).Replace('/', '.'));
                                                // 파일명
                                                IconDstName = "image/" + IconPkgName;
                                                const sint32 DotPos = IconSrcName.Find(0, ".");
                                                if(DotPos != -1) IconDstName += IconSrcName.Offset(DotPos);
                                            }
                                            // 컨텐츠
                                            for(sint32 j = 0, jend = ManifestJson("contents").LengthOfNamable(); j < jend; ++j)
                                            {
                                                chararray GetName;
                                                auto& SrcContent = ManifestJson("contents")(j, &GetName);
                                                #if BOSS_WINDOWS
                                                    if(String::Compare(&GetName[0], "app_rb") != 0)
                                                #elif BOSS_LINUX
                                                    if(String::Compare(&GetName[0], "app_win") != 0)
                                                #endif
                                                {
                                                    auto& DstContent = NewWpk.At("contents").AtAdding();
                                                    DstContent.At("type").Set(&GetName[0]);
                                                    DstContent.At("name").Set(SrcContent("name").GetText());
                                                    DstContent.At("buildcode").Set(SrcContent("buildcode").GetText());
                                                    DstContent.At("size").Set(SrcContent("size").GetText());
                                                    DstContent.At("hash").Set(SrcContent("hash").GetText());
                                                }
                                            }
                                        }
                                        Buffer::Free(ManifestBuffer);
                                    }
                                    break;
                                }
                            }
                            // 아이콘 저장
                            if(0 < IconSrcName.Length() && !Asset::Exist(IconDstName))
                            for(sint32 i = 0; i < FileCount && !Tasking::IsReleased(*PtrCommon); ++i)
                            {
                                const String FileName = AddOn::Zip::GetFileInfo(NewZip, i);
                                if(!FileName.CompareNoCase(IconSrcName))
                                {
                                    buffer IconBuffer = AddOn::Zip::ToBuffer(NewZip, i);
                                    if(auto NewAsset = Asset::OpenForWrite(IconDstName, true))
                                    {
                                        Asset::Write(NewAsset, (bytes) IconBuffer, Buffer::CountOf(IconBuffer));
                                        Asset::Close(NewAsset);
                                    }
                                    Buffer::Free(IconBuffer);
                                }
                            }
                            AddOn::Zip::Close(NewZip);
                        }
                        Buffer::Free(NewBuffer);

                        // 큐전달
                        if(!Tasking::IsReleased(*PtrCommon))
                        {
                            Context SortedWpkJson;
                            for(sint32 i = 0, iend = WpkCollector.LengthOfNamable(); i < iend; ++i)
                            {
                                chararray GetDate;
                                SortedWpkJson.At(iend - 1 - i) = WpkCollector(i, &GetDate);
                            }
                            PtrAnswer->Enqueue(SortedWpkJson.SaveJson().ToBuffer());
                        }
                    }
                }
            }, &WpkCollector, false, true);
        }
        PtrAnswer->Enqueue(String("PackageSearchEnd").ToBuffer());
        return -1;
    }, ((Share*)(id_share) DataDirs)->CopiedBuffer());
}

void ZayAppData::UpdateIPAddress()
{
    if(mIPAddress) return;
    mIPAddress = Tasking::Create([](buffer& self, Queue<buffer>& query, Queue<buffer>& answer, id_common common)->sint32
    {
        static sint32 LocalCount = 10;
        if(0 < LocalCount)
        {
            auto LocalIP = Platform::Socket::GetLocalAddress();
            if(LocalIP.ip[0] == 127 && LocalIP.ip[1] == 0 && LocalIP.ip[2] == 0 && LocalIP.ip[3] == 1 && 0 < LocalCount--)
                return 500;
            ZayWidgetDOM::SetValue("program.ip.local", String::Format("'%d.%d.%d.%d'", LocalIP.ip[0], LocalIP.ip[1], LocalIP.ip[2], LocalIP.ip[3]));
            LocalCount = 0;
        }
        static sint32 PublicCount = 10;
        if(0 < PublicCount)
        {
            auto PublicIP = Platform::Socket::GetPublicAddress();
            if(PublicIP.ip[0] == 127 && PublicIP.ip[1] == 0 && PublicIP.ip[2] == 0 && PublicIP.ip[3] == 1 && 0 < PublicCount--)
                return 500;
            ZayWidgetDOM::SetValue("program.ip.public", String::Format("'%d.%d.%d.%d'", PublicIP.ip[0], PublicIP.ip[1], PublicIP.ip[2], PublicIP.ip[3]));
            PublicCount = 0;
        }
        return -1;
    });
}

void ZayAppData::RunInstallWpk(chars wpkpath, chars exclusions)
{
    if(mInstallWpk)
    {
        Tasking::Release(mInstallWpk, true);
        mInstallWpk = nullptr;
    }
    if(exclusions && exclusions[0])
    {
        const String WpkPathAndExclusions = String::Format("%s|%s", wpkpath, exclusions);
        mInstallWpk = Tasking::Create([](buffer& self, Queue<buffer>& query, Queue<buffer>& answer, id_common common)->sint32
        {
            const String WpkPathAndExclusions = String::FromBuffer(self);
            self = nullptr;
            static id_common* PtrCommon;
            PtrCommon = &common;

            const Strings Args = String::Split(WpkPathAndExclusions, '|');
            const String WpkPath = Args[0];
            Map<bool> Exclusions;
            for(sint32 i = 1, iend = Args.Count(); i < iend; ++i)
                Exclusions(Args[i]) = true;

            auto CopyToNewFolder = [](id_zip zip, sint32 filecount, const String& dir, Map<String>& collector)->void
            {
                for(sint32 i = 0; i < filecount && !Tasking::IsReleased(*PtrCommon); ++i)
                {
                    const String FileName = String(AddOn::Zip::GetFileInfo(zip, i)).Replace('\\', '/');
                    if(!String::CompareNoCase(FileName, dir, dir.Length()))
                    {
                        // 최상위 폴더가 있다면 폴더명에 "-new"을 추가
                        const sint32 SlashPos = FileName.Find(dir.Length(), "/");
                        const String DstFileName = (SlashPos != -1)? FileName.Left(SlashPos) + "-new" + FileName.Offset(SlashPos) : FileName;
                        // 최상위 폴더명을 수집
                        if(SlashPos != -1)
                        {
                            const String TopFolderName = FileName.Left(SlashPos).Offset(dir.Length());
                            collector(TopFolderName) = TopFolderName;
                        }
                        // 파일 생성
                        buffer FileBuffer = AddOn::Zip::ToBuffer(zip, i);
                        if(auto NewFile = Platform::File::OpenForWrite(Platform::File::RootForAssetsRem() + "../" + DstFileName.Offset(dir.Length()), true))
                        {
                            Platform::File::Write(NewFile, (bytes) FileBuffer, Buffer::CountOf(FileBuffer));
                            Platform::File::Close(NewFile);
                        }
                        Buffer::Free(FileBuffer);
                    }
                }
            };
            auto CopyToFwFile = [](id_zip zip, sint32 filecount, const String& filename, uint32& filesize, uint32& filecrc)->String
            {
                for(sint32 i = 0; i < filecount && !Tasking::IsReleased(*PtrCommon); ++i)
                {
                    const String FileName = String(AddOn::Zip::GetFileInfo(zip, i)).Replace('\\', '/');
                    if(!String::CompareNoCase(FileName, filename))
                    {
                        // 파일 생성
                        buffer FileBuffer = AddOn::Zip::ToBuffer(zip, i);
                        filesize = Buffer::CountOf(FileBuffer);
                        filecrc = UpdateCrc((bytes) FileBuffer, filesize);
                        if(auto NewFile = Platform::File::OpenForWrite(Platform::File::RootForAssetsRem() + "../fwfile/data.bin", true))
                        {
                            Platform::File::Write(NewFile, (bytes) FileBuffer, filesize);
                            Platform::File::Close(NewFile);
                        }
                        Buffer::Free(FileBuffer);
                        return "fwfile/data.bin";
                    }
                }
                return String();
            };

            if(auto OldFile = Platform::File::OpenForRead(WpkPath))
            {
                // AddOn::Zip::OpenForFD방식은 라즈베리에서 실패함
                auto FileSize = Platform::File::Size(OldFile);
                buffer NewBuffer = Buffer::Alloc(BOSS_DBG FileSize);
                Platform::File::Read(OldFile, (uint08*) NewBuffer, FileSize);
                Platform::File::Close(OldFile);

                sint32 FileCount = 0;
                if(id_zip NewZip = AddOn::Zip::OpenForBuffer((bytes) NewBuffer, Buffer::CountOf(NewBuffer), &FileCount, "03417"))
                {
                    // 매니페스트
                    for(sint32 i = 0; i < FileCount && !Tasking::IsReleased(*PtrCommon); ++i)
                    {
                        const String FileName = AddOn::Zip::GetFileInfo(NewZip, i);
                        if(!FileName.CompareNoCase("manifest.json"))
                        {
                            if(buffer ManifestBuffer = AddOn::Zip::ToBuffer(NewZip, i))
                            {
                                Context ManifestJson(ST_Json, SO_OnlyReference, (chars) ManifestBuffer);

                                // 펌웨어 인스톨
                                for(sint32 j = 0, jend = ManifestJson("contents").LengthOfNamable(); j < jend && !Tasking::IsReleased(*PtrCommon); ++j)
                                {
                                    chararray GetName;
                                    auto& CurContent = ManifestJson("contents")(j, &GetName);
                                    // "fw_"로 시작하는 모든 FW컨텐츠를 패킷전송 및 FW재실행
                                    if(!String::Compare(&GetName[0], "fw_", 3))
                                    if(Exclusions.Access(&GetName[0]))
                                    if(auto FwFile = CurContent("file").GetText())
                                    {
                                        uint32 FileSize = 0, FileCrc = 0;
                                        if(auto FilePath = CopyToFwFile(NewZip, FileCount, FwFile, FileSize, FileCrc))
                                        {
                                            // 메인스레드로 처리이관
                                            auto FwCmd = CurContent("cmd").GetText();
                                            auto FwAck = CurContent("ack").GetText();
                                            answer.Enqueue(String::Format("BootLoaderStart:%s:%s:%u:%u:%s:%s", &GetName[0], (chars) FilePath, FileSize, FileCrc, (chars) FwCmd, (chars) FwAck).ToBuffer());
                                            while(!Tasking::IsReleased(*PtrCommon))
                                            {
                                                if(buffer CurQuery = query.Dequeue())
                                                if(!String::FromBuffer(CurQuery).Compare("BootLoaderEnd"))
                                                    break;
                                                Platform::Utility::Sleep(100, false, true, false);
                                            }
                                        }
                                    }
                                }

                                // 앱 인스톨
                                if(!Tasking::IsReleased(*PtrCommon))
                                {
                                    Map<String> FolderCollector;
                                    bool DoResChange = false;
                                    bool DoAppChange = false;

                                    // assets-new에 리소스 복사
                                    if(Exclusions.Access("app_res"))
                                    if(auto AppResDir = ManifestJson("contents")("app_res")("folder").GetText())
                                    {
                                        AppResDir += '/';
                                        CopyToNewFolder(NewZip, FileCount, AppResDir, FolderCollector);
                                        DoResChange = true;
                                    }

                                    // bin_Release64-new(bin_release-new)에 SW컨텐츠 복사
                                    #if BOSS_WINDOWS
                                        if(Exclusions.Access("app_win"))
                                        if(auto AppExeDir = ManifestJson("contents")("app_win")("folder").GetText())
                                    #elif BOSS_LINUX
                                        if(Exclusions.Access("app_rb"))
                                        if(auto AppExeDir = ManifestJson("contents")("app_rb")("folder").GetText())
                                    #else
                                        if(auto AppExeDir = String())
                                    #endif
                                    {
                                        AppExeDir += '/';
                                        CopyToNewFolder(NewZip, FileCount, AppExeDir, FolderCollector);
                                        DoAppChange = true;
                                    }

                                    // assets-old와 bin_Release64-old(bin_release-old)등이 있다면 삭제
                                    const String ProgramFolder = boss_normalpath(Platform::File::RootForAssetsRem() + "..", nullptr);
                                    for(sint32 i = 0, iend = FolderCollector.Count(); i < iend; ++i)
                                    {
                                        chararray GetFolder;
                                        if(FolderCollector.AccessByOrder(i, &GetFolder))
                                            ForcedRemove(ProgramFolder + "/" + &GetFolder[0] + "-old");
                                    }

                                    if(DoResChange || DoAppChange)
                                    {
                                        #if BOSS_WINDOWS
                                            // rollback.bat를 작성 및 저장
                                            String Rollback;
                                            Rollback += "icacls \"" + ProgramFolder + "\" /grant %USERNAME%:F\r\n";
                                            Rollback += "cd /d \"" + ProgramFolder + "\"\r\n\r\n";
                                            for(sint32 i = 0, iend = FolderCollector.Count(); i < iend; ++i)
                                            {
                                                chararray GetFolder;
                                                if(FolderCollector.AccessByOrder(i, &GetFolder))
                                                {
                                                    const String CurFolder = &GetFolder[0];
                                                    // Cur-old가 있다면 Cur를 삭제
                                                    Rollback += "if exist \"" + CurFolder + "-old\" rmdir /s /q \"" + CurFolder + "\"\r\n";
                                                    // Cur-old를 Cur로
                                                    Rollback += "ren \"" + CurFolder + "-old\" \"" + CurFolder + "\"\r\n";
                                                }
                                            }
                                            Rollback.ToFile(ProgramFolder + "/rollback.bat");

                                            // install.bat를 작성
                                            String Install;
                                            Install += "icacls \"" + ProgramFolder + "\" /grant %USERNAME%:F\r\n";
                                            Install += "cd /d \"" + ProgramFolder + "\"\r\n\r\n";
                                            for(sint32 i = 0, iend = FolderCollector.Count(); i < iend; ++i)
                                            {
                                                chararray GetFolder;
                                                if(FolderCollector.AccessByOrder(i, &GetFolder))
                                                {
                                                    const String CurFolder = &GetFolder[0];
                                                    // Cur를 Cur-old로
                                                    Install += ":retry_rename_" + CurFolder + "_old\r\n";
                                                    Install += "ren \"" + CurFolder + "\" \"" + CurFolder + "-old\" >nul 2>nul\r\n";
                                                    Install += "if errorlevel 1 (\r\n";
                                                    Install += "    timeout /t 1 /nobreak >nul\r\n";
                                                    Install += "    goto retry_rename_" + CurFolder + "_old\r\n";
                                                    Install += ")\r\n\r\n";
                                                    // Cur-new를 Cur로
                                                    Install += ":retry_rename_" + CurFolder + "_new\r\n";
                                                    Install += "ren \"" + CurFolder + "-new\" \"" + CurFolder + "\" >nul 2>nul\r\n";
                                                    Install += "if errorlevel 1 (\r\n";
                                                    Install += "    timeout /t 1 /nobreak >nul\r\n";
                                                    Install += "    goto retry_rename_" + CurFolder + "_new\r\n";
                                                    Install += ")\r\n\r\n";
                                                }
                                            }
                                            Install += "start \"\" \"bin_Release64/zayapp.exe\"\r\n";

                                            // 인스톨 실행 및 프로그램 종료
                                            const String InstallPath = ProgramFolder + "/install.bat";
                                            if(Install.ToFile(InstallPath))
                                            {
                                                Platform::Popup::OpenProgramDialog(InstallPath, nullptr, false, true);
                                                Platform::Utility::ExitProgram();
                                            }
                                        #elif BOSS_LINUX
                                            // rollback.sh를 작성 및 저장
                                            String Rollback;
                                            Rollback += "#!/bin/sh\n";
                                            Rollback += "cd \"" + ProgramFolder.Offset(2) + "\" || exit 1\n\n";
                                            for(sint32 i = 0, iend = FolderCollector.Count(); i < iend; ++i)
                                            {
                                                chararray GetFolder;
                                                if(FolderCollector.AccessByOrder(i, &GetFolder))
                                                {
                                                    const String CurFolder = &GetFolder[0];
                                                    // Cur-old가 있다면 Cur를 삭제
                                                    Rollback += "if [ -d \"" + CurFolder + "-old\" ]; then\n";
                                                    Rollback += "    rm -rf \"" + CurFolder + "\"\n";
                                                    Rollback += "fi\n";
                                                    // Cur-old를 Cur로
                                                    Rollback += "mv \"" + CurFolder + "-old\" \"" + CurFolder + "\"\n";
                                                    Rollback += "chmod -R u+rwX \"" + CurFolder + "\" 2>/dev/null\n\n";
                                                }
                                            }
                                            Rollback += "chmod -R u+rwX . 2>/dev/null\n";
                                            Rollback += "chmod +x ./bin_release/zayapp\n";
                                            Rollback += "chmod +x ./bin_release/zayapp.sh\n";
                                            Rollback.ToFile(ProgramFolder + "/rollback.sh");
                                            Platform::Popup::OpenProgramDialog("chmod", "+x " + ProgramFolder.Offset(2) + "/rollback.sh", false, true);

                                            // install.sh를 작성
                                            String Install;
                                            Install += "#!/bin/sh\n";
                                            Install += "cd \"" + ProgramFolder.Offset(2) + "\" || exit 1\n";
                                            for(sint32 i = 0, iend = FolderCollector.Count(); i < iend; ++i)
                                            {
                                                chararray GetFolder;
                                                if(FolderCollector.AccessByOrder(i, &GetFolder))
                                                {
                                                    const String CurFolder = &GetFolder[0];
                                                    // Cur를 Cur-old로
                                                    Install += "if [ -d \"" + CurFolder + "\" ]; then\n";
                                                    Install += "    while ! mv \"" + CurFolder + "\" \"" + CurFolder + "-old\" 2>/dev/null; do\n";
                                                    Install += "        sleep 1\n";
                                                    Install += "    done\n";
                                                    Install += "fi\n\n";
                                                    // Cur-new를 Cur로
                                                    Install += "while ! mv \"" + CurFolder + "-new\" \"" + CurFolder + "\" 2>/dev/null; do\n";
                                                    Install += "    sleep 1\n";
                                                    Install += "done\n";
                                                    Install += "chmod -R u+rwX \"" + CurFolder + "\" 2>/dev/null\n\n";
                                                }
                                            }
                                            Install += "chmod -R u+rwX . 2>/dev/null\n\n";
                                            Install += "chmod +x ./bin_release/zayapp\n";
                                            Install += "chmod +x ./bin_release/zayapp.sh\n";
                                            Install += "sync\n";
                                            Install += "sleep 1\n";
                                            Install += "sudo systemctl restart zayapp\n";

                                            // 인스톨 실행 및 프로그램 종료
                                            const String InstallPath = ProgramFolder + "/install.sh";
                                            if(Install.ToFile(InstallPath))
                                            {
                                                Platform::Popup::OpenProgramDialog("chmod", "+x " + InstallPath.Offset(2), false, true);
                                                Platform::Popup::OpenProgramDialog(InstallPath.Offset(2), nullptr, false, true);
                                                Platform::Utility::ExitProgram();
                                            }
                                        #endif
                                    }
                                }
                                Buffer::Free(ManifestBuffer);
                            }
                            break;
                        }
                    }
                    AddOn::Zip::Close(NewZip);
                }
                Buffer::Free(NewBuffer);
            }
            return -1;
        }, WpkPathAndExclusions.ToBuffer());
    }
}

void ZayAppData::AddLog(chars ch, const String& text, const String& color)
{
    const uint64 CurMsec = Platform::Utility::CurrentTimeMsec();
    auto& LogState = mLogStates(ch);
    LogState.mNumber = (LogState.mNumber + 1) % 1000000;
    LogState.mCount = Math::Min(LogState.mCount + 1, LogState::MAX);
    LogState.mOffset = (LogState.mOffset + 1) % LogState.mCount;
    LogState.mTexts[LogState.mOffset] = String::Format("[%06d:%03d.%03ds] %s",
        LogState.mNumber, (sint32) ((CurMsec / 1000) % 1000), (sint32) (CurMsec % 1000), (chars) text);
    LogState.mColors[LogState.mOffset] = Color(color);

    const String LogDom = String::Format("log.%s", ch);
    ZayWidgetDOM::SetValue(LogDom + ".number", String::FromInteger(LogState.mNumber));
    ZayWidgetDOM::SetValue(LogDom + ".count", String::FromInteger(LogState.mCount));
    ZayWidgetDOM::SetValue(LogDom + ".offset", String::FromInteger(LogState.mOffset));
}

void ZayAppData::AddHexLog(chars ch, chars tag, bytes data, sint32 size, const String& color)
{
    String HexCode;
    for(sint32 i = 0; i < size; ++i)
    {
        const sint32 HiWord = (data[i] & 0xF0) >> 4;
        const sint32 LoWord = data[i] & 0x0F;
        HexCode += " ";
        HexCode += (HiWord < 10)? '0' + HiWord : 'A' + HiWord - 10;
        HexCode += (LoWord < 10)? '0' + LoWord : 'A' + LoWord - 10;
    }
    AddLog(ch, tag + HexCode, color);
}

bool ZayAppData::RunEntry(chars projcode, chars ch, sint32 questidx, chars scriptfield)
{
    if(auto CurProject = mProjects.Access(projcode))
    {
        auto EntryIDAndParams = ZayWidgetDOM::GetValue(String::Format("%s.data.quest.%d.%s", projcode, questidx, scriptfield)).ToText();
        if(0 < EntryIDAndParams.Length())
        {
            const sint32 ColonPos = EntryIDAndParams.Find(0, ":");
            if(ColonPos == -1)
                mChannel.RunEntry(ch, CurProject->mScript, EntryIDAndParams, Strings());
            else
            {
                const String EntryID = EntryIDAndParams.Left(ColonPos);
                const Strings Params = String::Split(EntryIDAndParams.Offset(ColonPos + 1), ',');
                mChannel.RunEntry(ch, CurProject->mScript, EntryID, Params);
            }
            return true;
        }
    }
    return false;
}

bool ZayAppData::StopEntry(chars projcode, chars ch)
{
    if(auto CurProject = mProjects.Access(projcode))
    {
        mChannel.ClearBaton(ch, &CurProject->mScript);
        return true;
    }
    return false;
}

bool ZayAppData::ValidScope()
{
    if(mOscilloScope)
    {
        if(mOscilloScope->IsOpen())
            return true;
        else
        {
            if(mOscilloScope->Open())
            {
                std::string Value;
                mOscilloScope->Query("*IDN?", Value);
                ZayWidgetDOM::SetValue("vds.oscilloscope", String::Format("'%s'", Value.c_str()));
                mOscilloScope->Write(":MENU:RUN");
                mOscilloScope->Write(":CHANnel1:DISPlay ON");
                mOscilloScope->Write(":CHANnel2:DISPlay ON");
                mOscilloScope->Write(":CHANnel3:DISPlay ON");
                mOscilloScope->Write(":CHANnel4:DISPlay ON");
                return true;
            }
            else ZayWidgetDOM::SetValue("vds.oscilloscope", "'x'");
        }
    }
    return false;
}

void ZayAppData::SendWidgetLog(chars projcode, chars text)
{
    if(auto CurWidgetName = mLastWidgetName.Access(projcode))
    if(auto CurSubWidget = mSubWidgets.Access(*CurWidgetName))
        CurSubWidget->SendLog(text);
}
