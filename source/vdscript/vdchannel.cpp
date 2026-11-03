#include <boss.hpp>
#include "vdchannel.hpp"

#include <platform/boss_platform.hpp>

////////////////////////////////////////////////////////////////////////////////
// VDPeerSocket
class VDPeerSocket : public VDPeer
{
public:
	VDPeerSocket(id_server server, sint32 peerid) : mRefServer(server), mPeerID(peerid)
	{
	}
	~VDPeerSocket() override
	{
		Platform::Server::KickPeer(mRefServer, mPeerID);
	}

private:
    bool Connected() const override
    {
        return true;
    }
	bool Write(bytes data, sint32 size) const override
	{
        return Platform::Server::SendToPeer(mRefServer, mPeerID, data, size);
	}
    bool ReadOnce(Logger logger, chars ch, sint32* storesize) override
	{
        sint32 DataSize = 0;
        if(bytes DataPtr = Platform::Server::GetPacketBuffer(mRefServer, &DataSize))
        {
            if(0 < DataSize)
            {
                const sint32 StoreSize = Store(DataPtr, DataSize, logger, ch);
                if(storesize) *storesize = StoreSize;
                logger(ch, "normal:(recv)", DataPtr, DataSize);
                return true;
            }
        }
        return false;
	}

private:
	const id_server mRefServer;
    const sint32 mPeerID;
};

////////////////////////////////////////////////////////////////////////////////
// VDPeerSerial
class VDPeerSerial : public VDPeer
{
public:
	VDPeerSerial(chars port, sint32 baudrate = -1) : mPort(port)
	{
        mSerial = Platform::Serial::Open(mPort, baudrate);
	}
	~VDPeerSerial() override
	{
		Platform::Serial::Close(mSerial);
	}

private:
    bool Connected() const override
    {
        return Platform::Serial::Connected(mSerial);
    }
	bool Write(bytes data, sint32 size) const override
	{
		if(Platform::Serial::WriteData(mSerial, data, size))
        {
            Platform::Serial::WriteFlush(mSerial);
            return true;
        }
        return false;
	}
    bool ReadOnce(Logger logger, chars ch, sint32* storesize) override
	{
        if(auto ReadCount = Platform::Serial::ReadAvailable(mSerial))
        {
            uint08s ReadPacket;
            ReadCount = Platform::Serial::ReadData(mSerial, ReadPacket.AtDumpingAdded(ReadCount), ReadCount);
            if(0 < ReadCount)
            {
                const sint32 StoreSize = Store(&ReadPacket[0], ReadCount, logger, ch);
                if(storesize) *storesize = StoreSize;
                logger(ch, "normal:(recv)", &ReadPacket[0], ReadCount);
                return true;
            }
        }
        return false;
	}

private:
    const String mPort;
	id_serial mSerial;
};

////////////////////////////////////////////////////////////////////////////////
// VDPeer
VDPeer::VDPeer()
{
    mStoreMutex = Mutex::Open();
}

VDPeer::~VDPeer()
{
    ClearBaton();
    Mutex::Close(mStoreMutex);
}

VDPeer* VDPeer::CreateSocket(id_server server, sint32 peerid)
{
	auto NewPeer = new VDPeerSocket(server, peerid);
	return NewPeer;
}

VDPeer* VDPeer::CreateSerial(chars port, sint32 baudrate)
{
    VDPeer* NewPeer = new VDPeerSerial(port, baudrate);
    if(NewPeer->Connected())
        return NewPeer;
    delete NewPeer;
    return nullptr;
}

void VDPeer::Verify(chars sendcode, chars recvcode)
{
	////////////////////////////
}

void VDPeer::RunEntry(const VDScript& script, const VDEntry& entry, const Strings params, Setter setter, chars ch)
{
    for(sint32 i = 0, iend = entry.mParams.Count(); i < iend; ++i)
    {
        if(i < params.Count())
            setter(ch, entry.mParams[i].mName, params[i]);
        else setter(ch, entry.mParams[i].mName, entry.mParams[i].mValue);
    }

    // 새로운 바통은 최상위로 등록
    const uint64 NowMsec = Platform::Utility::CurrentTimeMsec();
    for(sint32 i = entry.mBatons.Count() - 1; 0 <= i; --i)
    {
        VDBaton* NewBaton = new VDBaton();
        NewBaton->mScript = &script;
        NewBaton->mBodyIDs = entry.mBatons[i];
        NewBaton->mExpireMsec = NowMsec + 5000;
        NewBaton->mNext = mBatonHead.mNext;
        mBatonHead.mNext = NewBaton;
    }
    if(mBatonFocus == nullptr)
        mBatonFocus = mBatonHead.mNext;
}

void VDPeer::ClearBaton(const VDScript* script)
{
    VDBaton* PrevBaton = &mBatonHead;
    while(PrevBaton->mNext)
    {
        VDBaton* CurBaton = PrevBaton->mNext;
        if(script == nullptr || CurBaton->mScript == script)
        {
            if(mBatonFocus == CurBaton)
                mBatonFocus = CurBaton->mNext;
            PrevBaton->mNext = CurBaton->mNext;
            CurBaton->mNext = nullptr;
            delete CurBaton;
        }
        else PrevBaton = CurBaton;
    }
    if(mBatonFocus == nullptr)
        mBatonFocus = mBatonHead.mNext;
}

bool VDPeer::BatonOnce(Getter getter, Setter setter, Actor actor, Logger logger, chars ch, chars monitorfocus)
{
	// 타임아웃된 바통처리
    bool ExpiredBaton = false;
    const uint64 NowMsec = Platform::Utility::CurrentTimeMsec();
    VDBaton* PrevBaton = &mBatonHead;
    while(PrevBaton->mNext)
    {
        VDBaton* CurBaton = PrevBaton->mNext;
        if(0 < CurBaton->mExpireMsec && CurBaton->mExpireMsec <= NowMsec)
        {
            const VDScript* OldScript = CurBaton->mScript;
            const String OldBodyIDs = CurBaton->mBodyIDs;
            const String OldRetryBodyID = CurBaton->mRetryBodyID;
            const sint32 OldBatonLifeStep = CurBaton->mBatonLifeStep;
            const sint32 OldRetryCount = CurBaton->mRetryCount;
            const sint32 OldResponseFocus = CurBaton->mResponseFocus;
            const uint32 OldScanAbsFocus = CurBaton->mScanAbsFocus;
            if(mBatonFocus == CurBaton)
                mBatonFocus = CurBaton->mNext;
            PrevBaton->mNext = CurBaton->mNext;
            CurBaton->mNext = nullptr;
            delete CurBaton;
            if(0 < OldRetryBodyID.Length())
            {
                const VDBody* RetryBody = OldScript->GetBody(OldRetryBodyID);
                const bool CanRetry = (RetryBody && RetryBody->mClass == "send" &&
                    (RetryBody->mIsBatonLifeRepeat || OldBatonLifeStep + 1 < RetryBody->mBatonLives.Count()));
                // Retry대상의 batonpass가 허용된 경우에만 재시도
                bool RetryEnabled = true;
                if(RetryBody && 0 < RetryBody->mBatonPass.Length())
                    RetryEnabled = (0 < getter(ch, RetryBody->mBatonPass).ToFloat());

                if(CanRetry && RetryEnabled)
                {
                    // Retry될 바통생성
                    VDBaton* NewBaton = new VDBaton();
                    NewBaton->mScript = OldScript;
                    NewBaton->mBodyIDs = OldRetryBodyID;
                    NewBaton->mBatonLifeStep = (RetryBody->mIsBatonLifeRepeat)?
                        OldBatonLifeStep : OldBatonLifeStep + 1;
                    NewBaton->mRetryCount = (OldRetryCount < 0x7FFFFFFF)? OldRetryCount + 1 : OldRetryCount;
                    NewBaton->mResponseFocus = OldResponseFocus;
                    NewBaton->mScanAbsFocus = OldScanAbsFocus;
                    // 최하단에 바통연결
                    VDBaton* LastBaton = &mBatonHead;
                    while(LastBaton->mNext)
                        LastBaton = LastBaton->mNext;
                    LastBaton->mNext = NewBaton;
                    // 로그기록
                    logger(ch, "info:(retry " + String::FromInteger(NewBaton->mRetryCount)
                        + ") " + OldRetryBodyID, nullptr, 0);
                }
                else if(!RetryEnabled)
                    logger(ch, "info:(retry-canceled) " + OldRetryBodyID, nullptr, 0);
                else
                {
                    // 유한 batonlife의 마지막 대기까지 실패
                    if(RetryBody && 0 < RetryBody->mMonitor.Length())
                        setter(ch, RetryBody->mMonitor + ".retry", "-1");
                    logger(ch, "error:(baton-failed) " + OldRetryBodyID, nullptr, 0);

                    // 최종 실패시 batonfail에 지정된 send바통으로 연결
                    if(RetryBody)
                    for(sint32 i = RetryBody->mBatonFails.Count() - 1; 0 <= i; --i)
                    {
                        const String FailBodyIDs = RetryBody->mBatonFails[i];
                        const Strings FailBodyIDList = String::Split(FailBodyIDs, '|');
                        bool IsSendOnly = (0 < FailBodyIDList.Count());
                        for(sint32 j = 0, jend = FailBodyIDList.Count(); j < jend; ++j)
                        {
                            const VDBody* FailBody = OldScript->GetBody(FailBodyIDList[j]);
                            if(FailBody == nullptr || FailBody->mClass != "send")
                            {
                                IsSendOnly = false;
                                logger(ch, "error:(batonfail-send-only) " + OldRetryBodyID
                                    + " -> " + FailBodyIDList[j], nullptr, 0);
                            }
                        }
                        if(IsSendOnly)
                        {
                            VDBaton* NewBaton = new VDBaton();
                            NewBaton->mScript = OldScript;
                            NewBaton->mBodyIDs = FailBodyIDs;
                            NewBaton->mNext = mBatonHead.mNext;
                            mBatonHead.mNext = NewBaton;
                            logger(ch, "info:(batonfail) " + OldRetryBodyID
                                + " -> " + FailBodyIDs, nullptr, 0);
                        }
                    }
                }
            }
            // 로그기록
            else logger(ch, "info:(expire) " + OldBodyIDs, nullptr, 0);
            ExpiredBaton = true;
            break;
        }
        PrevBaton = PrevBaton->mNext;
    }

    // 포커스된 바통실행(끝에 도달하면 최상위로 순환)
    Strings ClearedBodyIDs;
    if(mBatonFocus == nullptr)
        mBatonFocus = mBatonHead.mNext;
    if(auto TopBaton = mBatonFocus)
    {
        VDBaton* PrevTopBaton = &mBatonHead;
        while(PrevTopBaton->mNext && PrevTopBaton->mNext != TopBaton)
            PrevTopBaton = PrevTopBaton->mNext;

        const VDScript& CurScript = *TopBaton->mScript;
        const Strings CurBodyIDs = String::Split(TopBaton->mBodyIDs, '|');
        // send처리
        for(sint32 i = 0, iend = CurBodyIDs.Count(); i < iend; ++i)
        {
            auto CurBodyID = CurBodyIDs[i];
            if(auto CurBody = CurScript.GetBody(CurBodyID))
            {
                if(CurBody->mClass == "send")
                {
                    // dom옵션이 있으면 최초 0, 재송신 때마다 1, 2, 3...을 실시간 기록
                    if(0 < CurBody->mMonitor.Length())
                        setter(ch, CurBody->mMonitor + ".retry", String::FromInteger(TopBaton->mRetryCount));
                    uint08s NewPacket;
                    CurBody->mCode.WritePacket(NewPacket, getter, ch);
                    if(0 < NewPacket.Count())
                    {
                        Mutex::Lock(mStoreMutex);
                        TopBaton->mResponseFocus = mStoreEnd;
                        // send 이후 생성될 response 바통은 송신시점 이전의 Store를
                        // 다시 검색할 필요가 없다. 절대위치 scan focus도 함께 넘긴다.
                        TopBaton->mScanAbsFocus = mStoreAbsBegin + mStoreEnd;
                        Mutex::Unlock(mStoreMutex);
                        if(Write((bytes) &NewPacket[0], NewPacket.Count()))
                        {
                            logger(ch, "write:(send " + CurBodyID + ")", &NewPacket[0], NewPacket.Count()); // 로그기록
                            FlushMonitor(true, CurBody, setter, ch, monitorfocus, &NewPacket[0], NewPacket.Count());
                        }
                        else logger(ch, "error:(send-failed " + CurBodyID + ")", &NewPacket[0], NewPacket.Count()); // 로그기록
                    }
                    ClearedBodyIDs.AtAdding() = CurBodyID;
                }
            }
        }
        // recv처리
        Mutex::Lock(mStoreMutex);
        {
            FlushStore(logger, ch);
            uint08* StoreData = (uint08*) &mStorePool[0];
            const uint32 StoreAbsEnd = mStoreAbsBegin + mStoreEnd;
            const uint32 ScanAbsBegin = Math::Max((uint32) (mStoreAbsBegin + mStoreBegin), TopBaton->mScanAbsFocus);
            const sint32 ScanBegin = (sint32) Math::Min((uint32) mStoreEnd, ScanAbsBegin - mStoreAbsBegin);
            sint32 LastScanPos = ScanBegin;
            bool PrecedingBlocked = false;
            for(sint32 j = ScanBegin, jend = mStoreEnd; j < jend; ++j)
            {
                LastScanPos = j + 1;
                // 8바이트 경계의 클리어구간은 패킷매칭 없이 다음 블록으로 이동
                if((j & 7) == 0 && j + 8 <= jend && mStorePool[j / 8] == 0)
                {
                    j += 7;
                    continue;
                }
                // 패킷 매칭
                String BestBodyID;
                sint32 BestReadCount = 0;
                for(sint32 i = 0, iend = CurBodyIDs.Count(); i < iend; ++i)
                {
                    auto CurBodyID = CurBodyIDs[i];
                    if(auto CurBody = CurScript.GetBody(CurBodyID))
                    {
                        if(CurBody->mClass == "recv" || (CurBody->mClass == "response" && TopBaton->mResponseFocus <= j))
                        {
                            const sint32 ReadCount = CurBody->mCode.ReadPacket(StoreData, j, jend - j);
		                    if(BestReadCount < ReadCount)
                            {
                                BestBodyID = CurBodyID;
                                BestReadCount = ReadCount;
                            }
                        }
                    }
                }
                // 스토어 매칭
                if(0 < BestReadCount)
                {
                    if(auto CurBody = CurScript.GetBody(BestBodyID))
                    {
                        const uint32 PacketAbsPos = mStoreAbsBegin + j;
                        if(0 < CurBody->mPreceding && !IsPrecedingResolved(PacketAbsPos, CurBody->mPreceding))
                        {
                            // 선행구간이 남아 있으면 이번 매칭은 성립시키지 않는다.
                            // 패킷/DOM/action을 전혀 건드리지 않고 다음번에 같은 위치부터 다시 시도한다.
                            TopBaton->mScanAbsFocus = PacketAbsPos;
                            PrecedingBlocked = true;
                        }
                        else
                        {
                            logger(ch, "read:(recv " + BestBodyID + ")", &StoreData[j], BestReadCount);
                            CurBody->mCode.ReadFlush(setter, ch);
                            FlushMonitor(false, CurBody, setter, ch, monitorfocus, &StoreData[j], BestReadCount);
                            // preceding 조건이 만족된 시점에만 실제 인출(Zero Mark)한다.
                            Memory::Set(&StoreData[j], 0, BestReadCount);
                            // 후속 바통이 방금 처리한 패킷 앞쪽을 다시 훑지 않도록
                            // 매칭 패킷의 끝 절대위치를 승계할 scan focus로 저장한다.
                            TopBaton->mScanAbsFocus = PacketAbsPos + BestReadCount;
                            ClearedBodyIDs.AtAdding() = BestBodyID;
                        }
                    }
                    break;
                }
            }
            // 매칭되지 않은 바통은 이미 검사한 앞 구간을 다시 보지 않는다.
            // 단, Store 끝에 걸친 미완성 패킷이 다음 Read에서 완성될 수 있으므로 64바이트는 재검사한다.
            if(ClearedBodyIDs.Count() == 0 && !PrecedingBlocked)
            {
                static const sint32 ScanTailKeep = 64;
                const sint32 SafeScanPos = Math::Max(ScanBegin, LastScanPos - ScanTailKeep);
                TopBaton->mScanAbsFocus = mStoreAbsBegin + SafeScanPos;
                if(StoreAbsEnd < TopBaton->mScanAbsFocus)
                    TopBaton->mScanAbsFocus = StoreAbsEnd;
            }
        }
        Mutex::Unlock(mStoreMutex);

        // 바통을 완수하면
        if(0 < ClearedBodyIDs.Count())
        {
            const sint32 NextResponseFocus = TopBaton->mResponseFocus;
            const uint32 NextScanAbsFocus = TopBaton->mScanAbsFocus;
            const sint32 NextBatonLifeStep = TopBaton->mBatonLifeStep;
            const sint32 NextRetryCount = TopBaton->mRetryCount;
            const VDScript* NextScript = TopBaton->mScript;
            VDBaton* NextBaton = TopBaton->mNext;
            PrevTopBaton->mNext = NextBaton;
            TopBaton->mNext = nullptr;
            delete TopBaton;
            for(sint32 i = 0, iend = ClearedBodyIDs.Count(); i < iend; ++i)
            {
                auto CurBodyID = ClearedBodyIDs[i];
                if(auto CurBody = CurScript.GetBody(CurBodyID))
                {
                    // 바통패스가 아예 없거나 바통패스가 0이 아니면 바통실행
                    if(CurBody->mBatonPass.Length() == 0 || 0 < getter(ch, CurBody->mBatonPass).ToFloat())
                    for(sint32 j = CurBody->mBatons.Count() - 1; 0 <= j; --j)
                    {
                        // 다음 바통생성
                        VDBaton* NewBaton = new VDBaton();
                        NewBaton->mScript = NextScript;
                        NewBaton->mBodyIDs = CurBody->mBatons[j];
                        // retry로 다시 실행된 send만 자신의 단계/횟수를 response에 전달한다.
                        // 정상 response가 다음 send를 시작할 때는 새 시퀀스이므로 0부터 시작한다.
                        if(CurBody->mClass == "send")
                        {
                            NewBaton->mBatonLifeStep = NextBatonLifeStep;
                            NewBaton->mRetryCount = NextRetryCount;
                        }
                        NewBaton->mResponseFocus = NextResponseFocus;
                        NewBaton->mScanAbsFocus = NextScanAbsFocus;
                        // batonlife는 send body가 만든 다음 response 바통에만 적용
                        if(CurBody->mClass == "send" && 0 < CurBody->mBatonLives.Count())
                        {
                            const sint32 LifeStep = Math::Clamp(NextBatonLifeStep,
                                0, CurBody->mBatonLives.Count() - 1);
                            const sint32 WaitMsec = CurBody->mBatonLives[LifeStep];
                            NewBaton->mExpireMsec = Platform::Utility::CurrentTimeMsec() + WaitMsec;
                            NewBaton->mRetryBodyID = CurBodyID;
                        }
                        NewBaton->mNext = mBatonHead.mNext;
                        mBatonHead.mNext = NewBaton;
                    }
                    // preceding body도 여기까지 왔다면 선행구간 조건이 이미 만족된 상태다.
                    // DOM 저장 후 action을 기존 순서대로 실행한다.
                    if(0 < CurBody->mAction.Length())
                        actor(ch, CurBody->mAction);
                }
            }
            // action에서 ClearBaton이 호출되어 기존 NEXT까지 삭제됐을 수 있으므로
            // 포인터를 역참조하지 않고 리스트에 아직 존재하는지 확인한다.
            bool IsNextBatonAlive = false;
            for(VDBaton* CurBaton = mBatonHead.mNext; CurBaton; CurBaton = CurBaton->mNext)
            if(CurBaton == NextBaton)
            {
                IsNextBatonAlive = true;
                break;
            }
            mBatonFocus = (IsNextBatonAlive)? NextBaton : mBatonHead.mNext;
        }
        else
        {
            // 응답을 기다리는 바통도 다음 호출에서는 NEXT로 넘겨 기아를 방지한다.
            mBatonFocus = (TopBaton->mNext)? TopBaton->mNext : mBatonHead.mNext;
        }
    }
    return (ExpiredBaton || 0 < ClearedBodyIDs.Count());
}

bool VDPeer::IsPrecedingResolved(uint32 packetabspos, sint32 preceding) const
{
    if(preceding <= 0)
        return true;

    // preceding보다 실제 Store에 남아 있는 앞 구간이 짧으면 존재하는 구간만 검사한다.
    const uint32 StoreAbsActiveBegin = mStoreAbsBegin + mStoreBegin;
    const uint32 WantedBeginAbs = (packetabspos < (uint32) preceding)?
        0 : packetabspos - (uint32) preceding;
    const uint32 CheckBeginAbs = Math::Max(WantedBeginAbs, StoreAbsActiveBegin);
    if(packetabspos <= CheckBeginAbs)
        return true;

    const uint08* StoreData = (const uint08*) &mStorePool[0];
    const sint32 CheckBegin = (sint32) (CheckBeginAbs - mStoreAbsBegin);
    const sint32 CheckEnd = (sint32) (packetabspos - mStoreAbsBegin);
    for(sint32 i = CheckBegin; i < CheckEnd; ++i)
        if(StoreData[i] != 0)
            return false;
    return true;
}

sint32 VDPeer::StackSize() const
{
    sint32 StackSize = 0;
    auto CurBaton = &mBatonHead;
    while(CurBaton = CurBaton->mNext)
        StackSize++;
    return StackSize;
}

sint32 VDPeer::ReadBps() const
{
    if(mReadMsec != 0)
    {
        auto CurMsec = Platform::Utility::CurrentTimeMsec();
        return mReadSize / Math::Max(1, (CurMsec - mReadMsec) / 1000);
    }
    return 0;
}

sint64 VDPeer::MissSize() const
{
    return mMissSize;
}

String VDPeer::SaveStore() const
{
    String LineCollector;
    Mutex::Lock(mStoreMutex);
    {
        const uint08* StoreData = (const uint08*) &mStorePool[0];
        const char HexCode[] = "0123456789ABCDEF";
        for(sint32 i = mStoreBegin, iend = mStoreEnd; i < iend;)
        {
            const sint32 LineEnd = Math::Min(i + 16, iend);
            char CurLine[16 * 3 + 2];
            sint32 CurLinePos = 0;
            while(i < LineEnd)
            {
                const uint08 CurByte = StoreData[i++];
                if(0 < CurLinePos) CurLine[CurLinePos++] = ' ';
                CurLine[CurLinePos++] = HexCode[CurByte >> 4];
                CurLine[CurLinePos++] = HexCode[CurByte & 0x0F];
            }
            CurLine[CurLinePos++] = '\r';
            CurLine[CurLinePos++] = '\n';
            LineCollector.AddTail(CurLine, CurLinePos);
        }
    }
    Mutex::Unlock(mStoreMutex);
    return LineCollector;
}

sint32 VDPeer::Store(bytes data, sint32 size, Logger logger, chars ch)
{
    sint32 StoredSize = 0;
    Mutex::Lock(mStoreMutex);
    {
        const uint64 CurMsec = Platform::Utility::CurrentTimeMsec();
        if(mStoreBegin == mStoreEnd)
            mStoreBeginMsec = CurMsec;
        mStoreEndMsec = CurMsec;

        // 읽은 데이터의 정보
        mReadSize += size;
        if(mReadMsec == 0)
            mReadMsec = CurMsec;

        // 읽은 데이터의 저장
        if(size <= StoreBytesMax)
        {
            // 공간확보
            if(StoreBytesMax - mStoreEnd < size)
                RemoveStore(size - (StoreBytesMax - mStoreEnd), logger, ch);
            // 복사
            Memory::Copy(((uint08*) &mStorePool[0]) + mStoreEnd, data, size);
            mStoreEnd += size;
        }
        else
        {
            mMissSize += size; // 너무 큰 데이터는 폐기
            logger(ch, "error:(mismatch_oversize)", data, 8);
        }
        StoredSize = mStoreEnd - mStoreBegin;
    }
    Mutex::Unlock(mStoreMutex);
    return StoredSize;
}

void VDPeer::FlushStore(Logger logger, chars ch)
{
    const sint32 OldStoreBegin = mStoreBegin;
    const sint32 OldStoreSize = mStoreEnd - OldStoreBegin;
    const uint64 OldStoreBeginMsec = mStoreBeginMsec;
    const uint64 StoreMsecRange = mStoreEndMsec - OldStoreBeginMsec;
    sint32 NewStoreBegin = OldStoreBegin;
    uint64 NewStoreBeginMsec = OldStoreBeginMsec;
    bytes StoreData = (bytes) &mStorePool[0];
    while(NewStoreBegin + 8 <= mStoreEnd)
    {
        const bool IsZeroBlock = (mStorePool[NewStoreBegin / 8] == 0);
        const bool IsExpiredBlock = (StoreTimeoutMsec <= mStoreEndMsec - NewStoreBeginMsec);
        if(!IsZeroBlock && !IsExpiredBlock)
            break;
        if(!IsZeroBlock)
        {
            mMissSize += 8;
            logger(ch, "error:(mismatch_timeout)", &StoreData[NewStoreBegin], 8);
        }
        NewStoreBegin += 8;
        NewStoreBeginMsec = OldStoreBeginMsec
            + StoreMsecRange * (NewStoreBegin - OldStoreBegin) / OldStoreSize;
    }
    mStoreBegin = NewStoreBegin;
    mStoreBeginMsec = NewStoreBeginMsec;
}

void VDPeer::RemoveStore(sint32 size, Logger logger, chars ch)
{
    const sint32 RemoveEnd = Math::Clamp((size + 7) / 8 * 8, mStoreBegin, mStoreEnd);
    const sint32 OldStoreSize = mStoreEnd - mStoreBegin;
    const uint64 StoreMsecRange = mStoreEndMsec - mStoreBeginMsec;
    const uint64 NewStoreBeginMsec = (mStoreBegin < RemoveEnd && 0 < OldStoreSize)?
        mStoreBeginMsec + StoreMsecRange * (RemoveEnd - mStoreBegin) / OldStoreSize : mStoreBeginMsec;
    uint08* StoreData = (uint08*) &mStorePool[0];
    // 유실데이터 파악
    for(sint32 i = mStoreBegin / 8, iend = RemoveEnd / 8; i < iend; ++i)
    {
        if(mStorePool[i] != 0)
        {
            mMissSize += 8;
            logger(ch, "error:(mismatch_overflow)", &StoreData[i * 8], 8);
        }
    }
    // 절대 Store위치는 memmove와 무관하게 증가한다.
    // 바통별 mScanAbsFocus는 같은 절대좌표계이므로 일반 RemoveStore에서는 보정하지 않는다.
    mStoreAbsBegin += RemoveEnd;
    RebaseStoreAbs();
    // 데이터 이동
    if(RemoveEnd < mStoreEnd)
        Memory::Move(&StoreData[0], &StoreData[RemoveEnd], mStoreEnd - RemoveEnd);
    // 데이터 포커싱
    mStoreBegin = 0;
    mStoreEnd -= RemoveEnd;
    mStoreBeginMsec = NewStoreBeginMsec;
    if(mStoreEnd == 0)
        mStoreEndMsec = mStoreBeginMsec;
    // 응답위치 포커싱
    for(VDBaton* CurBaton = mBatonHead.mNext; CurBaton; CurBaton = CurBaton->mNext)
        CurBaton->mResponseFocus = Math::Max(0, CurBaton->mResponseFocus - RemoveEnd);
}

void VDPeer::RebaseStoreAbs()
{
    // 절대좌표가 너무 커지면 Store와 모든 바통의 스캔좌표를 같은 값만큼 함께 낮춘다.
    // 오래된 바통은 뺄 값보다 작을 수 있으므로 unsigned underflow 대신 0으로 절삭한다.
    static const uint32 StoreAbsRebaseValue = 10000000;
    while(StoreAbsRebaseValue <= mStoreAbsBegin)
    {
        mStoreAbsBegin -= StoreAbsRebaseValue;
        for(VDBaton* CurBaton = mBatonHead.mNext; CurBaton; CurBaton = CurBaton->mNext)
        {
            if(StoreAbsRebaseValue <= CurBaton->mScanAbsFocus)
                CurBaton->mScanAbsFocus -= StoreAbsRebaseValue;
            else CurBaton->mScanAbsFocus = 0;
        }
    }
}

void VDPeer::FlushMonitor(bool issend, const VDBody* body, Setter setter, chars ch, chars monitorfocus, bytes data, sint32 length)
{
    if(0 < body->mMonitor.Length())
    {
        setter(ch, body->mMonitor, (issend)? "1 ~ 0 : 0.5" : "-1 ~ 0 : 0.5");
        if(body->mMonitor == monitorfocus)
        {
            String HexString;
            const sint32 HexLength = Math::Min(length, 16);
            for(sint32 i = 0; i < HexLength; ++i)
                HexString += String::Format((i == 0)? "%02X" : " %02X", data[i]);
            if(16 < length) HexString += " ...";
            setter(ch, body->mMonitor + ((issend)? ".send" : ".recv"), "'" + HexString + "'");
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// VDChannel
VDChannel::VDChannel()
{
}

VDChannel::~VDChannel()
{
	mPeerPtrs.AccessByCallback([](const MapPath* path, VDPeer** peer, payload param)->void
	{
		delete *peer;
		*peer = nullptr;
	}, nullptr);
	Platform::Server::Release(mServer);
}

const Context VDChannel::EnumCh() const
{
    Context ChCollector;
    for(sint32 i = 0, iend = mServerPeers.Count(); i < iend; ++i)
    {
        chararray GetPeerID;
        if(mServerPeers.AccessByOrder(i, &GetPeerID))
        {
            sint32 CurPeerID = MapPath::ToInt(GetPeerID);
            const String CurCh = PeerIDToCh(CurPeerID);
            if(auto CurPeer = mPeerPtrs.Access(CurCh))
            {
                auto& NewCh = ChCollector.AtAdding();
                NewCh.At("type").Set("socket");
                NewCh.At("ch").Set(CurCh);
                NewCh.At("peerid").Set(String::FromInteger(CurPeerID));
                NewCh.At("connected").Set((*CurPeer)->Connected()? "1" : "0");
            }
        }
    }
    String SerialSpec;
    Platform::Serial::GetAllNames(&SerialSpec);
    const Context SerialJson(ST_Json, SO_OnlyReference, SerialSpec);
    for(sint32 i = 0, iend = SerialJson.LengthOfIndexable(); i < iend; ++i)
    {
        auto CurPortName = SerialJson[i]("portname").GetText();
        const String CurCh = SerialPortToCh(CurPortName);
        auto& NewCh = ChCollector.AtAdding();
        NewCh.At("type").Set("serial");
        NewCh.At("ch").Set(CurCh);
        NewCh.At("portname").Set(CurPortName);
        if(auto CurPeer = mPeerPtrs.Access(CurCh))
            NewCh.At("connected").Set((*CurPeer)->Connected()? "1" : "0");
    }
    return ChCollector;
}

String VDChannel::PeerIDToCh(sint32 peerid) const
{
    return String::Format("P%03d", peerid);
}

String VDChannel::SerialPortToCh(chars serialport) const
{
    return String(serialport);
}

bool VDChannel::BindServer(uint16 portnumber)
{
    Platform::Server::Release(mServer);
    mServer = Platform::Server::CreateTCP(false);
    mServerPeers.Reset();
    const bool Result = Platform::Server::Listen(mServer, portnumber);
    mLogger("server", String::Format("info:(bind %d - %s)", portnumber, (Result)? "success" : "failure"), nullptr, 0);
    return Result;
}

bool VDChannel::RecvServerOnce(bool* peerupdate)
{
    if(Platform::Server::TryNextPacket(mServer))
    {
        const sint32 PeerID = Platform::Server::GetPacketPeerID(mServer);
        auto PacketType = Platform::Server::GetPacketType(mServer);
        switch(PacketType)
        {
        case packettype_entrance:
            if(VDPeer* NewPeer = VDPeer::CreateSocket(mServer, PeerID))
	        {
                mServerPeers[PeerID] = true;
		        mPeerPtrs(PeerIDToCh(PeerID)) = NewPeer;
                mLogger("server", String::Format("info:(entrance peer_%d)", PeerID), nullptr, 0);
                if(peerupdate) *peerupdate = true;
		        return true;
	        }
            break;
        case packettype_message:
            Read(PeerIDToCh(PeerID));
            return true;
        case packettype_leaved:
        case packettype_kicked:
            mServerPeers.Remove(PeerID);
            Delete(PeerIDToCh(PeerID));
            mLogger("server", String::Format("info:(%s peer_%d)",
                (PacketType == packettype_leaved)? "leaved" : "kicked", PeerID), nullptr, 0);
            if(peerupdate) *peerupdate = true;
            return true;
        }
    }
    if(peerupdate) *peerupdate = false;
    return false;
}

bool VDChannel::ConnectSerial(chars serialport, sint32 baudrate)
{
	if(VDPeer* NewPeer = VDPeer::CreateSerial(serialport, baudrate))
	{
		mPeerPtrs(SerialPortToCh(serialport)) = NewPeer;
		return true;
	}
	return false;
}

void VDChannel::Delete(chars ch)
{
    if(auto OldPeer = mPeerPtrs.Access(ch))
    {
        delete *OldPeer;
	    mPeerPtrs.Remove(ch);
    }
}

bool VDChannel::Read(chars ch)
{
    sint32 StoreSize = 0;
    if(auto CurPeer = mPeerPtrs.Access(ch))
    if((*CurPeer)->ReadOnce(mLogger, ch, &StoreSize))
    {
        mSetter(ch, "baton.store", String::FromInteger(StoreSize));
        return true;
    }
    return false;
}

bool VDChannel::Write(chars ch, bytes data, sint32 size)
{
	if(auto CurPeer = mPeerPtrs.Access(ch))
		return (*CurPeer)->Write(data, size);
    return false;
}

void VDChannel::SaveStore(chars ch, chars assetpath)
{
    if(auto CurPeer = mPeerPtrs.Access(ch))
        (*CurPeer)->SaveStore().ToAsset(assetpath, true);
}

void VDChannel::RunEntry(chars ch, const VDScript& script, const String entryid, const Strings params)
{
    if(auto CurPeer = mPeerPtrs.Access(ch))
    if(auto CurEntry = script.GetEntry(entryid))
		(*CurPeer)->RunEntry(script, *CurEntry, params, mSetter, ch);
}

void VDChannel::ClearBatonAll(const VDScript* script)
{
    mPeerPtrs.AccessByCallback([](const MapPath* path, VDPeer** peer, payload param)->void
	{
        (*peer)->ClearBaton((const VDScript*) param);
	}, (payload) script);
}

void VDChannel::ClearBaton(chars ch, const VDScript* script)
{
    if(auto CurPeer = mPeerPtrs.Access(ch))
        (*CurPeer)->ClearBaton(script);
}

bool VDChannel::BatonProcess(uint64 timeout)
{
	static sint32 LoopFocus = 0;
	const uint64 LoopTimeout = Platform::Utility::CurrentTimeMsec() + timeout;
	bool Result = false;

	bool ProcessedInRound = true;
	while(ProcessedInRound && Platform::Utility::CurrentTimeMsec() < LoopTimeout)
	{
		ProcessedInRound = false;
		for(sint32 i = 0, iend = mPeerPtrs.Count(); i < iend; ++i)
		{
			const sint32 Index = (i + LoopFocus) % iend;
            chararray Ch;
			if(auto CurPeer = mPeerPtrs.AccessByOrder(Index, &Ch))
            {
                // 라운드 시작 시 존재한 바통들을 한 바퀴 라운드로빈 순회한다.
                // 처리 중 생성된 바통은 다음 라운드에서 실행한다.
                const sint32 VisitCount = (*CurPeer)->StackSize();
                for(sint32 j = 0; j < VisitCount; ++j)
                {
                    if(LoopTimeout <= Platform::Utility::CurrentTimeMsec())
                        break;
                    if((*CurPeer)->BatonOnce(mGetter, mSetter, mActor, mLogger, &Ch[0], mFocusedMonitor))
                    {
                        Result = true;
                        ProcessedInRound = true;
                    }
                }
                mSetter(&Ch[0], "baton.stack", String::FromInteger((*CurPeer)->StackSize()));
                mSetter(&Ch[0], "baton.bps", String::FromInteger((*CurPeer)->ReadBps()));
                mSetter(&Ch[0], "baton.miss", String::FromInteger((*CurPeer)->MissSize()));
            }
			if(LoopTimeout <= Platform::Utility::CurrentTimeMsec())
			{
				// 현재 peer의 mBatonFocus는 BatonOnce가 다음 바통으로 이동해 두었다.
				// 다음 호출은 다음 peer부터 시작하여 peer 간 기아도 방지한다.
				LoopFocus = (Index + 1) % iend;
				return Result;
			}
		}
	}
	return Result;
}

void VDChannel::SetGetter(Getter getter)
{
    mGetter = getter;
}

void VDChannel::SetSetter(Setter setter)
{
    mSetter = setter;
}

void VDChannel::SetActor(Actor actor)
{
    mActor = actor;
}

void VDChannel::SetLogger(Logger logger)
{
    mLogger = logger;
}

void VDChannel::SetMonitorFocus(chars monitor)
{
    mFocusedMonitor = monitor;
}

const SolverValue VDChannel::DefGetter(chars ch, chars key)
{
	BOSS_TRACE("[%s/Get] %s", ch, key);
    return SolverValue();
}

void VDChannel::DefSetter(chars ch, chars key, chars value)
{
    BOSS_TRACE("[%s/Set] %s = %s", ch, key, value);
}

void VDChannel::DefActor(chars ch, chars action)
{
    BOSS_TRACE("[%s/Action] %s", ch, action);
}

void VDChannel::DefLogger(chars ch, chars type, bytes data, sint32 size)
{
	String Log = String::Format("[%s/%s]", ch, type);
	for(sint32 i = 0; i < size; ++i)
		Log += String::Format(" %02X", data[i]);
	BOSS_TRACE("%s", (chars) Log);
}
