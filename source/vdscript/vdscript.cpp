#include <boss.hpp>
#include "vdscript.hpp"

void VDScript::Clear()
{
    mEntries.Reset();
    mBodies.Reset();
}

void VDScript::LoadMore(const String json)
{
    const Context Json(ST_Json, SO_OnlyReference, json);    
    for(sint32 i = 0, iend = Json("entry").LengthOfNamable(); i < iend; ++i)
    {
        chararray EntryID;
        auto& JsonEntry = Json("entry")(i, &EntryID);
        auto& NewEntry = mEntries(&EntryID[0]);
        for(sint32 j = 0, jend = JsonEntry("baton").LengthOfIndexable(); j < jend; ++j)
            NewEntry.mBatons.AtAdding() = JsonEntry("baton")[j].GetText();
        NewEntry.mComment = JsonEntry("comment").GetText();
        for(sint32 j = 0, jend = JsonEntry("param").LengthOfIndexable(); j < jend; ++j)
        {
            auto& NewParam = NewEntry.mParams.AtAdding();
            NewParam.mName = JsonEntry("param")[j]("name").GetText();
            NewParam.mValue = JsonEntry("param")[j]("value").GetText();
        }
    }
    for(sint32 i = 0, iend = Json("body").LengthOfNamable(); i < iend; ++i)
    {
        chararray BodyID;
        auto& JsonBody = Json("body")(i, &BodyID);
        auto& NewBody = mBodies(&BodyID[0]);
        NewBody.mAction = JsonBody("action").GetText();
        auto& JsonPreceding = JsonBody("preceding");
        NewBody.mPreceding = (JsonPreceding.IsValid())? Math::Max(0, JsonPreceding.GetInt()) : 0;
        NewBody.mBatons.Clear();
        for(sint32 j = 0, jend = JsonBody("baton").LengthOfIndexable(); j < jend; ++j)
            NewBody.mBatons.AtAdding() = JsonBody("baton")[j].GetText();
        NewBody.mBatonFails.Clear();
        for(sint32 j = 0, jend = JsonBody("batonfail").LengthOfIndexable(); j < jend; ++j)
            NewBody.mBatonFails.AtAdding() = JsonBody("batonfail")[j].GetText();
        // batonlife 문법
        //   미지정    : -5000과 동일(5초 한 번 대기 후 실패)
        //   0/양수    : 해당 시간마다 무한 재시도
        //   음수      : 절댓값 시간 한 번 대기 후 실패
        //   배열      : 각 원소 시간만큼 단계별 대기/재시도 후 실패
        NewBody.mBatonLives.Clear();
        NewBody.mIsBatonLifeRepeat = false;
        NewBody.mIsBatonLifeArray = false;
        NewBody.mIsBatonLifeDefined = false;
        auto& JsonBatonLife = JsonBody("batonlife");
        if(!JsonBatonLife.IsValid()) // 미지정 또는 비어 있거나 유효값이 없는 배열은 기본값 -5000
            NewBody.mBatonLives.AtAdding() = 5000;
        else
        {
            const sint32 BatonLifeCount = JsonBatonLife.LengthOfIndexable();
            if(0 < BatonLifeCount)
            {
                NewBody.mIsBatonLifeDefined = true;
                NewBody.mIsBatonLifeArray = true;
                for(sint32 j = 0; j < BatonLifeCount; ++j)
                {
                    const sint32 CurLife = JsonBatonLife[j].GetInt();
                    NewBody.mBatonLives.AtAdding() = Math::Abs(CurLife);
                }
            }
            else
            {
                NewBody.mIsBatonLifeDefined = true;
                const sint32 CurLife = JsonBatonLife.GetInt();
                NewBody.mIsBatonLifeRepeat = (0 <= CurLife);
                NewBody.mBatonLives.AtAdding() = Math::Abs(CurLife);
            }
        }
        NewBody.mBatonPass = JsonBody("batonpass").GetText();
        NewBody.mClass = JsonBody("class").GetText();
        NewBody.mMonitor = JsonBody("monitor").GetText();
        NewBody.mCodeLines.Clear();
        for(sint32 j = 0, jend = JsonBody("code").LengthOfIndexable(); j < jend; ++j)
            NewBody.mCodeLines.AtAdding() = JsonBody("code")[j].GetText();
        String WholeCode;
        for(sint32 j = 0, jend = NewBody.mCodeLines.Count(); j < jend; ++j)
            WholeCode += NewBody.mCodeLines[j];
        NewBody.mCode.Parse(WholeCode);
        NewBody.mComment = JsonBody("comment").GetText();
    }
}

String VDScript::Save() const
{
    Context Json;
    mEntries.AccessByCallback(
        [](const MapPath* path, VDEntry* entry, payload data)->void
        {
            const chararray EntryID = path->GetPath();
            auto& JsonEntry = ((Context*) data)->At(&EntryID[0]);
            if(0 < entry->mBatons.Count())
            for(sint32 j = 0, jend = entry->mBatons.Count(); j < jend; ++j)
                JsonEntry.At("baton").AtAdding().Set(entry->mBatons[j]);
            if(0 < entry->mComment.Length())
                JsonEntry.At("comment").Set(entry->mComment);
            if(0 < entry->mParams.Count())
            for(sint32 j = 0, jend = entry->mParams.Count(); j < jend; ++j)
            {
                auto& JsonParam = JsonEntry.At("param").AtAdding();
                JsonParam.At("name").Set(entry->mParams[j].mName);
                JsonParam.At("value").Set(entry->mParams[j].mValue);
            }
        }, &Json.At("entry"));
    mBodies.AccessByCallback(
        [](const MapPath* path, VDBody* body, payload data)->void
        {
            const chararray BodyID = path->GetPath();
            auto& JsonBody = ((Context*) data)->At(&BodyID[0]);
            if(0 < body->mAction.Length())
                JsonBody.At("action").Set(body->mAction);
            if(0 < body->mPreceding)
                JsonBody.At("preceding").Set(String::FromInteger(body->mPreceding));
            if(0 < body->mBatons.Count())
            for(sint32 j = 0, jend = body->mBatons.Count(); j < jend; ++j)
                JsonBody.At("baton").AtAdding().Set(body->mBatons[j]);
            if(0 < body->mBatonFails.Count())
            for(sint32 j = 0, jend = body->mBatonFails.Count(); j < jend; ++j)
                JsonBody.At("batonfail").AtAdding().Set(body->mBatonFails[j]);
            if(body->mIsBatonLifeDefined)
            {
                if(body->mIsBatonLifeArray)
                {
                    for(sint32 j = 0, jend = body->mBatonLives.Count(); j < jend; ++j)
                        JsonBody.At("batonlife").AtAdding().Set(String::FromInteger(body->mBatonLives[j]));
                }
                else
                {
                    const sint32 BatonLife = body->mBatonLives[0];
                    JsonBody.At("batonlife").Set(String::FromInteger(
                        (body->mIsBatonLifeRepeat)? BatonLife : -BatonLife));
                }
            }
            if(0 < body->mBatonPass.Length())
                JsonBody.At("batonpass").Set(body->mBatonPass);
            if(0 < body->mClass.Length())
                JsonBody.At("class").Set(body->mClass);
            if(0 < body->mMonitor.Length())
                JsonBody.At("monitor").Set(body->mMonitor);
            if(0 < body->mCodeLines.Count())
            for(sint32 j = 0, jend = body->mCodeLines.Count(); j < jend; ++j)
                JsonBody.At("code").AtAdding().Set(body->mCodeLines[j]);
            if(0 < body->mComment.Length())
                JsonBody.At("comment").Set(body->mComment);
        }, &Json.At("body"));
    return Json.SaveJson();
}

const VDEntry* VDScript::GetEntry(chars entryid) const
{
    return mEntries.Access(entryid);
}

const VDBody* VDScript::GetBody(chars bodyid) const
{
    return mBodies.Access(bodyid);
}
