/** @file
  BdtAcpiLib - ACPI table inspection.

  Tables are located through the EFI configuration table (RSDP) and walked
  via XSDT (or RSDT). DSDT and FACS are added from the FADT.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Guid/Acpi.h>
#include <Library/BdtFileLib.h>
#include <Library/BdtAcpiLib.h>

#define BDT_ACPI_MAX_TABLES     256
#define BDT_ACPI_MAX_LENGTH     SIZE_16MB

//
// Field offsets in the FADT (valid when the table is long enough).
//
#define FADT_FIRMWARE_CTRL      36
#define FADT_DSDT               40
#define FADT_MINOR_VERSION      131
#define FADT_X_FIRMWARE_CTRL    132
#define FADT_X_DSDT             140

STATIC EFI_ACPI_6_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *mRsdp = NULL;
STATIC EFI_ACPI_DESCRIPTION_HEADER                   *mTables[BDT_ACPI_MAX_TABLES];
STATIC UINTN                                         mTableCount = 0;
STATIC BOOLEAN                                       mScanned    = FALSE;

STATIC
VOID
AddTable (
  IN UINT64  Address
  )
{
  UINTN  Index;

  if ((Address == 0) || (Address > MAX_ADDRESS) || (mTableCount >= BDT_ACPI_MAX_TABLES)) {
    return;
  }

  for (Index = 0; Index < mTableCount; Index++) {
    if ((UINTN)mTables[Index] == (UINTN)Address) {
      return;
    }
  }

  mTables[mTableCount++] = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Address;
}

STATIC
EFI_STATUS
ScanTables (
  VOID
  )
{
  EFI_ACPI_DESCRIPTION_HEADER  *Root;
  EFI_ACPI_DESCRIPTION_HEADER  *Fadt;
  UINTN                        EntrySize;
  UINTN                        EntryCount;
  UINTN                        Index;
  UINT8                        *Entries;
  UINT64                       Address;
  UINTN                        RootCount;

  if (mScanned) {
    return (mRsdp == NULL) ? EFI_NOT_FOUND : EFI_SUCCESS;
  }

  mScanned = TRUE;
  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&mRsdp)) &&
      EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiAcpi10TableGuid, (VOID **)&mRsdp)))
  {
    mRsdp = NULL;
    return EFI_NOT_FOUND;
  }

  if ((mRsdp->Revision >= 2) && (mRsdp->XsdtAddress != 0)) {
    Root      = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)mRsdp->XsdtAddress;
    EntrySize = sizeof (UINT64);
  } else {
    Root      = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)mRsdp->RsdtAddress;
    EntrySize = sizeof (UINT32);
  }

  if ((Root == NULL) || (Root->Length < sizeof (EFI_ACPI_DESCRIPTION_HEADER)) || (Root->Length > BDT_ACPI_MAX_LENGTH)) {
    return EFI_NOT_FOUND;
  }

  AddTable ((UINTN)Root);
  EntryCount = (Root->Length - sizeof (EFI_ACPI_DESCRIPTION_HEADER)) / EntrySize;
  Entries    = (UINT8 *)(Root + 1);
  for (Index = 0; Index < EntryCount; Index++) {
    Address = (EntrySize == sizeof (UINT64)) ? ReadUnaligned64 ((UINT64 *)(Entries + Index * EntrySize)) :
              ReadUnaligned32 ((UINT32 *)(Entries + Index * EntrySize));
    AddTable (Address);
  }

  //
  // Add DSDT and FACS referenced by every FADT.
  //
  RootCount = mTableCount;
  for (Index = 0; Index < RootCount; Index++) {
    Fadt = mTables[Index];
    if (Fadt->Signature != EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {
      continue;
    }

    Address = 0;
    if (Fadt->Length >= FADT_X_DSDT + sizeof (UINT64)) {
      Address = ReadUnaligned64 ((UINT64 *)((UINT8 *)Fadt + FADT_X_DSDT));
    }

    if (Address == 0) {
      Address = ReadUnaligned32 ((UINT32 *)((UINT8 *)Fadt + FADT_DSDT));
    }

    AddTable (Address);

    Address = 0;
    if (Fadt->Length >= FADT_X_FIRMWARE_CTRL + sizeof (UINT64)) {
      Address = ReadUnaligned64 ((UINT64 *)((UINT8 *)Fadt + FADT_X_FIRMWARE_CTRL));
    }

    if (Address == 0) {
      Address = ReadUnaligned32 ((UINT32 *)((UINT8 *)Fadt + FADT_FIRMWARE_CTRL));
    }

    AddTable (Address);
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
IsFacs (
  IN EFI_ACPI_DESCRIPTION_HEADER  *Table
  )
{
  return Table->Signature == EFI_ACPI_6_0_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE;
}

STATIC
UINT8
Checksum (
  IN CONST VOID  *Buffer,
  IN UINTN       Length
  )
{
  return CalculateSum8 ((CONST UINT8 *)Buffer, Length);
}

STATIC
VOID
SignatureToText (
  IN  UINT32  Signature,
  OUT CHAR16  Text[5]
  )
{
  UINTN  Index;
  UINT8  Char;

  for (Index = 0; Index < 4; Index++) {
    Char        = (UINT8)(Signature >> (Index * 8));
    Text[Index] = ((Char >= 0x20) && (Char < 0x7F)) ? Char : L'?';
  }

  Text[4] = L'\0';
}

STATIC
VOID
CopyAsciiField (
  IN  CONST UINT8  *Source,
  IN  UINTN        Length,
  OUT CHAR16       *Text
  )
{
  UINTN  Index;

  for (Index = 0; Index < Length && Source[Index] != 0; Index++) {
    Text[Index] = ((Source[Index] >= 0x20) && (Source[Index] < 0x7F)) ? Source[Index] : L'?';
  }

  Text[Index] = L'\0';
}

/**
  Number of tables with the same signature as mTables[Index] and the 1-based
  instance number of mTables[Index] among them.
**/
STATIC
VOID
InstanceOf (
  IN  UINTN  Index,
  OUT UINTN  *Instance,
  OUT UINTN  *Total
  )
{
  UINTN  Walk;

  *Instance = 0;
  *Total    = 0;
  for (Walk = 0; Walk < mTableCount; Walk++) {
    if (mTables[Walk]->Signature == mTables[Index]->Signature) {
      (*Total)++;
      if (Walk <= Index) {
        (*Instance)++;
      }
    }
  }
}

EFI_STATUS
EFIAPI
BdtAcpiGetTable (
  IN  UINTN                        Index,
  OUT EFI_ACPI_DESCRIPTION_HEADER  **Table
  )
{
  EFI_STATUS  Status;

  Status = ScanTables ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Index >= mTableCount) {
    return EFI_NOT_FOUND;
  }

  *Table = mTables[Index];
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtAcpiFindTable (
  IN  UINT32                       Signature,
  IN  UINTN                        Instance,
  OUT EFI_ACPI_DESCRIPTION_HEADER  **Table
  )
{
  EFI_STATUS  Status;
  UINTN       Index;

  Status = ScanTables ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < mTableCount; Index++) {
    if (mTables[Index]->Signature == Signature) {
      if (Instance == 0) {
        *Table = mTables[Index];
        return EFI_SUCCESS;
      }

      Instance--;
    }
  }

  return EFI_NOT_FOUND;
}

EFI_STATUS
EFIAPI
BdtAcpiGetInfo (
  OUT CHAR16  *Version,
  IN  UINTN   VersionSize,
  OUT UINTN   *TableCount
  )
{
  EFI_STATUS                   Status;
  EFI_ACPI_DESCRIPTION_HEADER  *Fadt;

  Status = ScanTables ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *TableCount = mTableCount;
  if (!EFI_ERROR (BdtAcpiFindTable (EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE, 0, &Fadt))) {
    UnicodeSPrint (
      Version,
      VersionSize,
      L"%d.%d",
      Fadt->Revision,
      (Fadt->Length > FADT_MINOR_VERSION) ? (*((UINT8 *)Fadt + FADT_MINOR_VERSION) & 0xF) : 0
      );
  } else {
    UnicodeSPrint (Version, VersionSize, L"RSDP rev %d", mRsdp->Revision);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtAcpiList (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"signature", L"Sig",      6  },
    { L"address",   L"Address",  20 },
    { L"length",    L"Length",   10 },
    { L"revision",  L"Rev",      5  },
    { L"oem_id",    L"OEM ID",   8  },
    { L"oem_table", L"OEM Table", 10 },
    { L"oem_rev",   L"OEM Rev",  10 },
    { L"checksum",  L"Checksum", 0  },
  };
  EFI_STATUS                   Status;
  UINTN                        Index;
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  CHAR16                       Sig[5];
  CHAR16                       OemId[7];
  CHAR16                       OemTable[9];
  CHAR16                       RsdpOem[7];

  Status = ScanTables ();
  if (EFI_ERROR (Status)) {
    BdtOutError (L"ACPI tables not found");
    return Status;
  }

  CopyAsciiField (mRsdp->OemId, sizeof (mRsdp->OemId), RsdpOem);
  BdtOutSectionBegin (L"RSDP");
  BdtOutFieldHex (L"Address", (UINTN)mRsdp, 0);
  BdtOutFieldNum (L"Revision", mRsdp->Revision);
  BdtOutField (L"OEM ID", L"%s", RsdpOem);
  BdtOutFieldHex (L"RSDT Address", mRsdp->RsdtAddress, 8);
  if (mRsdp->Revision >= 2) {
    BdtOutFieldHex (L"XSDT Address", mRsdp->XsdtAddress, 16);
  }

  BdtOutSectionEnd ();

  BdtOutFieldNum (L"Table Count", mTableCount);
  BdtOutTableBegin (L"tables", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < mTableCount; Index++) {
    Table = mTables[Index];
    SignatureToText (Table->Signature, Sig);
    BdtOutRowBegin ();
    BdtOutCell (L"%s", Sig);
    BdtOutCell (L"0x%016lx", (UINT64)(UINTN)Table);
    BdtOutCell (L"0x%x", Table->Length);
    if (IsFacs (Table)) {
      BdtOutCell (L"-");
      BdtOutCell (L"-");
      BdtOutCell (L"-");
      BdtOutCell (L"-");
      BdtOutCell (L"N/A");
    } else {
      CopyAsciiField (Table->OemId, sizeof (Table->OemId), OemId);
      CopyAsciiField ((UINT8 *)&Table->OemTableId, sizeof (Table->OemTableId), OemTable);
      BdtOutCell (L"%d", Table->Revision);
      BdtOutCell (L"%s", OemId);
      BdtOutCell (L"%s", OemTable);
      BdtOutCell (L"0x%x", Table->OemRevision);
      BdtOutCell (L"%s", (Table->Length <= BDT_ACPI_MAX_LENGTH && Checksum (Table, Table->Length) == 0) ? L"OK" : L"BAD");
    }

    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtAcpiDump (
  IN CONST CHAR16  *Signature,
  IN UINTN         Instance
  )
{
  EFI_STATUS                   Status;
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  UINT32                       Sig;
  UINTN                        Index;
  CHAR16                       OemId[7];
  CHAR16                       OemTable[9];
  CHAR16                       Creator[5];

  Status = ScanTables ();
  if (EFI_ERROR (Status)) {
    BdtOutError (L"ACPI tables not found");
    return Status;
  }

  if (BdtStriCmp (Signature, L"RSDP") == 0) {
    BdtOutFieldHex (L"Address", (UINTN)mRsdp, 0);
    BdtOutHexDump (L"RSDP", (UINTN)mRsdp, mRsdp, (mRsdp->Revision >= 2) ? mRsdp->Length : 20);
    return EFI_SUCCESS;
  }

  if (StrLen (Signature) != 4) {
    BdtOutError (L"Table signature must be 4 characters");
    return EFI_INVALID_PARAMETER;
  }

  Sig = 0;
  for (Index = 0; Index < 4; Index++) {
    Sig |= (UINT32)(Signature[Index] & 0xFF) << (Index * 8);
  }

  Status = BdtAcpiFindTable (Sig, Instance, &Table);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"ACPI table %s (instance %d) not found", Signature, Instance);
    return Status;
  }

  if ((Table->Length < 8) || (Table->Length > BDT_ACPI_MAX_LENGTH)) {
    BdtOutError (L"Table length 0x%x is invalid", Table->Length);
    return EFI_VOLUME_CORRUPTED;
  }

  BdtOutField (L"Signature", L"%s", Signature);
  BdtOutFieldHex (L"Address", (UINTN)Table, 0);
  BdtOutFieldHex (L"Length", Table->Length, 0);
  if (!IsFacs (Table)) {
    CopyAsciiField (Table->OemId, sizeof (Table->OemId), OemId);
    CopyAsciiField ((UINT8 *)&Table->OemTableId, sizeof (Table->OemTableId), OemTable);
    SignatureToText (Table->CreatorId, Creator);
    BdtOutFieldNum (L"Revision", Table->Revision);
    BdtOutFieldHex (L"Checksum", Table->Checksum, 2);
    BdtOutFieldBool (L"Checksum Valid", Checksum (Table, Table->Length) == 0);
    BdtOutField (L"OEM ID", L"%s", OemId);
    BdtOutField (L"OEM Table ID", L"%s", OemTable);
    BdtOutFieldHex (L"OEM Revision", Table->OemRevision, 8);
    BdtOutField (L"Creator ID", L"%s", Creator);
    BdtOutFieldHex (L"Creator Revision", Table->CreatorRevision, 8);
  }

  BdtOutHexDump (L"Data", (UINTN)Table, Table, Table->Length);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtAcpiFind (
  IN CONST CHAR16  *String
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"signature", L"Table",    8  },
    { L"instance",  L"Instance", 10 },
    { L"offset",    L"Offset",   10 },
    { L"address",   L"Address",  0  },
  };
  EFI_STATUS                   Status;
  CHAR8                        Pattern[64];
  UINTN                        PatternLength;
  UINTN                        Index;
  UINTN                        Offset;
  UINTN                        Instance;
  UINTN                        Total;
  UINTN                        Hits;
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  CHAR16                       Sig[5];

  Status = ScanTables ();
  if (EFI_ERROR (Status)) {
    BdtOutError (L"ACPI tables not found");
    return Status;
  }

  PatternLength = StrLen (String);
  if ((PatternLength == 0) || (PatternLength >= sizeof (Pattern))) {
    BdtOutError (L"Search string must be 1 - %d characters", sizeof (Pattern) - 1);
    return EFI_INVALID_PARAMETER;
  }

  UnicodeStrToAsciiStrS (String, Pattern, sizeof (Pattern));
  BdtOutField (L"Pattern", L"%s", String);
  BdtOutTableBegin (L"matches", Columns, ARRAY_SIZE (Columns));
  Hits = 0;
  for (Index = 0; Index < mTableCount; Index++) {
    Table = mTables[Index];
    if ((Table->Length < PatternLength) || (Table->Length > BDT_ACPI_MAX_LENGTH)) {
      continue;
    }

    InstanceOf (Index, &Instance, &Total);
    SignatureToText (Table->Signature, Sig);
    for (Offset = 0; Offset + PatternLength <= Table->Length; Offset++) {
      if (CompareMem ((UINT8 *)Table + Offset, Pattern, PatternLength) == 0) {
        BdtOutRowBegin ();
        BdtOutCell (L"%s", Sig);
        BdtOutCell (L"%d", Instance - 1);
        BdtOutCell (L"0x%x", Offset);
        BdtOutCell (L"0x%lx", (UINT64)(UINTN)Table + Offset);
        BdtOutRowEnd ();
        Hits++;
      }
    }
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Matches", Hits);
  return EFI_SUCCESS;
}

STATIC
VOID
ValidateRow (
  IN CONST CHAR16  *Table,
  IN CONST CHAR16  *Check,
  IN BOOLEAN       Pass,
  IN CONST CHAR16  *Detail,
  IN OUT UINTN     *Failures
  )
{
  BdtOutRowBegin ();
  BdtOutCell (L"%s", Table);
  BdtOutCell (L"%s", Check);
  BdtOutCell (L"%s", Pass ? L"PASS" : L"FAIL");
  BdtOutCell (L"%s", Detail);
  BdtOutRowEnd ();
  if (!Pass) {
    (*Failures)++;
  }
}

EFI_STATUS
EFIAPI
BdtAcpiValidate (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"table",  L"Table",  8  },
    { L"check",  L"Check",  12 },
    { L"result", L"Result", 8  },
    { L"detail", L"Detail", 0  },
  };
  EFI_STATUS                   Status;
  UINTN                        Index;
  UINTN                        Instance;
  UINTN                        Total;
  UINTN                        Failures;
  UINTN                        Checks;
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  CHAR16                       Sig[5];
  CHAR16                       Detail[64];
  BOOLEAN                      Printable;
  UINTN                        Char;

  Status = ScanTables ();
  if (EFI_ERROR (Status)) {
    BdtOutError (L"ACPI tables not found");
    return Status;
  }

  Failures = 0;
  BdtOutTableBegin (L"validation", Columns, ARRAY_SIZE (Columns));

  ValidateRow (L"RSDP", L"Checksum", Checksum (mRsdp, 20) == 0, L"first 20 bytes", &Failures);
  Checks = 1;
  if (mRsdp->Revision >= 2) {
    ValidateRow (L"RSDP", L"ExtChecksum", Checksum (mRsdp, mRsdp->Length) == 0, L"full structure", &Failures);
    ValidateRow (L"RSDP", L"Pointer", mRsdp->XsdtAddress != 0, L"XSDT address present", &Failures);
    Checks += 2;
  }

  for (Index = 0; Index < mTableCount; Index++) {
    Table = mTables[Index];
    SignatureToText (Table->Signature, Sig);

    Printable = TRUE;
    for (Char = 0; Char < 4; Char++) {
      Printable &= (Sig[Char] != L'?');
    }

    ValidateRow (Sig, L"Signature", Printable, L"4 printable characters", &Failures);
    Checks++;
    if (!Printable) {
      continue;
    }

    if (IsFacs (Table)) {
      ValidateRow (Sig, L"Length", Table->Length >= 64, L"FACS >= 64 bytes", &Failures);
      Checks++;
      continue;
    }

    UnicodeSPrint (Detail, sizeof (Detail), L"0x%x bytes", Table->Length);
    ValidateRow (
      Sig,
      L"Length",
      (Table->Length >= sizeof (EFI_ACPI_DESCRIPTION_HEADER)) && (Table->Length <= BDT_ACPI_MAX_LENGTH),
      Detail,
      &Failures
      );
    Checks++;
    if ((Table->Length < sizeof (EFI_ACPI_DESCRIPTION_HEADER)) || (Table->Length > BDT_ACPI_MAX_LENGTH)) {
      continue;
    }

    UnicodeSPrint (Detail, sizeof (Detail), L"sum8 = 0x%02x", Checksum (Table, Table->Length));
    ValidateRow (Sig, L"Checksum", Checksum (Table, Table->Length) == 0, Detail, &Failures);
    UnicodeSPrint (Detail, sizeof (Detail), L"revision %d", Table->Revision);
    ValidateRow (Sig, L"Revision", Table->Revision != 0, Detail, &Failures);
    Checks += 2;

    InstanceOf (Index, &Instance, &Total);
    if ((Total > 1) && (Instance == 1) &&
        (Table->Signature != EFI_ACPI_6_0_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE))
    {
      UnicodeSPrint (Detail, sizeof (Detail), L"%d instances", Total);
      ValidateRow (Sig, L"Duplicate", FALSE, Detail, &Failures);
      Checks++;
    }
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Checks", Checks);
  BdtOutFieldNum (L"Failures", Failures);
  BdtOutField (L"Result", L"%s", (Failures == 0) ? L"PASS" : L"FAIL");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtAcpiSaveTables (
  IN CONST CHAR16  *Directory
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"file",   L"File",   20 },
    { L"length", L"Length", 0  },
  };
  EFI_STATUS                   Status;
  UINTN                        Index;
  UINTN                        Instance;
  UINTN                        Total;
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  CHAR16                       Sig[5];
  CHAR16                       FileName[32];
  CHAR16                       Path[512];
  BOOLEAN                      IsAml;

  Status = ScanTables ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = BdtFileCreateDirectory (Directory);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  BdtOutTableBegin (L"saved", Columns, ARRAY_SIZE (Columns));

  UnicodeSPrint (Path, sizeof (Path), L"%s\\RSDP.dat", Directory);
  if (!EFI_ERROR (BdtFileWriteAll (Path, mRsdp, (mRsdp->Revision >= 2) ? mRsdp->Length : 20))) {
    BdtOutRowBegin ();
    BdtOutCell (L"RSDP.dat");
    BdtOutCell (L"0x%x", (mRsdp->Revision >= 2) ? mRsdp->Length : 20);
    BdtOutRowEnd ();
  }

  for (Index = 0; Index < mTableCount; Index++) {
    Table = mTables[Index];
    if ((Table->Length < 8) || (Table->Length > BDT_ACPI_MAX_LENGTH)) {
      continue;
    }

    SignatureToText (Table->Signature, Sig);
    InstanceOf (Index, &Instance, &Total);
    IsAml = (Table->Signature == EFI_ACPI_6_0_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) ||
            (Table->Signature == EFI_ACPI_6_0_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE);
    if (Total > 1) {
      UnicodeSPrint (FileName, sizeof (FileName), L"%s%d.%s", Sig, Instance, IsAml ? L"aml" : L"dat");
    } else {
      UnicodeSPrint (FileName, sizeof (FileName), L"%s.%s", Sig, IsAml ? L"aml" : L"dat");
    }

    UnicodeSPrint (Path, sizeof (Path), L"%s\\%s", Directory, FileName);
    if (!EFI_ERROR (BdtFileWriteAll (Path, Table, Table->Length))) {
      BdtOutRowBegin ();
      BdtOutCell (L"%s", FileName);
      BdtOutCell (L"0x%x", Table->Length);
      BdtOutRowEnd ();
    }
  }

  BdtOutTableEnd ();
  return EFI_SUCCESS;
}

//
// ---------------------------------------------------------------------------
// acpi diff
// ---------------------------------------------------------------------------
//

#define BDT_DIFF_MAX_TABLES   256
#define BDT_DIFF_MAX_RANGES   256
#define BDT_DIFF_SHOW_BYTES   16
#define ACPI_HEADER_SIZE      sizeof (EFI_ACPI_DESCRIPTION_HEADER)

typedef struct {
  CONST UINT8    *Data;
  UINTN          Size;
  UINT32         Signature;
  BOOLEAN        HasHeader;     ///< Standard 36 byte header (not FACS).
  CHAR16         OemTableId[9];
  INTN           Match;         ///< Index in the other set, -1 when unmatched.
  BOOLEAN        Allocated;     ///< Data was read from a file.
} BDT_DIFF_TABLE;

typedef struct {
  BDT_DIFF_TABLE    Tables[BDT_DIFF_MAX_TABLES];
  UINTN             Count;
} BDT_DIFF_SET;

STATIC
VOID
DiffAddTable (
  IN OUT BDT_DIFF_SET  *Set,
  IN     CONST UINT8   *Data,
  IN     UINTN         Size,
  IN     BOOLEAN       Allocated
  )
{
  BDT_DIFF_TABLE  *Table;
  UINTN           Length;

  if (Set->Count >= BDT_DIFF_MAX_TABLES) {
    if (Allocated) {
      FreePool ((VOID *)Data);
    }

    return;
  }

  Table            = &Set->Tables[Set->Count++];
  Table->Data      = Data;
  Table->Size      = Size;
  Table->Signature = ReadUnaligned32 ((CONST UINT32 *)Data);
  Table->HasHeader = (Size >= ACPI_HEADER_SIZE) &&
                     (Table->Signature != EFI_ACPI_6_0_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE);
  Table->Match     = -1;
  Table->Allocated = Allocated;
  Table->OemTableId[0] = L'\0';
  if (Table->HasHeader) {
    CopyAsciiField ((CONST UINT8 *)&((EFI_ACPI_DESCRIPTION_HEADER *)Data)->OemTableId, 8, Table->OemTableId);
    Length = StrLen (Table->OemTableId);
    while ((Length > 0) && (Table->OemTableId[Length - 1] == L' ')) {
      Table->OemTableId[--Length] = L'\0';
    }
  }
}

STATIC
VOID
DiffFreeSet (
  IN BDT_DIFF_SET  *Set
  )
{
  UINTN  Index;

  for (Index = 0; Index < Set->Count; Index++) {
    if (Set->Tables[Index].Allocated) {
      FreePool ((VOID *)Set->Tables[Index].Data);
    }
  }

  FreePool (Set);
}

/**
  Table length from its header, limited to the available data.
**/
STATIC
UINTN
DiffTableLength (
  IN CONST UINT8  *Data,
  IN UINTN        Available
  )
{
  UINT32  Length;

  if (Available < 8) {
    return 0;
  }

  Length = ReadUnaligned32 ((CONST UINT32 *)(Data + 4));
  return (Length >= 8) ? MIN ((UINTN)Length, Available) : Available;
}

/**
  Load the live tables, or every table file of a directory written by
  "acpi save". Files are recognised by content, not by name; the RSDP and
  files that are not ACPI tables are skipped.
**/
STATIC
EFI_STATUS
DiffLoadSet (
  IN  CONST CHAR16  *Directory OPTIONAL,
  OUT BDT_DIFF_SET  **Result
  )
{
  EFI_STATUS    Status;
  BDT_DIFF_SET  *Set;
  CHAR16        **Names;
  UINTN         NameCount;
  UINTN         Index;
  UINT8         *Data;
  UINTN         Size;
  CHAR16        Path[512];

  Set = AllocateZeroPool (sizeof (BDT_DIFF_SET));
  if (Set == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (Directory == NULL) {
    Status = ScanTables ();
    if (EFI_ERROR (Status)) {
      FreePool (Set);
      return Status;
    }

    for (Index = 0; Index < mTableCount; Index++) {
      Size = DiffTableLength ((CONST UINT8 *)mTables[Index], BDT_ACPI_MAX_LENGTH);
      if (Size >= 8) {
        DiffAddTable (Set, (CONST UINT8 *)mTables[Index], Size, FALSE);
      }
    }

    *Result = Set;
    return EFI_SUCCESS;
  }

  Status = BdtFileListDirectory (Directory, &Names, &NameCount);
  if (EFI_ERROR (Status)) {
    FreePool (Set);
    return Status;
  }

  for (Index = 0; Index < NameCount; Index++) {
    UnicodeSPrint (Path, sizeof (Path), L"%s\\%s", Directory, Names[Index]);
    if (EFI_ERROR (BdtFileReadAll (Path, (VOID **)&Data, &Size))) {
      continue;
    }

    //
    // Skip the RSDP ("RSD PTR ") and anything that is not a table.
    //
    if ((Size < 8) || (CompareMem (Data, "RSD PTR ", 8) == 0) || (Size > BDT_ACPI_MAX_LENGTH)) {
      FreePool (Data);
      continue;
    }

    DiffAddTable (Set, Data, DiffTableLength (Data, Size), TRUE);
  }

  BdtFileFreeList (Names, NameCount);
  if (Set->Count == 0) {
    FreePool (Set);
    return EFI_NOT_FOUND;
  }

  *Result = Set;
  return EFI_SUCCESS;
}

/**
  Pair tables of the two sets. Pass 1 pairs equal signature and OEM Table ID
  (so reordered SSDTs still match); pass 2 pairs what is left of each
  signature in order.
**/
STATIC
VOID
DiffMatch (
  IN OUT BDT_DIFF_SET  *Old,
  IN OUT BDT_DIFF_SET  *New
  )
{
  UINTN  Pass;
  UINTN  OldIndex;
  UINTN  NewIndex;

  for (Pass = 0; Pass < 2; Pass++) {
    for (OldIndex = 0; OldIndex < Old->Count; OldIndex++) {
      if (Old->Tables[OldIndex].Match >= 0) {
        continue;
      }

      for (NewIndex = 0; NewIndex < New->Count; NewIndex++) {
        if ((New->Tables[NewIndex].Match >= 0) ||
            (New->Tables[NewIndex].Signature != Old->Tables[OldIndex].Signature))
        {
          continue;
        }

        if ((Pass == 0) && (StrCmp (New->Tables[NewIndex].OemTableId, Old->Tables[OldIndex].OemTableId) != 0)) {
          continue;
        }

        Old->Tables[OldIndex].Match = (INTN)NewIndex;
        New->Tables[NewIndex].Match = (INTN)OldIndex;
        break;
      }
    }
  }
}

STATIC
BOOLEAN
DiffSame (
  IN BDT_DIFF_TABLE  *A,
  IN BDT_DIFF_TABLE  *B
  )
{
  return (A->Size == B->Size) && (CompareMem (A->Data, B->Data, A->Size) == 0);
}

/**
  Names of the header fields that differ, e.g. "Length OEMRev".
**/
STATIC
VOID
DiffHeaderFields (
  IN  BDT_DIFF_TABLE  *Old,
  IN  BDT_DIFF_TABLE  *New,
  OUT CHAR16          *Buffer,
  IN  UINTN           BufferSize
  )
{
  STATIC CONST struct {
    UINT8           Offset;
    UINT8           Size;
    CONST CHAR16    *Name;
  } Fields[] = {
    { 4,  4, L"Length"     },
    { 8,  1, L"Revision"   },
    { 9,  1, L"Checksum"   },
    { 10, 6, L"OEMID"      },
    { 16, 8, L"OEMTableID" },
    { 24, 4, L"OEMRev"     },
    { 28, 4, L"CreatorID"  },
    { 32, 4, L"CreatorRev" },
  };
  UINTN  Index;

  Buffer[0] = L'\0';
  if (!Old->HasHeader || !New->HasHeader) {
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (Fields); Index++) {
    if (CompareMem (Old->Data + Fields[Index].Offset, New->Data + Fields[Index].Offset, Fields[Index].Size) != 0) {
      if (Buffer[0] != L'\0') {
        StrCatS (Buffer, BufferSize / sizeof (CHAR16), L" ");
      }

      StrCatS (Buffer, BufferSize / sizeof (CHAR16), Fields[Index].Name);
    }
  }
}

STATIC
VOID
DiffSummaryRow (
  IN BDT_DIFF_TABLE  *Old OPTIONAL,
  IN BDT_DIFF_TABLE  *New OPTIONAL
  )
{
  BDT_DIFF_TABLE  *Any;
  CHAR16          Sig[5];
  CHAR16          Fields[96];
  CONST CHAR16    *Status;

  Any = (Old != NULL) ? Old : New;
  SignatureToText (Any->Signature, Sig);
  Fields[0] = L'\0';
  if ((Old != NULL) && (New != NULL)) {
    Status = DiffSame (Old, New) ? L"SAME" : L"CHANGED";
    DiffHeaderFields (Old, New, Fields, sizeof (Fields));
  } else {
    Status = (Old != NULL) ? L"REMOVED" : L"ADDED";
  }

  BdtOutRowBegin ();
  BdtOutCell (L"%s", Sig);
  BdtOutCell (L"%s", Any->OemTableId);
  BdtOutCell (L"%s", Status);
  if (Old != NULL) {
    BdtOutCell (L"0x%x", Old->Size);
  } else {
    BdtOutCell (L"-");
  }

  if (New != NULL) {
    BdtOutCell (L"0x%x", New->Size);
  } else {
    BdtOutCell (L"-");
  }

  if ((Old != NULL) && Old->HasHeader) {
    BdtOutCell (L"%d / 0x%x", Old->Data[8], ReadUnaligned32 ((CONST UINT32 *)(Old->Data + 24)));
  } else {
    BdtOutCell (L"-");
  }

  if ((New != NULL) && New->HasHeader) {
    BdtOutCell (L"%d / 0x%x", New->Data[8], ReadUnaligned32 ((CONST UINT32 *)(New->Data + 24)));
  } else {
    BdtOutCell (L"-");
  }

  BdtOutCell (L"%s", Fields);
  BdtOutRowEnd ();
}

STATIC
VOID
DiffFormatBytes (
  IN  CONST UINT8  *Data,
  IN  UINTN        Size,
  IN  UINTN        Start,
  IN  UINTN        Count,
  OUT CHAR16       *Buffer,
  IN  UINTN        BufferSize
  )
{
  UINTN   Index;
  CHAR16  Hex[4];

  Buffer[0] = L'\0';
  for (Index = Start; Index < Start + Count && Index - Start < BDT_DIFF_SHOW_BYTES; Index++) {
    if (Index < Size) {
      UnicodeSPrint (Hex, sizeof (Hex), L"%02x", Data[Index]);
    } else {
      StrCpyS (Hex, ARRAY_SIZE (Hex), L"--");
    }

    if (Buffer[0] != L'\0') {
      StrCatS (Buffer, BufferSize / sizeof (CHAR16), L" ");
    }

    StrCatS (Buffer, BufferSize / sizeof (CHAR16), Hex);
  }

  if (Count > BDT_DIFF_SHOW_BYTES) {
    StrCatS (Buffer, BufferSize / sizeof (CHAR16), L" ...");
  }
}

/**
  Byte level differences of one table pair, grouped into ranges.
**/
STATIC
VOID
DiffBytes (
  IN BDT_DIFF_TABLE  *Old,
  IN BDT_DIFF_TABLE  *New
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"offset", L"Offset",      16 },
    { L"length", L"Bytes",       7  },
    { L"old",    L"Old",         50 },
    { L"new",    L"New",         0  },
  };
  UINTN   Max;
  UINTN   Offset;
  UINTN   Start;
  UINTN   Ranges;
  UINTN   Changed;
  CHAR16  OldText[BDT_DIFF_SHOW_BYTES * 3 + 8];
  CHAR16  NewText[BDT_DIFF_SHOW_BYTES * 3 + 8];
  CHAR16  Sig[5];

  SignatureToText (Old->Signature, Sig);
  BdtOutItemBegin (L"%s %s", Sig, Old->OemTableId);
  if (BdtOutGetFormat () != BdtFormatText) {
    BdtOutField (L"signature", L"%s", Sig);
    BdtOutField (L"oem_table_id", L"%s", Old->OemTableId);
  }

  BdtOutFieldHex (L"Old Length", Old->Size, 0);
  BdtOutFieldHex (L"New Length", New->Size, 0);

  Max     = MAX (Old->Size, New->Size);
  Ranges  = 0;
  Changed = 0;
  BdtOutTableBegin (L"ranges", Columns, ARRAY_SIZE (Columns));
  for (Offset = 0; Offset < Max; ) {
    if ((Offset < Old->Size) && (Offset < New->Size) && (Old->Data[Offset] == New->Data[Offset])) {
      Offset++;
      continue;
    }

    //
    // A range ends at the first equal byte present in both tables.
    //
    Start = Offset;
    while ((Offset < Max) &&
           !((Offset < Old->Size) && (Offset < New->Size) && (Old->Data[Offset] == New->Data[Offset])))
    {
      Offset++;
    }

    Changed += Offset - Start;
    if (Ranges < BDT_DIFF_MAX_RANGES) {
      DiffFormatBytes (Old->Data, Old->Size, Start, Offset - Start, OldText, sizeof (OldText));
      DiffFormatBytes (New->Data, New->Size, Start, Offset - Start, NewText, sizeof (NewText));
      BdtOutRowBegin ();
      BdtOutCell (L"0x%04x-0x%04x", Start, Offset - 1);
      BdtOutCell (L"%d", Offset - Start);
      BdtOutCell (L"%s", OldText);
      BdtOutCell (L"%s", NewText);
      BdtOutRowEnd ();
    }

    Ranges++;
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Changed Ranges", Ranges);
  BdtOutFieldNum (L"Changed Bytes", Changed);
  if (Ranges > BDT_DIFF_MAX_RANGES) {
    BdtOutNote (L"Only the first %d ranges are listed.", BDT_DIFF_MAX_RANGES);
  }

  BdtOutItemEnd ();
}

EFI_STATUS
EFIAPI
BdtAcpiDiff (
  IN CONST CHAR16  *OldDir,
  IN CONST CHAR16  *NewDir OPTIONAL,
  IN CONST CHAR16  *Signature OPTIONAL
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"signature",    L"Table",      7  },
    { L"oem_table_id", L"OEM Table",  11 },
    { L"status",       L"Status",     9  },
    { L"old_length",   L"Old Len",    9  },
    { L"new_length",   L"New Len",    9  },
    { L"old_revision", L"Old Rev/OEM", 13 },
    { L"new_revision", L"New Rev/OEM", 13 },
    { L"changed",      L"Changed Header Fields", 0 },
  };
  EFI_STATUS    Status;
  BDT_DIFF_SET  *Old;
  BDT_DIFF_SET  *New;
  UINTN         Index;
  UINTN         Same;
  UINTN         Changed;
  UINTN         Added;
  UINTN         Removed;
  UINTN         Shown;
  UINT32        Sig;

  Sig = 0;
  if (Signature != NULL) {
    if (StrLen (Signature) != 4) {
      BdtOutError (L"Table signature must be 4 characters");
      return EFI_INVALID_PARAMETER;
    }

    for (Index = 0; Index < 4; Index++) {
      Sig |= (UINT32)(Signature[Index] & 0xFF) << (Index * 8);
    }
  }

  Status = DiffLoadSet (OldDir, &Old);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"No ACPI tables in %s (%s)", OldDir, BdtStatusName (Status));
    return Status;
  }

  Status = DiffLoadSet (NewDir, &New);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"No ACPI tables in %s (%s)", (NewDir != NULL) ? NewDir : L"live system", BdtStatusName (Status));
    DiffFreeSet (Old);
    return Status;
  }

  DiffMatch (Old, New);

  BdtOutField (L"Old", L"%s", OldDir);
  BdtOutField (L"New", L"%s", (NewDir != NULL) ? NewDir : L"live tables");

  if (Signature != NULL) {
    //
    // Byte level view of every pair with this signature.
    //
    Shown = 0;
    BdtOutListBegin (L"Tables");
    for (Index = 0; Index < Old->Count; Index++) {
      if ((Old->Tables[Index].Signature == Sig) && (Old->Tables[Index].Match >= 0)) {
        DiffBytes (&Old->Tables[Index], &New->Tables[Old->Tables[Index].Match]);
        Shown++;
      }
    }

    BdtOutListEnd ();
    for (Index = 0; Index < Old->Count; Index++) {
      if ((Old->Tables[Index].Signature == Sig) && (Old->Tables[Index].Match < 0)) {
        BdtOutNote (L"%s %s only exists in the old set.", Signature, Old->Tables[Index].OemTableId);
      }
    }

    for (Index = 0; Index < New->Count; Index++) {
      if ((New->Tables[Index].Signature == Sig) && (New->Tables[Index].Match < 0)) {
        BdtOutNote (L"%s %s only exists in the new set.", Signature, New->Tables[Index].OemTableId);
      }
    }

    DiffFreeSet (Old);
    DiffFreeSet (New);
    if (Shown == 0) {
      BdtOutError (L"Table %s is not present in both sets", Signature);
      return EFI_NOT_FOUND;
    }

    return EFI_SUCCESS;
  }

  Same    = 0;
  Changed = 0;
  Added   = 0;
  Removed = 0;
  BdtOutTableBegin (L"tables", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < Old->Count; Index++) {
    if (Old->Tables[Index].Match < 0) {
      DiffSummaryRow (&Old->Tables[Index], NULL);
      Removed++;
    } else {
      DiffSummaryRow (&Old->Tables[Index], &New->Tables[Old->Tables[Index].Match]);
      if (DiffSame (&Old->Tables[Index], &New->Tables[Old->Tables[Index].Match])) {
        Same++;
      } else {
        Changed++;
      }
    }
  }

  for (Index = 0; Index < New->Count; Index++) {
    if (New->Tables[Index].Match < 0) {
      DiffSummaryRow (NULL, &New->Tables[Index]);
      Added++;
    }
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Same", Same);
  BdtOutFieldNum (L"Changed", Changed);
  BdtOutFieldNum (L"Added", Added);
  BdtOutFieldNum (L"Removed", Removed);
  BdtOutField (L"Result", L"%s", ((Changed + Added + Removed) == 0) ? L"IDENTICAL" : L"DIFFERENT");
  if ((Changed > 0) && (BdtOutGetFormat () == BdtFormatText)) {
    BdtOutNote (L"Use \"acpi diff <old> [new] <signature>\" for byte level differences.");
  }

  DiffFreeSet (Old);
  DiffFreeSet (New);
  return EFI_SUCCESS;
}
