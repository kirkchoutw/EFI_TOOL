/** @file
  BDT.efi - command handlers. Each handler validates its arguments, then runs
  the library function inside an output document.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include "BdtInternal.h"

#define BDT_MAX_PATTERN  64

//
// Run Call inside an output document labelled Label and return its status.
//
#define BDT_RUN(Context, Label, Call)                       \
  do {                                                      \
    EFI_STATUS  RunStatus_;                                 \
    RunStatus_ = BdtBeginOutput ((Context), (Label));       \
    if (EFI_ERROR (RunStatus_)) {                           \
      return RunStatus_;                                    \
    }                                                       \
    return BdtEndOutput (Call);                             \
  } while (FALSE)

STATIC CHAR16  mLabel[256];

/**
  Build "group arg1 arg2 ..." as the command label of the output document.
**/
STATIC
CONST CHAR16 *
Label (
  IN CONST CHAR16  *Group,
  IN UINTN         Argc,
  IN CHAR16        **Argv
  )
{
  UINTN  Index;

  StrCpyS (mLabel, ARRAY_SIZE (mLabel), Group);
  for (Index = 0; Index < Argc; Index++) {
    if (StrLen (mLabel) + StrLen (Argv[Index]) + 2 >= ARRAY_SIZE (mLabel)) {
      break;
    }

    StrCatS (mLabel, ARRAY_SIZE (mLabel), L" ");
    StrCatS (mLabel, ARRAY_SIZE (mLabel), Argv[Index]);
  }

  return mLabel;
}

STATIC
BOOLEAN
IsSub (
  IN UINTN         Argc,
  IN CHAR16        **Argv,
  IN CONST CHAR16  *Name
  )
{
  return (Argc > 0) && (BdtStriCmp (Argv[0], Name) == 0);
}

STATIC
EFI_STATUS
ParseBdf (
  IN  CONST CHAR16     *String,
  OUT BDT_PCI_ADDRESS  *Address
  )
{
  if (EFI_ERROR (BdtParseBdf (String, &Address->Segment, &Address->Bus, &Address->Device, &Address->Function))) {
    BdtOutError (L"Invalid PCI address '%s' (expected [seg:]bus:dev.func in hex)", String);
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ParseHexArg (
  IN  CONST CHAR16  *String,
  OUT UINT64        *Value
  )
{
  if (EFI_ERROR (BdtParseHex (String, Value))) {
    BdtOutError (L"Invalid hex value '%s'", String);
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ParseNumberArg (
  IN  CONST CHAR16  *String,
  OUT UINT64        *Value
  )
{
  if (EFI_ERROR (BdtParseNumber (String, Value))) {
    BdtOutError (L"Invalid number '%s'", String);
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ParseGuidArg (
  IN  CONST CHAR16  *String,
  OUT EFI_GUID      *Guid
  )
{
  if (EFI_ERROR (BdtParseGuid (String, Guid))) {
    BdtOutError (L"Invalid GUID '%s' (expected xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx or a known name)", String);
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

//
// ---------------------------------------------------------------------------
// boot
// ---------------------------------------------------------------------------
//

EFI_STATUS
BdtCmdBoot (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  UINT64  Number;

  if (IsSub (Argc, Argv, L"order")) {
    BDT_RUN (Context, Label (L"boot", Argc, Argv), BdtBootOrder ());
  }

  if (IsSub (Argc, Argv, L"option") || IsSub (Argc, Argv, L"options")) {
    BDT_RUN (Context, Label (L"boot", Argc, Argv), BdtBootOptions ());
  }

  if (IsSub (Argc, Argv, L"dump") && (Argc == 2)) {
    if (EFI_ERROR (ParseHexArg (Argv[1], &Number)) || (Number > MAX_UINT16)) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"boot", Argc, Argv), BdtBootDump ((UINT16)Number));
  }

  if (IsSub (Argc, Argv, L"timing")) {
    BDT_RUN (Context, Label (L"boot", Argc, Argv), BdtBootTiming ());
  }

  if (IsSub (Argc, Argv, L"modules")) {
    BDT_RUN (Context, Label (L"boot", Argc, Argv), BdtBootModules ());
  }

  if (IsSub (Argc, Argv, L"trace")) {
    BdtOutError (L"boot trace needs the BdtDxe monitor driver (planned); use \"boot timing\" for FPDT data");
    return EFI_UNSUPPORTED;
  }

  return EFI_INVALID_PARAMETER;
}

//
// ---------------------------------------------------------------------------
// pci
// ---------------------------------------------------------------------------
//

EFI_STATUS
BdtCmdPci (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  BDT_PCI_ADDRESS  Address;
  BOOLEAN          Extended;

  if (IsSub (Argc, Argv, L"list")) {
    BDT_RUN (Context, Label (L"pci", Argc, Argv), BdtPciList ());
  }

  if (IsSub (Argc, Argv, L"tree")) {
    BDT_RUN (Context, Label (L"pci", Argc, Argv), BdtPciTree ());
  }

  if (IsSub (Argc, Argv, L"aer") && (Argc == 1)) {
    BDT_RUN (Context, Label (L"pci", Argc, Argv), BdtPciAer (NULL));
  }

  if ((Argc < 2) || EFI_ERROR (ParseBdf (Argv[1], &Address))) {
    return EFI_INVALID_PARAMETER;
  }

  if (IsSub (Argc, Argv, L"dump")) {
    Extended = (Argc >= 3) && (BdtStriCmp (Argv[2], L"--ext") == 0);
    BDT_RUN (Context, Label (L"pci", Argc, Argv), BdtPciDump (&Address, Extended));
  }

  if (IsSub (Argc, Argv, L"cap")) {
    BDT_RUN (Context, Label (L"pci", Argc, Argv), BdtPciCap (&Address));
  }

  if (IsSub (Argc, Argv, L"bar")) {
    BDT_RUN (Context, Label (L"pci", Argc, Argv), BdtPciBar (&Address));
  }

  if (IsSub (Argc, Argv, L"link")) {
    BDT_RUN (Context, Label (L"pci", Argc, Argv), BdtPciLink (&Address));
  }

  if (IsSub (Argc, Argv, L"aer")) {
    BDT_RUN (Context, Label (L"pci", Argc, Argv), BdtPciAer (&Address));
  }

  return EFI_INVALID_PARAMETER;
}

//
// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------
//

/**
  Convert a search argument to bytes:
    --string <text>  ASCII bytes of text
    0x<value>        value in little endian (1, 2, 4 or 8 bytes)
    <hex bytes>      bytes in the given order, e.g. DEADBEEF = DE AD BE EF
**/
STATIC
EFI_STATUS
ParsePattern (
  IN  UINTN   Argc,
  IN  CHAR16  **Argv,
  OUT UINT8   *Pattern,
  OUT UINTN   *PatternSize,
  OUT UINTN   *Consumed
  )
{
  CONST CHAR16  *Text;
  UINTN         Length;
  UINTN         Index;
  UINT64        Value;
  UINT64        Byte;
  CHAR16        Pair[3];

  if (Argc < 1) {
    return EFI_INVALID_PARAMETER;
  }

  if (BdtStriCmp (Argv[0], L"--string") == 0) {
    if ((Argc < 2) || (StrLen (Argv[1]) == 0) || (StrLen (Argv[1]) > BDT_MAX_PATTERN)) {
      return EFI_INVALID_PARAMETER;
    }

    for (Index = 0; Argv[1][Index] != L'\0'; Index++) {
      Pattern[Index] = (UINT8)Argv[1][Index];
    }

    *PatternSize = Index;
    *Consumed    = 2;
    return EFI_SUCCESS;
  }

  Text   = Argv[0];
  Length = StrLen (Text);
  if ((Length > 2) && (Text[0] == L'0') && ((Text[1] == L'x') || (Text[1] == L'X'))) {
    if (EFI_ERROR (BdtParseHex (Text, &Value))) {
      return EFI_INVALID_PARAMETER;
    }

    Length       = (Length - 2 + 1) / 2;
    *PatternSize = (Length <= 1) ? 1 : ((Length <= 2) ? 2 : ((Length <= 4) ? 4 : 8));
    CopyMem (Pattern, &Value, *PatternSize);
    *Consumed = 1;
    return EFI_SUCCESS;
  }

  if ((Length == 0) || (Length % 2 != 0) || (Length / 2 > BDT_MAX_PATTERN)) {
    return EFI_INVALID_PARAMETER;
  }

  Pair[2] = L'\0';
  for (Index = 0; Index < Length / 2; Index++) {
    Pair[0] = Text[Index * 2];
    Pair[1] = Text[Index * 2 + 1];
    if (EFI_ERROR (BdtParseHex (Pair, &Byte))) {
      return EFI_INVALID_PARAMETER;
    }

    Pattern[Index] = (UINT8)Byte;
  }

  *PatternSize = Length / 2;
  *Consumed    = 1;
  return EFI_SUCCESS;
}

EFI_STATUS
BdtCmdMemory (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  UINT64  Address;
  UINT64  Length;
  UINT8   Pattern[BDT_MAX_PATTERN];
  UINTN   PatternSize;
  UINTN   Consumed;

  if (IsSub (Argc, Argv, L"map")) {
    BDT_RUN (Context, Label (L"memory", Argc, Argv), BdtMemoryMap ());
  }

  if (IsSub (Argc, Argv, L"info")) {
    BDT_RUN (Context, Label (L"memory", Argc, Argv), BdtMemoryInfo ());
  }

  if (IsSub (Argc, Argv, L"dump") && (Argc == 3)) {
    if (EFI_ERROR (ParseHexArg (Argv[1], &Address)) || EFI_ERROR (ParseNumberArg (Argv[2], &Length))) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"memory", Argc, Argv), BdtMemoryDump (Address, (UINTN)Length));
  }

  if (IsSub (Argc, Argv, L"search") && (Argc >= 2)) {
    if (EFI_ERROR (ParsePattern (Argc - 1, &Argv[1], Pattern, &PatternSize, &Consumed))) {
      BdtOutError (L"Invalid search pattern");
      return EFI_INVALID_PARAMETER;
    }

    Address = 0;
    Length  = 0;
    if (Argc > 1 + Consumed) {
      if (EFI_ERROR (ParseHexArg (Argv[1 + Consumed], &Address))) {
        return EFI_INVALID_PARAMETER;
      }

      if ((Argc > 2 + Consumed) && EFI_ERROR (ParseNumberArg (Argv[2 + Consumed], &Length))) {
        return EFI_INVALID_PARAMETER;
      }
    }

    BDT_RUN (Context, Label (L"memory", Argc, Argv), BdtMemorySearch (Pattern, PatternSize, Address, Length));
  }

  return EFI_INVALID_PARAMETER;
}

//
// ---------------------------------------------------------------------------
// var
// ---------------------------------------------------------------------------
//

EFI_STATUS
BdtCmdVar (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  EFI_GUID  Guid;
  BOOLEAN   Full;

  if (IsSub (Argc, Argv, L"list")) {
    BDT_RUN (Context, Label (L"var", Argc, Argv), BdtVarList ());
  }

  if (IsSub (Argc, Argv, L"usage")) {
    BDT_RUN (Context, Label (L"var", Argc, Argv), BdtVarUsage ());
  }

  if ((IsSub (Argc, Argv, L"get") || IsSub (Argc, Argv, L"dump")) && (Argc >= 2) && (Argc <= 3)) {
    Full = IsSub (Argc, Argv, L"dump");
    if (Argc == 3) {
      if (EFI_ERROR (ParseGuidArg (Argv[2], &Guid))) {
        return EFI_INVALID_PARAMETER;
      }
    } else {
      BdtParseGuid (L"EFI_GLOBAL_VARIABLE", &Guid);
    }

    BDT_RUN (Context, Label (L"var", Argc, Argv), BdtVarGet (Argv[1], &Guid, Full));
  }

  if (IsSub (Argc, Argv, L"save") && (Argc == 2)) {
    BDT_RUN (Context, Label (L"var", Argc, Argv), BdtVarSaveAll (Argv[1]));
  }

  if (IsSub (Argc, Argv, L"diff") && (Argc == 3)) {
    BDT_RUN (Context, Label (L"var", Argc, Argv), BdtVarDiffFiles (Argv[1], Argv[2]));
  }

  return EFI_INVALID_PARAMETER;
}

//
// ---------------------------------------------------------------------------
// handle / protocol / devicepath
// ---------------------------------------------------------------------------
//

EFI_STATUS
BdtCmdHandle (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  UINT64    Index;
  EFI_GUID  Guid;

  if (IsSub (Argc, Argv, L"list")) {
    BDT_RUN (Context, Label (L"handle", Argc, Argv), BdtHandleList ());
  }

  if (IsSub (Argc, Argv, L"tree")) {
    BDT_RUN (Context, Label (L"handle", Argc, Argv), BdtHandleTree ());
  }

  if (IsSub (Argc, Argv, L"info") && (Argc == 2)) {
    if (EFI_ERROR (ParseNumberArg (Argv[1], &Index))) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"handle", Argc, Argv), BdtHandleInfo ((UINTN)Index));
  }

  if (IsSub (Argc, Argv, L"protocol") && (Argc == 2)) {
    if (EFI_ERROR (ParseGuidArg (Argv[1], &Guid))) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"handle", Argc, Argv), BdtHandleByProtocol (&Guid));
  }

  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
BdtCmdProtocol (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  EFI_GUID  Guid;

  if (IsSub (Argc, Argv, L"list")) {
    BDT_RUN (Context, Label (L"protocol", Argc, Argv), BdtProtocolList ());
  }

  if (IsSub (Argc, Argv, L"find") && (Argc == 2)) {
    if (EFI_ERROR (ParseGuidArg (Argv[1], &Guid))) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"protocol", Argc, Argv), BdtHandleByProtocol (&Guid));
  }

  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
BdtCmdDevicePath (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  UINT64  Index;

  if (IsSub (Argc, Argv, L"list")) {
    BDT_RUN (Context, Label (L"devicepath", Argc, Argv), BdtDevicePathList ());
  }

  if (IsSub (Argc, Argv, L"decode") && (Argc == 2)) {
    if (EFI_ERROR (ParseNumberArg (Argv[1], &Index))) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"devicepath", Argc, Argv), BdtDevicePathDecode ((UINTN)Index));
  }

  return EFI_INVALID_PARAMETER;
}

//
// ---------------------------------------------------------------------------
// acpi / smbios
// ---------------------------------------------------------------------------
//

EFI_STATUS
BdtCmdAcpi (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  UINT64  Instance;

  if (IsSub (Argc, Argv, L"list")) {
    BDT_RUN (Context, Label (L"acpi", Argc, Argv), BdtAcpiList ());
  }

  if (IsSub (Argc, Argv, L"validate")) {
    BDT_RUN (Context, Label (L"acpi", Argc, Argv), BdtAcpiValidate ());
  }

  if (IsSub (Argc, Argv, L"dump") && (Argc >= 2) && (Argc <= 3)) {
    Instance = 0;
    if ((Argc == 3) && EFI_ERROR (ParseNumberArg (Argv[2], &Instance))) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"acpi", Argc, Argv), BdtAcpiDump (Argv[1], (UINTN)Instance));
  }

  if (IsSub (Argc, Argv, L"find") && (Argc == 2)) {
    BDT_RUN (Context, Label (L"acpi", Argc, Argv), BdtAcpiFind (Argv[1]));
  }

  if (IsSub (Argc, Argv, L"save") && (Argc == 2)) {
    BDT_RUN (Context, Label (L"acpi", Argc, Argv), BdtAcpiSaveTables (Argv[1]));
  }

  if (IsSub (Argc, Argv, L"diff")) {
    BdtOutError (L"acpi diff is not implemented yet; save tables with \"acpi save\" and compare on the PC");
    return EFI_UNSUPPORTED;
  }

  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
BdtCmdSmbios (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  UINT64  Type;

  if (IsSub (Argc, Argv, L"list")) {
    BDT_RUN (Context, Label (L"smbios", Argc, Argv), BdtSmbiosList ());
  }

  if (IsSub (Argc, Argv, L"dump")) {
    BDT_RUN (Context, Label (L"smbios", Argc, Argv), BdtSmbiosDump (MAX_UINT16));
  }

  if (IsSub (Argc, Argv, L"validate")) {
    BDT_RUN (Context, Label (L"smbios", Argc, Argv), BdtSmbiosValidate ());
  }

  if (IsSub (Argc, Argv, L"type") && (Argc == 2)) {
    if (EFI_ERROR (ParseNumberArg (Argv[1], &Type)) || (Type > 255)) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"smbios", Argc, Argv), BdtSmbiosDump ((UINT16)Type));
  }

  return EFI_INVALID_PARAMETER;
}

//
// ---------------------------------------------------------------------------
// cpu / firmware / security
// ---------------------------------------------------------------------------
//

EFI_STATUS
BdtCmdCpu (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  UINT64  Leaf;
  UINT64  SubLeaf;
  UINT64  Msr;

  if (IsSub (Argc, Argv, L"info")) {
    BDT_RUN (Context, Label (L"cpu", Argc, Argv), BdtCpuInfo ());
  }

  if (IsSub (Argc, Argv, L"cpuid")) {
    if (Argc == 1) {
      BDT_RUN (Context, Label (L"cpu", Argc, Argv), BdtCpuCpuid (TRUE, 0, 0));
    }

    SubLeaf = 0;
    if ((Argc > 3) || EFI_ERROR (ParseHexArg (Argv[1], &Leaf)) || (Leaf > MAX_UINT32) ||
        ((Argc == 3) && (EFI_ERROR (ParseHexArg (Argv[2], &SubLeaf)) || (SubLeaf > MAX_UINT32))))
    {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"cpu", Argc, Argv), BdtCpuCpuid (FALSE, (UINT32)Leaf, (UINT32)SubLeaf));
  }

  if (IsSub (Argc, Argv, L"msr") && (Argc == 2)) {
    if (EFI_ERROR (ParseHexArg (Argv[1], &Msr)) || (Msr > MAX_UINT32)) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"cpu", Argc, Argv), BdtCpuMsr ((UINT32)Msr));
  }

  if (IsSub (Argc, Argv, L"mtrr")) {
    BDT_RUN (Context, Label (L"cpu", Argc, Argv), BdtCpuMtrr ());
  }

  if (IsSub (Argc, Argv, L"mca")) {
    BDT_RUN (Context, Label (L"cpu", Argc, Argv), BdtCpuMca ());
  }

  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
BdtCmdFirmware (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  UINT64    FvIndex;
  EFI_GUID  Guid;

  if (IsSub (Argc, Argv, L"fv")) {
    BDT_RUN (Context, Label (L"firmware", Argc, Argv), BdtFirmwareFvList ());
  }

  if (IsSub (Argc, Argv, L"ffs") && (Argc <= 2)) {
    FvIndex = 0;
    if ((Argc == 2) && EFI_ERROR (ParseNumberArg (Argv[1], &FvIndex))) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"firmware", Argc, Argv), BdtFirmwareFfsList ((UINTN)FvIndex));
  }

  if (IsSub (Argc, Argv, L"find") && (Argc == 2)) {
    if (EFI_ERROR (ParseGuidArg (Argv[1], &Guid))) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"firmware", Argc, Argv), BdtFirmwareFind (&Guid));
  }

  if (IsSub (Argc, Argv, L"verify")) {
    BdtOutError (L"firmware verify is not implemented yet (needs a reference hash manifest)");
    return EFI_UNSUPPORTED;
  }

  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
BdtCmdSecurity (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  STATIC CONST CHAR16  *Databases[] = { L"PK", L"KEK", L"db", L"dbx", L"dbt", L"dbr" };
  UINTN                Index;
  UINT64               Pcr;

  if (IsSub (Argc, Argv, L"secureboot")) {
    BDT_RUN (Context, Label (L"security", Argc, Argv), BdtSecureBoot ());
  }

  for (Index = 0; Index < ARRAY_SIZE (Databases); Index++) {
    if (IsSub (Argc, Argv, Databases[Index])) {
      BDT_RUN (Context, Label (L"security", Argc, Argv), BdtSecurityDb (Databases[Index]));
    }
  }

  if (IsSub (Argc, Argv, L"tpm")) {
    BDT_RUN (Context, Label (L"security", Argc, Argv), BdtTpmInfo ());
  }

  if (IsSub (Argc, Argv, L"pcr") && (Argc <= 2)) {
    Pcr = MAX_UINT32;
    if ((Argc == 2) && EFI_ERROR (ParseNumberArg (Argv[1], &Pcr))) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"security", Argc, Argv), BdtTpmPcr ((UINT32)MIN (Pcr, MAX_UINT32)));
  }

  if (IsSub (Argc, Argv, L"tcglog")) {
    BDT_RUN (Context, Label (L"security", Argc, Argv), BdtTcgLog ());
  }

  return EFI_INVALID_PARAMETER;
}

//
// ---------------------------------------------------------------------------
// io (read only)
// ---------------------------------------------------------------------------
//

EFI_STATUS
BdtCmdIo (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  UINT64           Address;
  UINT64           Length;
  UINT64           Width;
  BDT_PCI_ADDRESS  Pci;

  if ((Argc >= 2) && (BdtStriCmp (Argv[1], L"write") == 0)) {
    BdtOutError (L"Write operations are disabled: BDT is a read-only toolkit");
    return EFI_ACCESS_DENIED;
  }

  if ((Argc < 3) || (BdtStriCmp (Argv[1], L"read") != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (IsSub (Argc, Argv, L"mmio") && (Argc >= 4) && (Argc <= 5)) {
    Width = 4;
    if (EFI_ERROR (ParseHexArg (Argv[2], &Address)) || EFI_ERROR (ParseNumberArg (Argv[3], &Length)) ||
        ((Argc == 5) && EFI_ERROR (ParseNumberArg (Argv[4], &Width))))
    {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"io", Argc, Argv), BdtMmioRead (Address, (UINTN)Length, (UINTN)Width));
  }

  if (IsSub (Argc, Argv, L"pci") && (Argc >= 4) && (Argc <= 5)) {
    Width = 4;
    if (EFI_ERROR (ParseBdf (Argv[2], &Pci)) || EFI_ERROR (ParseHexArg (Argv[3], &Address)) || (Address > 0xFFF) ||
        ((Argc == 5) && EFI_ERROR (ParseNumberArg (Argv[4], &Width))))
    {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"io", Argc, Argv), BdtPciReadRegister (&Pci, (UINT32)Address, (UINTN)Width));
  }

  if (IsSub (Argc, Argv, L"msr") && (Argc == 3)) {
    if (EFI_ERROR (ParseHexArg (Argv[2], &Address)) || (Address > MAX_UINT32)) {
      return EFI_INVALID_PARAMETER;
    }

    BDT_RUN (Context, Label (L"io", Argc, Argv), BdtCpuMsr ((UINT32)Address));
  }

  return EFI_INVALID_PARAMETER;
}
