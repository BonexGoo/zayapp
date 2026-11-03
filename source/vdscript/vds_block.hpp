#pragma once
#include <boss.hpp>
#include "vds_object.hpp"

class VDSBlock : public VDSObject
{
public:
    VDSBlock();
    ~VDSBlock() override;

protected:
    void Parse(chars text, sint32 length) override;
    void WritePacket(uint08s& output, Getter getter, chars ch) const override;
    sint32 ReadPacket(bytes input, sint32 offset, sint32 length) const override;
    void ReadFlush(Setter setter, chars ch) const override;
    sint32 CalcChildWidth(sint32 innergap) const override;
    sint32 Render(ZayPanel& panel, sint32 innergap) const override;

private:
    void AppendTo(uint08s& packet, bytes data, sint32 size, sint32 available, bool forced_normalorder = false) const;
    bool SameTo(bytes packet, bytes data, sint32 size, sint32 available, bool forced_normalorder = false) const;

private:
    enum class Type {Null, Number, RealNumber, HexCode, BinCode, String, Variable, Function};
    Type mType {Type::Null};
    bool mHasTextSizeField {false};
    sint32 mPacketSize {-1};
    bool mNormalOrder {true};
    String mData;
    mutable uint08s mVariablePacket; // Variable전용
    String mFunctionBracket; // Function전용
};
