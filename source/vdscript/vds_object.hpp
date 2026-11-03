#pragma once
#include <boss.hpp>
#include <service/boss_zay.hpp>
#include <functional>

class VDS
{
public:
    typedef std::function<const SolverValue(chars ch, chars key)> Getter;
    typedef std::function<void(chars ch, chars key, chars value)> Setter;
    typedef std::function<void(chars ch, chars action)> Actor;
    typedef std::function<void(chars ch, chars type, bytes data, sint32 size)> Logger;
};

class VDSObject : public VDS
{
public:
    VDSObject();
    virtual ~VDSObject();

public:
    void Clear();
    virtual void Parse(chars text, sint32 length = -1);
    virtual void WritePacket(uint08s& output, Getter getter, chars ch) const;
    virtual sint32 ReadPacket(bytes input, sint32 offset, sint32 length) const;
    virtual void ReadFlush(Setter setter, chars ch) const;
    virtual sint32 CalcChildWidth(sint32 innergap) const;
    virtual sint32 Render(ZayPanel& panel, sint32 innergap) const;

protected:
    static void Destroy(VDSObject* vds);

private:
    VDSObject* mNext {nullptr};
};
