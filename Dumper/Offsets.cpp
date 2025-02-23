#include <format>

#include "Offsets.h"
#include "ObjectArray.h"
#include "OffsetFinder.h"

#include "NameArray.h"

void Off::InSDK::ProcessEvent::InitPE()
{
	void** Vft = *(void***)ObjectArray::GetByIndex(0).GetAddress();

	auto Resolve32BitRelativeJump = [](void* FunctionPtr) -> uint8_t*
	{
		uint8_t* Address = reinterpret_cast<uint8_t*>(FunctionPtr);
		if (*Address == 0xE9)
		{
			uint8_t* Ret = ((Address + 5) + *reinterpret_cast<int32_t*>(Address + 1));

			if (IsInProcessRange(uintptr_t(Ret)))
				return Ret;
		}

		return reinterpret_cast<uint8_t*>(FunctionPtr);
	};

	for (int i = 0; i < 0x150; i++)
	{
		if (!Vft[i] || !IsInProcessRange(reinterpret_cast<uintptr_t>(Vft[i])))
			break;

		if (FindPatternInRange({ 0xF7, -0x1, Off::UFunction::FunctionFlags, 0x0, 0x0, 0x0, 0x0, 0x04, 0x0, 0x0 }, Resolve32BitRelativeJump(Vft[i]), 0x400)
		&&  FindPatternInRange({ 0xF7, -0x1, Off::UFunction::FunctionFlags, 0x0, 0x0, 0x0, 0x0, 0x0, 0x40, 0x0 }, Resolve32BitRelativeJump(Vft[i]), 0xF00))
		{
			Off::InSDK::ProcessEvent::PEIndex = i;
			Off::InSDK::ProcessEvent::PEOffset = GetOffset(Vft[i]);

			std::cout << std::format("PE-Offset: 0x{:X}\n", Off::InSDK::ProcessEvent::PEOffset);
			std::cout << std::format("PE-Index: 0x{:X}\n\n", i);
			return;
		}
	}

	void* PeAddr = (void*)FindByWStringInAllSections(L"Accessed None").FindNextFunctionStart();

	for (int i = 0; i < 0x150; i++)
	{
		if (!PeAddr)
			break;

		if (Resolve32BitRelativeJump(Vft[i]) == PeAddr)
		{
			Off::InSDK::ProcessEvent::PEIndex = i;
			Off::InSDK::ProcessEvent::PEOffset = GetOffset(PeAddr);

			std::cout << std::format("PE-Offset: 0x{:X}\n", Off::InSDK::ProcessEvent::PEOffset);
			std::cout << std::format("PE-Index: 0x{:X}\n\n", i);
			return;
		}
	}
}

void Off::InSDK::ProcessEvent::InitPE(int32 Index)
{
	Off::InSDK::ProcessEvent::PEIndex = Index;

	void** VFT = *reinterpret_cast<void***>(ObjectArray::GetByIndex(0).GetAddress());

	uintptr_t Imagebase = GetImageBase();

	Off::InSDK::ProcessEvent::PEOffset = uintptr_t(VFT[Off::InSDK::ProcessEvent::PEIndex]) - Imagebase;

	std::cout << std::format("VFT-Offset: 0x{:X}\n", uintptr_t(VFT) - Imagebase);
}

/* UWorld */
void Off::InSDK::World::InitGWorld()
{
	UEClass UWorld = ObjectArray::FindClassFast("World");

	for (UEObject Obj : ObjectArray())
	{
		if (Obj.HasAnyFlags(EObjectFlags::ClassDefaultObject) || !Obj.IsA(UWorld))
			continue;

		/* Try to find a pointer to the word, aka UWorld** GWorld */
		void* Result = FindAlignedElementInProcess(Obj.GetAddress());

		/* Pointer to UWorld* couldn't be found */
		if (Result)
		{
			Off::InSDK::World::GWorld = GetOffset(Result);
			std::cout << std::format("GWorld-Offset: 0x{:X}\n\n", Off::InSDK::World::GWorld);
			break;
		}
	}

	if (Off::InSDK::World::GWorld == 0x0)
		std::cout << std::format("\nGWorld WAS NOT FOUND!!!!!!!!!\n\n");
}


/* FText */
void Off::InSDK::Text::InitTextOffsets()
{
	if (!Off::InSDK::ProcessEvent::PEIndex)
	{
		std::cout << std::format("\nDumper-7: Error, 'InitInSDKTextOffsets' was called before ProcessEvent was initialized!\n") << std::endl;
		return;
	}

	auto IsValidPtr = [](void* a) -> bool
	{
		return !IsBadReadPtr(a) && (uintptr_t(a) & 0x1) == 0; // realistically, there wont be any pointers to unaligned memory
	};


	UEFunction Conv_StringToText = ObjectArray::FindObjectFast<UEFunction>("Conv_StringToText", EClassCastFlags::Function);

	UEProperty InStringProp = nullptr;
	UEProperty ReturnProp = nullptr;

	for (UEProperty Prop : Conv_StringToText.GetProperties())
	{
		/* Func has 2 params, if the param is the return value assign to ReturnProp, else InStringProp*/
		(Prop.HasPropertyFlags(EPropertyFlags::ReturnParm) ? ReturnProp : InStringProp) = Prop;
	}

	const int32 ParamSize = Conv_StringToText.GetStructSize();

	const int32 FTextSize = ReturnProp.GetSize();

	const int32 StringOffset = InStringProp.GetOffset();
	const int32 ReturnValueOffset = ReturnProp.GetOffset();

	Off::InSDK::Text::TextSize = FTextSize;


	/* Allocate and zero-initialize ParamStruct */
#pragma warning(disable: 6255)
	uint8_t* ParamPtr = static_cast<uint8_t*>(alloca(ParamSize));
	memset(ParamPtr, 0, ParamSize);

	/* Choose a, fairly random, string to later search for in FTextData */
	constexpr const wchar_t* StringText = L"ThisIsAGoodString!";
	constexpr int32 StringLength = (sizeof(L"ThisIsAGoodString!") / sizeof(wchar_t));
	constexpr int32 StringLengthBytes = (sizeof(L"ThisIsAGoodString!"));

	/* Initialize 'InString' in the ParamStruct */
	*reinterpret_cast<FString*>(ParamPtr + StringOffset) = StringText;

	/* This function is 'static' so the object on which we call it doesn't matter */
	ObjectArray::GetByIndex(0).ProcessEvent(Conv_StringToText, ParamPtr);

	uint8_t* FTextDataPtr = nullptr;

	/* Search for the first valid pointer inside of the FText and make the offset our 'TextDatOffset' */
	for (int32 i = 0; i < (FTextSize - sizeof(void*)); i += sizeof(void*))
	{
		void* PossibleTextDataPtr = *reinterpret_cast<void**>(ParamPtr + ReturnValueOffset + i);

		if (IsValidPtr(PossibleTextDataPtr))
		{
			FTextDataPtr = static_cast<uint8_t*>(PossibleTextDataPtr);

			Off::InSDK::Text::TextDatOffset = i;
			break;
		}
	}

	if (!FTextDataPtr)
	{
		std::cout << std::format("\nDumper-7: Error, 'FTextDataPtr' could not be found!\n") << std::endl;
		return;
	}

	constexpr int32 MaxOffset = 0x50;
	constexpr int32 StartOffset = sizeof(void*); // FString::NumElements offset

	/* Search for a pointer pointing to a int32 Value (FString::NumElements) equal to StringLength */
	for (int32 i = StartOffset; i < MaxOffset; i += sizeof(int32))
	{
		wchar_t* PosibleStringPtr = *reinterpret_cast<wchar_t**>((FTextDataPtr + i) - 0x8);
		const int32 PossibleLength = *reinterpret_cast<int32*>(FTextDataPtr + i);

		/* Check if our length matches and see if the data before the length is a pointer to our StringText */
		if (PossibleLength == StringLength && PosibleStringPtr && IsValidPtr(PosibleStringPtr) && memcmp(StringText, PosibleStringPtr, StringLengthBytes) == 0)
		{
			Off::InSDK::Text::InTextDataStringOffset = (i - 0x8);
			break;
		}
	}

	std::cout << std::format("Off::InSDK::Text::TextSize: 0x{:X}\n", Off::InSDK::Text::TextSize);
	std::cout << std::format("Off::InSDK::Text::TextDatOffset: 0x{:X}\n", Off::InSDK::Text::TextDatOffset);
	std::cout << std::format("Off::InSDK::Text::InTextDataStringOffset: 0x{:X}\n", Off::InSDK::Text::InTextDataStringOffset);
}

void Off::Init()
{
	OffsetFinder::InitUObjectOffsets();

	OffsetFinder::InitFNameSettings();

	::NameArray::PostInit();

	Off::UStruct::Children = OffsetFinder::FindChildOffset();
	std::cout << std::format("Off::UStruct::Children: 0x{:X}\n", Off::UStruct::Children);

	Off::UField::Next = OffsetFinder::FindUFieldNextOffset();
	std::cout << std::format("Off::Field::Next: 0x{:X}\n", Off::UField::Next);

	Off::UStruct::SuperStruct = OffsetFinder::FindSuperOffset();
	std::cout << std::format("Off::UStruct::SuperStruct: 0x{:X}\n", Off::UStruct::SuperStruct);

	Off::UStruct::Size = OffsetFinder::FindStructSizeOffset();
	std::cout << std::format("Off::UStruct::Size: 0x{:X}\n", Off::UStruct::Size);

	Off::UStruct::MinAlignemnt = OffsetFinder::FindMinAlignment();
	std::cout << std::format("Off::UStruct::MinAlignemnts: 0x{:X}\n", Off::UStruct::MinAlignemnt);

	Off::UClass::CastFlags = OffsetFinder::FindCastFlagsOffset();
	std::cout << std::format("Off::UClass::CastFlags: 0x{:X}\n", Off::UClass::CastFlags);

	if (Settings::Internal::bUseFProperty)
	{
		std::cout << std::format("Game uses FProperty system\n\n");

		Off::UStruct::ChildProperties = OffsetFinder::FindChildPropertiesOffset();
		std::cout << std::format("Off::UStruct::ChildProperties: 0x{:X}\n", Off::UStruct::ChildProperties);

		OffsetFinder::FixupHardcodedOffsets(); // must be called after FindChildPropertiesOffset 

		Off::FField::Next = OffsetFinder::FindFFieldNextOffset();
		std::cout << std::format("Off::FField::Next: 0x{:X}\n", Off::FField::Next);
		
		Off::FField::Name = OffsetFinder::FindFFieldNameOffset();
		std::cout << std::format("Off::FField::Name: 0x{:X}\n", Off::FField::Name);

		Off::FField::Flags = Off::FField::Name + Off::InSDK::Name::FNameSize;
		std::cout << std::format("Off::FField::Flags: 0x{:X}\n", Off::FField::Flags);
	}

	Off::UClass::ClassDefaultObject = OffsetFinder::FindDefaultObjectOffset();
	std::cout << std::format("Off::UClass::ClassDefaultObject: 0x{:X}\n", Off::UClass::ClassDefaultObject);

	Off::UEnum::Names = OffsetFinder::FindEnumNamesOffset();
	std::cout << std::format("Off::UEnum::Names: 0x{:X}\n", Off::UEnum::Names);

	Off::UFunction::FunctionFlags = OffsetFinder::FindFunctionFlagsOffset();
	std::cout << std::format("Off::UFunction::FunctionFlags: 0x{:X}\n", Off::UFunction::FunctionFlags) << std::endl;

	Off::UFunction::ExecFunction = OffsetFinder::FindFunctionNativeFuncOffset();
	std::cout << std::format("Off::UFunction::ExecFunction: 0x{:X}\n", Off::UFunction::ExecFunction) << std::endl;

	Off::Property::ElementSize = OffsetFinder::FindElementSizeOffset();
	std::cout << std::format("Off::Property::ElementSize: 0x{:X}\n", Off::Property::ElementSize);

	Off::Property::ArrayDim = OffsetFinder::FindArrayDimOffset();
	std::cout << std::format("Off::Property::ArrayDim: 0x{:X}\n", Off::Property::ArrayDim);

	Off::Property::Offset_Internal = OffsetFinder::FindOffsetInternalOffset();
	std::cout << std::format("Off::Property::Offset_Internal: 0x{:X}\n", Off::Property::Offset_Internal);

	Off::Property::PropertyFlags = OffsetFinder::FindPropertyFlagsOffset();
	std::cout << std::format("Off::Property::PropertyFlags: 0x{:X}\n", Off::Property::PropertyFlags);

	Off::InSDK::Properties::PropertySize = OffsetFinder::FindBoolPropertyBaseOffset();
	std::cout << std::format("UPropertySize: 0x{:X}\n", Off::InSDK::Properties::PropertySize) << std::endl;

	Off::ArrayProperty::Inner = OffsetFinder::FindInnerTypeOffset(Off::InSDK::Properties::PropertySize);
	std::cout << std::format("Off::ArrayProperty::Inner: 0x{:X}\n", Off::ArrayProperty::Inner);
	
	Off::SetProperty::ElementProp = OffsetFinder::FindSetPropertyBaseOffset(Off::InSDK::Properties::PropertySize);
	std::cout << std::format("Off::SetProperty::ElementProp: 0x{:X}\n", Off::SetProperty::ElementProp);
	
	Off::MapProperty::Base = OffsetFinder::FindMapPropertyBaseOffset(Off::InSDK::Properties::PropertySize);
	std::cout << std::format("Off::MapProperty::Base: 0x{:X}\n", Off::MapProperty::Base) << std::endl;

	Off::ULevel::Actors = OffsetFinder::FindLevelActorsOffset();
	std::cout << std::format("Off::ULevel::Actors: 0x{:X}\n", Off::ULevel::Actors) << std::endl;

	OffsetFinder::PostInitFNameSettings();

	Off::ByteProperty::Enum = Off::InSDK::Properties::PropertySize;
	Off::BoolProperty::Base = Off::InSDK::Properties::PropertySize;
	Off::ObjectProperty::PropertyClass = Off::InSDK::Properties::PropertySize;
	Off::StructProperty::Struct = Off::InSDK::Properties::PropertySize;
	Off::EnumProperty::Base = Off::InSDK::Properties::PropertySize;
	Off::DelegateProperty::SignatureFunction =  Off::InSDK::Properties::PropertySize;
	Off::FieldPathProperty::FieldClass = Off::InSDK::Properties::PropertySize;
	Off::OptionalProperty::ValueProperty = Off::InSDK::Properties::PropertySize;

	Off::ClassProperty::MetaClass = Off::InSDK::Properties::PropertySize + 0x8; //0x8 inheritance from ObjectProperty
}
