#pragma once
#include <boss.hpp>
#include "vds_object.hpp"

class VDSComment : public VDSObject
{
public:
    VDSComment();
    ~VDSComment() override;

protected:
    void Parse(chars text, sint32 length) override;
    void WritePacket(uint08s& output, Getter getter, chars ch) const override;
    sint32 ReadPacket(bytes input, sint32 offset, sint32 length) const override;
    void ReadFlush(Setter setter, chars ch) const override;
    sint32 CalcChildWidth(sint32 innergap) const override;
    sint32 Render(ZayPanel& panel, sint32 innergap) const override;

private:
    String mComment;
};
