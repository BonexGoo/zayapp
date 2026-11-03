#include <boss.hpp>
#include "vds_comment.hpp"

VDSComment::VDSComment()
{
}

VDSComment::~VDSComment()
{
}

void VDSComment::Parse(chars text, sint32 length)
{
	mComment.Empty();
	if(text[0] == '#')
	{
		const String Text = String(text + 1, length - 1).Trim();
		if(const sint32 Length = Text.Length())
		{
			if(Text.Right(1) == "#")
				mComment = Text.Left(Length - 1);
			else mComment = Text;
		}
	}
}

void VDSComment::WritePacket(uint08s& output, Getter getter, chars ch) const
{
}

sint32 VDSComment::ReadPacket(bytes input, sint32 offset, sint32 length) const
{
	return 0;
}

void VDSComment::ReadFlush(Setter setter, chars ch) const
{
}

sint32 VDSComment::CalcChildWidth(sint32 innergap) const
{
	return Platform::Graphics::GetStringWidth(mComment) + innergap * 2;
}

sint32 VDSComment::Render(ZayPanel& panel, sint32 innergap) const
{
	const float FontSize = panel.h() / 20.0;
	const sint32 ChildWidth = CalcChildWidth(innergap);
	ZAY_XYWH(panel, 0, 0, ChildWidth, panel.h())
	{
		ZAY_COLOR(panel, 0, 255, 0)
			panel.fill();
		ZAY_COLOR(panel, 0, 64, 0)
		ZAY_FONT(panel, FontSize)
			panel.text(mComment, UIFA_CenterMiddle, UIFE_Right);
	}
	return ChildWidth;
}
