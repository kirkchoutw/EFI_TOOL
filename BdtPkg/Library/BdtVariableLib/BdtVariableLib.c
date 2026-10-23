/** @file
  BdtVariableLib - UEFI variable inspection.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Guid/GlobalVariable.h>
#include <Library/BdtFileLib.h>
#include <Library/BdtVariableLib.h>

#define BDT_VAR_PREVIEW_SIZE  256

STATIC
VOID
FormatVarAttributes (
  IN  UINT32  Attributes,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  STATIC CONST struct {
    UINT32          Bit;
    CONST CHAR16    *Name;
  } Names[] = {
    { EFI_VARIABLE_NON_VOLATILE,                          L"NV" },
    { EFI_VARIABLE_BOOTSERVICE_ACCESS,                    L"BS" },
    { EFI_VARIABLE_RUNTIME_ACCESS,                        L"RT" },
    { EFI_VARIABLE_HARDWARE_ERROR_RECORD,                 L"HR" },
    { EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS,            L"AW" },
    { EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS, L"AT" },
    { EFI_VARIABLE_APPEND_WRITE,                          L"AP" },
    { 0x00000080,                                         L"EA" },
  };
  UINTN  Index;

  Buffer[0] = L'\0';
  for (Index = 0; Index < ARRAY_SIZE (Names); Index++) {
    if ((Attributes & Names[Index].Bit) != 0) {
      if (Buffer[0] != L'\0') {
        StrCatS (Buffer, BufferSize / sizeof (CHAR16), L"|");
      }

      StrCatS (Buffer, BufferSize / sizeof (CHAR16), Names[Index].Name);
    }
  }
}

/**
  Iterate variables. *Name must start as an allocated empty string.

  @retval EFI_NOT_FOUND  No more variables.
**/
STATIC
EFI_STATUS
NextVariable (
  IN OUT CHAR16    **Name,
  IN OUT UINTN     *NameBufferSize,
  IN OUT EFI_GUID  *Guid
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  CHAR16      *NewName;

  Size   = *NameBufferSize;
  Status = gRT->GetNextVariableName (&Size, *Name, Guid);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    NewName = ReallocatePool (*NameBufferSize, Size, *Name);
    if (NewName == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    *Name           = NewName;
    *NameBufferSize = Size;
    Status          = gRT->GetNextVariableName (&Size, *Name, Guid);
  }

  return Status;
}

STATIC
EFI_STATUS
StartEnumeration (
  OUT CHAR16    **Name,
  OUT UINTN     *NameBufferSize,
  OUT EFI_GUID  *Guid
  )
{
  *NameBufferSize = 256 * sizeof (CHAR16);
  *Name           = AllocateZeroPool (*NameBufferSize);
  ZeroMem (Guid, sizeof (*Guid));
  return (*Name == NULL) ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtVarList (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"name",       L"Variable",    34 },
    { L"guid",       L"GUID",        40 },
    { L"size",       L"Size",        10 },
    { L"attributes", L"Attributes",  0  },
  };
  EFI_STATUS  Status;
  CHAR16      *Name;
  UINTN       NameSize;
  EFI_GUID    Guid;
  UINTN       DataSize;
  UINT32      Attributes;
  UINTN       Count;
  UINT64      Total;
  CHAR16      GuidText[40];
  CHAR16      AttrText[32];

  Status = StartEnumeration (&Name, &NameSize, &Guid);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Count = 0;
  Total = 0;
  BdtOutTableBegin (L"variables", Columns, ARRAY_SIZE (Columns));
  while (!EFI_ERROR (Status = NextVariable (&Name, &NameSize, &Guid))) {
    DataSize   = 0;
    Attributes = 0;
    gRT->GetVariable (Name, &Guid, &Attributes, &DataSize, NULL);
    FormatVarAttributes (Attributes, AttrText, sizeof (AttrText));
    BdtOutRowBegin ();
    BdtOutCell (L"%s", Name);
    BdtOutCell (L"%s", BdtGuidToString (&Guid, GuidText, sizeof (GuidText)));
    BdtOutCell (L"0x%04x", DataSize);
    BdtOutCell (L"%s", AttrText);
    BdtOutRowEnd ();
    Count++;
    Total += DataSize;
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Variable Count", Count);
  BdtOutFieldHex (L"Total Data Size", Total, 0);
  FreePool (Name);
  return (Status == EFI_NOT_FOUND) ? EFI_SUCCESS : Status;
}

/**
  Print an obvious interpretation of well known or text variables.
**/
STATIC
VOID
DecodeValue (
  IN CONST CHAR16    *Name,
  IN CONST EFI_GUID  *Guid,
  IN CONST UINT8     *Data,
  IN UINTN           Size
  )
{
  UINTN    Index;
  BOOLEAN  Ascii;
  BOOLEAN  Ucs2;
  CHAR16   *Text;

  if (Size == 0) {
    return;
  }

  if (CompareGuid (Guid, &gEfiGlobalVariableGuid) &&
      ((StrCmp (Name, L"BootOrder") == 0) || (StrCmp (Name, L"DriverOrder") == 0) ||
       (StrCmp (Name, L"SysPrepOrder") == 0)) && (Size % 2 == 0))
  {
    BdtOutListBegin (L"Order");
    for (Index = 0; Index < Size / 2; Index++) {
      BdtOutItemBegin (L"%04X", ((CONST UINT16 *)Data)[Index]);
      if (BdtOutGetFormat () != BdtFormatText) {
        BdtOutFieldHex (L"option", ((CONST UINT16 *)Data)[Index], 4);
      }

      BdtOutItemEnd ();
    }

    BdtOutListEnd ();
    return;
  }

  switch (Size) {
    case 1:
      BdtOutFieldNum (L"Value", Data[0]);
      return;
    case 2:
      BdtOutFieldNum (L"Value", ReadUnaligned16 ((CONST UINT16 *)Data));
      return;
    case 4:
      BdtOutFieldHex (L"Value", ReadUnaligned32 ((CONST UINT32 *)Data), 8);
      return;
    case 8:
      BdtOutFieldHex (L"Value", ReadUnaligned64 ((CONST UINT64 *)Data), 16);
      return;
    default:
      break;
  }

  //
  // Text detection: NULL terminated printable ASCII or UCS-2.
  //
  Ascii = (Data[Size - 1] == 0);
  for (Index = 0; Index + 1 < Size && Ascii; Index++) {
    Ascii = (Data[Index] >= 0x20) && (Data[Index] < 0x7F);
  }

  Ucs2 = !Ascii && (Size % 2 == 0) && (((CONST CHAR16 *)Data)[Size / 2 - 1] == 0);
  for (Index = 0; Index + 1 < Size / 2 && Ucs2; Index++) {
    Ucs2 = (ReadUnaligned16 ((CONST UINT16 *)Data + Index) >= 0x20) &&
           (ReadUnaligned16 ((CONST UINT16 *)Data + Index) < 0x7F);
  }

  if (Ascii) {
    BdtOutField (L"Value", L"%a", (CONST CHAR8 *)Data);
  } else if (Ucs2) {
    Text = AllocateCopyPool (Size, Data);
    if (Text != NULL) {
      BdtOutField (L"Value", L"%s", Text);
      FreePool (Text);
    }
  }
}

EFI_STATUS
EFIAPI
BdtVarGet (
  IN CONST CHAR16    *Name,
  IN CONST EFI_GUID  *Guid,
  IN BOOLEAN         Full
  )
{
  EFI_STATUS  Status;
  VOID        *Data;
  UINTN       Size;
  UINT32      Attributes;
  CHAR16      AttrText[32];

  Status = BdtReadVariable (Name, Guid, &Data, &Size, &Attributes);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"Variable %s (%g) not readable (%s)", Name, Guid, BdtStatusName (Status));
    return Status;
  }

  FormatVarAttributes (Attributes, AttrText, sizeof (AttrText));
  BdtOutField (L"Name", L"%s", Name);
  BdtOutField (L"GUID", L"%g", Guid);
  if (BdtGuidName (Guid) != NULL) {
    BdtOutField (L"GUID Name", L"%s", BdtGuidName (Guid));
  }

  BdtOutFieldHex (L"Attributes", Attributes, 8);
  BdtOutField (L"Attribute Flags", L"%s", AttrText);
  BdtOutFieldHex (L"Size", Size, 0);
  DecodeValue (Name, Guid, Data, Size);
  if (!Full && (Size > BDT_VAR_PREVIEW_SIZE)) {
    BdtOutNote (L"Showing first %d bytes; use \"var dump\" for all data.", BDT_VAR_PREVIEW_SIZE);
  }

  BdtOutHexDump (L"Data", 0, Data, Full ? Size : MIN (Size, BDT_VAR_PREVIEW_SIZE));
  FreePool (Data);
  return EFI_SUCCESS;
}

STATIC
VOID
OutUsage (
  IN CONST CHAR16  *Section,
  IN UINT32        Attributes
  )
{
  EFI_STATUS  Status;
  UINT64      MaxStorage;
  UINT64      Remaining;
  UINT64      MaxVariable;

  BdtOutSectionBegin (Section);
  Status = gRT->QueryVariableInfo (Attributes, &MaxStorage, &Remaining, &MaxVariable);
  if (EFI_ERROR (Status)) {
    BdtOutField (L"Status", L"%s", BdtStatusName (Status));
  } else {
    BdtOutFieldHex (L"Total", MaxStorage, 0);
    BdtOutFieldHex (L"Used", MaxStorage - Remaining, 0);
    BdtOutFieldHex (L"Free", Remaining, 0);
    BdtOutFieldHex (L"Max Variable Size", MaxVariable, 0);
    BdtOutFieldNum (L"Usage Percent", (MaxStorage == 0) ? 0 : DivU64x64Remainder (MultU64x32 (MaxStorage - Remaining, 100), MaxStorage, NULL));
  }

  BdtOutSectionEnd ();
}

EFI_STATUS
EFIAPI
BdtVarUsage (
  VOID
  )
{
  EFI_STATUS  Status;
  CHAR16      *Name;
  UINTN       NameSize;
  EFI_GUID    Guid;
  UINTN       DataSize;
  UINT32      Attributes;
  UINTN       NvCount;
  UINTN       VolatileCount;
  UINT64      NvData;
  UINT64      VolatileData;

  if (gST->Hdr.Revision < EFI_2_00_SYSTEM_TABLE_REVISION) {
    BdtOutError (L"QueryVariableInfo needs UEFI 2.0 or later");
    return EFI_UNSUPPORTED;
  }

  OutUsage (L"Non-Volatile", EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS);
  OutUsage (L"Volatile", EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS);
  OutUsage (L"Hardware Error Record", EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_HARDWARE_ERROR_RECORD);

  Status = StartEnumeration (&Name, &NameSize, &Guid);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  NvCount       = 0;
  VolatileCount = 0;
  NvData        = 0;
  VolatileData  = 0;
  while (!EFI_ERROR (Status = NextVariable (&Name, &NameSize, &Guid))) {
    DataSize   = 0;
    Attributes = 0;
    gRT->GetVariable (Name, &Guid, &Attributes, &DataSize, NULL);
    if ((Attributes & EFI_VARIABLE_NON_VOLATILE) != 0) {
      NvCount++;
      NvData += DataSize + StrSize (Name);
    } else {
      VolatileCount++;
      VolatileData += DataSize + StrSize (Name);
    }
  }

  FreePool (Name);

  BdtOutSectionBegin (L"Visible Variables");
  BdtOutFieldNum (L"Non-Volatile Count", NvCount);
  BdtOutFieldHex (L"Non-Volatile Name+Data", NvData, 0);
  BdtOutFieldNum (L"Volatile Count", VolatileCount);
  BdtOutFieldHex (L"Volatile Name+Data", VolatileData, 0);
  BdtOutSectionEnd ();
  BdtOutNote (L"Deleted / reclaimable space is not reported by UEFI; it is included in Used.");

  return (Status == EFI_NOT_FOUND) ? EFI_SUCCESS : Status;
}

EFI_STATUS
EFIAPI
BdtVarSaveAll (
  IN CONST CHAR16  *Directory
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"file",   L"File",   0 },
  };
  EFI_STATUS  Status;
  CHAR16      *Name;
  UINTN       NameSize;
  EFI_GUID    Guid;
  VOID        *Data;
  UINTN       DataSize;
  CHAR16      Path[512];
  CHAR16      *Walk;
  UINTN       Prefix;
  UINTN       Saved;

  Status = BdtFileCreateDirectory (Directory);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = StartEnumeration (&Name, &NameSize, &Guid);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Saved = 0;
  BdtOutTableBegin (L"saved", Columns, ARRAY_SIZE (Columns));
  while (!EFI_ERROR (Status = NextVariable (&Name, &NameSize, &Guid))) {
    if (EFI_ERROR (BdtReadVariable (Name, &Guid, &Data, &DataSize, NULL))) {
      continue;
    }

    UnicodeSPrint (Path, sizeof (Path), L"%s\\", Directory);
    Prefix = StrLen (Path);
    UnicodeSPrint (&Path[Prefix], sizeof (Path) - Prefix * sizeof (CHAR16), L"%s-%g.bin", Name, &Guid);
    for (Walk = &Path[Prefix]; *Walk != L'\0'; Walk++) {
      if ((*Walk < 0x20) || (*Walk == L'\\') || (*Walk == L'/') || (*Walk == L':') || (*Walk == L'*') ||
          (*Walk == L'?') || (*Walk == L'"') || (*Walk == L'<') || (*Walk == L'>') || (*Walk == L'|'))
      {
        *Walk = L'_';
      }
    }

    if (!EFI_ERROR (BdtFileWriteAll (Path, Data, DataSize))) {
      BdtOutRowBegin ();
      BdtOutCell (L"%s", &Path[Prefix]);
      BdtOutRowEnd ();
      Saved++;
    }

    FreePool (Data);
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Saved", Saved);
  FreePool (Name);
  return (Status == EFI_NOT_FOUND) ? EFI_SUCCESS : Status;
}

EFI_STATUS
EFIAPI
BdtVarDiffFiles (
  IN CONST CHAR16  *File1,
  IN CONST CHAR16  *File2
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"offset", L"Offset", 12 },
    { L"file1",  L"File1",  8  },
    { L"file2",  L"File2",  0  },
  };
  EFI_STATUS  Status;
  UINT8       *Data1;
  UINT8       *Data2;
  UINTN       Size1;
  UINTN       Size2;
  UINTN       Offset;
  UINTN       Differences;

  Status = BdtFileReadAll (File1, (VOID **)&Data1, &Size1);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"Cannot read %s (%s)", File1, BdtStatusName (Status));
    return Status;
  }

  Status = BdtFileReadAll (File2, (VOID **)&Data2, &Size2);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"Cannot read %s (%s)", File2, BdtStatusName (Status));
    FreePool (Data1);
    return Status;
  }

  BdtOutField (L"File1", L"%s", File1);
  BdtOutFieldHex (L"File1 Size", Size1, 0);
  BdtOutField (L"File2", L"%s", File2);
  BdtOutFieldHex (L"File2 Size", Size2, 0);

  Differences = 0;
  BdtOutTableBegin (L"differences", Columns, ARRAY_SIZE (Columns));
  for (Offset = 0; Offset < MIN (Size1, Size2); Offset++) {
    if (Data1[Offset] != Data2[Offset]) {
      if (Differences < BDT_VAR_PREVIEW_SIZE) {
        BdtOutRowBegin ();
        BdtOutCell (L"0x%04x", Offset);
        BdtOutCell (L"%02x", Data1[Offset]);
        BdtOutCell (L"%02x", Data2[Offset]);
        BdtOutRowEnd ();
      }

      Differences++;
    }
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Different Bytes", Differences);
  BdtOutFieldBool (L"Identical", (Differences == 0) && (Size1 == Size2));
  if (Differences > BDT_VAR_PREVIEW_SIZE) {
    BdtOutNote (L"Only the first %d differences are listed.", BDT_VAR_PREVIEW_SIZE);
  }

  FreePool (Data1);
  FreePool (Data2);
  return EFI_SUCCESS;
}
