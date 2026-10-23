/** @file
  BDT.efi - UI pages: PCI, Memory, I/O, MSR, ACPI, SMBIOS, Variable.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include "BdtUi.h"
#include <Guid/GlobalVariable.h>

#define UI_MAX_VARIABLES   1024
#define UI_MAX_USER_MSRS   16

//
// ---------------------------------------------------------------------------
// PCI
// ---------------------------------------------------------------------------
//

STATIC BDT_PCI_INFO  *mPciList  = NULL;
STATIC UINTN         mPciCount  = 0;

STATIC
VOID
PciFormatBdf (
  IN  BDT_PCI_INFO  *Info,
  OUT CHAR16        *Buffer,
  IN  UINTN         BufferSize
  )
{
  if (Info->Address.Segment != 0) {
    UnicodeSPrint (Buffer, BufferSize, L"%04x:%02x:%02x.%x", Info->Address.Segment, Info->Address.Bus, Info->Address.Device, Info->Address.Function);
  } else {
    UnicodeSPrint (Buffer, BufferSize, L"%02x:%02x.%x", Info->Address.Bus, Info->Address.Device, Info->Address.Function);
  }
}

STATIC
VOID
PciUnload (
  VOID
  )
{
  if (mPciList != NULL) {
    FreePool (mPciList);
  }

  mPciList  = NULL;
  mPciCount = 0;
}

STATIC
UINTN
PciLoad (
  VOID
  )
{
  PciUnload ();
  if (EFI_ERROR (BdtPciGetList (&mPciList, &mPciCount))) {
    mPciList  = NULL;
    mPciCount = 0;
  }

  return mPciCount;
}

STATIC
VOID
PciRow (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  BDT_PCI_INFO  *Info;
  CHAR16        Bdf[20];

  Info = &mPciList[Index];
  PciFormatBdf (Info, Bdf, sizeof (Bdf));
  UnicodeSPrint (
    Buffer,
    BufferSize,
    L"%-14s %04x  %04x  %02x%02x%02x  %02x   %02x   %s",
    Bdf,
    Info->VendorId,
    Info->DeviceId,
    Info->BaseClass,
    Info->SubClass,
    Info->ProgIf,
    Info->Revision,
    Info->HeaderType,
    BdtPciClassName (Info->BaseClass, Info->SubClass)
    );
}

STATIC
VOID
PciDescribe (
  IN  BDT_UI_HEX  *Hex,
  IN  UINT64      Offset,
  OUT CHAR16      *Buffer,
  IN  UINTN       BufferSize
  )
{
  BdtPciDescribeRegister (Hex->Pci, (UINT32)Offset, Buffer, BufferSize);
}

STATIC
BOOLEAN
PciOpen (
  IN  UINTN       Index,
  OUT BDT_UI_HEX  *Hex
  )
{
  BDT_PCI_INFO  *Info;
  CHAR16        Bdf[20];

  if (Index >= mPciCount) {
    return FALSE;
  }

  Info = &mPciList[Index];
  PciFormatBdf (Info, Bdf, sizeof (Bdf));
  Hex->Kind     = BdtUiSourcePci;
  Hex->Pci      = Info;
  Hex->Last     = BdtPciConfigSize (Info) - 1;
  Hex->MaxWidth = 4;
  Hex->Describe = PciDescribe;
  UnicodeSPrint (
    Hex->Title,
    sizeof (Hex->Title),
    L"PCI %s  %04x:%04x  %s",
    Bdf,
    Info->VendorId,
    Info->DeviceId,
    BdtPciClassName (Info->BaseClass, Info->SubClass)
    );
  return TRUE;
}

STATIC
VOID
PciInfo (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  BDT_PCI_INFO  *Info;

  Info = &mPciList[Index];
  if ((Info->HeaderType & 0x7F) == 1) {
    UnicodeSPrint (
      Buffer,
      BufferSize,
      L"Device %d of %d  Bridge  Bus %02x-%02x  Config 0x%x bytes",
      Index + 1,
      mPciCount,
      Info->SecondaryBus,
      Info->SubordinateBus,
      BdtPciConfigSize (Info)
      );
  } else {
    UnicodeSPrint (Buffer, BufferSize, L"Device %d of %d  Config 0x%x bytes", Index + 1, mPciCount, BdtPciConfigSize (Info));
  }
}

STATIC BDT_UI_PAGE  mPciPage = {
  L"PCI",
  L" BDF            VID   DID   Class   Rev  Hdr  Description",
  FALSE,
  PciLoad,
  PciRow,
  PciOpen,
  PciInfo,
  NULL,
  NULL,
  PciUnload
};

//
// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
//

STATIC EFI_MEMORY_DESCRIPTOR  *mMemMap      = NULL;
STATIC UINTN                  mMemMapSize   = 0;
STATIC UINTN                  mMemDescSize  = 0;
STATIC UINTN                  mMemCount     = 0;

#define MEM_DESC(Index)  ((EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mMemMap + (Index) * mMemDescSize))

STATIC
VOID
MemUnload (
  VOID
  )
{
  if (mMemMap != NULL) {
    FreePool (mMemMap);
  }

  mMemMap   = NULL;
  mMemCount = 0;
}

STATIC
UINTN
MemLoad (
  VOID
  )
{
  MemUnload ();
  if (EFI_ERROR (BdtMemoryGetMap (&mMemMap, &mMemMapSize, &mMemDescSize)) || (mMemDescSize == 0)) {
    mMemMap = NULL;
    return 0;
  }

  mMemCount = mMemMapSize / mMemDescSize;
  return mMemCount;
}

STATIC
VOID
MemRow (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  EFI_MEMORY_DESCRIPTOR  *Desc;

  Desc = MEM_DESC (Index);
  UnicodeSPrint (
    Buffer,
    BufferSize,
    L"%-20s  %016lx  %016lx  %10lx  %016lx",
    BdtMemoryTypeName (Desc->Type),
    Desc->PhysicalStart,
    Desc->PhysicalStart + EFI_PAGES_TO_SIZE (Desc->NumberOfPages) - 1,
    Desc->NumberOfPages,
    Desc->Attribute
    );
}

STATIC
VOID
MemDescribe (
  IN  BDT_UI_HEX  *Hex,
  IN  UINT64      Offset,
  OUT CHAR16      *Buffer,
  IN  UINTN       BufferSize
  )
{
  UINTN                  Index;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINT64                 Address;

  Address = Hex->Base + Offset;
  for (Index = 0; Index < mMemCount; Index++) {
    Desc = MEM_DESC (Index);
    if ((Address >= Desc->PhysicalStart) && (Address - Desc->PhysicalStart < EFI_PAGES_TO_SIZE (Desc->NumberOfPages))) {
      UnicodeSPrint (Buffer, BufferSize, L"[%s]", BdtMemoryTypeName (Desc->Type));
      return;
    }
  }

  StrCpyS (Buffer, BufferSize / sizeof (CHAR16), L"[not in memory map]");
}

STATIC
VOID
MemSetup (
  IN  UINT64      Address,
  OUT BDT_UI_HEX  *Hex
  )
{
  Hex->Kind     = BdtUiSourceMemory;
  Hex->Last     = MAX_ADDRESS;
  Hex->MaxWidth = 8;
  Hex->Describe = MemDescribe;
  Hex->Cursor   = Address;
  StrCpyS (Hex->Title, ARRAY_SIZE (Hex->Title), L"Physical Memory");
}

STATIC
BOOLEAN
MemOpen (
  IN  UINTN       Index,
  OUT BDT_UI_HEX  *Hex
  )
{
  if (Index >= mMemCount) {
    return FALSE;
  }

  MemSetup (MEM_DESC (Index)->PhysicalStart, Hex);
  return TRUE;
}

STATIC
VOID
MemInfo (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  CHAR16  Size[24];

  BdtFormatSize (EFI_PAGES_TO_SIZE (MEM_DESC (Index)->NumberOfPages), Size, sizeof (Size));
  UnicodeSPrint (Buffer, BufferSize, L"Region %d of %d  %s", Index + 1, mMemCount, Size);
}

STATIC
BOOLEAN
MemGoto (
  IN  UINT64      Value,
  OUT BDT_UI_HEX  *Hex,
  OUT UINTN       *Select
  )
{
  if (Value > MAX_ADDRESS) {
    return FALSE;
  }

  MemSetup (Value, Hex);
  return TRUE;
}

STATIC BDT_UI_PAGE  mMemoryPage = {
  L"Memory",
  L" Type                  Start             End               Pages       Attributes",
  FALSE,
  MemLoad,
  MemRow,
  MemOpen,
  MemInfo,
  MemGoto,
  L"Memory address:",
  MemUnload
};

//
// ---------------------------------------------------------------------------
// I/O space
// ---------------------------------------------------------------------------
//

typedef struct {
  UINT16          First;
  UINT16          Last;
  CONST CHAR16    *Name;
} BDT_UI_IO_RANGE;

STATIC CONST BDT_UI_IO_RANGE  mIoRanges[] = {
  { 0x0000, 0x000F, L"DMA controller 1"          },
  { 0x0020, 0x0021, L"PIC master"                },
  { 0x002E, 0x002F, L"Super I/O config"          },
  { 0x0040, 0x0043, L"PIT 8254 timer"            },
  { 0x004E, 0x004F, L"Super I/O / EC config"     },
  { 0x0060, 0x0060, L"Keyboard controller data"  },
  { 0x0061, 0x0061, L"NMI status / speaker"      },
  { 0x0062, 0x0062, L"EC data"                   },
  { 0x0064, 0x0064, L"Keyboard controller cmd"   },
  { 0x0066, 0x0066, L"EC command / status"       },
  { 0x0070, 0x0071, L"RTC / CMOS index, data"    },
  { 0x0072, 0x0073, L"CMOS extended index, data" },
  { 0x0080, 0x0080, L"POST code"                 },
  { 0x0081, 0x008F, L"DMA page registers"        },
  { 0x0092, 0x0092, L"Fast A20 / reset"          },
  { 0x00A0, 0x00A1, L"PIC slave"                 },
  { 0x00B2, 0x00B3, L"APM / SMI command, status" },
  { 0x00C0, 0x00DF, L"DMA controller 2"          },
  { 0x00F0, 0x00FF, L"FPU"                       },
  { 0x02F8, 0x02FF, L"COM2"                      },
  { 0x03F8, 0x03FF, L"COM1"                      },
  { 0x0CF8, 0x0CFB, L"PCI config address"        },
  { 0x0CF9, 0x0CF9, L"Reset control"             },
  { 0x0CFC, 0x0CFF, L"PCI config data"           },
};

STATIC
VOID
IoDescribe (
  IN  BDT_UI_HEX  *Hex,
  IN  UINT64      Offset,
  OUT CHAR16      *Buffer,
  IN  UINTN       BufferSize
  )
{
  UINTN  Index;

  //
  // Search backwards so 0xCF9 matches "Reset control" before the 0xCF8 range.
  //
  for (Index = ARRAY_SIZE (mIoRanges); Index > 0; Index--) {
    if ((Offset >= mIoRanges[Index - 1].First) && (Offset <= mIoRanges[Index - 1].Last)) {
      UnicodeSPrint (Buffer, BufferSize, L"[%s]", mIoRanges[Index - 1].Name);
      return;
    }
  }

  Buffer[0] = L'\0';
}

STATIC
UINTN
IoLoad (
  VOID
  )
{
  return 1;
}

STATIC
VOID
IoRow (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  StrCpyS (Buffer, BufferSize / sizeof (CHAR16), L"I/O space");
}

STATIC
BOOLEAN
IoOpen (
  IN  UINTN       Index,
  OUT BDT_UI_HEX  *Hex
  )
{
  Hex->Kind     = BdtUiSourceIo;
  Hex->Last     = 0xFFFF;
  Hex->MaxWidth = 4;
  Hex->Describe = IoDescribe;
  Hex->Cursor   = 0x80;
  StrCpyS (Hex->Title, ARRAY_SIZE (Hex->Title), L"I/O Space (reads may have side effects; R refreshes)");
  return TRUE;
}

STATIC BDT_UI_PAGE  mIoPage = {
  L"I/O",
  L"",
  TRUE,
  IoLoad,
  IoRow,
  IoOpen,
  NULL,
  NULL,
  NULL,
  NULL
};

//
// ---------------------------------------------------------------------------
// MSR
// ---------------------------------------------------------------------------
//

typedef struct {
  UINT32          Index;
  CONST CHAR16    *Name;
} BDT_UI_MSR_NAME;

STATIC CONST BDT_UI_MSR_NAME  mMsrNames[] = {
  { 0x00000010, L"IA32_TIME_STAMP_COUNTER"    },
  { 0x0000001B, L"IA32_APIC_BASE"             },
  { 0x0000003A, L"IA32_FEATURE_CONTROL"       },
  { 0x0000008B, L"IA32_BIOS_SIGN_ID"          },
  { 0x000000CE, L"MSR_PLATFORM_INFO"          },
  { 0x000000E2, L"MSR_PKG_CST_CONFIG_CONTROL" },
  { 0x000000FE, L"IA32_MTRRCAP"               },
  { 0x00000179, L"IA32_MCG_CAP"               },
  { 0x0000017A, L"IA32_MCG_STATUS"            },
  { 0x00000198, L"IA32_PERF_STATUS"           },
  { 0x00000199, L"IA32_PERF_CTL"              },
  { 0x000001A0, L"IA32_MISC_ENABLE"           },
  { 0x000001A2, L"MSR_TEMPERATURE_TARGET"     },
  { 0x00000277, L"IA32_PAT"                   },
  { 0x000002FF, L"IA32_MTRR_DEF_TYPE"         },
  { 0x00000606, L"MSR_RAPL_POWER_UNIT"        },
  { 0xC0000080, L"IA32_EFER"                  },
  { 0xC0000081, L"IA32_STAR"                  },
  { 0xC0000100, L"IA32_FS_BASE"               },
  { 0xC0000101, L"IA32_GS_BASE"               },
};

typedef struct {
  UINT32          Index;
  CONST CHAR16    *Name;
  UINT64          Value;
  BOOLEAN         Valid;
} BDT_UI_MSR;

STATIC UINT32      mUserMsrs[UI_MAX_USER_MSRS];
STATIC UINTN       mUserMsrCount = 0;
STATIC BDT_UI_MSR  mMsrs[UI_MAX_USER_MSRS + ARRAY_SIZE (mMsrNames)];
STATIC UINTN       mMsrCount = 0;

STATIC
CONST CHAR16 *
MsrName (
  IN UINT32  Index
  )
{
  UINTN  Walk;

  for (Walk = 0; Walk < ARRAY_SIZE (mMsrNames); Walk++) {
    if (mMsrNames[Walk].Index == Index) {
      return mMsrNames[Walk].Name;
    }
  }

  return L"(user)";
}

/**
  User entered MSRs are listed first, then the built in list.
**/
STATIC
UINTN
MsrLoad (
  VOID
  )
{
  UINTN  Index;

  mMsrCount = 0;
  for (Index = 0; Index < mUserMsrCount; Index++) {
    mMsrs[mMsrCount].Index = mUserMsrs[Index];
    mMsrs[mMsrCount].Name  = MsrName (mUserMsrs[Index]);
    mMsrCount++;
  }

  for (Index = 0; Index < ARRAY_SIZE (mMsrNames); Index++) {
    mMsrs[mMsrCount].Index = mMsrNames[Index].Index;
    mMsrs[mMsrCount].Name  = mMsrNames[Index].Name;
    mMsrCount++;
  }

  for (Index = 0; Index < mMsrCount; Index++) {
    mMsrs[Index].Valid = !EFI_ERROR (BdtCpuReadMsr (mMsrs[Index].Index, &mMsrs[Index].Value));
  }

  return mMsrCount;
}

STATIC
VOID
MsrRow (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  if (mMsrs[Index].Valid) {
    UnicodeSPrint (Buffer, BufferSize, L"%08x  %-28s  %016lx", mMsrs[Index].Index, mMsrs[Index].Name, mMsrs[Index].Value);
  } else {
    UnicodeSPrint (Buffer, BufferSize, L"%08x  %-28s  (not readable)", mMsrs[Index].Index, mMsrs[Index].Name);
  }
}

STATIC
VOID
MsrInfo (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  UINTN   Bit;
  CHAR16  Number[8];

  if (!mMsrs[Index].Valid) {
    StrCpyS (Buffer, BufferSize / sizeof (CHAR16), L"MSR not readable on this CPU (#GP caught)");
    return;
  }

  StrCpyS (Buffer, BufferSize / sizeof (CHAR16), L"Set bits:");
  for (Bit = 0; Bit < 64; Bit++) {
    if ((RShiftU64 (mMsrs[Index].Value, Bit) & 1) != 0) {
      UnicodeSPrint (Number, sizeof (Number), L" %d", Bit);
      if (StrLen (Buffer) + StrLen (Number) + 4 >= BufferSize / sizeof (CHAR16)) {
        StrCatS (Buffer, BufferSize / sizeof (CHAR16), L" ...");
        break;
      }

      StrCatS (Buffer, BufferSize / sizeof (CHAR16), Number);
    }
  }
}

STATIC
BOOLEAN
MsrOpen (
  IN  UINTN       Index,
  OUT BDT_UI_HEX  *Hex
  )
{
  return FALSE;
}

STATIC
BOOLEAN
MsrGoto (
  IN  UINT64      Value,
  OUT BDT_UI_HEX  *Hex,
  OUT UINTN       *Select
  )
{
  UINTN  Index;

  if (Value > MAX_UINT32) {
    return FALSE;
  }

  for (Index = 0; Index < mUserMsrCount; Index++) {
    if (mUserMsrs[Index] == (UINT32)Value) {
      *Select = Index;
      return FALSE;
    }
  }

  //
  // Newest entry first; drop the oldest when the list is full.
  //
  if (mUserMsrCount < UI_MAX_USER_MSRS) {
    mUserMsrCount++;
  }

  CopyMem (&mUserMsrs[1], &mUserMsrs[0], (mUserMsrCount - 1) * sizeof (UINT32));
  mUserMsrs[0] = (UINT32)Value;
  *Select      = 0;
  return FALSE;
}

STATIC
EFI_STATUS
MsrEdit (
  IN  UINTN   Index,
  IN  UINT64  Value,
  OUT CHAR16  *Message,
  IN  UINTN   MessageSize
  )
{
  EFI_STATUS  Status;
  UINT64      ReadBack;

  Status = BdtCpuWriteMsr (mMsrs[Index].Index, Value);
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (Message, MessageSize, L" WRMSR 0x%x = 0x%lx failed (#GP caught)", mMsrs[Index].Index, Value);
  } else if (!EFI_ERROR (BdtCpuReadMsr (mMsrs[Index].Index, &ReadBack))) {
    UnicodeSPrint (Message, MessageSize, L" Wrote MSR 0x%x = 0x%lx, read back 0x%lx", mMsrs[Index].Index, Value, ReadBack);
  } else {
    UnicodeSPrint (Message, MessageSize, L" Wrote MSR 0x%x = 0x%lx", mMsrs[Index].Index, Value);
  }

  return Status;
}

STATIC BDT_UI_PAGE  mMsrPage = {
  L"MSR",
  L" Index     Name                          Value",
  FALSE,
  MsrLoad,
  MsrRow,
  MsrOpen,
  MsrInfo,
  MsrGoto,
  L"MSR index:",
  NULL,
  MsrEdit,
  16
};

//
// ---------------------------------------------------------------------------
// ACPI
// ---------------------------------------------------------------------------
//

STATIC UINTN  mAcpiCount = 0;

STATIC
UINTN
AcpiLoad (
  VOID
  )
{
  EFI_ACPI_DESCRIPTION_HEADER  *Table;

  for (mAcpiCount = 0; !EFI_ERROR (BdtAcpiGetTable (mAcpiCount, &Table)); mAcpiCount++) {
  }

  return mAcpiCount;
}

STATIC
VOID
AsciiField (
  IN  CONST UINT8  *Source,
  IN  UINTN        Length,
  OUT CHAR16       *Text
  )
{
  UINTN  Index;

  for (Index = 0; Index < Length; Index++) {
    Text[Index] = ((Source[Index] >= 0x20) && (Source[Index] < 0x7F)) ? Source[Index] : L' ';
  }

  Text[Length] = L'\0';
}

STATIC
BOOLEAN
IsFacs (
  IN EFI_ACPI_DESCRIPTION_HEADER  *Table
  )
{
  return Table->Signature == SIGNATURE_32 ('F', 'A', 'C', 'S');
}

STATIC
VOID
AcpiRow (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  CHAR16                       Sig[5];
  CHAR16                       OemId[7];
  CHAR16                       OemTable[9];

  if (EFI_ERROR (BdtAcpiGetTable (Index, &Table))) {
    Buffer[0] = L'\0';
    return;
  }

  AsciiField ((UINT8 *)&Table->Signature, 4, Sig);
  if (IsFacs (Table)) {
    UnicodeSPrint (Buffer, BufferSize, L"%s  %016lx  %08x", Sig, (UINT64)(UINTN)Table, Table->Length);
    return;
  }

  AsciiField (Table->OemId, 6, OemId);
  AsciiField ((UINT8 *)&Table->OemTableId, 8, OemTable);
  UnicodeSPrint (
    Buffer,
    BufferSize,
    L"%s  %016lx  %08x  %3d  %s  %s  %08x",
    Sig,
    (UINT64)(UINTN)Table,
    Table->Length,
    Table->Revision,
    OemId,
    OemTable,
    Table->OemRevision
    );
}

STATIC
VOID
AcpiDescribe (
  IN  BDT_UI_HEX  *Hex,
  IN  UINT64      Offset,
  OUT CHAR16      *Buffer,
  IN  UINTN       BufferSize
  )
{
  STATIC CONST struct {
    UINT8           End;
    CONST CHAR16    *Name;
  } Fields[] = {
    { 4,  L"Signature"        },
    { 8,  L"Length"           },
    { 9,  L"Revision"         },
    { 10, L"Checksum"         },
    { 16, L"OEM ID"           },
    { 24, L"OEM Table ID"     },
    { 28, L"OEM Revision"     },
    { 32, L"Creator ID"       },
    { 36, L"Creator Revision" },
  };
  UINTN  Index;

  Buffer[0] = L'\0';
  if (IsFacs ((EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Hex->Base)) {
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (Fields); Index++) {
    if (Offset < Fields[Index].End) {
      UnicodeSPrint (Buffer, BufferSize, L"[Header: %s]", Fields[Index].Name);
      return;
    }
  }
}

STATIC
BOOLEAN
AcpiOpen (
  IN  UINTN       Index,
  OUT BDT_UI_HEX  *Hex
  )
{
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  CHAR16                       Sig[5];

  if (EFI_ERROR (BdtAcpiGetTable (Index, &Table)) || (Table->Length == 0) || (Table->Length > SIZE_16MB)) {
    return FALSE;
  }

  AsciiField ((UINT8 *)&Table->Signature, 4, Sig);
  Hex->Kind     = BdtUiSourceMemory;
  Hex->Base     = (UINTN)Table;
  Hex->Last     = Table->Length - 1;
  Hex->MaxWidth = 8;
  Hex->Describe = AcpiDescribe;
  UnicodeSPrint (Hex->Title, sizeof (Hex->Title), L"ACPI %s @ 0x%lx  Length 0x%x", Sig, (UINT64)(UINTN)Table, Table->Length);
  return TRUE;
}

STATIC
VOID
AcpiInfo (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  EFI_ACPI_DESCRIPTION_HEADER  *Table;

  if (EFI_ERROR (BdtAcpiGetTable (Index, &Table)) || IsFacs (Table) || (Table->Length > SIZE_16MB)) {
    UnicodeSPrint (Buffer, BufferSize, L"Table %d of %d", Index + 1, mAcpiCount);
    return;
  }

  UnicodeSPrint (
    Buffer,
    BufferSize,
    L"Table %d of %d  Checksum %s",
    Index + 1,
    mAcpiCount,
    (CalculateSum8 ((UINT8 *)Table, Table->Length) == 0) ? L"OK" : L"BAD"
    );
}

STATIC BDT_UI_PAGE  mAcpiPage = {
  L"ACPI",
  L" Sig   Address           Length    Rev  OEM ID  OEM Table  OEM Rev",
  FALSE,
  AcpiLoad,
  AcpiRow,
  AcpiOpen,
  AcpiInfo,
  NULL,
  NULL,
  NULL
};

//
// ---------------------------------------------------------------------------
// SMBIOS
// ---------------------------------------------------------------------------
//

STATIC UINTN  mSmbiosCount = 0;

STATIC
UINTN
SmbiosLoad (
  VOID
  )
{
  VOID   *Structure;
  UINTN  Size;

  for (mSmbiosCount = 0; !EFI_ERROR (BdtSmbiosGetStructure (mSmbiosCount, &Structure, &Size)); mSmbiosCount++) {
  }

  return mSmbiosCount;
}

STATIC
VOID
SmbiosRow (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  UINT8  *S;
  UINTN  Size;

  if (EFI_ERROR (BdtSmbiosGetStructure (Index, (VOID **)&S, &Size))) {
    Buffer[0] = L'\0';
    return;
  }

  UnicodeSPrint (
    Buffer,
    BufferSize,
    L"%04x    %3d   %02x      %04x   %s",
    ReadUnaligned16 ((UINT16 *)(S + 2)),
    S[0],
    S[1],
    Size,
    BdtSmbiosTypeName (S[0])
    );
}

STATIC
VOID
SmbiosDescribe (
  IN  BDT_UI_HEX  *Hex,
  IN  UINT64      Offset,
  OUT CHAR16      *Buffer,
  IN  UINTN       BufferSize
  )
{
  UINT8  Length;

  Length = *((UINT8 *)(UINTN)Hex->Base + 1);
  if (Offset == 0) {
    StrCpyS (Buffer, BufferSize / sizeof (CHAR16), L"[Type]");
  } else if (Offset == 1) {
    StrCpyS (Buffer, BufferSize / sizeof (CHAR16), L"[Length]");
  } else if (Offset < 4) {
    StrCpyS (Buffer, BufferSize / sizeof (CHAR16), L"[Handle]");
  } else if (Offset < Length) {
    UnicodeSPrint (Buffer, BufferSize, L"[Formatted area +0x%02x]", (UINTN)Offset);
  } else {
    StrCpyS (Buffer, BufferSize / sizeof (CHAR16), L"[String set]");
  }
}

STATIC
BOOLEAN
SmbiosOpen (
  IN  UINTN       Index,
  OUT BDT_UI_HEX  *Hex
  )
{
  UINT8  *S;
  UINTN  Size;

  if (EFI_ERROR (BdtSmbiosGetStructure (Index, (VOID **)&S, &Size)) || (Size == 0)) {
    return FALSE;
  }

  Hex->Kind     = BdtUiSourceMemory;
  Hex->Base     = (UINTN)S;
  Hex->Last     = Size - 1;
  Hex->MaxWidth = 8;
  Hex->Describe = SmbiosDescribe;
  UnicodeSPrint (
    Hex->Title,
    sizeof (Hex->Title),
    L"SMBIOS Type %d %s  Handle 0x%04x  @ 0x%lx",
    S[0],
    BdtSmbiosTypeName (S[0]),
    ReadUnaligned16 ((UINT16 *)(S + 2)),
    (UINT64)(UINTN)S
    );
  return TRUE;
}

STATIC BDT_UI_PAGE  mSmbiosPage = {
  L"SMBIOS",
  L" Handle  Type  Length  Total  Name",
  FALSE,
  SmbiosLoad,
  SmbiosRow,
  SmbiosOpen,
  NULL,
  NULL,
  NULL,
  NULL
};

//
// ---------------------------------------------------------------------------
// UEFI variables
// ---------------------------------------------------------------------------
//

typedef struct {
  CHAR16      *Name;
  EFI_GUID    Guid;
  UINTN       Size;
  UINT32      Attributes;
} BDT_UI_VARIABLE;

STATIC BDT_UI_VARIABLE  *mVars     = NULL;
STATIC UINTN            mVarCount  = 0;
STATIC UINT8            *mVarData  = NULL;

STATIC
VOID
VarUnload (
  VOID
  )
{
  UINTN  Index;

  if (mVars != NULL) {
    for (Index = 0; Index < mVarCount; Index++) {
      FreePool (mVars[Index].Name);
    }

    FreePool (mVars);
  }

  if (mVarData != NULL) {
    FreePool (mVarData);
  }

  mVars     = NULL;
  mVarCount = 0;
  mVarData  = NULL;
}

STATIC
UINTN
VarLoad (
  VOID
  )
{
  EFI_STATUS  Status;
  CHAR16      *Name;
  CHAR16      *NewName;
  UINTN       NameSize;
  UINTN       Size;
  EFI_GUID    Guid;

  VarUnload ();
  mVars    = AllocateZeroPool (UI_MAX_VARIABLES * sizeof (BDT_UI_VARIABLE));
  NameSize = 128 * sizeof (CHAR16);
  Name     = AllocateZeroPool (NameSize);
  if ((mVars == NULL) || (Name == NULL)) {
    if (Name != NULL) {
      FreePool (Name);
    }

    return 0;
  }

  ZeroMem (&Guid, sizeof (Guid));
  while (mVarCount < UI_MAX_VARIABLES) {
    Size   = NameSize;
    Status = gRT->GetNextVariableName (&Size, Name, &Guid);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      NewName = ReallocatePool (NameSize, Size, Name);
      if (NewName == NULL) {
        break;
      }

      Name     = NewName;
      NameSize = Size;
      Status   = gRT->GetNextVariableName (&Size, Name, &Guid);
    }

    if (EFI_ERROR (Status)) {
      break;
    }

    mVars[mVarCount].Name = AllocateCopyPool (StrSize (Name), Name);
    if (mVars[mVarCount].Name == NULL) {
      break;
    }

    CopyGuid (&mVars[mVarCount].Guid, &Guid);
    mVars[mVarCount].Size = 0;
    gRT->GetVariable (Name, &Guid, &mVars[mVarCount].Attributes, &mVars[mVarCount].Size, NULL);
    mVarCount++;
  }

  FreePool (Name);
  return mVarCount;
}

STATIC
VOID
VarAttributes (
  IN  UINT32  Attributes,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  UnicodeSPrint (
    Buffer,
    BufferSize,
    L"%s%s%s%s%s",
    ((Attributes & EFI_VARIABLE_NON_VOLATILE) != 0) ? L"NV " : L"",
    ((Attributes & EFI_VARIABLE_BOOTSERVICE_ACCESS) != 0) ? L"BS " : L"",
    ((Attributes & EFI_VARIABLE_RUNTIME_ACCESS) != 0) ? L"RT " : L"",
    ((Attributes & EFI_VARIABLE_HARDWARE_ERROR_RECORD) != 0) ? L"HR " : L"",
    ((Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0) ? L"AT " : L""
    );
}

STATIC
VOID
VarRow (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  CHAR16  Name[29];
  CHAR16  Guid[40];
  CHAR16  Attr[24];

  StrnCpyS (Name, ARRAY_SIZE (Name), mVars[Index].Name, ARRAY_SIZE (Name) - 1);
  VarAttributes (mVars[Index].Attributes, Attr, sizeof (Attr));
  UnicodeSPrint (
    Buffer,
    BufferSize,
    L"%-28s  %6x  %-12s  %s",
    Name,
    mVars[Index].Size,
    Attr,
    BdtGuidToString (&mVars[Index].Guid, Guid, sizeof (Guid))
    );
}

STATIC
BOOLEAN
VarOpen (
  IN  UINTN       Index,
  OUT BDT_UI_HEX  *Hex
  )
{
  UINTN  Size;

  if (Index >= mVarCount) {
    return FALSE;
  }

  if (mVarData != NULL) {
    FreePool (mVarData);
    mVarData = NULL;
  }

  if (EFI_ERROR (BdtReadVariable (mVars[Index].Name, &mVars[Index].Guid, (VOID **)&mVarData, &Size, NULL)) || (Size == 0)) {
    return FALSE;
  }

  //
  // The data is a private copy (writing it would change nothing); show
  // offsets from 0.
  //
  Hex->ReadOnly = TRUE;
  Hex->Kind     = BdtUiSourceMemory;
  Hex->Base     = (UINTN)mVarData;
  Hex->Last     = Size - 1;
  Hex->MaxWidth = 8;
  UnicodeSPrint (Hex->Title, sizeof (Hex->Title), L"Variable %s  %g  Size 0x%x", mVars[Index].Name, &mVars[Index].Guid, Size);
  return TRUE;
}

STATIC
VOID
VarInfo (
  IN  UINTN   Index,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  UnicodeSPrint (Buffer, BufferSize, L"Variable %d of %d  %s  %g", Index + 1, mVarCount, mVars[Index].Name, &mVars[Index].Guid);
}

STATIC BDT_UI_PAGE  mVariablePage = {
  L"Variable",
  L" Name                            Size  Attributes    GUID",
  FALSE,
  VarLoad,
  VarRow,
  VarOpen,
  VarInfo,
  NULL,
  NULL,
  VarUnload
};

//
// ---------------------------------------------------------------------------
// Page table
// ---------------------------------------------------------------------------
//

BDT_UI_PAGE  *gBdtUiPages[] = {
  &mPciPage,
  &mMemoryPage,
  &mIoPage,
  &mMsrPage,
  &mAcpiPage,
  &mSmbiosPage,
  &mVariablePage,
};

CONST UINTN  gBdtUiPageCount = ARRAY_SIZE (gBdtUiPages);
