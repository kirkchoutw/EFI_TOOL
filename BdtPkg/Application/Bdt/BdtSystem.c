/** @file
  BDT.efi - "system" summary command.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include "BdtInternal.h"

EFI_STATUS
BdtSystemSummary (
  VOID
  )
{
  CHAR16        Vendor[64];
  CHAR16        Version[64];
  CHAR16        Date[32];
  CHAR16        Text[32];
  UINT32        Revision;
  BDT_CPU_INFO  Cpu;
  UINT64        Memory;
  UINTN         Count;
  UINT8         Major;
  UINT8         Minor;
  UINT8         DocRev;
  BOOLEAN       SecureBoot;
  BOOLEAN       SetupMode;

  BdtOutSectionBegin (L"BIOS");
  if (!EFI_ERROR (BdtSmbiosGetBiosInfo (Vendor, sizeof (Vendor), Version, sizeof (Version), Date, sizeof (Date)))) {
    BdtOutField (L"Vendor", L"%s", Vendor);
    BdtOutField (L"Version", L"%s", Version);
    BdtOutField (L"Build Date", L"%s", Date);
  } else {
    BdtOutField (L"Vendor", L"Unknown (no SMBIOS Type 0)");
  }

  //
  // UEFI revision: major.minor where minor is encoded as minor * 10 + patch.
  //
  Revision = gST->Hdr.Revision;
  if ((Revision & 0xFFFF) % 10 != 0) {
    BdtOutField (L"UEFI Version", L"%d.%d.%d", Revision >> 16, (Revision & 0xFFFF) / 10, (Revision & 0xFFFF) % 10);
  } else {
    BdtOutField (L"UEFI Version", L"%d.%d", Revision >> 16, (Revision & 0xFFFF) / 10);
  }

  BdtOutField (L"Firmware Vendor", L"%s", (gST->FirmwareVendor != NULL) ? gST->FirmwareVendor : L"");
  BdtOutFieldHex (L"Firmware Revision", gST->FirmwareRevision, 8);
  BdtOutSectionEnd ();

  BdtOutSectionBegin (L"CPU");
  BdtCpuGetInfo (&Cpu);
  BdtOutField (L"Vendor", L"%a", Cpu.Vendor);
  BdtOutField (L"Brand", L"%a", Cpu.Brand);
  BdtOutFieldHex (L"Family", Cpu.Family, 2);
  BdtOutFieldHex (L"Model", Cpu.Model, 2);
  BdtOutFieldHex (L"Stepping", Cpu.Stepping, 1);
  if (Cpu.Threads != 0) {
    BdtOutFieldNum (L"Packages", Cpu.Packages);
    BdtOutFieldNum (L"Cores", Cpu.Cores);
    BdtOutFieldNum (L"Threads", Cpu.Threads);
  } else {
    BdtOutField (L"Threads", L"Unknown");
  }

  BdtOutSectionEnd ();

  BdtOutSectionBegin (L"Memory");
  if (!EFI_ERROR (BdtMemoryGetTotal (&Memory))) {
    BdtFormatSize (Memory, Text, sizeof (Text));
    BdtOutField (L"Total", L"%s", Text);
    BdtOutFieldNum (L"Total Bytes", Memory);
  } else {
    BdtOutField (L"Total", L"Unknown");
  }

  BdtOutSectionEnd ();

  BdtOutSectionBegin (L"PCI");
  if (!EFI_ERROR (BdtPciGetDeviceCount (&Count))) {
    BdtOutFieldNum (L"Devices", Count);
  } else {
    BdtOutFieldNum (L"Devices", 0);
  }

  BdtOutSectionEnd ();

  BdtOutSectionBegin (L"ACPI");
  if (!EFI_ERROR (BdtAcpiGetInfo (Text, sizeof (Text), &Count))) {
    BdtOutField (L"Revision", L"%s", Text);
    BdtOutFieldNum (L"Tables", Count);
  } else {
    BdtOutField (L"Revision", L"Not Present");
  }

  BdtOutSectionEnd ();

  BdtOutSectionBegin (L"SMBIOS");
  if (!EFI_ERROR (BdtSmbiosGetVersion (&Major, &Minor, &DocRev))) {
    BdtOutField (L"Version", L"%d.%d", Major, Minor);
  } else {
    BdtOutField (L"Version", L"Not Present");
  }

  BdtOutSectionEnd ();

  BdtOutSectionBegin (L"Secure Boot");
  if (!EFI_ERROR (BdtSecureBootGetState (&SecureBoot, &SetupMode))) {
    BdtOutField (L"Status", L"%s", SecureBoot ? L"Enabled" : L"Disabled");
    BdtOutFieldBool (L"Setup Mode", SetupMode);
  } else {
    BdtOutField (L"Status", L"Not Supported");
  }

  BdtOutSectionEnd ();

  BdtOutSectionBegin (L"TPM");
  BdtOutField (L"Status", L"%s", BdtTpmIsPresent () ? L"Present" : L"Not Present");
  BdtOutSectionEnd ();

  return EFI_SUCCESS;
}

EFI_STATUS
BdtCmdSystem (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  EFI_STATUS  Status;

  if (Argc != 0) {
    return EFI_INVALID_PARAMETER;
  }

  Status = BdtBeginOutput (Context, L"system");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return BdtEndOutput (BdtSystemSummary ());
}
