#pragma once
#include <boss.hpp>

class HIDApi
{
public:
    virtual Context GetDevices() = 0;
    virtual bool OpenForID(uint32 vendorid, uint32 productid) = 0;
    virtual bool OpenForPath(chars path) = 0;
    virtual void Close() = 0;
    virtual sint32 Read(uint08* out, sint32 maxsize, sint32 timeout) = 0;
    virtual sint32 Write(bytes data, sint32 size) = 0;

public:
    static HIDApi* Create();
    static void Release(HIDApi*& api);
};
