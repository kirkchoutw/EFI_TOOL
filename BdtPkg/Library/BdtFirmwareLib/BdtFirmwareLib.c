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
#include <Library/BdtFileLib.h>
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

//
// ---------------------------------------------------------------------------
// hash / manifest / verify
//
// A manifest is an ASCII text file, one entry per line:
//
//   # comment
//   <kind> <fv> <guid> <type> <size> <sha256> <name>
//
//   kind    FV (whole firmware volume) or FFS (file contents, no FFS header)
//   fv      1-based FV index as listed by "firmware fv"
//   guid    FV name GUID / file GUID, "-" when the FV has no name
//   type    FFS file type name, "FV" for a volume
//   size    hex byte count
//   sha256  64 hex digits, "-" when the data was not readable
//   name    UI name, rest of the line, "-" when none
// ---------------------------------------------------------------------------
//

#define FW_KIND_FV         0
#define FW_KIND_FFS        1
#define FW_NAME_LENGTH     48
#define FW_TYPE_LENGTH     24
#define FW_MANIFEST_MAGIC  "# BDT firmware manifest v1"

typedef struct {
  UINT8       Kind;
  UINTN       Fv;
  EFI_GUID    Guid;
  BOOLEAN     HasGuid;
  CHAR16      Type[FW_TYPE_LENGTH];
  UINT64      Size;
  BOOLEAN     Hashed;
  UINT8       Hash[BDT_SHA256_SIZE];
  CHAR16      Name[FW_NAME_LENGTH];
  INTN        Match;
} BDT_FW_ENTRY;

typedef struct {
  BDT_FW_ENTRY    *Entries;
  UINTN           Count;
  UINTN           Capacity;
  UINTN           FvCount;
  UINTN           FileCount;
} BDT_FW_SET;

STATIC
VOID
FwFreeSet (
  IN BDT_FW_SET  *Set
  )
{
  if (Set->Entries != NULL) {
    FreePool (Set->Entries);
  }

  ZeroMem (Set, sizeof (*Set));
}

STATIC
BDT_FW_ENTRY *
FwAddEntry (
  IN OUT BDT_FW_SET  *Set,
  IN     UINT8       Kind
  )
{
  BDT_FW_ENTRY  *Grown;
  UINTN         NewCapacity;

  if (Set->Count == Set->Capacity) {
    NewCapacity = (Set->Capacity == 0) ? 128 : Set->Capacity * 2;
    Grown       = ReallocatePool (
                    Set->Capacity * sizeof (BDT_FW_ENTRY),
                    NewCapacity * sizeof (BDT_FW_ENTRY),
                    Set->Entries
                    );
    if (Grown == NULL) {
      return NULL;
    }

    Set->Entries  = Grown;
    Set->Capacity = NewCapacity;
  }

  ZeroMem (&Set->Entries[Set->Count], sizeof (BDT_FW_ENTRY));
  Set->Entries[Set->Count].Kind  = Kind;
  Set->Entries[Set->Count].Match = -1;
  if (Kind == FW_KIND_FV) {
    Set->FvCount++;
  } else {
    Set->FileCount++;
  }

  return &Set->Entries[Set->Count++];
}

/**
  Hash every FV (raw volume bytes) and every FFS file (file contents) of the
  running system. FvIndex limits it to one FV when not 0.
**/
STATIC
EFI_STATUS
FwLoadLive (
  OUT BDT_FW_SET  *Set,
  IN  UINTN       FvIndex
  )
{
  EFI_STATUS                      Status;
  EFI_HANDLE                      *Handles;
  UINTN                           Count;
  UINTN                           Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL   *Fv;
  EFI_FIRMWARE_VOLUME_HEADER      *Header;
  EFI_FIRMWARE_VOLUME_EXT_HEADER  *Ext;
  BDT_FW_ENTRY                    *Entry;
  VOID                            *Key;
  EFI_FV_FILETYPE                 Type;
  EFI_GUID                        Name;
  EFI_FV_FILE_ATTRIBUTES          Attributes;
  UINTN                           Size;
  UINT32                          AuthStatus;
  VOID                            *Buffer;
  CHAR16                          *UiName;

  ZeroMem (Set, sizeof (*Set));
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

  for (Index = 0; Index < Count; Index++) {
    if (((FvIndex != 0) && (Index + 1 != FvIndex)) ||
        EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiFirmwareVolume2ProtocolGuid, (VOID **)&Fv)))
    {
      continue;
    }

    Entry = FwAddEntry (Set, FW_KIND_FV);
    if (Entry == NULL) {
      break;
    }

    Entry->Fv = Index + 1;
    StrCpyS (Entry->Type, FW_TYPE_LENGTH, L"FV");
    Header = GetFvHeader (Handles[Index]);
    if (Header != NULL) {
      Entry->Size = Header->FvLength;
      if ((Header->ExtHeaderOffset != 0) &&
          (Header->ExtHeaderOffset + sizeof (EFI_FIRMWARE_VOLUME_EXT_HEADER) <= Header->FvLength))
      {
        Ext = (EFI_FIRMWARE_VOLUME_EXT_HEADER *)((UINT8 *)Header + Header->ExtHeaderOffset);
        CopyGuid (&Entry->Guid, &Ext->FvName);
        Entry->HasGuid = TRUE;
      }

      if (Header->FvLength <= MAX_UINTN) {
        BdtSha256 (Header, (UINTN)Header->FvLength, Entry->Hash);
        Entry->Hashed = TRUE;
      }
    }

    Key = AllocateZeroPool (Fv->KeySize);
    if (Key == NULL) {
      continue;
    }

    while (TRUE) {
      Type = EFI_FV_FILETYPE_ALL;
      if (EFI_ERROR (Fv->GetNextFile (Fv, Key, &Type, &Name, &Attributes, &Size))) {
        break;
      }

      Entry = FwAddEntry (Set, FW_KIND_FFS);
      if (Entry == NULL) {
        break;
      }

      Entry->Fv      = Index + 1;
      Entry->HasGuid = TRUE;
      Entry->Size    = Size;
      CopyGuid (&Entry->Guid, &Name);
      StrCpyS (Entry->Type, FW_TYPE_LENGTH, FileTypeName (Type));

      Buffer = NULL;
      if (!EFI_ERROR (Fv->ReadFile (Fv, &Name, &Buffer, &Size, &Type, &Attributes, &AuthStatus)) &&
          (Buffer != NULL))
      {
        Entry->Size = Size;
        BdtSha256 (Buffer, Size, Entry->Hash);
        Entry->Hashed = TRUE;
        FreePool (Buffer);
      }

      UiName = GetFileUiName (Fv, &Name);
      if (UiName != NULL) {
        StrnCpyS (Entry->Name, FW_NAME_LENGTH, UiName, FW_NAME_LENGTH - 1);
        FreePool (UiName);
      }
    }

    FreePool (Key);
  }

  FreePool (Handles);
  return EFI_SUCCESS;
}

STATIC
VOID
FwHashText (
  IN  CONST BDT_FW_ENTRY  *Entry,
  OUT CHAR8               Text[BDT_SHA256_SIZE * 2 + 1]
  )
{
  STATIC CONST CHAR8  Hex[] = "0123456789abcdef";
  UINTN               Index;

  if (!Entry->Hashed) {
    AsciiStrCpyS (Text, BDT_SHA256_SIZE * 2 + 1, "-");
    return;
  }

  for (Index = 0; Index < BDT_SHA256_SIZE; Index++) {
    Text[Index * 2]     = Hex[Entry->Hash[Index] >> 4];
    Text[Index * 2 + 1] = Hex[Entry->Hash[Index] & 0xF];
  }

  Text[BDT_SHA256_SIZE * 2] = '\0';
}

STATIC
VOID
FwGuidText (
  IN  CONST BDT_FW_ENTRY  *Entry,
  OUT CHAR16              *Text,
  IN  UINTN               TextSize
  )
{
  if (Entry->HasGuid) {
    UnicodeSPrint (Text, TextSize, L"%g", &Entry->Guid);
  } else {
    StrCpyS (Text, TextSize / sizeof (CHAR16), L"-");
  }
}

EFI_STATUS
EFIAPI
BdtFirmwareHash (
  IN UINTN  FvIndex
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"fv",     L"FV",     4  },
    { L"kind",   L"Kind",   5  },
    { L"guid",   L"GUID",   38 },
    { L"type",   L"Type",   22 },
    { L"size",   L"Size",   10 },
    { L"name",   L"Name",   34 },
    { L"sha256", L"SHA256", 0  },
  };
  EFI_STATUS    Status;
  BDT_FW_SET    Set;
  BDT_FW_ENTRY  *Entry;
  UINTN         Index;
  CHAR16        GuidText[40];
  CHAR8         HashText[BDT_SHA256_SIZE * 2 + 1];

  Status = FwLoadLive (&Set, FvIndex);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  BdtOutTableBegin (L"hashes", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < Set.Count; Index++) {
    Entry = &Set.Entries[Index];
    FwGuidText (Entry, GuidText, sizeof (GuidText));
    FwHashText (Entry, HashText);
    BdtOutRowBegin ();
    BdtOutCell (L"%d", Entry->Fv);
    BdtOutCell (L"%s", (Entry->Kind == FW_KIND_FV) ? L"FV" : L"FFS");
    BdtOutCell (L"%s", GuidText);
    BdtOutCell (L"%s", Entry->Type);
    BdtOutCell (L"0x%lx", Entry->Size);
    BdtOutCell (L"%s", Entry->Name);
    BdtOutCell (L"%a", HashText);
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"FVs", Set.FvCount);
  BdtOutFieldNum (L"Files", Set.FileCount);
  FwFreeSet (&Set);
  return EFI_SUCCESS;
}

/**
  Append a formatted ASCII line to a growing buffer.
**/
STATIC
EFI_STATUS
EFIAPI
FwAppend (
  IN OUT CHAR8        **Buffer,
  IN OUT UINTN        *Used,
  IN OUT UINTN        *Capacity,
  IN     CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Marker;
  CHAR8    Line[256];
  UINTN    Length;
  CHAR8    *Grown;

  VA_START (Marker, Format);
  Length = AsciiVSPrint (Line, sizeof (Line), Format, Marker);
  VA_END (Marker);

  if (*Used + Length + 1 > *Capacity) {
    Grown = ReallocatePool (*Capacity, MAX (*Capacity * 2, *Used + Length + 1024), *Buffer);
    if (Grown == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    *Capacity = MAX (*Capacity * 2, *Used + Length + 1024);
    *Buffer   = Grown;
  }

  CopyMem (*Buffer + *Used, Line, Length);
  *Used += Length;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtFirmwareManifest (
  IN CONST CHAR16  *Path
  )
{
  EFI_STATUS    Status;
  BDT_FW_SET    Set;
  BDT_FW_ENTRY  *Entry;
  UINTN         Index;
  CHAR8         *Buffer;
  UINTN         Used;
  UINTN         Capacity;
  CHAR16        GuidText[40];
  CHAR8         HashText[BDT_SHA256_SIZE * 2 + 1];
  CHAR8         NameText[FW_NAME_LENGTH];

  Status = FwLoadLive (&Set, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer   = NULL;
  Used     = 0;
  Capacity = 0;
  FwAppend (&Buffer, &Used, &Capacity, "%a\r\n", FW_MANIFEST_MAGIC);
  FwAppend (&Buffer, &Used, &Capacity, "# Created by BDT.efi %s\r\n", BDT_VERSION_STRING);
  FwAppend (
    &Buffer,
    &Used,
    &Capacity,
    "# Firmware: %s 0x%08x\r\n",
    (gST->FirmwareVendor != NULL) ? gST->FirmwareVendor : L"",
    gST->FirmwareRevision
    );
  FwAppend (&Buffer, &Used, &Capacity, "# kind fv guid type size sha256 name\r\n");
  for (Index = 0; Index < Set.Count; Index++) {
    Entry = &Set.Entries[Index];
    FwGuidText (Entry, GuidText, sizeof (GuidText));
    FwHashText (Entry, HashText);
    UnicodeStrToAsciiStrS ((Entry->Name[0] != L'\0') ? Entry->Name : L"-", NameText, sizeof (NameText));
    Status = FwAppend (
               &Buffer,
               &Used,
               &Capacity,
               "%a %d %s %s 0x%lx %a %a\r\n",
               (Entry->Kind == FW_KIND_FV) ? "FV" : "FFS",
               Entry->Fv,
               GuidText,
               Entry->Type,
               Entry->Size,
               HashText,
               NameText
               );
    if (EFI_ERROR (Status)) {
      break;
    }
  }

  if (!EFI_ERROR (Status)) {
    Status = BdtFileWriteAll (Path, Buffer, Used);
  }

  if (EFI_ERROR (Status)) {
    BdtOutError (L"Cannot write %s (%s)", Path, BdtStatusName (Status));
  } else {
    BdtOutField (L"Manifest", L"%s", Path);
    BdtOutFieldNum (L"FVs", Set.FvCount);
    BdtOutFieldNum (L"Files", Set.FileCount);
  }

  if (Buffer != NULL) {
    FreePool (Buffer);
  }

  FwFreeSet (&Set);
  return Status;
}

/**
  Split off the next blank separated token. Returns NULL at the end of line.
**/
STATIC
CHAR8 *
FwNextToken (
  IN OUT CHAR8  **Cursor
  )
{
  CHAR8  *Start;

  while ((**Cursor == ' ') || (**Cursor == '\t')) {
    (*Cursor)++;
  }

  if (**Cursor == '\0') {
    return NULL;
  }

  Start = *Cursor;
  while ((**Cursor != '\0') && (**Cursor != ' ') && (**Cursor != '\t')) {
    (*Cursor)++;
  }

  if (**Cursor != '\0') {
    **Cursor = '\0';
    (*Cursor)++;
  }

  return Start;
}

STATIC
BOOLEAN
FwParseHash (
  IN  CONST CHAR8  *Text,
  OUT UINT8        Hash[BDT_SHA256_SIZE]
  )
{
  UINTN  Index;
  UINT8  Nibble;
  CHAR8  Char;

  if (AsciiStrLen (Text) != BDT_SHA256_SIZE * 2) {
    return FALSE;
  }

  ZeroMem (Hash, BDT_SHA256_SIZE);
  for (Index = 0; Index < BDT_SHA256_SIZE * 2; Index++) {
    Char = Text[Index];
    if ((Char >= '0') && (Char <= '9')) {
      Nibble = (UINT8)(Char - '0');
    } else if ((Char >= 'a') && (Char <= 'f')) {
      Nibble = (UINT8)(Char - 'a' + 10);
    } else if ((Char >= 'A') && (Char <= 'F')) {
      Nibble = (UINT8)(Char - 'A' + 10);
    } else {
      return FALSE;
    }

    Hash[Index / 2] |= (UINT8)(((Index % 2) == 0) ? (Nibble << 4) : Nibble);
  }

  return TRUE;
}

/**
  Parse one manifest line into a new entry. Returns FALSE on a syntax error.
**/
STATIC
BOOLEAN
FwParseLine (
  IN OUT BDT_FW_SET  *Set,
  IN OUT CHAR8       *Line
  )
{
  CHAR8         *Cursor;
  CHAR8         *Token[6];
  UINTN         Index;
  UINT8         Kind;
  BDT_FW_ENTRY  *Entry;
  CHAR16        Text[48];
  UINTN         Length;

  Cursor = Line;
  for (Index = 0; Index < ARRAY_SIZE (Token); Index++) {
    Token[Index] = FwNextToken (&Cursor);
    if (Token[Index] == NULL) {
      return FALSE;
    }
  }

  if (AsciiStrCmp (Token[0], "FV") == 0) {
    Kind = FW_KIND_FV;
  } else if (AsciiStrCmp (Token[0], "FFS") == 0) {
    Kind = FW_KIND_FFS;
  } else {
    return FALSE;
  }

  if ((AsciiStrLen (Token[1]) > 8) || (AsciiStrLen (Token[2]) >= ARRAY_SIZE (Text)) ||
      (AsciiStrLen (Token[3]) >= FW_TYPE_LENGTH) || (AsciiStrLen (Token[4]) > 18))
  {
    return FALSE;
  }

  Entry = FwAddEntry (Set, Kind);
  if (Entry == NULL) {
    return FALSE;
  }

  Entry->Fv = AsciiStrDecimalToUintn (Token[1]);
  if (AsciiStrCmp (Token[2], "-") != 0) {
    AsciiStrToUnicodeStrS (Token[2], Text, ARRAY_SIZE (Text));
    if (EFI_ERROR (StrToGuid (Text, &Entry->Guid)) || (StrLen (Text) != 36)) {
      return FALSE;
    }

    Entry->HasGuid = TRUE;
  } else if (Kind == FW_KIND_FFS) {
    return FALSE;
  }

  AsciiStrToUnicodeStrS (Token[3], Entry->Type, FW_TYPE_LENGTH);
  Entry->Size = AsciiStrHexToUint64 (Token[4]);
  if (AsciiStrCmp (Token[5], "-") != 0) {
    if (!FwParseHash (Token[5], Entry->Hash)) {
      return FALSE;
    }

    Entry->Hashed = TRUE;
  }

  //
  // The rest of the line is the name.
  //
  while ((*Cursor == ' ') || (*Cursor == '\t')) {
    Cursor++;
  }

  Length = AsciiStrLen (Cursor);
  while ((Length > 0) && ((Cursor[Length - 1] == ' ') || (Cursor[Length - 1] == '\t'))) {
    Cursor[--Length] = '\0';
  }

  if ((Length > 0) && (AsciiStrCmp (Cursor, "-") != 0)) {
    if (Length >= FW_NAME_LENGTH) {
      Cursor[FW_NAME_LENGTH - 1] = '\0';
    }

    AsciiStrToUnicodeStrS (Cursor, Entry->Name, FW_NAME_LENGTH);
  }

  return TRUE;
}

STATIC
EFI_STATUS
FwLoadManifest (
  IN  CONST CHAR16  *Path,
  OUT BDT_FW_SET    *Set
  )
{
  EFI_STATUS  Status;
  CHAR8       *Data;
  UINTN       Size;
  CHAR8       *Line;
  CHAR8       *End;
  UINTN       LineNumber;
  BOOLEAN     Magic;

  ZeroMem (Set, sizeof (*Set));
  Status = BdtFileReadAll (Path, (VOID **)&Data, &Size);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"Cannot read %s (%s)", Path, BdtStatusName (Status));
    return Status;
  }

  //
  // BdtFileReadAll adds a terminating zero. Stop at an embedded one too.
  //
  Size       = AsciiStrnLenS (Data, Size + 1);
  Line       = Data;
  LineNumber = 0;
  Magic      = FALSE;
  while (Line < Data + Size) {
    LineNumber++;
    End = Line;
    while ((*End != '\0') && (*End != '\n')) {
      End++;
    }

    if (*End == '\n') {
      *End = '\0';
      End++;
    }

    if ((AsciiStrLen (Line) > 0) && (Line[AsciiStrLen (Line) - 1] == '\r')) {
      Line[AsciiStrLen (Line) - 1] = '\0';
    }

    if (AsciiStrnCmp (Line, FW_MANIFEST_MAGIC, AsciiStrLen (FW_MANIFEST_MAGIC)) == 0) {
      Magic = TRUE;
    } else if ((Line[0] != '#') && (Line[0] != '\0') && !FwParseLine (Set, Line)) {
      BdtOutError (L"%s line %d: not a valid manifest entry", Path, LineNumber);
      Status = EFI_VOLUME_CORRUPTED;
      break;
    }

    Line = End;
  }

  FreePool (Data);
  if (!EFI_ERROR (Status) && (!Magic || (Set->Count == 0))) {
    BdtOutError (L"%s is not a BDT firmware manifest (create one with \"firmware manifest <file>\")", Path);
    Status = EFI_VOLUME_CORRUPTED;
  }

  if (EFI_ERROR (Status)) {
    FwFreeSet (Set);
  }

  return Status;
}

/**
  Pair manifest entries with live entries.

  FV:  by FV name GUID, then unnamed volumes by FV index.
  FFS: by file GUID in the same FV index, then by file GUID anywhere, so a
       file that moved to another FV still matches.
**/
STATIC
VOID
FwMatch (
  IN OUT BDT_FW_SET  *Old,
  IN OUT BDT_FW_SET  *New
  )
{
  UINTN         Pass;
  UINTN         OldIndex;
  UINTN         NewIndex;
  BDT_FW_ENTRY  *A;
  BDT_FW_ENTRY  *B;
  BOOLEAN       Pair;

  for (Pass = 0; Pass < 2; Pass++) {
    for (OldIndex = 0; OldIndex < Old->Count; OldIndex++) {
      A = &Old->Entries[OldIndex];
      if (A->Match >= 0) {
        continue;
      }

      for (NewIndex = 0; NewIndex < New->Count; NewIndex++) {
        B = &New->Entries[NewIndex];
        if ((B->Match >= 0) || (A->Kind != B->Kind)) {
          continue;
        }

        if (A->Kind == FW_KIND_FV) {
          Pair = (Pass == 0) ? (A->HasGuid && B->HasGuid && CompareGuid (&A->Guid, &B->Guid))
                             : (!A->HasGuid && !B->HasGuid && (A->Fv == B->Fv));
        } else {
          Pair = CompareGuid (&A->Guid, &B->Guid) && ((Pass == 1) || (A->Fv == B->Fv));
        }

        if (Pair) {
          A->Match = (INTN)NewIndex;
          B->Match = (INTN)OldIndex;
          break;
        }
      }
    }
  }
}

STATIC
VOID
FwDiffRow (
  IN CONST CHAR16        *Status,
  IN CONST BDT_FW_ENTRY  *Old OPTIONAL,
  IN CONST BDT_FW_ENTRY  *New OPTIONAL,
  IN BOOLEAN             ShowHash
  )
{
  CONST BDT_FW_ENTRY  *Any;
  CHAR16              GuidText[40];
  CHAR8               HashText[BDT_SHA256_SIZE * 2 + 1];

  Any = (New != NULL) ? New : Old;
  FwGuidText (Any, GuidText, sizeof (GuidText));
  BdtOutRowBegin ();
  BdtOutCell (L"%s", Status);
  BdtOutCell (L"%s", (Any->Kind == FW_KIND_FV) ? L"FV" : L"FFS");
  BdtOutCell (L"%d", Any->Fv);
  BdtOutCell (L"%s", GuidText);
  BdtOutCell (L"%s", Any->Type);
  BdtOutCell (L"%s", (Old != NULL) && (Old->Name[0] != L'\0') ? Old->Name : Any->Name);
  if (Old != NULL) {
    BdtOutCell (L"0x%lx", Old->Size);
  } else {
    BdtOutCell (L"-");
  }

  if (New != NULL) {
    BdtOutCell (L"0x%lx", New->Size);
  } else {
    BdtOutCell (L"-");
  }

  if (ShowHash) {
    if (Old != NULL) {
      FwHashText (Old, HashText);
      BdtOutCell (L"%a", HashText);
    } else {
      BdtOutCell (L"-");
    }

    if (New != NULL) {
      FwHashText (New, HashText);
      BdtOutCell (L"%a", HashText);
    } else {
      BdtOutCell (L"-");
    }
  }

  BdtOutRowEnd ();
}

EFI_STATUS
EFIAPI
BdtFirmwareVerify (
  IN CONST CHAR16  *Path
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"status",     L"Status",   10 },
    { L"kind",       L"Kind",     5  },
    { L"fv",         L"FV",       4  },
    { L"guid",       L"GUID",     38 },
    { L"type",       L"Type",     22 },
    { L"name",       L"Name",     34 },
    { L"old_size",   L"Old Size", 10 },
    { L"new_size",   L"New Size", 10 },
    { L"old_sha256", L"Old SHA256", 66 },
    { L"new_sha256", L"New SHA256", 0  },
  };
  EFI_STATUS    Status;
  BDT_FW_SET    Old;
  BDT_FW_SET    New;
  BDT_FW_ENTRY  *A;
  BDT_FW_ENTRY  *B;
  UINTN         Index;
  UINTN         Matched;
  UINTN         Changed;
  UINTN         Missing;
  UINTN         Added;
  UINTN         Unverified;
  BOOLEAN       ShowHash;

  Status = FwLoadManifest (Path, &Old);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FwLoadLive (&New, 0);
  if (EFI_ERROR (Status)) {
    FwFreeSet (&Old);
    return Status;
  }

  FwMatch (&Old, &New);

  //
  // Text output keeps the table readable; CSV / JSON also carry the hashes.
  //
  ShowHash   = (BOOLEAN)(BdtOutGetFormat () != BdtFormatText);
  Matched    = 0;
  Changed    = 0;
  Missing    = 0;
  Added      = 0;
  Unverified = 0;
  BdtOutField (L"Manifest", L"%s", Path);
  BdtOutFieldNum (L"Manifest Entries", Old.Count);
  BdtOutFieldNum (L"Live Entries", New.Count);
  BdtOutTableBegin (L"differences", Columns, ShowHash ? ARRAY_SIZE (Columns) : ARRAY_SIZE (Columns) - 2);
  for (Index = 0; Index < Old.Count; Index++) {
    A = &Old.Entries[Index];
    if (A->Match < 0) {
      FwDiffRow (L"MISSING", A, NULL, ShowHash);
      Missing++;
      continue;
    }

    B = &New.Entries[A->Match];
    if (A->Size != B->Size) {
      FwDiffRow (L"CHANGED", A, B, ShowHash);
      Changed++;
    } else if (!A->Hashed || !B->Hashed) {
      FwDiffRow (L"NO HASH", A, B, ShowHash);
      Unverified++;
    } else if (CompareMem (A->Hash, B->Hash, BDT_SHA256_SIZE) != 0) {
      FwDiffRow (L"CHANGED", A, B, ShowHash);
      Changed++;
    } else {
      Matched++;
    }
  }

  for (Index = 0; Index < New.Count; Index++) {
    if (New.Entries[Index].Match < 0) {
      FwDiffRow (L"NEW", NULL, &New.Entries[Index], ShowHash);
      Added++;
    }
  }

  BdtOutTableEnd ();
  if ((Changed + Missing + Added + Unverified) == 0) {
    BdtOutNote (L"No differences.");
  }

  BdtOutFieldNum (L"Matched", Matched);
  BdtOutFieldNum (L"Changed", Changed);
  BdtOutFieldNum (L"Missing", Missing);
  BdtOutFieldNum (L"New", Added);
  BdtOutFieldNum (L"Unverified", Unverified);
  BdtOutField (L"Result", L"%s", ((Changed + Missing + Added) == 0) ? L"PASS" : L"FAIL");

  FwFreeSet (&Old);
  FwFreeSet (&New);
  return ((Changed + Missing + Added) == 0) ? EFI_SUCCESS : EFI_SECURITY_VIOLATION;
}
