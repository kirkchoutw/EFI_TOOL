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
