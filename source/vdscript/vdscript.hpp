#pragma once
#include <boss.hpp>
#include "vds_object.hpp"

////////////////////////////////////////////////////////////////////////////////
// VDParam
class VDParam
{
public:
    String mName;
    String mValue;
};
typedef Array<VDParam> VDParams;

////////////////////////////////////////////////////////////////////////////////
// VDEntry
class VDEntry
{
public:
    Strings mBatons;
    String mComment;
    VDParams mParams;
};
typedef Map<VDEntry> VDEntries;

////////////////////////////////////////////////////////////////////////////////
// VDBody
class VDBody
{
public:
    String mAction;
    sint32 mPreceding {0}; // 선행 N바이트가 모두 인출될 때까지 DOM/action 처리보류
    Strings mBatons;
    Strings mBatonFails; // batonlife 최종 실패시 연결할 send바통
    Array<sint32> mBatonLives; // response 대기시간 목록(ms), 미지정 기본값은 [5000]
    bool mIsBatonLifeRepeat {false}; // scalar 0/양수일 때 무한 재시도
    bool mIsBatonLifeArray {false}; // Save시 배열문법 보존
    bool mIsBatonLifeDefined {false}; // 미지정 기본값(-5000)과 명시값 구분
    String mBatonPass;
    String mClass;
    String mMonitor;
    Strings mCodeLines;
    VDSObject mCode;
    String mComment;
};
typedef Map<VDBody> VDBodies;

////////////////////////////////////////////////////////////////////////////////
// VDScript
class VDScript
{
public:
    void Clear();
    void LoadMore(const String json);
    String Save() const;
    const VDEntry* GetEntry(chars entryid) const;
    const VDBody* GetBody(chars bodyid) const;

private:
    VDEntries mEntries;
    VDBodies mBodies;
};
typedef Map<VDScript> VDScripts;
