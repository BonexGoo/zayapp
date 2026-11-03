#pragma once
#include <service/boss_zaywidget.hpp>
#include "dependencytable.hpp"
#include "vdscript/vdchannel.hpp"
#include "vdscript/vdscript.hpp"
#include "usb/visaapi.hpp"

class DomJson
{
public:
    String mAssetName;
    String mHeaderName;
    uint64 mModifiedMsec {0};
    bool mUpdated {false};
};
typedef Map<DomJson> DomJsons;

class UrlImage
{
public:
    UrlImage() {}
    ~UrlImage() {}
    BOSS_DECLARE_NONCOPYABLE_CLASS(UrlImage)

public:
    String mFileTitle;
    uint64 mFadeMsec {0};
    uint64 mLoadedMsec {0};
    Image mImage;
    id_tasking mTasking {nullptr};
};
typedef Map<UrlImage> UrlImages;

class HotKey
{
public:
    HotKey() {}
    ~HotKey() {}
    BOSS_DECLARE_NONCOPYABLE_CLASS(HotKey)

public:
    String mPressGate;
    String mReleaseGate;
};
typedef Map<HotKey> HotKeys;

class Quest
{
public:
    sint32 mQuestIndex {0};
    sint32 mStepIndex {-1};
};
typedef Map<Quest> Quests;

class LogState
{
public:
    sint32 mNumber {-1};
    sint32 mCount {0};
    sint32 mOffset {-1};
    enum {MAX = 1000};
    String mTexts[MAX];
    Color mColors[MAX];
};
typedef Map<LogState> LogStates;

class Project
{
public:
    VDScript mScript;
    HotKeys mHotKeys; // [13:Enter키] -> HotKey
    typedef Map<sint32> Quests;
    Map<Quests> mChQuests; // [Ch:COM6][QuestCode:HLT] -> QuestIdx
};
typedef Map<Project> Projects;

class ZayAppData : public ZayObject
{
public:
    ZayAppData();
    ~ZayAppData();
public:
    static void ForcedRemove(chars itemname, payload data = nullptr);
    static uint32 UpdateCrc(bytes data, sint32 datalen, uint32 oldcrc = 0xFFFFFFFF);

public:
    bool IsFullScreen();
    void SetFullScreen();
    void SetNormalWindow();
    void SetCursor(CursorRole role);
    sint32 MoveNcLeft(const rect128& rect, sint32 addx);
    sint32 MoveNcTop(const rect128& rect, sint32 addy);
    sint32 MoveNcRight(const rect128& rect, sint32 addx);
    sint32 MoveNcBottom(const rect128& rect, sint32 addy);
    void CollectModifiedDoms(chars widgetname, chars projcode, bool forced = false);
    bool UpdateModifiedDoms();
    bool UpdateModifiedVds();
    void InitWidget(ZayWidget* widget, const String viewname);
    void RenderWindowOutline(ZayPanel& panel);
    bool RenderUC_UrlImage(ZayPanel& panel, double fadesec, chars url);
    bool RenderUC_Lottie(ZayPanel& panel, chars assetname, double sec);
    bool RenderUC_Movie(ZayPanel& panel, chars assetname, double sec);
    bool RenderUC_APng(ZayPanel& panel, chars assetname, double sec);
    bool RenderUC_Graph(ZayPanel& panel, chars graphname);
    bool RenderUC_Tag(ZayPanel& panel, chars posdom, chars text, sint32 linegap, sint32 lastgap, sint32 border, chars color, bool anticut);
    bool RenderUC_Log(ZayPanel& panel, chars ch, sint32 index);
    bool RenderUC_Slider(ZayPanel& panel, chars domname, sint32 min, sint32 max, sint32 unit);
    bool RenderUC_SlideVH(ZayPanel& panel, chars domname, sint32 min, sint32 limit, sint32 max, sint32 step, bool vertical, sint32 border, const String& gatename);
    bool RenderUC_Picker(ZayPanel& panel, chars domname, sint32 min, sint32 max, sint32 step, sint32 def, sint32 trunc, chars unit, sint32 side, chars imagename);
    bool RenderUC_TouchCircle(ZayPanel& panel, sint32 x, sint32 y, sint32 width, sint32 height, chars domname, bool show, const Points& dots);
    bool RenderUC_ZoomArea(ZayPanel& panel, chars domname);

public:
    void UpdateChannelList();
    void UpdateDeviceList();
    void UpdateStorageList();
    void UpdateNetworkList();
    void UpdateSoundDeviceList(sint32 focus = -1);
    void UpdatePackageList();
    void UpdateIPAddress();
    void RunInstallWpk(chars wpkpath, chars exclusions);
    void AddLog(chars ch, const String& text, const String& color);
    void AddHexLog(chars ch, chars tag, bytes data, sint32 size, const String& color);
    bool RunEntry(chars projcode, chars ch, sint32 questidx, chars scriptfield);
    bool StopEntry(chars projcode, chars ch);
    bool ValidScope();
    void SendWidgetLog(chars projcode, chars text);

public:
    ZayWidget mMainWidget;
    Map<ZayWidget> mSubWidgets; // [widgetname]
    uint64 mUpdateMsec {0};
    bool mNcLeftBorder {false};
    bool mNcTopBorder {false};
    bool mNcRightBorder {false};
    bool mNcBottomBorder {false};
    bool mIsFullScreen {false};
    rect128 mSavedNormalRect {0, 0, 0, 0};
    CursorRole mNowCursor {CR_Arrow};
    DomJsons mDomJsons; // [filename]
    UrlImages mUrlImages; // [url]
    Image mLastGrabImage;
    DependencyTable mDependencyTable;
    Map<bool> mLoadedTableFiles; // Successfully parsed CSV asset paths
    const InnerTable* mFocusedTable {nullptr};
    Map<id_animate> mLotties; // [assetpath]
    Map<id_animate> mMovies; // [assetpath]
    Map<id_animate> mAPngs; // [assetpath]
    Map<id_sound> mSounds; // [assetpath]
    sint32 mSoundDeviceFocus {-1};
    bool mSoundMuted {false};

public:
    String mLastProjCode; // cmd, mvf, nrf
    String mLastVdsProjCode; // cmd, mvf, nrf
    Map<uint64> mLastVdsModifyMsec; // [projcode] -> msec
    Map<String> mLastWidgetName; // [projcode] -> cmd, mvf_test1, nrf_fact
    VDSObject mEditText; // 직접송신용 스크립트에디터 (user_content: script)
    Map<doubles> mGraphs; // [name]
    Map<String> mLogHighlights; // [colorcode] -> matchtext
    VDChannel mChannel; // 모든 채널관리
    Map<String> mDevices; // [devicename:'001'] -> Ch
    typedef struct {sint64 mFree; sint64 mTotal;} Storage;
    Map<Storage> mStorages; // [path:'D:/'] -> Storage
    typedef struct {bool mConnected; sint32 mSignal; String mSecurity; String mIp;} Wifi;
    Map<Wifi> mWifis; // [ssid:'weero612_5G'] -> Wifi
    id_tasking mPackageList {nullptr};
    id_tasking mInstallWpk {nullptr};
    id_tasking mIPAddress {nullptr};
    Projects mProjects; // [projcode]
    LogStates mLogStates; // 채널별 로그관리 [Ch]
    VisaApi* mOscilloScope {nullptr};
};
