/** @file
  BdtFirmwareLib - firmware volume / FFS inspection.

  Uses EFI_FIRMWARE_VOLUME2_PROTOCOL produced by the DXE core for every
  firmware volume it dispatched, so only FVs known to DXE are visible.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Library/DevicePathLib.h>
#include <Library/BdtFirmwareLib.h>
#include <Pi/PiFirmwareVolume.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/FirmwareVolumeBlock.h>

STATIC CONST CHAR16  *mFileTypeNames[] = {
  L"ALL",
  L"RAW",
  L"FREEFORM",
  L"SECURITY_CORE",
  L"PEI_CORE",
  L"DXE_CORE",
  L"PEIM",
  L"DRIVER",
  L"COMBINED_PEIM_DRIVER",
  L"APPLICATION",
  L"MM",
  L"FV_IMAGE",
  L"COMBINED_MM_DXE",
  L"MM_CORE",
  L"MM_STANDALONE",
  L"MM_CORE_STANDALONE",
};

STATIC
CONST CHAR16 *
FileTypeName (
  IN EFI_FV_FILETYPE  Type
  )
{
  if (Type < ARRAY_SIZE (mFileTypeNames)) {
    return mFileTypeNames[Type];
  }

  if (Type == EFI_FV_FILETYPE_FFS_PAD) {
    return L"FFS_PAD";
  }

  if (Type >= EFI_FV_FILETYPE_OEM_MIN) {
    return L"OEM";
  }

  return L"UNKNOWN";
}

STATIC
EFI_STATUS
GetFvHandles (
  OUT EFI_HANDLE  **Handles,
  OUT UINTN       *Count
  )
{
  return gBS->LocateHandleBuffer (ByProtocol, &gEfiFirmwareVolume2ProtocolGuid, NULL, Count, Handles);
}

/**
  Get the memory mapped FV header behind an FV2 handle, or NULL.
**/
STATIC
EFI_FIRMWARE_VOLUME_HEADER *
GetFvHeader (
  IN EFI_HANDLE  Handle
  )
{
  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *Fvb;
  EFI_PHYSICAL_ADDRESS                 Address;
  EFI_FIRMWARE_VOLUME_HEADER           *Header;

  if (EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiFirmwareVolumeBlockProtocolGuid, (VOID **)&Fvb)) ||
      EFI_ERROR (Fvb->GetPhysicalAddress (Fvb, &Address)) || (Address == 0) || (Address > MAX_ADDRESS))
  {
    return NULL;
  }

  Header = (EFI_FIRMWARE_VOLUME_HEADER *)(UINTN)Address;
  return (Header->Signature == EFI_FVH_SIGNATURE) ? Header : NULL;
}

/**
  Return the user interface name of a file in a newly allocated buffer, or NULL.
**/
STATIC
CHAR16 *
GetFileUiName (
  IN EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv,
  IN EFI_GUID                       *Name
  )
{
  VOID    *Buffer;
  UINTN   Size;
  UINT32  AuthStatus;

  Buffer = NULL;
  Size   = 0;
  if (EFI_ERROR (Fv->ReadSection (Fv, Name, EFI_SECTION_USER_INTERFACE, 0, &Buffer, &Size, &AuthStatus))) {
    return NULL;
  }

  return (CHAR16 *)Buffer;
}

STATIC
UINTN
CountFiles (
  IN EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv
  )
{
  VOID                    *Key;
  EFI_FV_FILETYPE         Type;
  EFI_GUID                Name;
  EFI_FV_FILE_ATTRIBUTES  Attributes;
  UINTN                   Size;
  UINTN                   Count;

  Key = AllocateZeroPool (Fv->KeySize);
  if (Key == NULL) {
    return 0;
  }

  Count = 0;
  while (TRUE) {
    Type = EFI_FV_FILETYPE_ALL;
    if (EFI_ERROR (Fv->GetNextFile (Fv, Key, &Type, &Name, &Attributes, &Size))) {
      break;
    }

    Count++;
  }

  FreePool (Key);
  return Count;
}

EFI_STATUS
EFIAPI
BdtFirmwareFvList (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"index",       L"FV",          4  },
    { L"base",        L"Base",        20 },
    { L"size",        L"Size",        12 },
    { L"files",       L"Files",       7  },
    { L"name",        L"FV Name",     38 },
    { L"device_path", L"Device Path", 0  },
  };
  EFI_STATUS                     Status;
  EFI_HANDLE                     *Handles;
  UINTN                          Count;
  UINTN                          Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv;
  EFI_FIRMWARE_VOLUME_HEADER     *Header;
  EFI_FIRMWARE_VOLUME_EXT_HEADER *Ext;
  CHAR16                         *Text;
  CHAR16                         NameText[40];

  Status = GetFvHandles (&Handles, &Count);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"No firmware volume found (%s)", BdtStatusName (Status));
    return Status;
  }

  BdtOutFieldNum (L"FV Count", Count);
  BdtOutTableBegin (L"firmware_volumes", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiFirmwareVolume2ProtocolGuid, (VOID **)&Fv))) {
      continue;
    }

    Header      = GetFvHeader (Handles[Index]);
    NameText[0] = L'\0';
    if ((Header != NULL) && (Header->ExtHeaderOffset != 0) &&
        (Header->ExtHeaderOffset + sizeof (EFI_FIRMWARE_VOLUME_EXT_HEADER) <= Header->FvLength))
    {
      Ext = (EFI_FIRMWARE_VOLUME_EXT_HEADER *)((UINT8 *)Header + Header->ExtHeaderOffset);
      UnicodeSPrint (NameText, sizeof (NameText), L"%g", &Ext->FvName);
    }

    Text = ConvertDevicePathToText (DevicePathFromHandle (Handles[Index]), FALSE, TRUE);
    BdtOutRowBegin ();
    BdtOutCell (L"%d", Index + 1);
    if (Header != NULL) {
      BdtOutCell (L"0x%016lx", (UINT64)(UINTN)Header);
      BdtOutCell (L"0x%lx", Header->FvLength);
    } else {
      BdtOutCell (L"-");
      BdtOutCell (L"-");
    }

    BdtOutCell (L"%d", CountFiles (Fv));
    BdtOutCell (L"%s", NameText);
    BdtOutCell (L"%s", (Text != NULL) ? Text : L"");
    BdtOutRowEnd ();
    if (Text != NULL) {
      FreePool (Text);
    }
  }

  BdtOutTableEnd ();
  FreePool (Handles);
  return EFI_SUCCESS;
}

STATIC
VOID
ListFiles (
  IN UINTN                          FvIndex,
  IN EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv
  )
{
  VOID                    *Key;
  EFI_FV_FILETYPE         Type;
  EFI_GUID                Name;
  EFI_FV_FILE_ATTRIBUTES  Attributes;
  UINTN                   Size;
  CHAR16                  *UiName;

  Key = AllocateZeroPool (Fv->KeySize);
  if (Key == NULL) {
    return;
  }

  while (TRUE) {
    Type = EFI_FV_FILETYPE_ALL;
    if (EFI_ERROR (Fv->GetNextFile (Fv, Key, &Type, &Name, &Attributes, &Size))) {
      break;
    }

    UiName = GetFileUiName (Fv, &Name);
    BdtOutRowBegin ();
    BdtOutCell (L"%d", FvIndex);
    BdtOutCell (L"%g", &Name);
    BdtOutCell (L"%s", FileTypeName (Type));
    BdtOutCell (L"0x%x", Size);
    BdtOutCell (L"%s", (UiName != NULL) ? UiName : L"");
    BdtOutRowEnd ();
    if (UiName != NULL) {
      FreePool (UiName);
    }
  }

  FreePool (Key);
}

STATIC CONST BDT_COLUMN  mFileColumns[] = {
  { L"fv",   L"FV",   4  },
  { L"guid", L"GUID", 38 },
  { L"type", L"Type", 22 },
  { L"size", L"Size", 10 },
  { L"name", L"Name", 0  },
};

EFI_STATUS
EFIAPI
BdtFirmwareFfsList (
  IN UINTN  FvIndex
  )
{
  EFI_STATUS                     Status;
  EFI_HANDLE                     *Handles;
  UINTN                          Count;
  UINTN                          Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv;

  Status = GetFvHandles (&Handles, &Count);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"No firmware volume found (%s)", BdtStatusName (Status));
    return Status;
  }

  if (FvIndex > Count) {
    BdtOutError (L"FV index %d out of range (1 - %d)", FvIndex, Count);
    FreePool (Handles);
    return EFI_INVALID_PARAMETER;
  }

  BdtOutTableBegin (L"files", mFileColumns, ARRAY_SIZE (mFileColumns));
  for (Index = 0; Index < Count; Index++) {
    if ((FvIndex != 0) && (Index + 1 != FvIndex)) {
      continue;
    }

    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiFirmwareVolume2ProtocolGuid, (VOID **)&Fv))) {
      ListFiles (Index + 1, Fv);
    }
  }

  BdtOutTableEnd ();
  FreePool (Handles);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtFirmwareFind (
  IN CONST EFI_GUID  *FileName
  )
{
  EFI_STATUS                     Status;
  EFI_HANDLE                     *Handles;
  UINTN                          Count;
  UINTN                          Index;
  UINTN                          Found;
  EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv;
  EFI_FV_FILETYPE                Type;
  EFI_FV_FILE_ATTRIBUTES         Attributes;
  UINTN                          Size;
  UINT32                         AuthStatus;
  VOID                           *Buffer;
  CHAR16                         *UiName;

  Status = GetFvHandles (&Handles, &Count);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"No firmware volume found (%s)", BdtStatusName (Status));
    return Status;
  }

  BdtOutField (L"File", L"%g", FileName);
  Found = 0;
  BdtOutTableBegin (L"matches", mFileColumns, ARRAY_SIZE (mFileColumns));
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiFirmwareVolume2ProtocolGuid, (VOID **)&Fv))) {
      continue;
    }

    Buffer = NULL;
    Size   = 0;
    if (EFI_ERROR (Fv->ReadFile (Fv, FileName, &Buffer, &Size, &Type, &Attributes, &AuthStatus))) {
      continue;
    }

    UiName = GetFileUiName (Fv, (EFI_GUID *)FileName);
    BdtOutRowBegin ();
    BdtOutCell (L"%d", Index + 1);
    BdtOutCell (L"%g", FileName);
    BdtOutCell (L"%s", FileTypeName (Type));
    BdtOutCell (L"0x%x", Size);
    BdtOutCell (L"%s", (UiName != NULL) ? UiName : L"");
    BdtOutRowEnd ();
    if (UiName != NULL) {
      FreePool (UiName);
    }

    Found++;
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Found", Found);
  FreePool (Handles);
  return (Found == 0) ? EFI_NOT_FOUND : EFI_SUCCESS;
}
