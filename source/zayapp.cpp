#include <boss.hpp>
#include "zayapp.hpp"

#include <resource.hpp>
#include <format/boss_bmp.hpp>
#include <vector>
#include <string>
#include <exception>

ZAY_DECLARE_VIEW_CLASS("ZayAppView", ZayAppData)

String gLastLocale;
sint32 gMinWindowWidth {420};
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
            if(CurMsec <= m->mUpdateMsec)
                m->invalidate(2);

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
            if(auto CurWidgetName = m->mLastWidgetName.Access(m->mLastProjCode))
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
    else if(type == GT_LongPressed)
    {
        m->mMainWidget.JumpCall("bg_longpressed", "bg");
        m->invalidate();
    }
}

ZAY_VIEW_API OnRender(ZayPanel& panel)
{
    m->mMainWidget.Render(panel);
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

    // 메인위젯 구성
    CollectModifiedDoms("main", "");
    UpdateModifiedDoms();
    InitWidget(&mMainWidget, "main");
    mMainWidget.Reload("widget/main.zay");
    mMainWidget.UpdateAtlas(R::PrintUpdatedAtlas(true));

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
    const String WidgetFile = String::Format("%s.json", widgetname);
    if(Asset::Exist("widget/" + WidgetFile, nullptr, nullptr, nullptr, nullptr, &DataModifyMsec))
    if(forced || !mDomJsons.Access(WidgetFile) || mDomJsons(WidgetFile).mModifiedMsec < DataModifyMsec)
    {
        hook(mDomJsons(WidgetFile))
        {
            fish.mAssetName = WidgetFile;
            if(!String::Compare(widgetname, "main"))
                fish.mHeaderName = "data.";
            else fish.mHeaderName = String::Format("%s.data.", projcode);
            fish.mModifiedMsec = DataModifyMsec;
            fish.mUpdated = true;
        }
    }

    // 위젯의 확장DOM들
    static String CurWidgetName;
    static String CurProjCode;
    static bool CurForced;
    auto DomFinder = [](chars itemname, payload data)->void
    {
        auto Self = (ZayAppData*) data;
        const String FileName = itemname;
        const sint32 HeadLength = CurWidgetName.Length() + 1;
        if(!FileName.Left(HeadLength).CompareNoCase(CurWidgetName + "_"))
        if(!FileName.Right(5).CompareNoCase(".json"))
        {
            uint64 DataModifyMsec = 0;
            if(Asset::Exist("widget/" + FileName, nullptr, nullptr, nullptr, nullptr, &DataModifyMsec))
            if(CurForced || !Self->mDomJsons.Access(FileName) || Self->mDomJsons(FileName).mModifiedMsec < DataModifyMsec)
            {
                hook(Self->mDomJsons(FileName))
                {
                    const String DomPath = FileName.Middle(HeadLength, FileName.Length() - (HeadLength + 5));
                    fish.mAssetName = FileName;
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
    CurProjCode = projcode;
    CurForced = forced;
    Platform::File::Search(Platform::File::RootForAssets() + "widget", DomFinder, this, false);
    Platform::File::Search(Platform::File::RootForAssetsRem() + "widget", DomFinder, this, false);
}

bool ZayAppData::UpdateModifiedDoms()
{
    bool NeedRedraw = false;
    for(sint32 i = 0, iend = mDomJsons.Count(); i < iend; ++i)
    {
        if(auto CurDomJson = mDomJsons.AccessByOrder(i))
        {
            if(!CurDomJson->mUpdated) continue;
            const String DataJson = String::FromAsset("widget/" + CurDomJson->mAssetName);
            Context Data(ST_Json, SO_OnlyReference, DataJson, DataJson.Length());
            ZayWidgetDOM::RemoveVariables(CurDomJson->mHeaderName);
            ZayWidgetDOM::SetJson(Data, CurDomJson->mHeaderName);
            CurDomJson->mUpdated = false;
            NeedRedraw = true;

            if(CurDomJson->mHeaderName.Right(5) == "data.") // 기본DOM
            {
                if(auto AtlasCount = ZayWidgetDOM::GetValue(CurDomJson->mHeaderName + "atlas.count").ToInteger())
                {
                    const String AtlasInfoJson = String::FromAsset("atlasinfo.json");
                    Context AtlasInfo(ST_Json, SO_OnlyReference, AtlasInfoJson, AtlasInfoJson.Length());
                    for(sint32 j = 0; j < AtlasCount; ++j)
                    {
                        const String CurHeader = String::Format(CurDomJson->mHeaderName + "atlas.%d.", j);
                        const String CurPath = ZayWidgetDOM::GetValue(CurHeader + "path").ToText();
                        R::AddAtlas("ui_atlaskey2.png", CurPath, AtlasInfo, 2);
                    }
                    if(R::IsAtlasUpdated())
                    {
                        R::RebuildAll();
                        R::SaveAtlas(AtlasInfo);
                        AtlasInfo.SaveJson().ToAsset("atlasinfo.json", true);
                    }
                }
                if(auto FontsCount = ZayWidgetDOM::GetValue(CurDomJson->mHeaderName + "fonts.count").ToInteger())
                {
                    Strings Collector;
                    for(sint32 j = 0; j < FontsCount; ++j)
                    {
                        const String CurHeader = String::Format(CurDomJson->mHeaderName + "fonts.%d.", j);
                        const String CurPath = ZayWidgetDOM::GetValue(CurHeader + "path").ToText();
                        if(0 < CurPath.Length())
                        {
                            buffer NewFontData = Asset::ToBuffer(CurPath);
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
        }
    }
    return NeedRedraw;
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

void ZayAppData::InitWidget(ZayWidget* widget, const String viewname)
{
    auto& NewZayson = widget->Init(viewname, nullptr, [](chars name)->const Image* {return &((const Image&) R(name));});

    ////////////////////////////////////////////////////////////////////////////////
    // update
    NewZayson.AddGlue("update", ZAY_DECLARE_GLUE(params, this) // 업데이트를 위한 글루함수를 추가
    {
        if(params.ParamCount() == 1)
        {
            auto Msec = sint32(params.Param(0).ToFloat() * 1000);
            const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + Msec;
            if(mUpdateMsec < NewUpdateMsec)
                mUpdateMsec = NewUpdateMsec;
        }
    }, "[Msec]", "Msec동안 화면갱신유지");

    ////////////////////////////////////////////////////////////////////////////////
    // renderhint
    NewZayson.AddGlue("renderhint", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto Smooth = (params.Param(0).ToInteger() != 0);
            auto Antialiasing = (params.Param(1).ToInteger() != 0);
            Platform::Graphics::SetRenderHint(Smooth, Antialiasing);
            ZayWidgetDOM::SetValue("program.smooth", (Smooth)? "1" : "0");
            ZayWidgetDOM::SetValue("program.antialiasing", (Antialiasing)? "1" : "0");
            invalidate();
        }
    }, "[Smooth][Antialiasing]", "랜더링품질 설정");

    ////////////////////////////////////////////////////////////////////////////////
    // sleep
    NewZayson.AddGlue("sleep", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto Msec = sint32(params.Param(0).ToFloat() * 1000);
            Platform::Utility::Sleep(Msec, true, true, false);
        }
    }, "[Msec]", "Msec동안 슬립처리");

    ////////////////////////////////////////////////////////////////////////////////
    // print
    NewZayson.AddGlue("print", ZAY_DECLARE_GLUE(params, this)
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
    NewZayson.AddGlue("setlocale", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            gLastLocale = params.Param(0).ToText();
            ZayWidgetDOM::SetValue("program.locale", '\'' + gLastLocale + '\'');
        }
    }, "[Locale:'ko-KR']", "언어코드를 설정");

    ////////////////////////////////////////////////////////////////////////////////
    // langturn
    NewZayson.AddGlue("langturn", ZAY_DECLARE_GLUE(params, this)
    {
        mMainWidget.LangTurn();
        invalidate();
    }, nullptr, "현재 언어를 전환");

    ////////////////////////////////////////////////////////////////////////////////
    // ascii
    NewZayson.AddGlue("ascii", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto AsciiCode = params.Param(0).ToInteger();
            char AsciiText[2] = {char(AsciiCode & 0xFF), '\0'};
            Platform::SendKeyEvent(view(), 0, AsciiText, true);
            Platform::SendKeyEvent(view(), 0, AsciiText, false);
        }
    }, "[AsciiCode:65]", "아스키를 입력");

    ////////////////////////////////////////////////////////////////////////////////
    // keypad
    NewZayson.AddGlue("keypad", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto KeyCode = params.Param(0).ToInteger();
            auto KeyText = params.Param(1).ToText();
            Platform::SendKeyEvent(view(), KeyCode, KeyText, true);
            Platform::SendKeyEvent(view(), KeyCode, KeyText, false);
        }
    }, "[KeyCode:65][KeyText:'A']", "키패드를 입력");

    ////////////////////////////////////////////////////////////////////////////////
    // clearcapture
    NewZayson.AddGlue("clearcapture", ZAY_DECLARE_GLUE(params, this)
    {
        clearCapture();
        invalidate();
    }, nullptr, "Capture를 없애기");

    ////////////////////////////////////////////////////////////////////////////////
    // setcapture
    NewZayson.AddGlue("setcapture", ZAY_DECLARE_GLUE(params, this, viewname)
    {
        if(params.ParamCount() == 1)
        {
            auto UIName = viewname + '.' + params.Param(0).ToText();
            setCapture(UIName);
            invalidate();
        }
    }, "[UIName:'ui_editbox']", "Capture를 설정하기");

    ////////////////////////////////////////////////////////////////////////////////
    // windowgrap
    NewZayson.AddGlue("windowgrab", ZAY_DECLARE_GLUE(params, this)
    {
        if(6 <= params.ParamCount())
        {
            auto ResName = params.Param(0).ToText();
            auto X = params.Param(1).ToInteger();
            auto Y = params.Param(2).ToInteger();
            auto Width = params.Param(3).ToInteger();
            auto Height = params.Param(4).ToInteger();
            auto Blur = params.Param(5).ToFloat();
            auto UseRepaint = (6 < params.ParamCount())? (params.Param(6).ToInteger() != 0) : true;
            if(ResName.Left(2) == "r.")
                R::MakeGrapImage(ResName.Offset(2), X, Y, Width, Height, Blur, UseRepaint);
        }
    }, "[ResName:'r.grab'][X:0][Y:0][Width:100][Height:100][Blur:5.0]#[UseRepaint:1]", "해당 윈도우영역을 리소스화");

    ////////////////////////////////////////////////////////////////////////////////
    // focustable
    NewZayson.AddGlue("focustable", ZAY_DECLARE_GLUE(params, this)
    {
        // loaded_file = focustable("table_new/Target_", "INNER", "Level", "EDGE")
        // loaded_file = focustable("table_new/CoreX_", "PORE", "Face", "Z25")
        // loaded_file = focustable("table_new/CoreX_", "ATAC", "Level", "Z25", "BI200")

        mFocusedTable = nullptr;
        String LoadedFile;
        if(3 <= params.ParamCount())
        {
            const String FileTitle = params.Param(0).ToText();
            const String Group = params.Param(1).ToText();
            const String TableName = params.Param(2).ToText();
            const String RequestedFile = FileTitle + Group + ".csv";
            const String DefaultFile = FileTitle + "default.csv";

            // Only default groups use the filename prefix to avoid collisions.
            const std::string Prefix((chars) FileTitle);
            const size_t Slash = Prefix.find_last_of("/\\");
            const String DefaultGroup((Prefix.substr(
                (Slash == std::string::npos)? 0 : Slash + 1) + "default").c_str());
            const String RequestedGroup = (Group == "default")? DefaultGroup : Group;

            const auto LoadTableFile = [&](const String& AssetName, const String& ParseGroup) -> bool
            {
                if(mLoadedTableFiles.Access(AssetName))
                    return true;
                if(auto CurAsset = Asset::OpenForRead(AssetName))
                {
                    Asset::Close(CurAsset);
                    try
                    {
                        const String CsvText = String::FromAsset(AssetName);
                        mDependencyTable.Parse(ParseGroup, CsvText);
                        mLoadedTableFiles(AssetName) = true;
                        return true;
                    }
                    catch(const std::exception& Error)
                    {
                        BOSS_TRACE("[focustable] %s: %s", (chars) AssetName, Error.what());
                    }
                }
                return false;
            };
            const auto FindDependentTable = [&](const String& SearchGroup) -> const InnerTable*
            {
                if(params.ParamCount() == 3)
                    return mDependencyTable.Table(SearchGroup, TableName);
                for(sint32 i = 3; i < params.ParamCount(); ++i)
                {
                    const String Dependency = params.Param(i).ToText();
                    if(auto Found = mDependencyTable.Table(SearchGroup, TableName, Dependency))
                        return Found;
                }
                return nullptr;
            };

            if(LoadTableFile(RequestedFile, RequestedGroup))
                mFocusedTable = FindDependentTable(RequestedGroup);
            if(mFocusedTable)
                LoadedFile = RequestedFile;
            else if(LoadTableFile(DefaultFile, DefaultGroup))
            {
                mFocusedTable = FindDependentTable(DefaultGroup);
                if(!mFocusedTable)
                    mFocusedTable = mDependencyTable.Table(DefaultGroup, TableName);
                if(mFocusedTable)
                    LoadedFile = DefaultFile;
            }
        }
        if(mFocusedTable)
            LoadedFile += String::Format("<%d>", mFocusedTable->TableIndex());
        if(params.CanReturn())
            params.Return('\'' + LoadedFile + '\'');
    }, "[FileTitle:'table_new/Target_'][Group:'INNER'][Table:'Level']#[Dependency1:'Z25'][Dependency2:'BI200']", "테이블 포커싱");

    ////////////////////////////////////////////////////////////////////////////////
    // gettablecell
    NewZayson.AddGlue("gettablecell", ZAY_DECLARE_GLUE(params, this)
    {
        // cell0 = gettablecell("Forehead", "Level")
        // cell1 = gettablecell("Forehead", "Interval")
        // cell2 = gettablecell("Forehead", "MotorSpeed")

        if(params.CanReturn())
        {
            String Result = "''";
            if(params.ParamCount() == 2 && mFocusedTable)
            {
                const String Column = params.Param(0).ToText();
                const String Row = params.Param(1).ToText();
                if(auto Cell = mFocusedTable->Cell(Column, Row))
                {
                    sint32 OffsetCheck = 0;
                    Parser::GetFloat<double>(*Cell, Cell->Length(), &OffsetCheck);
                    if(0 < OffsetCheck && OffsetCheck == Cell->Length())
                        Result = *Cell;
                    else
                    {
                        String Text = *Cell;
                        Text.Replace("\\", "\\\\");
                        Text.Replace("'", "\\'");
                        Text.Replace("\r", "\\r");
                        Text.Replace("\n", "\\n");
                        Result = '\'' + Text + '\'';
                    }
                }
            }
            params.Return(Result);
        }
    }, "[Column:'NoSelect'][Row:'Level']", "테이블셀 반환");

    ////////////////////////////////////////////////////////////////////////////////
    // loadtable
    NewZayson.AddGlue("loadtable", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1 && mFocusedTable)
        {
            const String DomNameD = params.Param(0).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                const String DomRoot = DomNameD.Offset(2);
                for(sint32 y = 0, yend = 1 + mFocusedTable->RowCount(); y < yend; ++y)
                {
                    const String DomHead = DomRoot + String::Format(".%d", y);
                    ZayWidgetDOM::RemoveVariables(DomHead + ".");
                    for(sint32 x = 0, xend = 1 + mFocusedTable->ColumnCount(); x < xend; ++x)
                    {
                        String Header;
                        const String* Cell = nullptr;
                        if(y == 0)
                        {
                            if(x != 0) Header = mFocusedTable->ColumnName(x - 1);
                            Cell = &Header;
                        }
                        else if(x == 0)
                        {
                            Header = mFocusedTable->RowName(y - 1);
                            Cell = &Header;
                        }
                        else Cell = mFocusedTable->Cell(x - 1, y - 1);
                        if(Cell)
                        {
                            sint32 OffsetCheck = 0;
                            Parser::GetFloat<double>(*Cell, Cell->Length(), &OffsetCheck);
                            String Value = *Cell;
                            if(OffsetCheck <= 0 || OffsetCheck != Cell->Length())
                            {
                                Value.Replace("\\", "\\\\");
                                Value.Replace("'", "\\'");
                                Value.Replace("\r", "\\r");
                                Value.Replace("\n", "\\n");
                                Value = '\'' + Value + '\'';
                            }
                            ZayWidgetDOM::SetValue(DomHead + String::Format(".%d", x), Value);
                        }
                    }
                    ZayWidgetDOM::SetValue(DomHead + ".count", String::FromInteger(1 + mFocusedTable->ColumnCount()));
                }
                ZayWidgetDOM::SetValue(DomRoot + ".count", String::FromInteger(1 + mFocusedTable->RowCount()));
            }
        }
    }, "[DomNameD:'d.value']", "포커싱된 테이블을 Dom으로 전개");

    ////////////////////////////////////////////////////////////////////////////////
    // wificonnect
    NewZayson.AddGlue("wificonnect", ZAY_DECLARE_GLUE(params, this)
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
    NewZayson.AddGlue("wifidisconnect", ZAY_DECLARE_GLUE(params, this)
    {
        Platform::Socket::DisconnectWifi();
    }, nullptr, "와이파이 연결해제");

    ////////////////////////////////////////////////////////////////////////////////
    // bindsounddevice
    NewZayson.AddGlue("bindsounddevice", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto DeviceIndex = params.Param(0).ToInteger();
            UpdateSoundDeviceList(DeviceIndex);
            for(sint32 i = 0, iend = mSounds.Count(); i < iend; ++i)
                if(auto CurSound = mSounds.AccessByOrder(i))
                    Platform::Sound::SetDevice(*CurSound, DeviceIndex);
        }
    }, "[DeviceIndex:0]", "사운드장치선택");

    ////////////////////////////////////////////////////////////////////////////////
    // mutesound
    NewZayson.AddGlue("mutesound", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto MuteFlag = (params.Param(0).ToInteger() != 0);
            mSoundMuted = MuteFlag;
            if(mSoundMuted)
            for(sint32 i = 0, iend = mSounds.Count(); i < iend; ++i)
                if(auto CurSound = mSounds.AccessByOrder(i))
                    Platform::Sound::Stop(*CurSound);
        }
    }, "[MuteFlag:1]", "사운드무음설정");

    ////////////////////////////////////////////////////////////////////////////////
    // playsound
    NewZayson.AddGlue("playsound", ZAY_DECLARE_GLUE(params, this)
    {
        if(1 <= params.ParamCount())
        {
            auto SoundAssetName = params.Param(0).ToText();
            auto Volume = (1 < params.ParamCount())? params.Param(1).ToFloat() : 1.0;
            if(!mSoundMuted)
            {
                if(!mSounds.Access(SoundAssetName))
                {
                    #if BOSS_ANDROID | BOSS_IPHONE
                        buffer NewSoundData = Asset::ToBuffer(SoundAssetName);
                        mSounds(SoundAssetName) = Platform::Sound::OpenForData((bytes) NewSoundData,
                            Buffer::SizeOf(NewSoundData) * Buffer::CountOf(NewSoundData));
                        Buffer::Free(NewSoundData);
                    #elif BOSS_WASM
                        auto SoundHost = ZayWidgetDOM::GetValue("data.soundhost").ToText();
                        mSounds(SoundAssetName) = Platform::Sound::OpenForUrl(SoundHost + SoundAssetName, false);
                    #else
                        mSounds(SoundAssetName) = Platform::Sound::OpenForFile(
                            Platform::File::RootForAssets() + SoundAssetName, false);
                    #endif
                    if(mSoundDeviceFocus != -1)
                        Platform::Sound::SetDevice(mSounds(SoundAssetName), mSoundDeviceFocus);
                }
                auto CurSoundID = mSounds(SoundAssetName);
                if(Platform::Sound::NowPlaying(CurSoundID))
                    Platform::Sound::Stop(CurSoundID);
                Platform::Sound::SetVolume(CurSoundID, Volume);
                Platform::Sound::Play(CurSoundID);
            }
        }
    }, "[SoundAssetName:'sound/sound1.mp3']#[Volume:1.0]", "사운드시작");

    ////////////////////////////////////////////////////////////////////////////////
    // stopsound
    NewZayson.AddGlue("stopsound", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto SoundAssetName = params.Param(0).ToText();
            if(auto CurSoundID = mSounds.Access(SoundAssetName))
            if(Platform::Sound::NowPlaying(*CurSoundID))
                Platform::Sound::Stop(*CurSoundID);
        }
    }, "[SoundAssetName:'sound/sound1.mp3']", "사운드중지");

    ////////////////////////////////////////////////////////////////////////////////
    // readyapng
    NewZayson.AddGlue("readyapng", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto APngAssetName = params.Param(0).ToText();
            if(!mAPngs.Access(APngAssetName))
                mAPngs(APngAssetName) = Platform::Animate::OpenForAPngFile(
                    Platform::File::RootForAssets() + APngAssetName);
        }
    }, "[APngAssetName:'image/animation.apng']", "APNG 사전로딩");

    ////////////////////////////////////////////////////////////////////////////////
    // setvalue
    NewZayson.AddGlue("setvalue", ZAY_DECLARE_GLUE(params, this)
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
    NewZayson.AddGlue("calcvalue", ZAY_DECLARE_GLUE(params, this)
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
    NewZayson.AddGlue("clearvalue", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto DomNameD = params.Param(0).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                ZayWidgetDOM::RemoveVariables(DomNameD.Offset(2));
                invalidate();
            }
        }
    }, "[DomNameD:'d.aaa.bbb']", "DomName이 포함된 모든 DOM을 제거");

    ////////////////////////////////////////////////////////////////////////////////
    // getcomment
    NewZayson.AddGlue("getcomment", ZAY_DECLARE_GLUE(params, this)
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
    NewZayson.AddGlue("setcomment", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto DomNameD = params.Param(0).ToText();
            auto Value = params.Param(1).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                ZayWidgetDOM::SetComment(DomNameD.Offset(2), Value);
                ZayWidgetDOM::SetValue(DomNameD.Offset(2) + ".text", '\'' + Value + '\'');
                clearCapture();
                invalidate();
            }
        }
    }, "[DomNameD:'d.aaa.bbb'][Value]", "Comment타입 DOM에 값을 설정");

    ////////////////////////////////////////////////////////////////////////////////
    // setdom
    NewZayson.AddGlue("setdom", ZAY_DECLARE_GLUE(params, this)
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
                    const Context Json(ST_Json, SO_NeedCopy, String::FromFile(FilePath));
                    ZayWidgetDOM::SetJson(Json, DomHeader);
                }
                else if(2 < params.ParamCount())
                {
                    auto AssetPath = params.Param(2).ToText();
                    const Context Json(ST_Json, SO_NeedCopy, String::FromAsset(AssetPath));
                    ZayWidgetDOM::SetJson(Json, DomHeader);
                }
                invalidate();
            }
        }
    }, "[DomNameD:'d.aaa.bbb'][FilePath:'C:/aaa/bbb.json']", "DomName의 아래에 FilePath의 json을 로딩");

    ////////////////////////////////////////////////////////////////////////////////
    // gettext
    NewZayson.AddGlue("gettext", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto Title = params.Param(0).ToText();
            auto Topic = params.Param(1).ToText();
            SolverValue OldText;
            if(params.CanReturn(&OldText))
            {
                String GetText = OldText.ToText();
                if(Platform::Popup::TextDialog(GetText, Title, Topic))
                {
                    params.Return('\'' + GetText + '\'');
                    invalidate();
                }
            }
        }
    }, "[Title:'타이틀명'][Topic:'토픽명']", "유저로부터 값을 하나 입력받아 지역변수로 Return처리");

    ////////////////////////////////////////////////////////////////////////////////
    // savetext
    NewZayson.AddGlue("savetext", ZAY_DECLARE_GLUE(params, this)
    {
        if(2 <= params.ParamCount())
        {
            auto FileName = params.Param(0).ToText();
            auto Text = params.Param(1).ToText();
            Text.Replace("\\n", "\r\n");
            auto UniqueFlag = (2 < params.ParamCount())? (params.Param(2).ToInteger() != 0) : false;
            auto ExtendPath = (3 < params.ParamCount())? params.Param(3).ToText() : String();

            if(0 < ExtendPath.Length())
            {
                String FilePath = ExtendPath + FileName;
                if(UniqueFlag)
                {
                    const sint32 DotPos = FileName.Find(0, ".");
                    if(DotPos != -1)
                    for(sint32 i = 1; i < 1000 && Platform::File::Exist(FilePath); ++i)
                        FilePath = ExtendPath + FileName.Left(DotPos) + String::Format("_%03d", i) + FileName.Offset(DotPos);
                }
                if(auto NewFile = Platform::File::OpenForWrite(FilePath, true))
                {
                    Platform::File::Write(NewFile, (bytes)(chars) Text, Text.Length());
                    Platform::File::Close(NewFile);
                    ZayWidgetDOM::SetValue("savefile.log", "'[SAVED usb] " + FilePath + '\'');
                }
                else ZayWidgetDOM::SetValue("savefile.log", "'[NOT SAVE usb] " + FilePath + '\'');
            }
            else
            {
                String AssetName = FileName;
                if(UniqueFlag)
                {
                    const sint32 DotPos = FileName.Find(0, ".");
                    if(DotPos != -1)
                    for(sint32 i = 1; i < 1000 && Asset::Exist(AssetName); ++i)
                        AssetName = FileName.Left(DotPos) + String::Format("_%03d", i) + FileName.Offset(DotPos);
                }
                Text.ToAsset(AssetName, true);
                ZayWidgetDOM::SetValue("savefile.log", "'[SAVED assets-rem] " + AssetName + '\'');
            }
        }
    }, "[FileName:'파일명'][Text:'ABC']#[UniqueFlag:1][ExtendPath:'D:/']", "Text를 파일에 저장");

    ////////////////////////////////////////////////////////////////////////////////
    // savebase64
    NewZayson.AddGlue("savebase64", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto FileName = params.Param(0).ToText();
            auto JsonText = params.Param(1).ToText();
            if(auto NewAsset = Asset::OpenForWrite(FileName, true))
            {
                const Context Json(ST_Json, SO_OnlyReference, JsonText);
                String Base64 = Json("base64").GetText();
                buffer ImageData = AddOn::Ssl::FromBASE64(Base64.Replace("\\/", "/"));
                Asset::Write(NewAsset, (bytes) ImageData, Buffer::CountOf(ImageData));
                Asset::Close(NewAsset);
                Buffer::Free(ImageData);
            }
        }
    }, "[FileName:'파일명'][JsonText:'{\"base64\":\"...\"}']", "JsonText속 base64항목을 디코딩해 파일로 저장");

    ////////////////////////////////////////////////////////////////////////////////
    // loadcsv
    NewZayson.AddGlue("loadcsv", ZAY_DECLARE_GLUE(params, this)
    {
        if(2 <= params.ParamCount())
        {
            auto FileName = params.Param(0).ToText();
            auto DomNameD = params.Param(1).ToText();
            auto ExtendPath = (2 < params.ParamCount())? params.Param(2).ToText() : String();
            if(DomNameD.Left(2) == "d.")
            {
                String Csv;
                if(0 < ExtendPath.Length())
                {
                    String FilePath = ExtendPath + FileName;
                    Csv = String::FromFile(FilePath);
                }
                else
                {
                    const String AssetName = FileName;
                    Csv = String::FromAsset(AssetName);
                }

                Csv.Replace("\r\n", "\n");
                const Strings CsvLines = String::Split(Csv, '\n');
                for(sint32 i = 0, iend = CsvLines.Count(); i < iend; ++i)
                {
                    const Strings CsvCells = String::Split(CsvLines[i], ',');
                    const String DomHead = DomNameD.Offset(2) + String::Format(".%d", i);
                    for(sint32 j = 0, jend = CsvCells.Count(); j < jend; ++j)
                    {
                        const String CurCell = CsvCells[j].Trim();
                        sint32 OffsetCheck = 0;
                        Parser::GetFloat<double>(CurCell, CurCell.Length(), &OffsetCheck);
					    if(OffsetCheck == CurCell.Length() && 0 < OffsetCheck) // 실수 또는 정수
					        ZayWidgetDOM::SetValue(DomHead + String::Format(".%d", j), CurCell);
                        else ZayWidgetDOM::SetValue(DomHead + String::Format(".%d", j), '\'' + CurCell + '\'');
                    }
                    ZayWidgetDOM::SetValue(DomHead + ".count", String::FromInteger(CsvCells.Count()));
                }
                ZayWidgetDOM::SetValue(DomNameD.Offset(2) + ".count", String::FromInteger(CsvLines.Count()));
            }
        }
    }, "[FileName:'파일명'][DomNameD:'d.value']#[ExtendPath:'D:/']", "파일에서 Csv를 로딩");

    ////////////////////////////////////////////////////////////////////////////////
    // loadjson
    NewZayson.AddGlue("loadjson", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto JsonText = params.Param(0).ToText();
            auto DomNameD = params.Param(1).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                String DomHeader = DomNameD.Offset(2) + '.';
                ZayWidgetDOM::RemoveVariables(DomHeader);
                const Context Json(ST_Json, SO_OnlyReference, JsonText);
                ZayWidgetDOM::SetJson(Json, DomHeader);
            }
        }
    }, "[JsonText:'{a:b}'][DomNameD:'d.value']", "Json을 Dom으로 전개");

    ////////////////////////////////////////////////////////////////////////////////
    // geturltext
    NewZayson.AddGlue("geturltext", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto Text = params.Param(0).ToText();
            if(params.CanReturn())
                params.Return('\'' + Text.ToUrlString() + '\'');
        }
    }, "[Text:'abc']", "UrlText로 변환");

    ////////////////////////////////////////////////////////////////////////////////
    // makeuid
    NewZayson.AddGlue("makeuid", ZAY_DECLARE_GLUE(params, this)
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
    NewZayson.AddGlue("maketimetag", ZAY_DECLARE_GLUE(params, this)
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

    ////////////////////////////////////////////////////////////////////////////////
    // movescrollv
    NewZayson.AddGlue("movescrollv", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto UIName = params.Param(0).ToText();
            auto ScrollTarget = params.Param(1).ToInteger();
            const sint32 ScrollPos = scrollpos(UIName).y;
            moveScroll(UIName, 0, ScrollPos, 0, ScrollTarget, 0.5, false);
        }
    }, "[UIName:'ui_scroll'][ScrollTarget:1000]", "수직스크롤바를 이동");

    ////////////////////////////////////////////////////////////////////////////////
    // slideonce
    NewZayson.AddGlue("slideonce", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 4)
        {
            const String DomNameD = params.Param(0).ToText();
            const sint32 Min = params.Param(1).ToInteger();
            const sint32 Max = params.Param(2).ToInteger();
            const sint32 Step = params.Param(3).ToInteger();
            if(DomNameD.Left(2) == "d." && Min < Max && Step != 0)
            {
                const String DomName = DomNameD.Offset(2);
                const float OldValue = ZayWidgetDOM::ExistVariable(DomName + ".ani")?
                    ZayWidgetDOM::GetValue(DomName + ".ani").ToFloat() : ZayWidgetDOM::GetValue(DomName).ToFloat();
                const sint32 AbsStep = Math::Abs(Step);
                const sint32 NewValue = Min + sint32(Math::Round((OldValue - Min) / AbsStep)) * AbsStep + Step;
                const sint32 ClampValue = Math::Clamp(NewValue, Min, Max);
                ZayWidgetDOM::SetValue(DomName, String::FromInteger(ClampValue));
                ZayWidgetDOM::SetValue(DomName + ".ani", String::Format("%f ~ %d : 0.1", OldValue, ClampValue));
                const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + 100;
                if(mUpdateMsec < NewUpdateMsec)
                    mUpdateMsec = NewUpdateMsec;
                clearCapture();
                invalidate(2);
            }
        }
    }, "[DomNameD:'d.value'][Step:-10][Limit:1]", "슬라이드를 한번 이동");

    ////////////////////////////////////////////////////////////////////////////////
    // addgraph
    NewZayson.AddGlue("addgraph", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 3)
        {
            auto GraphName = params.Param(0).ToText();
            auto Value = params.Param(1).ToFloat();
            auto MaxCount = params.Param(1).ToInteger();
            // 그래프에 값추가
            auto& CurGraph = mGraphs(GraphName);
            if(0 < CurGraph.Count() - MaxCount)
                CurGraph.SubtractionSection(0, CurGraph.Count() - MaxCount);
            CurGraph.AtAdding() = Value;
        }
    }, "[GraphName:'temp'][Value:500][MaxCount:100]", "특정 그래프에 하나의 값을 추가");

    ////////////////////////////////////////////////////////////////////////////////
    // cleargraph
    NewZayson.AddGlue("cleargraph", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto GraphName = params.Param(0).ToText();
            mGraphs.Remove(GraphName);
        }
    }, "[GraphName:'temp']", "특정 그래프를 제거");

    ////////////////////////////////////////////////////////////////////////////////
    // setloghighlight
    NewZayson.AddGlue("setloghighlight", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto ColorCode = params.Param(0).ToText();
            auto MatchText = params.Param(1).ToText();
            if(0 < MatchText.Length())
                mLogHighlights(ColorCode) = MatchText;
            else mLogHighlights.Remove(ColorCode);
        }
    }, "[ColorCode:'#ff00ff'][MatchText:'80 00']", "로그의 강조옵션");

    ////////////////////////////////////////////////////////////////////////////////
    // updateipaddress
    NewZayson.AddGlue("updateipaddress", ZAY_DECLARE_GLUE(params, this)
    {
        UpdateIPAddress();
    }, nullptr, "IP주소를 알기");

    ////////////////////////////////////////////////////////////////////////////////
    // attachserialonce
    NewZayson.AddGlue("attachserialonce", ZAY_DECLARE_GLUE(params, this)
    {
        Platform::Serial::AttachOnce();
    }, nullptr, "시리얼포트 권한추가(WASM전용)");

    ////////////////////////////////////////////////////////////////////////////////
    // connectserial
    NewZayson.AddGlue("connectserial", ZAY_DECLARE_GLUE(params, this)
    {
        if(1 <= params.ParamCount())
        {
            auto Port = params.Param(0).ToText();
            auto Baudrate = (1 < params.ParamCount())? params.Param(1).ToInteger() : -1;
            if(mChannel.ConnectSerial(Port, Baudrate))
                UpdateChannelList();
            invalidate();
        }
    }, "[Port:'COM1']#[Baudrate:115200]", "시리얼포트 연결");

    ////////////////////////////////////////////////////////////////////////////////
    // closechannel
    NewZayson.AddGlue("closechannel", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            mChannel.Delete(Ch);
            UpdateChannelList();
            if(mProjects.Access(mLastVdsProjCode))
                ZayWidgetDOM::RemoveVariables(mLastVdsProjCode + String::Format(".%s.", (chars) Ch));
            invalidate();
        }
    }, "[Ch:'COM1']", "채널 닫기");

    ////////////////////////////////////////////////////////////////////////////////
    // sendchannel
    NewZayson.AddGlue("sendchannel", ZAY_DECLARE_GLUE(params, this)
    {
        clearCapture();
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            uint08s NewPacket;
            mEditText.WritePacket(NewPacket, [this](chars ch, chars key)->const SolverValue
                {return ZayWidgetDOM::GetValue(String::Format("vds.%s", key));}, Ch);
            if(0 < NewPacket.Count())
            {
                if(mChannel.Write(Ch, (bytes) &NewPacket[0], NewPacket.Count()))
                    AddHexLog(Ch, "(send CMD)", &NewPacket[0], NewPacket.Count(), "#800080");
                else AddHexLog(Ch, "(send-failed CMD)", &NewPacket[0], NewPacket.Count(), "#800000");
            }
            invalidate();
        }
    }, "[Ch:'COM1']", "채널로 패킷송신");

    ////////////////////////////////////////////////////////////////////////////////
    // adddevice
    NewZayson.AddGlue("adddevice", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            if(Ch != "x")
            {
                String FindedDeviceName;
                for(sint32 i = 1; i < 1000; ++i)
                {
                    const String CurDeviceName = String::Format("%03d", i);
                    if(auto CurCh = mDevices.Access(CurDeviceName))
                    {
                        if(*CurCh == Ch)
                        {
                            FindedDeviceName = CurDeviceName;
                            break;
                        }
                    }
                    else if(FindedDeviceName.Length() == 0)
                        FindedDeviceName = CurDeviceName;
                }
                if(0 < FindedDeviceName.Length())
                {
                    mDevices(FindedDeviceName) = Ch;
                    UpdateDeviceList();
                }
                invalidate();
            }
        }
    }, "[Ch:'COM1']", "채널로 장치추가");

    ////////////////////////////////////////////////////////////////////////////////
    // subdevice
    NewZayson.AddGlue("subdevice", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            for(sint32 i = 1; i < 1000; ++i)
            {
                const String CurDeviceName = String::Format("%03d", i);
                if(auto CurCh = mDevices.Access(CurDeviceName))
                if(*CurCh == Ch)
                {
                    mDevices.Remove(CurDeviceName);
                    UpdateDeviceList();
                    break;
                }
            }
            invalidate();
        }
    }, "[Ch:'COM1']", "채널로 장치제거");

    ////////////////////////////////////////////////////////////////////////////////
    // connectscope
    NewZayson.AddGlue("connectscope", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto ScopeId = params.Param(0).ToText();
            if(!mOscilloScope)
                mOscilloScope = VisaApi::Create();
            if(mOscilloScope)
            {
                if(mOscilloScope->IsOpen())
                    mOscilloScope->Close();
                mOscilloScope->SetResource(ScopeId);
                ValidScope();
            }
        }
    }, "[ScopeId:'USB0::0x18D1::0x0007::410001140::1::INSTR']", "스코프에 연결하기");

    ////////////////////////////////////////////////////////////////////////////////
    // getscope
    NewZayson.AddGlue("getscope", ZAY_DECLARE_GLUE(params, this)
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
                if(ValidScope())
                {
                    const String DomNamesD[4] = {DomNameD1, DomNameD2, DomNameD3, DomNameD4};
                    for(sint32 i = 0; i < 4; ++i)
                    {
                        if(0 < DomNamesD[i].Length())
                        {
                            std::string Value;
                            mOscilloScope->Write((chars) String::Format(":MEASure:OPEN %s, CH%d", (chars) Measure, i + 1)); // 메뉴가 없다면 새로 생기게 됨
                            mOscilloScope->Query((chars) String::Format(":MEASure:%s? CH%d", (chars) Measure, i + 1), Value); // 메뉴가 없을땐 "-"로 나옴
                            const String ErrorCheck = String(Value.c_str()).Trim();
                            sint32 ReadOffset = 0;
                            Parser::GetFloat((chars) ErrorCheck, ErrorCheck.Length(), &ReadOffset);
                            if(ReadOffset == ErrorCheck.Length())
                                ZayWidgetDOM::SetValue(DomNamesD[i].Offset(2), Value.c_str());
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

    //jj260821_oscilloscope scale
    NewZayson.AddGlue("setscopetimescale", ZAY_DECLARE_GLUE(params, this)
    {
        if (ValidScope())
        {
            mOscilloScope->Write(
                ":TIMebase:EXTent 2.000000e-6\n");
        }
    }, "", "스코프 시간축 2us/div 설정"); 
    NewZayson.AddGlue("setscopetime500ms", ZAY_DECLARE_GLUE(params, this)
    {
        if (ValidScope())
        {
            mOscilloScope->Write(
                ":TIMebase:EXTent 5.000000e-1\n");
        }
    }, "", "스코프 시간축 500ms/div 설정");
    NewZayson.AddGlue("setscopech1_50v", ZAY_DECLARE_GLUE(params, this)
    {
        if (ValidScope())
        {
            mOscilloScope->Write(
                ":CHANnel1:SCALe 50\n");
        }
    }, "", "CH1 전압 스케일 50V/div 설정");
    NewZayson.AddGlue("setscopech2_50v", ZAY_DECLARE_GLUE(params, this)
    {
        if (ValidScope())
        {
            mOscilloScope->Write(
                ":CHANnel2:SCALe 50\n");
        }
    }, "", "CH2 전압 스케일 50V/div 설정");

	//jj260825_inspection Tool HP Count
    NewZayson.AddGlue("gethpcount", ZAY_DECLARE_GLUE(params, this)
    {
        if (params.ParamCount() == 1)
        {
            const sint32 StartIndex = params.Param(0).ToInteger();
            const String ItemPath = "inspection.apq.inspect_table.items";

            const sint32 ItemCount =
                ZayWidgetDOM::GetValue(ItemPath + ".count").ToInteger();

            sint32 Count = 0;

            if (0 <= StartIndex && StartIndex < ItemCount)
            {
                const String TargetHp = ZayWidgetDOM::GetValue(
                    ItemPath + String::Format(".%d.hp", StartIndex)
                ).ToText();

                for (sint32 i = StartIndex; i < ItemCount; ++i)
                {
                    const String CurrentHp = ZayWidgetDOM::GetValue(
                        ItemPath + String::Format(".%d.hp", i)
                    ).ToText();

                    if (CurrentHp != TargetHp)
                        break;

                    ++Count;
                }
            }

            ZayWidgetDOM::SetValue(
                "inspection.hp_count." + String::FromInteger(StartIndex),
                String::FromInteger(Count)
            );
        }
    }, "[StartIndex:0]", "현재 위치부터 연속된 HP 개수 계산");

    //jj260826_inspection Tool Output Count
    NewZayson.AddGlue("getoutputcount", ZAY_DECLARE_GLUE(params, this)
    {
        if (params.ParamCount() == 1)
        {
            const sint32 StartIndex = params.Param(0).ToInteger();
            const String ItemPath = "inspection.apq.inspect_table.items";

            const sint32 ItemCount =
                ZayWidgetDOM::GetValue(ItemPath + ".count").ToInteger();

            sint32 Count = 0;

            if (0 <= StartIndex && StartIndex < ItemCount)
            {
                const String TargetOutput = ZayWidgetDOM::GetValue(
                    ItemPath + String::Format(".%d.output", StartIndex)
                ).ToText();

                for (sint32 i = StartIndex; i < ItemCount; ++i)
                {
                    const String CurrentOutput = ZayWidgetDOM::GetValue(
                        ItemPath + String::Format(".%d.output", i)
                    ).ToText();

                    if (CurrentOutput != TargetOutput)
                        break;

                    ++Count;
                }
            }

            ZayWidgetDOM::SetValue(
                "inspection.output_count." + String::FromInteger(StartIndex),
                String::FromInteger(Count)
            );
        }

    }, "[StartIndex:0]", "현재 위치부터 연속된 Output 개수 계산");

    // restapi
    NewZayson.AddGlue("restapi", ZAY_DECLARE_GLUE(params, this)
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

                struct Payload {ZayAppData* mSelf; String mDomNameD; String mCallBack;} *Data = new Payload({this, DomNameD, CallBack});
                auto Method = [](payload data, chars text)->void
                    {
                        const Payload* Data = (const Payload*) data;
                        const String ResultJson = text;
                        const Context Json(ST_Json, SO_OnlyReference, ResultJson);
                        ZayWidgetDOM::SetJson(Json, Data->mDomNameD.Offset(2) + '.');
                        BOSS_TRACE("##### RestAPI(%s) Response: %s...", (chars) Data->mDomNameD, (chars) ResultJson.Left(40).ToUrlString());

                        if(0 < Data->mCallBack.Length())
                        if(auto CurWidgetName = Data->mSelf->mLastWidgetName.Access(Data->mSelf->mLastProjCode))
                        if(auto CurSubWidget = Data->mSelf->mSubWidgets.Access(*CurWidgetName))
                        {
                            auto Success = CurSubWidget->JumpCallDirectly(Data->mCallBack, Data->mDomNameD);
                            BOSS_TRACE("##### RestAPI(%s) JumpCall: %s (%s)", (chars) Data->mDomNameD, (chars) Data->mCallBack, (Success)? "success" : "failure");
                        }
                        delete Data;
                        Platform::UpdateAllViews();
                    };

                if(ResName.Left(2) == "r.")
                {
                    auto RawHeader = (5 < params.ParamCount())? params.Param(5).ToText() : String();
                    Platform::Utility::SendImageRequest(Query, R(ResName.Offset(2))->GetImage(), Method, Data, RawHeader);
                }
                else if(!ResName.Right(4).CompareNoCase(".csv"))
                    Platform::Utility::SendCsvRequest(Query, String::FromAsset(ResName), Method, Data);
                else Platform::Utility::SendRequest(Query, Method, Data);
            }
        }
    }, "[DomNameD:'d.return'][HostUrl:'localhost/member/login'][Params:'id=aaaa&key=bbbb']#[CallBack:'gate1'][ResName:'r.grab']", "REST-API를 사용하여 결과를 DOM으로 받기");

    ////////////////////////////////////////////////////////////////////////////////
    // installwpk
    NewZayson.AddGlue("installwpk", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 3)
        {
            auto WpkPath = params.Param(0).ToText();
            auto DomNameD = params.Param(1).ToText();
            auto Exclusions = params.Param(2).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                ZayWidgetDOM::SetValue(DomNameD.Offset(2) + ".status", "'run'");
                RunInstallWpk(WpkPath, Exclusions);
            }
        }
    }, "[WpkPath:'E:/hxz_1.wpk'][DomNameD:'d.install']#[Exclusions:'app_win|fw_main|fw_pixel']", "WPK파일로 재설치하기");

    ////////////////////////////////////////////////////////////////////////////////
    // installsector
    NewZayson.AddGlue("installsector", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 3)
        {
            auto FileOffset = params.Param(0).ToInteger();
            auto DataCount = params.Param(1).ToInteger();
            auto DataUnit = Math::Clamp(params.Param(2).ToInteger(), 1, 8);
            if(auto FilePath = ZayWidgetDOM::GetValue("program.tasking.install.path").ToText())
            if(auto OldFile = Platform::File::OpenForRead(Platform::File::RootForAssetsRem() + "../" + FilePath))
            {
                Platform::File::Seek(OldFile, FileOffset);
                uint32 SectorCrc = 0xFFFFFFFF;
                for(sint32 i = 0; i < DataCount; ++i)
                {
                    uint64 DataPool = oxFFFFFFFFFFFFFFFF;
                    Platform::File::Read(OldFile, (uint08*) &DataPool, DataUnit);
                    ZayWidgetDOM::SetValue(String::Format("sector.%d", i), String::FromInteger(DataPool));
                    SectorCrc = UpdateCrc((bytes) &DataPool, DataUnit, SectorCrc);
                }
                Platform::File::Close(OldFile);
                ZayWidgetDOM::SetValue("sector.unit", String::FromInteger(DataUnit));
                ZayWidgetDOM::SetValue("sector.count", String::FromInteger(DataCount));
                if(params.CanReturn())
                    params.Return(String::FromInteger(SectorCrc & 0xFFFF));
            }
        }
    }, "[FileOffset:30][DataCount:100][DataUnit:2]", "WPK파일 재설치중 포커싱된 파일의 일부를 DOM에 전개");

    ////////////////////////////////////////////////////////////////////////////////
    // installstatus
    NewZayson.AddGlue("installstatus", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto Status = params.Param(0).ToText();
            if(mInstallWpk)
                Tasking::SendQuery(mInstallWpk, Status.ToBuffer());
        }
    }, "[Status:'BootLoaderEnd']", "WPK파일 재설치중 상태업데이트");

    ////////////////////////////////////////////////////////////////////////////////
    // sethotkey
    NewZayson.AddGlue("sethotkey", ZAY_DECLARE_GLUE(params, this)
    {
        if(2 <= params.ParamCount())
        {
            auto Key = params.Param(0).ToText();
            auto Gate = params.Param(1).ToText();
            auto NeedPress = (2 < params.ParamCount())? (params.Param(2).ToInteger() != 0) : false;
            sint32 KeyCode = 0;
            branch;
            jump(!Key.CompareNoCase("ESC")) KeyCode = 27;
            jump(!Key.CompareNoCase("ENTER")) KeyCode = 13;
            jump(!Key.CompareNoCase("SPACE")) KeyCode = 32;
            jump(!Key.CompareNoCase("BACKSPACE")) KeyCode = 8;
            jump(!Key.CompareNoCase("TAB")) KeyCode = 9;
            jump(!Key.CompareNoCase("INSERT")) KeyCode = 45;
            jump(!Key.CompareNoCase("DELETE")) KeyCode = 46;
            jump(!Key.CompareNoCase("HOME")) KeyCode = 36;
            jump(!Key.CompareNoCase("END")) KeyCode = 35;
            jump(!Key.CompareNoCase("PAGEUP")) KeyCode = 33;
            jump(!Key.CompareNoCase("PAGEDOWN")) KeyCode = 34;
            jump(!Key.CompareNoCase("LEFT")) KeyCode = 37;
            jump(!Key.CompareNoCase("UP")) KeyCode = 38;
            jump(!Key.CompareNoCase("RIGHT")) KeyCode = 39;
            jump(!Key.CompareNoCase("DOWN")) KeyCode = 40;
            if(KeyCode != 0)
            if(auto CurProject = mProjects.Access(mLastProjCode))
            {
                auto& NewKey = CurProject->mHotKeys[KeyCode];
                if(NeedPress) NewKey.mPressGate = Gate + "_press";
                else NewKey.mPressGate.Empty();
                NewKey.mReleaseGate = Gate;
            }
        }
    }, "[Key:'Enter'][Gate:'aaa']#[NeedPress:0]", "핫키추가");

    ////////////////////////////////////////////////////////////////////////////////
    // clearhotkey
    NewZayson.AddGlue("clearhotkey", ZAY_DECLARE_GLUE(params, this)
    {
        if(auto CurProject = mProjects.Access(mLastProjCode))
            CurProject->mHotKeys.Reset();
    }, nullptr, "핫키초기화");

    ////////////////////////////////////////////////////////////////////////////////
    // pickeronce
    NewZayson.AddGlue("pickeronce", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 6)
        {
            const String DomNameD = params.Param(0).ToText();
            const sint32 Min = params.Param(1).ToInteger();
            const sint32 Max = params.Param(2).ToInteger();
            const sint32 Step = params.Param(3).ToInteger();
            const sint32 Def = params.Param(4).ToInteger();
            const bool IsRight = (params.Param(5).ToInteger() != 0);
            if(DomNameD.Left(2) == "d." && Min < Max && 0 < Step)
            {
                const String DomName = DomNameD.Offset(2);
                auto CurDom = String(DomName) + ".value";
                auto AniDom = String(DomName) + ".ani";
                const float AniValue = (ZayWidgetDOM::ExistVariable(AniDom))? ZayWidgetDOM::GetValue(AniDom).ToFloat() : Def;
                const float NewValue = Math::Clamp((Math::Round(AniValue / Step) + ((IsRight)? 1 : -1)) * Step, Min, Max);
                ZayWidgetDOM::SetValue(CurDom, String::FromInteger((sint32) Math::Round(NewValue)));
                ZayWidgetDOM::SetValue(AniDom, String::Format("%f ~ %f : %f", AniValue, NewValue, 0.2));
                const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + 200;
                if(mUpdateMsec < NewUpdateMsec)
                    mUpdateMsec = NewUpdateMsec;
                clearCapture();
                invalidate(2);
            }
        }
    }, "[DomNameD:'d.value'][Min:10][Max:100][Step:5][IsRight:0]", "픽커를 한번 이동");

    ////////////////////////////////////////////////////////////////////////////////
    // vdsentry
    NewZayson.AddGlue("vdsentry", ZAY_DECLARE_GLUE(params, this)
    {
        if(3 <= params.ParamCount())
        {
            auto Ch = params.Param(0).ToText();
            auto QuestIdx = params.Param(1).ToInteger();
            auto ScriptField = params.Param(2).ToText();
            auto StepIdx = (3 < params.ParamCount())? params.Param(3).ToInteger() : -1;
            if(auto CurProject = mProjects.Access(mLastVdsProjCode))
            {
                // 라스트퀘스트 확인
                const String CacheLastQuestDom = mLastVdsProjCode + String::Format(".%s.cache.lastquest", (chars) Ch);
                const sint32 OldQuestIdx = (ZayWidgetDOM::ExistVariable(CacheLastQuestDom))? ZayWidgetDOM::GetValue(CacheLastQuestDom).ToInteger() : -1;
                const sint32 NewQuestIdx = (QuestIdx != -1)? QuestIdx : OldQuestIdx;
                if(NewQuestIdx != -1)
                {
                    const String QuestCode = ZayWidgetDOM::GetValue(mLastVdsProjCode + String::Format(".data.quest.%d.code", NewQuestIdx)).ToText();
                    if(ScriptField == "script_start")
                    {
                        CurProject->mChQuests(Ch)(QuestCode) = NewQuestIdx;
                        // 라스트퀘스트 기억
                        ZayWidgetDOM::SetValue(CacheLastQuestDom, String::FromInteger(NewQuestIdx));
                        // 스텝포커스 저장 및 받아오기
                        const String CacheDom = mLastVdsProjCode + String::Format(".%s.cache.quest.%d", (chars) Ch, NewQuestIdx);
                        if(StepIdx == -1) StepIdx = ZayWidgetDOM::GetValue(CacheDom + ".focus").ToInteger();
                        else ZayWidgetDOM::SetValue(CacheDom + ".focus", String::FromInteger(StepIdx));
                        // 스텝별 스크립트가 있으면 쌓아둠
                        if(RunEntry(mLastVdsProjCode, Ch, NewQuestIdx, String::Format("step.%d.script", StepIdx)))
                            invalidate();
                        // 퀘스트진입 스크립트가 있으면 쌓아둠
                        if(RunEntry(mLastVdsProjCode, Ch, NewQuestIdx, "script_start"))
                            invalidate();
                    }
                    else if(ScriptField == "script_stop")
                    {
                        CurProject->mChQuests(Ch).Remove(QuestCode);
                        // 라스트퀘스트 제거
                        if(QuestIdx == -1)
                            ZayWidgetDOM::RemoveVariables(CacheLastQuestDom);
                        // 퀘스트퇴장 스크립트가 있으면 쌓아둠
                        if(RunEntry(mLastVdsProjCode, Ch, NewQuestIdx, "script_stop"))
                            invalidate();
                    }
                }
            }
        }
        else SendWidgetLog(mLastVdsProjCode, "vdsentry의 파라미터 수량에러");
    }, "[Ch:'채널명'][QuestIdx:'퀘스트Idx'][ScriptField:'스크립트필드명']#[StepIdx:'스텝Idx']", "퀘스트로 VDS스크립트를 시작/종료");

    ////////////////////////////////////////////////////////////////////////////////
    // vdsreset
    NewZayson.AddGlue("vdsreset", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto Ch = params.Param(0).ToText();
            auto QuestIdx = params.Param(1).ToInteger();
            if(mProjects.Access(mLastVdsProjCode))
            {
                const String CacheDom = mLastVdsProjCode + String::Format(".%s.cache.quest.%d", (chars) Ch, QuestIdx);
                ZayWidgetDOM::RemoveVariables(CacheDom + ".result.");
            }
            invalidate();
        }
        else SendWidgetLog(mLastVdsProjCode, "vdsreset의 파라미터 수량에러");
    }, "[Ch:'채널명'][QuestIdx:'퀘스트Idx']", "VDS스크립트의 결과를 초기화");

    ////////////////////////////////////////////////////////////////////////////////
    // vdsstop
    NewZayson.AddGlue("vdsstop", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 1)
        {
            auto Ch = params.Param(0).ToText();
            if(StopEntry(mLastVdsProjCode, Ch))
                invalidate();
        }
        else SendWidgetLog(mLastVdsProjCode, "vdsstop의 파라미터 수량에러");
    }, "[Ch:'채널명']", "VDS스크립트의 통신을 중지");

    ////////////////////////////////////////////////////////////////////////////////
    // vdsseek
    NewZayson.AddGlue("vdsseek", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 3)
        {
            auto Ch = params.Param(0).ToText();
            auto QuestIdx = params.Param(1).ToInteger();
            auto StepIdx = params.Param(2).ToInteger();
            if(mProjects.Access(mLastVdsProjCode))
            {
                const String CacheDom = mLastVdsProjCode + String::Format(".%s.cache.quest.%d", (chars) Ch, QuestIdx);
                const sint32 StepFocus = ZayWidgetDOM::GetValue(CacheDom + ".focus").ToInteger();
                const String CacheStepDom = CacheDom + ".result.step" + String::FromInteger(StepFocus);
                ZayWidgetDOM::SetValue(CacheDom + ".next_step", String::FromInteger(StepIdx));
                ZayWidgetDOM::SetValue(CacheDom + ".next_wait", "0");
                ZayWidgetDOM::SetValue(CacheStepDom, "0"); // 실패
                ZayWidgetDOM::SetValue(CacheDom + ".result.errored", "1"); // 에러있음
                invalidate();
            }
        }
        else SendWidgetLog(mLastVdsProjCode, "vdsseek의 파라미터 수량에러");
    }, "[Ch:'채널명'][QuestIdx:'퀘스트Idx'][StepIdx:'스텝Idx']", "VDS스크립트를 포커스이동");

    ////////////////////////////////////////////////////////////////////////////////
    // vdscheck
    NewZayson.AddGlue("vdscheck", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 3)
        {
            auto Ch = params.Param(0).ToText();
            auto QuestIdx = params.Param(1).ToInteger();
            auto UserFlag = params.Param(2).ToText();
            if(mProjects.Access(mLastVdsProjCode))
            {
                const String CacheDom = mLastVdsProjCode + String::Format(".%s.cache.quest.%d", (chars) Ch, QuestIdx);
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
                        const String StepDom = mLastVdsProjCode + String::Format(".data.quest.%d.step.%d", QuestIdx, StepFocus);
                        const sint32 NextStep = ZayWidgetDOM::GetValue(StepDom + ".next_step").ToInteger();
                        ZayWidgetDOM::SetValue(CacheDom + ".next_step", String::FromInteger(NextStep));
                        if(GoNext == 1)
                        {
                            ZayWidgetDOM::SetValue(CacheDom + ".next_wait", "1 ~ 0 : 0.5"); // 0.5초 기다림
                            const uint64 NewUpdateMsec = Platform::Utility::CurrentTimeMsec() + 500;
                            if(mUpdateMsec < NewUpdateMsec)
                                mUpdateMsec = NewUpdateMsec;
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
                invalidate();
            }
        }
        else SendWidgetLog(mLastVdsProjCode, "vdscheck의 파라미터 수량에러");
    }, "[Ch:'채널명'][QuestIdx:'퀘스트Idx'][UserFlag:ok|skip|seek_n]", "VDS스크립트에 사용자체크");

    ////////////////////////////////////////////////////////////////////////////////
    // vdssavestore
    NewZayson.AddGlue("vdssavestore", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto Ch = params.Param(0).ToText();
            auto AssetPath = params.Param(1).ToText();
            mChannel.SaveStore(Ch, AssetPath);
        }
        else SendWidgetLog(mLastVdsProjCode, "vdssavestore의 파라미터 수량에러");
    }, "[Ch:'채널명'][AssetPath:'log/a.txt']", "VDS채널의 읽기버퍼 파일저장");

    ////////////////////////////////////////////////////////////////////////////////
    // vdssetmonitor
    NewZayson.AddGlue("vdssetmonitorfocus", ZAY_DECLARE_GLUE(params, this)
    {
        if(params.ParamCount() == 2)
        {
            auto Ch = params.Param(0).ToText();
            auto MonitorFocus = params.Param(1).ToText();
            mChannel.SetMonitorFocus(MonitorFocus);
        }
        else SendWidgetLog(mLastVdsProjCode, "vdssetmonitorfocus의 파라미터 수량에러");
    }, "[Ch:'채널명'][MonitorFocus:'d.hxz.data.x00']", "VDS채널의 패킷모니터기능");

    ////////////////////////////////////////////////////////////////////////////////
    // user_content
    NewZayson.AddComponent(ZayExtend::ComponentType::ContentWithParameter, "user_content", ZAY_DECLARE_COMPONENT(panel, params, this)
    {
        if(params.ParamCount() < 1)
            return panel._push_pass();
        const String Type = params.Param(0).ToText();
        bool HasRender = false;

        branch;
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
                            CollectModifiedDoms(CurWidgetName, ProjCode);
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
                    if(FileData[0] == 0x89 && FileData[1] == 0x50 && FileData[2] == 0x4E && FileData[3] == 0x47)
                        FileName += ".png";
                    else if(FileData[0] == 0xFF && FileData[1] == 0xD8 && FileData[2] == 0xFF)
                        FileName += ".jpg";
                    else if(FileData[0] == 0x42 && FileData[1] == 0x4D)
                        FileName += ".bmp";
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
        mMovies(assetname) = Platform::Animate::OpenForMovieFile(Platform::File::RootForAssets() + assetname);
    if(auto CurMovie = mMovies.Access(assetname))
    {
        Platform::Animate::Next(*CurMovie, true);
        auto Pos = panel.toview(0, 0);
        Platform::Animate::Draw(*CurMovie, Pos.x, Pos.y, panel.w(), panel.h(), 0);
    }
    return true;
}

bool ZayAppData::RenderUC_APng(ZayPanel& panel, chars assetname, double sec)
{
    if(!mAPngs.Access(assetname))
        mAPngs(assetname) = Platform::Animate::OpenForAPngFile(Platform::File::RootForAssets() + assetname);
    if(auto CurAPng = mAPngs.Access(assetname))
    {
        Platform::Animate::Seek(*CurAPng, sec, true);
        auto Pos = panel.toview(0, 0);
        Platform::Animate::Draw(*CurAPng, Pos.x, Pos.y, panel.w(), panel.h(), 0);
    }
    return true;
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
                    if(auto CurWidgetName = mLastWidgetName.Access(mLastProjCode))
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
        ZAY_GESTURE_VNTXY(v, n, t, x, y, DomName)
        {
            static sint32 PressX = 0, PressY = 0;
            static sint32 ZoomX = 0, ZoomY = 0;
            if(t == GT_Pressed)
            {
                PressX = x; PressY = y;
                ZoomX = ZayWidgetDOM::GetValue(DomName + ".x").ToInteger();
                ZoomY = ZayWidgetDOM::GetValue(DomName + ".y").ToInteger();
                v->clearCapture();
            }
            else if(t == GT_InDragging || t == GT_OutDragging)
            {
                ZayWidgetDOM::SetValue(DomName + ".x", String::FromInteger(ZoomX + (x - PressX)));
                ZayWidgetDOM::SetValue(DomName + ".y", String::FromInteger(ZoomY + (y - PressY)));
                v->invalidate();
            }
            else if(t == GT_WheelUp || t == GT_WheelDown)
            {
                const float Speed = 0.05;
                const auto CurRect = v->rect();
                const sint32 CursorX = x - (CurRect.l + CurRect.r) / 2;
                const sint32 CursorY = y - (CurRect.t + CurRect.b) / 2;
                const float CurZoomX = ZayWidgetDOM::GetValue(DomName + ".x").ToFloat();
                const float CurZoomY = ZayWidgetDOM::GetValue(DomName + ".y").ToFloat();
                const sint32 OldZoomW = ZayWidgetDOM::GetValue(DomName + ".w").ToInteger();
                const sint32 NewZoomW = OldZoomW + ((t == GT_WheelUp)? -1 : 1);
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
