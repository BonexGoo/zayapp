#include <boss.hpp>
#include "vds_loop.hpp"

VDSLoop::VDSLoop()
{
}

VDSLoop::~VDSLoop()
{
}

void VDSLoop::Parse(chars text, sint32 length)
{
	if(text[0] == '[')
	{
		const String Text = String(text + 1, length - 1).Trim();
		if(const sint32 Length = Text.Length())
		{
			if(Text.Right(1) == "]")
				mChild.Parse(Text.Left(Length - 1));
		}
	}
}

void VDSLoop::WritePacket(uint08s& output, Getter getter, chars ch) const
{
	mChild.WritePacket(output, getter, ch);
}

sint32 VDSLoop::ReadPacket(bytes input, sint32 offset, sint32 length) const
{
	return mChild.ReadPacket(input, offset, length);
}

void VDSLoop::ReadFlush(Setter setter, chars ch) const
{
	mChild.ReadFlush(setter, ch);
}

sint32 VDSLoop::CalcChildWidth(sint32 innergap) const
{
	return mChild.CalcChildWidth(innergap);
}

sint32 VDSLoop::Render(ZayPanel& panel, sint32 innergap) const
{
	const sint32 RevGap = Math::Max(0, innergap - 1);
	const sint32 ChildWidth = CalcChildWidth(innergap);
	ZAY_LTRB(panel, 0, 1, ChildWidth, panel.h() - 1)
	{
		ZAY_RGBA(panel, 0, 0, 0, 32)
			panel.fill();
		ZAY_RGB(panel, 0, 0, 0)
			panel.rect(1);
		ZAY_LTRB(panel, 0, -RevGap, panel.w(), panel.h() + RevGap)
			mChild.Render(panel, innergap);
	}
	return ChildWidth;
}
