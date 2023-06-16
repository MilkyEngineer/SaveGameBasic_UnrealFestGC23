// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#include "SaveGameFunctionLibrary.h"

#include "SaveGameSettings.h"

#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"

void BreakpointWithError(FFrame& Stack, const FText& Text)
{
	const FBlueprintExceptionInfo ExceptionInfo(EBlueprintExceptionType::Breakpoint, Text);

	const int32 BreakpointOpCodeOffset = Stack.Code - Stack.Node->Script.GetData() - 1;
	const UEdGraphNode* Node = FKismetDebugUtilities::FindSourceNodeForCodeLocation(Stack.Object, Stack.Node, BreakpointOpCodeOffset, true);

	struct Local
	{
		static void OnMessageLogLinkActivated(const class TSharedRef<IMessageToken>& Token)
		{
			if( Token->GetType() == EMessageToken::Object )
			{
				const TSharedRef<FUObjectToken> UObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
				if(UObjectToken->GetObject().IsValid())
				{
					FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(UObjectToken->GetObject().Get());
				}	
			}
		}
	};
		
	FMessageLog MessageLog("PIE");
	MessageLog.Error()
		->AddToken(FUObjectToken::Create(Node, Node->GetNodeTitle(ENodeTitleType::ListView))
			->OnMessageTokenActivated(FOnMessageTokenActivated::CreateStatic(&Local::OnMessageLogLinkActivated)))
		->AddToken(FTextToken::Create(Text));
	MessageLog.Open(EMessageSeverity::Error);

	FBlueprintCoreDelegates::ThrowScriptException(Stack.Object, Stack, ExceptionInfo);
}
#endif

bool USaveGameFunctionLibrary::WasObjectLoaded(const UObject* Object)
{
	return Object && Object->HasAnyFlags(RF_WasLoaded | RF_LoadCompleted);
}

bool USaveGameFunctionLibrary::IsLoading(const FSaveGameArchive& Archive)
{
	return Archive.IsValid() && Archive.GetRecord().GetUnderlyingArchive().IsLoading();
}

bool USaveGameFunctionLibrary::SerializeActorTransform(FSaveGameArchive& Archive, AActor* Actor)
{
	if (Archive.IsValid() && IsValid(Actor))
	{
		const bool bIsMovable = Actor->IsRootComponentMovable();
		FStructuredArchive::FRecord& Record = Archive.GetRecord();

		// Save into a slot only if the actor is movable
		if (TOptional<FStructuredArchive::FSlot> TransformSlot = Record.TryEnterField(TEXT("Transform"), bIsMovable))
		{
			const bool bIsLoading = Record.GetUnderlyingArchive().IsLoading();
			FTransform Transform;

			if (!bIsLoading)
			{
				Transform = Actor->GetActorTransform();
			}

			// Serialize the transform
			TransformSlot.GetValue() << Transform;

			if (bIsLoading && bIsMovable)
			{
				// If the actor is movable, set its transform
				Actor->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	return false;
}

bool USaveGameFunctionLibrary::SerializeItem(FSaveGameArchive& Archive, int32& Value, bool bSave)
{
	checkf(false, TEXT("Shouldn't call this natively!"));
	return false;
}

DEFINE_FUNCTION(USaveGameFunctionLibrary::execSerializeItem)
{
	// Get a reference to our archive that contains the record
	P_GET_STRUCT_REF(FSaveGameArchive, Archive);

	// This will step into the property that we've attached
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	uint8* ValueAddress = Stack.MostRecentPropertyAddress;

	// If we're saving, should we serialize this value?
	P_GET_UBOOL(bSave);
	
	P_FINISH;
	
	P_NATIVE_BEGIN;

	*(bool*)RESULT_PARAM = false;

#if WITH_EDITOR
	if (!ValueProperty->HasAnyPropertyFlags(CPF_Edit) || ValueProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
	{
		BreakpointWithError(Stack,
			FText::Format(NSLOCTEXT("SaveGame", "SerialiseItem_NotVariableException", "'{0}' connected to the Value pin is not an editable variable!"), ValueProperty->GetDisplayNameText()));
	}
	else
#endif
	if (Archive.IsValid())
	{
		FStructuredArchive::FRecord& Record = Archive.GetRecord();

		if (const TOptional<FStructuredArchive::FSlot> PropertySlot = Record.TryEnterField(*ValueProperty->GetName(), bSave))
		{
			// Note: SerializeItem will not handle type conversions, though ConvertFromType will do this with some
			// questionable address arithmetic 
			ValueProperty->SerializeItem(PropertySlot.GetValue(), ValueAddress, nullptr);
			*(bool*)RESULT_PARAM = true;
		}
	}
	
	P_NATIVE_END;
}

int32 USaveGameFunctionLibrary::UseCustomVersion(FSaveGameArchive& Archive, const UEnum* VersionEnum)
{
	if (Archive.IsValid() && IsValid(VersionEnum))
	{
		FArchive& UnderlyingArchive = Archive.GetRecord().GetUnderlyingArchive();
		const FGuid VersionId = GetDefault<USaveGameSettings>()->GetVersionId(VersionEnum);

		if (VersionId.IsValid())
		{
			if (UnderlyingArchive.IsLoading())
			{
				// If the archive has one, return its saved version
				const FCustomVersion* CustomVersion = UnderlyingArchive.GetCustomVersions().GetVersion(VersionId);
				return CustomVersion ? CustomVersion->Version : INDEX_NONE;
			}

			// Get the latest version and save it
			const int32 Version = VersionEnum->GetMaxEnumValue() - 1;
			UnderlyingArchive.SetCustomVersion(VersionId, Version, VersionEnum->GetFName());
			return Version;
		}
	}

	return INDEX_NONE;
}
