#include <boss.hpp>
#include "vds_block.hpp"

VDSBlock::VDSBlock()
{
}

VDSBlock::~VDSBlock()
{
}

void VDSBlock::Parse(chars text, sint32 length)
{
	mType = Type::Null;
	mHasTextSizeField = false;
	mPacketSize = -1;
	mNormalOrder = true;
	mData.Empty();
	mVariablePacket.Clear();
	mFunctionBracket.Empty();
	if(text[0] == '|')
	{
		String Text = String(text + 1, length - 1).Trim();
		// 패킷사이즈
		const sint32 BaseColonPos = Text.Find(0, ":");
		const sint32 SemiColonPos = Text.Find(0, ";");
		const sint32 ColonPos = (BaseColonPos == -1)? SemiColonPos : ((SemiColonPos == -1)? BaseColonPos : Math::Min(BaseColonPos, SemiColonPos));
		if(ColonPos != -1)
		{
			bool IsParsingError = false;
			mHasTextSizeField = (0 < ColonPos && Text[ColonPos - 1] == '+');
			const String PacketInfo = Text.Left((mHasTextSizeField)? ColonPos - 1 : ColonPos).Trim();
			if(auto Length = PacketInfo.Length())
			{
				sint32 OffsetCheck = 0;
				auto Size = Parser::GetInt<sint64>(PacketInfo, Length, &OffsetCheck);
				if(OffsetCheck == Length)
					mPacketSize = Size;
				else // 패킷내용이 json이면 ':'이 우연히 나올 수 있음
				{
					IsParsingError = true;
					mHasTextSizeField = false;
				}
			}
			else mPacketSize = -1;
			if(!IsParsingError)
			{
				mNormalOrder = (Text[ColonPos] != ';');
				Text = Text.Offset(ColonPos + 1);
			}
		}
		// 패킷내용
		if(auto Length = Text.Length())
		{
			if(1 < Length && Text[0] == '@')
			{
				mType = Type::Variable;
				mData = Text.Offset(1);
			}
			else if(2 < Length && (Text[0] == '\'' || Text[0] == '\"') && Text[0] == Text[-2])
			{
				mType = Type::String;
				mData = Text.Middle(1, Length - 2);
			}
			else
			{
				sint32 OffsetCheck = 0;
				Parser::GetInt<sint64>(Text, Length, &OffsetCheck);
				if(OffsetCheck == Length)
				{
					mType = Type::Number;
					mData = Text;
				}
				else
				{
					OffsetCheck = 0;
					Parser::GetFloat<double>(Text, Length, &OffsetCheck);
					if(OffsetCheck == Length)
					{
						mType = Type::RealNumber;
						mData = Text;
					}
					else
					{
						const sint32 BracketPos = Text.Find(1, "(");
						const sint32 BracketEnd = Text.Find(BracketPos + 1, ")");
						if(BracketPos != -1 && BracketEnd != -1)
						{
							mType = Type::Function;
							mData = Text.Left(BracketPos);
							mFunctionBracket = Text.Middle(BracketPos + 1, BracketEnd - BracketPos - 1);
						}
						else if(1 < Length && Text[0] == 'x')
						{
							OffsetCheck = 0;
							Parser::GetHex32<uint64>(((chars) Text) + 1, Length - 1, &OffsetCheck);
							if(OffsetCheck == Length - 1)
							{
								mType = Type::HexCode;
								mData = Text.Offset(1);
							}
							else
							{
								mType = Type::String;
								mData = Text;
							}
						}
						else if(1 < Length && Text[0] == 'b')
						{
							for(OffsetCheck = 1; OffsetCheck < Length; ++OffsetCheck)
								if(Text[OffsetCheck] != '0' && Text[OffsetCheck] != '1')
									break;
							if(OffsetCheck == Length)
							{
								mType = Type::BinCode;
								mData = Text.Offset(1);
							}
							else
							{
								mType = Type::String;
								mData = Text;
							}
						}
						else
						{
							mType = Type::String;
							mData = Text;
						}
					}
				}
			}
		}
	}
}

static sint64 _BigMin(const uint64 a, const uint64 b)
{return sint64((a < b)? a : b);}

void VDSBlock::WritePacket(uint08s& output, Getter getter, chars ch) const
{
	switch(mType)
	{
	case Type::Number:
		{
			const sint64 Number = Parser::GetInt<sint64>(mData);
			const sint32 PacketSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
			AppendTo(output, (bytes) &Number, PacketSize, mPacketSize - PacketSize);
		}
		break;
	case Type::RealNumber:
		{
			const double RealNumber = Parser::GetFloat<double>(mData);
			const bool UseDouble = (mPacketSize == -1)? false : (4 < mPacketSize);
			if(UseDouble) AppendTo(output, (bytes) &RealNumber, sizeof(RealNumber), 0);
			else
			{
				const float FloatNumber = RealNumber;
				AppendTo(output, (bytes) &FloatNumber, sizeof(FloatNumber), 0);
			}
		}
		break;
	case Type::HexCode:
		{
			const uint64 Number = Parser::GetHex32<uint64>(mData);
			const sint32 PacketSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
			AppendTo(output, (bytes) &Number, PacketSize, mPacketSize - PacketSize);
		}
		break;
	case Type::BinCode:
		{
			uint64 Number = 0;
			const chars BinCode = (chars) mData;
			for(sint32 i = 0, iend = mData.Length(); i < iend; ++i)
				if(BinCode[iend - 1 - i] == '1')
					Number |= Unsigned64(1) << i;
			const sint32 PacketSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
			AppendTo(output, (bytes) &Number, PacketSize, mPacketSize - PacketSize);
		}
		break;
	case Type::String:
		{
			if(mHasTextSizeField)
			{
				const sint32 SizeFieldSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
				const sint64 PacketSize = _BigMin(mData.Length(), oxFFFFFFFFFFFFFFFF >> 8 * (8 - SizeFieldSize));
				AppendTo(output, (bytes) &PacketSize, SizeFieldSize, mPacketSize - SizeFieldSize);
				AppendTo(output, (bytes)(chars) mData, PacketSize, 0, true);
			}
			else
			{
				const sint32 PacketSize = (mPacketSize == -1)? mData.Length() : Math::Clamp(mPacketSize, 0, mData.Length());
				AppendTo(output, (bytes)(chars) mData, PacketSize, mPacketSize - PacketSize);
			}
		}
		break;
	case Type::Variable:
		{
			const auto Value = getter(ch, mData);
			switch(Value.GetType())
			{
			case SolverValueType::Integer:
				{
					const sint64 Number = Value.ToInteger();
					const sint32 PacketSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
					AppendTo(output, (bytes) &Number, PacketSize, mPacketSize - PacketSize);
				}
				break;
			case SolverValueType::Float:
			case SolverValueType::Range:
				{
					const double RealNumber = Value.ToFloat();
					const bool UseDouble = (mPacketSize == -1)? false : (4 < mPacketSize);
					if(UseDouble) AppendTo(output, (bytes) &RealNumber, sizeof(RealNumber), 0);
					else
					{
						const float FloatNumber = RealNumber;
						AppendTo(output, (bytes) &FloatNumber, sizeof(FloatNumber), 0);
					}
				}
				break;
			case SolverValueType::Text:
				{
					const String Text = Value.ToText();
					if(mHasTextSizeField)
					{
						const sint32 SizeFieldSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
						const sint64 PacketSize = _BigMin(Text.Length(), oxFFFFFFFFFFFFFFFF >> 8 * (8 - SizeFieldSize));
						AppendTo(output, (bytes) &PacketSize, SizeFieldSize, mPacketSize - SizeFieldSize);
						AppendTo(output, (bytes)(chars) Text, PacketSize, 0, true);
					}
					else
					{
						const sint32 PacketSize = (mPacketSize == -1)? Text.Length() : Math::Clamp(mPacketSize, 0, Text.Length());
						AppendTo(output, (bytes)(chars) Text, PacketSize, mPacketSize - PacketSize);
					}
				}
				break;
			default:
				{
					const sint32 ZeroSize = (mPacketSize == -1)? 1 : mPacketSize;
					AppendTo(output, nullptr, 0, ZeroSize);
				}
				break;
			}
		}
		break;
	case Type::Function:
		{
			if(mData == "add")
			{
				auto Params = String::Split(mFunctionBracket);
				const sint32 Begin = (0 < Params.Count())? Parser::GetInt(Params[0].Trim()) : 0;
				const sint32 End = (1 < Params.Count())? Begin + Parser::GetInt(Params[1].Trim()) : output.Count();
				uint08 Sum = 0;
				for(sint32 i = Math::Clamp(Begin, 0, output.Count()), iend = Math::Clamp(End, 0, output.Count()); i < iend; ++i)
					Sum += output[i];
				output.AtAdding() = Sum;
			}
			else if(mData == "xor")
			{
				auto Params = String::Split(mFunctionBracket);
				const sint32 Begin = (0 < Params.Count())? Parser::GetInt(Params[0].Trim()) : 0;
				const sint32 End = (1 < Params.Count())? Begin + Parser::GetInt(Params[1].Trim()) : output.Count();
				uint08 Xor = 0;
				for(sint32 i = Math::Clamp(Begin, 0, output.Count()), iend = Math::Clamp(End, 0, output.Count()); i < iend; ++i)
					Xor ^= output[i];
				output.AtAdding() = Xor;
			}
		}
		break;
	}
}

sint32 VDSBlock::ReadPacket(bytes input, sint32 offset, sint32 length) const
{
	switch(mType)
	{
	case Type::Number:
		{
			const sint32 PacketSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
			if(PacketSize <= length)
			{
				const sint64 Number = Parser::GetInt<sint64>(mData);
				if(SameTo(&input[offset], (bytes) &Number, PacketSize, mPacketSize - PacketSize))
					return PacketSize;
			}
		}
		break;
	case Type::RealNumber:
		{
			const bool UseDouble = (mPacketSize == -1)? false : (4 < mPacketSize);
			if(UseDouble)
			{
				const sint32 PacketSize = sizeof(double);
				if(PacketSize <= length)
				{
					const double RealNumber = Parser::GetFloat<double>(mData);
					if(SameTo(&input[offset], (bytes) &RealNumber, PacketSize, 0))
						return PacketSize;
				}
			}
			else
			{
				const sint32 PacketSize = sizeof(float);
				if(PacketSize <= length)
				{
					const float FloatNumber = Parser::GetFloat<float>(mData);
					if(SameTo(&input[offset], (bytes) &FloatNumber, PacketSize, 0))
						return PacketSize;
				}
			}
		}
		break;
	case Type::HexCode:
		{
			const sint32 PacketSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
			if(PacketSize <= length)
			{
				const uint64 Number = Parser::GetHex32<uint64>(mData);
				if(SameTo(&input[offset], (bytes) &Number, PacketSize, mPacketSize - PacketSize))
					return PacketSize;
			}
		}
		break;
	case Type::BinCode:
		{
			const sint32 PacketSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
			if(PacketSize <= length)
			{
				uint64 Number = 0;
				const chars BinCode = (chars) mData;
				for(sint32 i = 0, iend = mData.Length(); i < iend; ++i)
					if(BinCode[iend - 1 - i] == '1')
						Number |= Unsigned64(1) << i;
				if(SameTo(&input[offset], (bytes) &Number, PacketSize, mPacketSize - PacketSize))
					return PacketSize;
			}
		}
		break;
	case Type::String:
		{
			if(mHasTextSizeField)
			{
				const sint32 SizeFieldMax = Math::Max(0, mPacketSize);
				const sint32 SizeFieldSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
				const sint64 PacketSize = _BigMin(mData.Length(), oxFFFFFFFFFFFFFFFF >> 8 * (8 - SizeFieldSize));
				if(SizeFieldMax + PacketSize <= length)
				if(SameTo(&input[offset], (bytes) &PacketSize, SizeFieldSize, SizeFieldMax - SizeFieldSize))
				if(SameTo(&input[offset + SizeFieldMax], (bytes)(chars) mData, PacketSize, 0, true))
					return SizeFieldMax + PacketSize;
			}
			else
			{
				const sint32 PacketSize = (mPacketSize == -1)? mData.Length() : Math::Clamp(mPacketSize, 0, mData.Length());
				if(PacketSize <= length)
				{
					if(SameTo(&input[offset], (bytes)(chars) mData, PacketSize, mPacketSize - PacketSize))
						return PacketSize;
				}
			}
		}
		break;
	case Type::Variable:
		{
			if(mHasTextSizeField)
			{
				const sint32 SizeFieldMax = Math::Max(0, mPacketSize);
				const sint32 SizeFieldSize = (mPacketSize == -1)? 1 : Math::Clamp(mPacketSize, 0, 8);
				if(SizeFieldMax <= length)
				{
					uint64 PacketSize64 = 0;
					if(mNormalOrder)
						Memory::Copy(&PacketSize64, &input[offset], SizeFieldSize);
					else for(sint32 i = SizeFieldMax - SizeFieldSize; i < SizeFieldMax; ++i)
						PacketSize64 = (PacketSize64 << 8) | input[offset + i];
					if(PacketSize64 <= (uint64) Math::Max(0, length - SizeFieldMax))
					{
						const sint32 PacketSize = (sint32) PacketSize64;
						mVariablePacket.Clear();
						AppendTo(mVariablePacket, &input[offset + SizeFieldMax], PacketSize, 0, true);
						return SizeFieldMax + PacketSize;
					}
				}
			}
			else
			{
				const sint32 PacketSize = (mPacketSize == -1)? 1 : mPacketSize;
				if(PacketSize <= length)
				{
					mVariablePacket.Clear();
					AppendTo(mVariablePacket, &input[offset], PacketSize, 0);
					return PacketSize;
				}
			}
		}
		break;
	case Type::Function:
		{
			if(mData == "add")
			{
				auto Params = String::Split(mFunctionBracket);
				const sint32 Begin = (0 < Params.Count())? Parser::GetInt(Params[0].Trim()) : 0;
				const sint32 End = (1 < Params.Count())? Begin + Parser::GetInt(Params[1].Trim()) : offset + length;
				uint08 Sum = 0;
				for(sint32 i = Math::Clamp(Begin, 0, offset + length), iend = Math::Clamp(End, 0, offset + length); i < iend; ++i)
					Sum += input[i];
				if(input[offset] == Sum)
					return 1;
			}
			else if(mData == "xor")
			{
				auto Params = String::Split(mFunctionBracket);
				const sint32 Begin = (0 < Params.Count())? Parser::GetInt(Params[0].Trim()) : 0;
				const sint32 End = (1 < Params.Count())? Begin + Parser::GetInt(Params[1].Trim()) : offset + length;
				uint08 Xor = 0;
				for(sint32 i = Math::Clamp(Begin, 0, offset + length), iend = Math::Clamp(End, 0, offset + length); i < iend; ++i)
					Xor ^= input[i];
				if(input[offset] == Xor)
					return 1;
			}
		}
		break;
	}
	return -1;
}

void VDSBlock::ReadFlush(Setter setter, chars ch) const
{
	if(mType == Type::Variable)
	{
		const sint32 PacketCount = mVariablePacket.Count();
		if(mHasTextSizeField)
		{
			const String Text((chars) &mVariablePacket[0], PacketCount);
			setter(ch, mData, '\'' + Text + '\'');
		}
		else if(PacketCount <= 8)
		{
			uint64 Bytes = 0;
			for(sint32 i = 0; i < PacketCount; ++i)
				Bytes |= (mVariablePacket[i] & ox00000000000000FF) << (8 * i);
			// 음수처리
			const sint32 ShiftCount = 8 * (PacketCount - 1);
			if(Bytes & (Unsigned64(0x0000000000000080) << ShiftCount))
				Bytes |= Unsigned64(0xFFFFFFFFFFFFFF00) << ShiftCount;
			setter(ch, mData, String::FromInteger((sint64) Bytes));
		}
	}
}

sint32 VDSBlock::CalcChildWidth(sint32 innergap) const
{
	switch(mType)
	{
	case Type::BinCode:
		return 160;
	}
	return 80;
}

sint32 VDSBlock::Render(ZayPanel& panel, sint32 innergap) const
{
	const float FontSize = panel.h() / 20.0;
	const sint32 ChildWidth = CalcChildWidth(innergap);
	ZAY_XYWH(panel, 0, 0, ChildWidth, panel.h())
	{
		switch(mType)
		{
		case Type::Number:
			ZAY_COLOR(panel, 255, 255, 0)
				panel.circle();
			break;
		case Type::RealNumber:
			ZAY_COLOR(panel, 255, 192, 0)
				panel.circle();
			break;
		case Type::HexCode:
			ZAY_COLOR(panel, 224, 224, 224)
				panel.circle();
			break;
		case Type::BinCode:
			ZAY_COLOR(panel, 192, 192, 192)
				panel.circle();
			break;
		case Type::String:
			ZAY_COLOR(panel, 255, 192, 192)
				panel.circle();
			break;
		case Type::Variable:
			ZAY_COLOR(panel, 224, 224, 255)
				panel.circle();
			break;
		case Type::Function:
			ZAY_COLOR(panel, 255, 224, 255)
				panel.circle();
			break;
		}
		ZAY_COLOR(panel, 0, 0, 0)
		ZAY_INNER(panel, 1)
			panel.circleline(1);
		ZAY_COLOR(panel, 0, 0, 0)
		ZAY_FONT(panel, FontSize)
			panel.text(mData, UIFA_CenterMiddle, UIFE_Right);
	}
	return ChildWidth;
}

void VDSBlock::AppendTo(uint08s& packet, bytes data, sint32 size, sint32 available, bool forced_normalorder) const
{
	if(mNormalOrder || forced_normalorder)
	{
		if(0 < size)
			Memory::Copy(packet.AtDumpingAdded(size), data, size);
		while(0 < available--) packet.AtAdding() = 0x00;
	}
	else
	{
		while(0 < available--) packet.AtAdding() = 0x00;
		for(sint32 i = size - 1; 0 <= i; --i)
			packet.AtAdding() = data[i];
	}
}

bool VDSBlock::SameTo(bytes packet, bytes data, sint32 size, sint32 available, bool forced_normalorder) const
{
	if(mNormalOrder || forced_normalorder)
	{
		if(0 < size)
		{
			if(Memory::Compare(packet, data, size))
				return false;
			packet += size;
		}
		while(0 < available--)
			if(*(packet++) != 0x00)
				return false;
	}
	else
	{
		while(0 < available--)
			if(*(packet++) != 0x00)
				return false;
		for(sint32 i = size - 1; 0 <= i; --i)
			if(*(packet++) != data[i])
				return false;
	}
	return true;
}
