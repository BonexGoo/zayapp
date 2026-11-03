#include <boss.hpp>
#include "vds_object.hpp"

#include "vds_block.hpp"
#include "vds_loop.hpp"
#include "vds_comment.hpp"
#include <vector>

VDSObject::VDSObject()
{
}

VDSObject::~VDSObject()
{
	Clear();
}

void VDSObject::Clear()
{
	if(mNext)
	{
		Destroy(mNext);
		mNext = nullptr;
	}
}

void VDSObject::Parse(chars text, sint32 length)
{
	Clear();
	if(length == -1)
		length = boss_strlen(text);

	chars Focus = text;
	chars FocusOld = text;
	chars FocusNext = text;
	const chars FocusEnd = text + length;
	VDSObject* NewChild = nullptr;
	VDSObject* LastChild = this;
	while(Focus = FocusNext)
	{
		FocusNext = Focus + 1;
		VDSObject* OldChild = nullptr;
		if(Focus == FocusEnd)
		{
			OldChild = NewChild;
			FocusNext = nullptr;
		}
		else if(*Focus == '|')
		{
			OldChild = NewChild;
			NewChild = new VDSBlock();
		}
		else if(*Focus == '[')
		{
			OldChild = NewChild;
			NewChild = new VDSLoop();
			// 루프구간 스킵
			sint32 DeepLevel = 1;
			while(FocusNext < FocusEnd && 0 < DeepLevel)
			{
				if(*FocusNext == '[') DeepLevel++;
				else if(*FocusNext == ']') DeepLevel--;
				else if(*FocusNext == '#') // 루프내 주석구간 스킵
					while(FocusNext + 1 < FocusEnd && *(++FocusNext) != '#');
				FocusNext++;
			}
		}
		else if(*Focus == '#')
		{
			OldChild = NewChild;
			NewChild = new VDSComment();
			// 주석구간 스킵
			while(FocusNext < FocusEnd && *(FocusNext++) != '#');
		}
		if(OldChild)
		{
			LastChild = LastChild->mNext = OldChild;
			OldChild->Parse(FocusOld, Focus - FocusOld);
			FocusOld = Focus;
		}
	}
}

void VDSObject::WritePacket(uint08s& output, Getter getter, chars ch) const
{
	const VDSObject* Node = this;
	while(Node = Node->mNext)
		Node->WritePacket(output, getter, ch);
}

sint32 VDSObject::ReadPacket(bytes input, sint32 offset, sint32 length) const
{
	sint32 ReadFocus = offset;
	const sint32 ReadEnd = offset + length;
	const VDSObject* Node = this;
	while(Node = Node->mNext)
	{
		if(ReadEnd <= ReadFocus) return -1;
		const sint32 ReadCount = Node->ReadPacket(input, ReadFocus, ReadEnd - ReadFocus);
		if(ReadCount == -1) return -1;
		ReadFocus += ReadCount;
	}
	return ReadFocus - offset;
}

void VDSObject::ReadFlush(Setter setter, chars ch) const
{
	const VDSObject* Node = this;
	while(Node = Node->mNext)
		Node->ReadFlush(setter, ch);
}

sint32 VDSObject::CalcChildWidth(sint32 innergap) const
{
	sint32 WholeWidth = innergap * 2;
	const VDSObject* Node = this;
	while(Node = Node->mNext)
	{
		WholeWidth += Node->CalcChildWidth(innergap);
		if(Node->mNext)
			WholeWidth += innergap;
	}
	return WholeWidth;
}

sint32 VDSObject::Render(ZayPanel& panel, sint32 innergap) const
{
	const sint32 ChildWidth = CalcChildWidth(innergap);
	ZAY_XYWH(panel, 0, 0, ChildWidth, panel.h())
	{
		sint32 PosLeft = innergap;
		const VDSObject* Node = this;
		while(Node = Node->mNext)
			ZAY_LTRB(panel, PosLeft, innergap, panel.w(), panel.h() - innergap)
				PosLeft += Node->Render(panel, innergap) + innergap;
	}
	return ChildWidth;
}

void VDSObject::Destroy(VDSObject* script)
{
	std::vector<VDSObject*> Stack;
	Stack.push_back(script);
	while(!Stack.empty())
	{
		auto CurScript = Stack.back();
		Stack.pop_back();
		if(CurScript->mNext)
		{
			Stack.push_back(CurScript->mNext);
			CurScript->mNext = nullptr;
		}
		delete CurScript;
	}
}
