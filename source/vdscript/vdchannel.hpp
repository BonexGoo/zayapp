#pragma once
#include <boss.hpp>
#include "vdscript.hpp"

////////////////////////////////////////////////////////////////////////////////
// VDBaton
class VDBaton
{
public:
    const VDScript* mScript {nullptr};
    String mBodyIDs; // "name1" 또는 "name1|name2"
    uint64 mExpireMsec {0};
    String mRetryBodyID; // 현재 response를 만든 send body ID
    sint32 mBatonLifeStep {0}; // 배열형 batonlife의 현재 대기 단계
    sint32 mRetryCount {0}; // 최초 send 이후 실제 재송신 횟수
    sint32 mResponseFocus {0};
    uint32 mScanAbsFocus {0}; // 현재 바통이 Store에서 이미 매칭시도한 절대 위치
    VDBaton* mNext {nullptr};
};

////////////////////////////////////////////////////////////////////////////////
// VDPeer
class VDPeer : public VDS
{
public:
    VDPeer();
    virtual ~VDPeer();

public:
    static VDPeer* CreateSocket(id_server server, sint32 peerid);
    static VDPeer* CreateSerial(chars port, sint32 baudrate = -1);

public:
    virtual bool Connected() const = 0;
    void Verify(chars sendcode, chars recvcode);
    virtual bool Write(bytes data, sint32 size) const = 0;
    virtual bool ReadOnce(Logger logger, chars ch, sint32* storesize) = 0;

public:
    void RunEntry(const VDScript& script, const VDEntry& entry, const Strings params, Setter setter, chars ch);
    void ClearBaton(const VDScript* script = nullptr);
    bool BatonOnce(Getter getter, Setter setter, Actor actor, Logger logger, chars ch, chars monitorfocus);

public:
    sint32 StackSize() const;
    sint32 ReadBps() const;
    sint64 MissSize() const;
    String SaveStore() const;

protected:
    sint32 Store(bytes data, sint32 size, Logger logger, chars ch);

private:
    void FlushStore(Logger logger, chars ch);
    void RemoveStore(sint32 size, Logger logger, chars ch);
    void RebaseStoreAbs();
    bool IsPrecedingResolved(uint32 packetabspos, sint32 preceding) const;
    void FlushMonitor(bool issend, const VDBody* body, Setter setter, chars ch, chars monitorfocus, bytes data, sint32 length);

private:
    bool mVerified {false}; // 유효한 접속자임이 증명
    Map<String> mVerifyInfo; // ['APP CODE'] → 'MVF'
    VDBaton mBatonHead;
    VDBaton* mBatonFocus {nullptr}; // 다음 BatonOnce에서 처리할 라운드로빈 포커스
    static const uint64 StoreTimeoutMsec {30000};
    static const sint32 StoreBytesMax {1024 * 1024 * 8};
    uint64 mStorePool[StoreBytesMax / 8];
    sint32 mStoreBegin {0};
    sint32 mStoreEnd {0};
    uint32 mStoreAbsBegin {0}; // 현재 Store[0]이 전체 수신스트림에서 가지는 절대 위치
    uint64 mStoreBeginMsec {0};
    uint64 mStoreEndMsec {0};
    id_mutex mStoreMutex {nullptr};
    sint64 mReadSize {0};
    uint64 mReadMsec {0};
    sint64 mMissSize {0};
};

////////////////////////////////////////////////////////////////////////////////
// VDChannel
class VDChannel : public VDS
{
public:
    VDChannel();
    ~VDChannel();

public:
    typedef std::function<void(chars ch, bytes data, sint32 size)> ChReadLog;

public:
    const Context EnumCh() const;
    String PeerIDToCh(sint32 peerid) const;
    String SerialPortToCh(chars serialport) const;
    bool BindServer(uint16 portnumber);
    bool RecvServerOnce(bool* peerupdate = nullptr);
    bool ConnectSerial(chars serialport, sint32 baudrate = -1);
    void Delete(chars ch);
    bool Read(chars ch);
    bool Write(chars ch, bytes data, sint32 size);
    void SaveStore(chars ch, chars assetpath);

public:
    void RunEntry(chars ch, const VDScript& script, const String entryid, const Strings params);
    void ClearBatonAll(const VDScript* script = nullptr);
    void ClearBaton(chars ch, const VDScript* script);
    bool BatonProcess(uint64 timeout);

public:
    void SetGetter(Getter getter);
    void SetSetter(Setter setter);
    void SetActor(Actor actor);
    void SetLogger(Logger logger);
    void SetMonitorFocus(chars monitor);

private:
    static const SolverValue DefGetter(chars ch, chars key);
    static void DefSetter(chars ch, chars key, chars value);
    static void DefActor(chars ch, chars action);
    static void DefLogger(chars ch, chars type, bytes data, sint32 size);

private:
    id_server mServer {nullptr};
    Map<bool> mServerPeers; // [PeerID]
    Map<VDPeer*> mPeerPtrs; // ['COM6'] 또는 ['W126']
    Getter mGetter {DefGetter};
    Setter mSetter {DefSetter};
    Actor mActor {DefActor};
    Logger mLogger {DefLogger};
    String mFocusedMonitor;
};
