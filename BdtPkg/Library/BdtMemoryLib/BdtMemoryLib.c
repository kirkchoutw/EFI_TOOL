/** @file
  BdtMemoryLib - memory map, memory dump / search and MMIO read.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Library/IoLib.h>
#include <Library/BdtMemoryLib.h>
#include <Protocol/Cpu.h>
#include <Protocol/DebugSupport.h>

#define BDT_MEMORY_DUMP_MAX     SIZE_1MB
#define BDT_MMIO_READ_MAX       SIZE_4KB
#define BDT_SEARCH_MAX_HITS     64

STATIC CONST CHAR16  *mMemoryTypeNames[] = {
  L"Reserved",
  L"LoaderCode",
  L"LoaderData",
  L"BootServicesCode",
  L"BootServicesData",
  L"RuntimeServicesCode",
  L"RuntimeServicesData",
  L"Conventional",
  L"Unusable",
  L"ACPIReclaim",
  L"ACPINVS",
  L"MMIO",
  L"MMIOPortSpace",
  L"PalCode",
  L"Persistent",
  L"Unaccepted",
};

STATIC
CONST CHAR16 *
MemoryTypeName (
  IN UINT32  Type
  )
{
  if (Type < ARRAY_SIZE (mMemoryTypeNames)) {
    return mMemoryTypeNames[Type];
  }

  if (Type >= 0x80000000) {
    return L"OSVendor";
  }

  if (Type >= 0x70000000) {
    return L"OEMReserved";
  }

  return L"Unknown";
}

/**
  TRUE for memory types backed by system RAM.
**/
STATIC
BOOLEAN
IsRamType (
  IN UINT32  Type
  )
{
  switch (Type) {
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
    case EfiConventionalMemory:
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
    case EfiPersistentMemory:
    case EfiUnacceptedMemoryType:
      return TRUE;
    default:
      return FALSE;
  }
}

/**
  Get a copy of the current memory map.
**/
STATIC
EFI_STATUS
GetMemoryMapCopy (
  OUT EFI_MEMORY_DESCRIPTOR  **Map,
  OUT UINTN                  *MapSize,
  OUT UINTN                  *DescriptorSize,
  OUT UINT32                 *DescriptorVersion OPTIONAL
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Buffer;
  UINTN                  Size;
  UINTN                  MapKey;
  UINT32                 Version;

  *Map   = NULL;
  Size   = 0;
  Buffer = NULL;
  Status = gBS->GetMemoryMap (&Size, Buffer, &MapKey, DescriptorSize, &Version);
  while (Status == EFI_BUFFER_TOO_SMALL) {
    if (Buffer != NULL) {
      FreePool (Buffer);
    }

    //
    // Allocating the buffer may split a descriptor; leave some headroom.
    //
    Size  += 8 * sizeof (EFI_MEMORY_DESCRIPTOR);
    Buffer = AllocatePool (Size);
    if (Buffer == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = gBS->GetMemoryMap (&Size, Buffer, &MapKey, DescriptorSize, &Version);
  }

  if (EFI_ERROR (Status)) {
    if (Buffer != NULL) {
      FreePool (Buffer);
    }

    return Status;
  }

  *Map     = Buffer;
  *MapSize = Size;
  if (DescriptorVersion != NULL) {
    *DescriptorVersion = Version;
  }

  return EFI_SUCCESS;
}

#define NEXT_DESC(Desc, Size)  ((EFI_MEMORY_DESCRIPTOR *)((UINT8 *)(Desc) + (Size)))

STATIC
VOID
FormatAttributes (
  IN  UINT64  Attribute,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  STATIC CONST struct {
    UINT64          Bit;
    CONST CHAR16    *Name;
  } Names[] = {
    { EFI_MEMORY_UC,            L"UC"  },
    { EFI_MEMORY_WC,            L"WC"  },
    { EFI_MEMORY_WT,            L"WT"  },
    { EFI_MEMORY_WB,            L"WB"  },
    { EFI_MEMORY_UCE,           L"UCE" },
    { EFI_MEMORY_WP,            L"WP"  },
    { EFI_MEMORY_RP,            L"RP"  },
    { EFI_MEMORY_XP,            L"XP"  },
    { EFI_MEMORY_NV,            L"NV"  },
    { EFI_MEMORY_MORE_RELIABLE, L"MR"  },
    { EFI_MEMORY_RO,            L"RO"  },
    { EFI_MEMORY_SP,            L"SP"  },
    { EFI_MEMORY_CPU_CRYPTO,    L"CC"  },
    { EFI_MEMORY_RUNTIME,       L"RT"  },
  };
  UINTN  Index;

  Buffer[0] = L'\0';
  for (Index = 0; Index < ARRAY_SIZE (Names); Index++) {
    if ((Attribute & Names[Index].Bit) != 0) {
      if (Buffer[0] != L'\0') {
        StrCatS (Buffer, BufferSize / sizeof (CHAR16), L"|");
      }

      StrCatS (Buffer, BufferSize / sizeof (CHAR16), Names[Index].Name);
    }
  }
}

EFI_STATUS
EFIAPI
BdtMemoryGetTotal (
  OUT UINT64  *TotalBytes
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINTN                  MapSize;
  UINTN                  DescSize;
  UINT64                 Total;

  Status = GetMemoryMapCopy (&Map, &MapSize, &DescSize, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Total = 0;
  for (Desc = Map; (UINT8 *)Desc < (UINT8 *)Map + MapSize; Desc = NEXT_DESC (Desc, DescSize)) {
    if (IsRamType (Desc->Type)) {
      Total += EFI_PAGES_TO_SIZE (Desc->NumberOfPages);
    }
  }

  FreePool (Map);
  *TotalBytes = Total;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtMemoryMap (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"type",       L"Type",        22 },
    { L"start",      L"Start",       20 },
    { L"end",        L"End",         20 },
    { L"pages",      L"Pages",       14 },
    { L"attributes", L"Attributes",  20 },
    { L"flags",      L"Flags",       0  },
  };
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINTN                  MapSize;
  UINTN                  DescSize;
  UINT32                 Version;
  CHAR16                 Flags[64];

  Status = GetMemoryMapCopy (&Map, &MapSize, &DescSize, &Version);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"GetMemoryMap failed (%s)", BdtStatusName (Status));
    return Status;
  }

  BdtOutFieldNum (L"Descriptor Count", MapSize / DescSize);
  BdtOutFieldNum (L"Descriptor Size", DescSize);
  BdtOutFieldNum (L"Descriptor Version", Version);
  BdtOutTableBegin (L"memory_map", Columns, ARRAY_SIZE (Columns));
  for (Desc = Map; (UINT8 *)Desc < (UINT8 *)Map + MapSize; Desc = NEXT_DESC (Desc, DescSize)) {
    FormatAttributes (Desc->Attribute, Flags, sizeof (Flags));
    BdtOutRowBegin ();
    BdtOutCell (L"%s", MemoryTypeName (Desc->Type));
    BdtOutCell (L"0x%016lx", Desc->PhysicalStart);
    BdtOutCell (L"0x%016lx", Desc->PhysicalStart + EFI_PAGES_TO_SIZE (Desc->NumberOfPages) - 1);
    BdtOutCell (L"0x%lx", Desc->NumberOfPages);
    BdtOutCell (L"0x%016lx", Desc->Attribute);
    BdtOutCell (L"%s", Flags);
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  FreePool (Map);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtMemoryInfo (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"type",  L"Type",  22 },
    { L"count", L"Count", 8  },
    { L"pages", L"Pages", 14 },
    { L"size",  L"Size",  0  },
  };
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINTN                  MapSize;
  UINTN                  DescSize;
  UINT64                 Pages[ARRAY_SIZE (mMemoryTypeNames) + 1];
  UINTN                  Counts[ARRAY_SIZE (mMemoryTypeNames) + 1];
  UINTN                  Type;
  UINT64                 Ram;
  UINT64                 Largest;
  UINT64                 Highest;
  CHAR16                 Text[32];

  Status = GetMemoryMapCopy (&Map, &MapSize, &DescSize, NULL);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"GetMemoryMap failed (%s)", BdtStatusName (Status));
    return Status;
  }

  ZeroMem (Pages, sizeof (Pages));
  ZeroMem (Counts, sizeof (Counts));
  Ram     = 0;
  Largest = 0;
  Highest = 0;
  for (Desc = Map; (UINT8 *)Desc < (UINT8 *)Map + MapSize; Desc = NEXT_DESC (Desc, DescSize)) {
    Type = (Desc->Type < ARRAY_SIZE (mMemoryTypeNames)) ? Desc->Type : ARRAY_SIZE (mMemoryTypeNames);
    Pages[Type] += Desc->NumberOfPages;
    Counts[Type]++;
    if (IsRamType (Desc->Type)) {
      Ram    += EFI_PAGES_TO_SIZE (Desc->NumberOfPages);
      Highest = MAX (Highest, Desc->PhysicalStart + EFI_PAGES_TO_SIZE (Desc->NumberOfPages));
    }

    if ((Desc->Type == EfiConventionalMemory) && (Desc->NumberOfPages > Largest)) {
      Largest = Desc->NumberOfPages;
    }
  }

  BdtOutSectionBegin (L"Summary");
  BdtFormatSize (Ram, Text, sizeof (Text));
  BdtOutField (L"Total RAM", L"%s", Text);
  BdtOutFieldNum (L"Total RAM Bytes", Ram);
  BdtFormatSize (EFI_PAGES_TO_SIZE (Pages[EfiConventionalMemory]), Text, sizeof (Text));
  BdtOutField (L"Free (Conventional)", L"%s", Text);
  BdtFormatSize (EFI_PAGES_TO_SIZE (Largest), Text, sizeof (Text));
  BdtOutField (L"Largest Free Block", L"%s", Text);
  BdtOutFieldHex (L"Highest RAM Address", (Highest == 0) ? 0 : Highest - 1, 16);
  BdtOutFieldNum (L"Descriptor Count", MapSize / DescSize);
  BdtOutSectionEnd ();

  BdtOutTableBegin (L"by_type", Columns, ARRAY_SIZE (Columns));
  for (Type = 0; Type <= ARRAY_SIZE (mMemoryTypeNames); Type++) {
    if (Counts[Type] == 0) {
      continue;
    }

    BdtFormatSize (EFI_PAGES_TO_SIZE (Pages[Type]), Text, sizeof (Text));
    BdtOutRowBegin ();
    BdtOutCell (L"%s", (Type < ARRAY_SIZE (mMemoryTypeNames)) ? mMemoryTypeNames[Type] : L"Other");
    BdtOutCell (L"%d", Counts[Type]);
    BdtOutCell (L"0x%lx", Pages[Type]);
    BdtOutCell (L"%s", Text);
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  FreePool (Map);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtMemoryDump (
  IN UINT64  Address,
  IN UINTN   Length
  )
{
  if ((Length == 0) || (Length > BDT_MEMORY_DUMP_MAX)) {
    BdtOutError (L"Length must be 1 - 0x%x bytes", BDT_MEMORY_DUMP_MAX);
    return EFI_INVALID_PARAMETER;
  }

  if ((Address < EFI_PAGE_SIZE) || (Address + Length < Address) || (Address + Length - 1 > MAX_ADDRESS)) {
    BdtOutError (L"Address range 0x%lx + 0x%x is not accessible", Address, Length);
    return EFI_INVALID_PARAMETER;
  }

  BdtOutHexDump (L"Memory", Address, (VOID *)(UINTN)Address, Length);
  return EFI_SUCCESS;
}

STATIC
UINTN
SearchRange (
  IN     UINT64       Start,
  IN     UINT64       End,
  IN     CONST UINT8  *Pattern,
  IN     UINTN        PatternSize,
  IN OUT UINTN        Hits
  )
{
  CONST UINT8  *Walk;
  CONST UINT8  *Last;

  if ((End - Start) < PatternSize) {
    return Hits;
  }

  Walk = (CONST UINT8 *)(UINTN)Start;
  Last = (CONST UINT8 *)(UINTN)(End - PatternSize);
  for ( ; Walk <= Last && Hits < BDT_SEARCH_MAX_HITS; Walk++) {
    if ((*Walk == Pattern[0]) && (Walk != Pattern) && (CompareMem (Walk, Pattern, PatternSize) == 0)) {
      BdtOutRowBegin ();
      BdtOutCell (L"0x%016lx", (UINT64)(UINTN)Walk);
      BdtOutRowEnd ();
      Hits++;
    }
  }

  return Hits;
}

EFI_STATUS
EFIAPI
BdtMemorySearch (
  IN CONST UINT8  *Pattern,
  IN UINTN        PatternSize,
  IN UINT64       Start,
  IN UINT64       Length
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"address", L"Address", 0 },
  };
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINTN                  MapSize;
  UINTN                  DescSize;
  UINTN                  Hits;
  UINT64                 RegionStart;
  UINT64                 RegionEnd;
  UINT64                 End;

  if ((Pattern == NULL) || (PatternSize == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = GetMemoryMapCopy (&Map, &MapSize, &DescSize, NULL);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"GetMemoryMap failed (%s)", BdtStatusName (Status));
    return Status;
  }

  End = (Length == 0) ? MAX_UINT64 : Start + Length;
  if (End < Start) {
    End = MAX_UINT64;
  }

  BdtOutFieldNum (L"Pattern Size", PatternSize);
  BdtOutTableBegin (L"found", Columns, ARRAY_SIZE (Columns));
  Hits = 0;

  //
  // Only RAM regions from the memory map are searched, never MMIO, so the
  // search cannot hit device registers.
  //
  for (Desc = Map; (UINT8 *)Desc < (UINT8 *)Map + MapSize && Hits < BDT_SEARCH_MAX_HITS; Desc = NEXT_DESC (Desc, DescSize)) {
    if (!IsRamType (Desc->Type) || (Desc->Type == EfiUnacceptedMemoryType) || ((Desc->Attribute & EFI_MEMORY_RP) != 0)) {
      continue;
    }

    RegionStart = MAX (Desc->PhysicalStart, Start);
    RegionStart = MAX (RegionStart, EFI_PAGE_SIZE);
    RegionEnd   = MIN (Desc->PhysicalStart + EFI_PAGES_TO_SIZE (Desc->NumberOfPages), End);
    if ((RegionStart >= RegionEnd) || (RegionEnd - 1 > MAX_ADDRESS)) {
      continue;
    }

    Hits = SearchRange (RegionStart, RegionEnd, Pattern, PatternSize, Hits);
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Matches", Hits);
  if (Hits >= BDT_SEARCH_MAX_HITS) {
    BdtOutNote (L"Stopped after %d matches.", BDT_SEARCH_MAX_HITS);
  }

  FreePool (Map);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtMmioRead (
  IN UINT64  Address,
  IN UINTN   Length,
  IN UINTN   Width
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"address", L"Address", 20 },
    { L"value",   L"Value",   0  },
  };
  UINTN   Offset;
  UINT64  Value;
  UINTN   Addr;

  if ((Width != 1) && (Width != 2) && (Width != 4) && (Width != 8)) {
    BdtOutError (L"Width must be 1, 2, 4 or 8");
    return EFI_INVALID_PARAMETER;
  }

  if ((Length == 0) || (Length > BDT_MMIO_READ_MAX) || ((Address & (Width - 1)) != 0) ||
      (Address + Length < Address) || (Address + Length - 1 > MAX_ADDRESS))
  {
    BdtOutError (L"Address must be aligned to the width and length must be 1 - 0x%x", BDT_MMIO_READ_MAX);
    return EFI_INVALID_PARAMETER;
  }

  BdtOutFieldHex (L"Address", Address, 0);
  BdtOutFieldHex (L"Length", Length, 0);
  BdtOutFieldNum (L"Width", Width);
  BdtOutTableBegin (L"mmio", Columns, ARRAY_SIZE (Columns));
  for (Offset = 0; Offset < Length; Offset += Width) {
    Addr = (UINTN)(Address + Offset);
    switch (Width) {
      case 1:
        Value = MmioRead8 (Addr);
        break;
      case 2:
        Value = MmioRead16 (Addr);
        break;
      case 4:
        Value = MmioRead32 (Addr);
        break;
      default:
        Value = MmioRead64 (Addr);
        break;
    }

    BdtOutRowBegin ();
    BdtOutCell (L"0x%016lx", (UINT64)Addr);
    BdtOutCell (L"0x%0*lx", Width * 2, Value);
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtMemoryGetMap (
  OUT EFI_MEMORY_DESCRIPTOR  **Map,
  OUT UINTN                  *MapSize,
  OUT UINTN                  *DescriptorSize
  )
{
  return GetMemoryMapCopy (Map, MapSize, DescriptorSize, NULL);
}

CONST CHAR16 *
EFIAPI
BdtMemoryTypeName (
  IN UINT32  Type
  )
{
  return MemoryTypeName (Type);
}

//
// Fault guard for BdtMemorySafeRead: #PF / #GP handlers redirect execution to
// FaultRecover, which long-jumps back to the read loop.
//
STATIC BASE_LIBRARY_JUMP_BUFFER  mFaultJump;
STATIC volatile BOOLEAN          mFaultGuard = FALSE;

STATIC
VOID
FaultRecover (
  VOID
  )
{
  LongJump (&mFaultJump, 1);
}

STATIC
VOID
EFIAPI
FaultHandler (
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  if (!mFaultGuard) {
    CpuDeadLoop ();
  }

  mFaultGuard = FALSE;
 #if defined (MDE_CPU_X64)
  SystemContext.SystemContextX64->Rip = (UINT64)(UINTN)FaultRecover;
 #elif defined (MDE_CPU_IA32)
  SystemContext.SystemContextIa32->Eip = (UINT32)(UINTN)FaultRecover;
 #else
  CpuDeadLoop ();
 #endif
}

/**
  Install the #PF / #GP fault guard handlers. Each flag tells whether the
  handler could be registered.
**/
STATIC
VOID
GuardBegin (
  OUT EFI_CPU_ARCH_PROTOCOL  **Cpu,
  OUT BOOLEAN                *GuardPf,
  OUT BOOLEAN                *GuardGp
  )
{
  *GuardPf = FALSE;
  *GuardGp = FALSE;
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)Cpu))) {
    *GuardPf = !EFI_ERROR ((*Cpu)->RegisterInterruptHandler (*Cpu, EXCEPT_IA32_PAGE_FAULT, FaultHandler));
    *GuardGp = !EFI_ERROR ((*Cpu)->RegisterInterruptHandler (*Cpu, EXCEPT_IA32_GP_FAULT, FaultHandler));
  }
}

STATIC
VOID
GuardEnd (
  IN EFI_CPU_ARCH_PROTOCOL  *Cpu,
  IN BOOLEAN                GuardPf,
  IN BOOLEAN                GuardGp
  )
{
  if (GuardPf) {
    Cpu->RegisterInterruptHandler (Cpu, EXCEPT_IA32_PAGE_FAULT, NULL);
  }

  if (GuardGp) {
    Cpu->RegisterInterruptHandler (Cpu, EXCEPT_IA32_GP_FAULT, NULL);
  }
}

EFI_STATUS
EFIAPI
BdtMemorySafeRead (
  IN  UINT64   Address,
  IN  UINTN    Width,
  IN  UINTN    Count,
  OUT VOID     *Buffer,
  OUT BOOLEAN  *Valid OPTIONAL,
  OUT BOOLEAN  *Protected OPTIONAL
  )
{
  EFI_CPU_ARCH_PROTOCOL  *Cpu = NULL;
  BOOLEAN                GuardPf;
  BOOLEAN                GuardGp;
  volatile UINTN         Index;
  UINTN                  Addr;
  UINT8                  *Out;

  if (((Width != 1) && (Width != 2) && (Width != 4) && (Width != 8)) || ((Address & (Width - 1)) != 0) ||
      (Count == 0) || (Address + Width * Count - 1 < Address) || (Address + Width * Count - 1 > MAX_ADDRESS))
  {
    return EFI_INVALID_PARAMETER;
  }

  GuardBegin (&Cpu, &GuardPf, &GuardGp);

  Out = (UINT8 *)Buffer;
  for (Index = 0; Index < Count; Index++) {
    Addr = (UINTN)(Address + Index * Width);
    if (SetJump (&mFaultJump) != 0) {
      SetMem (Out + Index * Width, Width, 0xFF);
      if (Valid != NULL) {
        Valid[Index] = FALSE;
      }

      continue;
    }

    mFaultGuard = TRUE;
    switch (Width) {
      case 1:
        Out[Index] = MmioRead8 (Addr);
        break;
      case 2:
        WriteUnaligned16 ((UINT16 *)(Out + Index * 2), MmioRead16 (Addr));
        break;
      case 4:
        WriteUnaligned32 ((UINT32 *)(Out + Index * 4), MmioRead32 (Addr));
        break;
      default:
        WriteUnaligned64 ((UINT64 *)(Out + Index * 8), MmioRead64 (Addr));
        break;
    }

    mFaultGuard = FALSE;
    if (Valid != NULL) {
      Valid[Index] = TRUE;
    }
  }

  GuardEnd (Cpu, GuardPf, GuardGp);
  if (Protected != NULL) {
    *Protected = GuardPf && GuardGp;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtMemorySafeWrite (
  IN UINT64  Address,
  IN UINTN   Width,
  IN UINT64  Value
  )
{
  EFI_CPU_ARCH_PROTOCOL  *Cpu = NULL;
  BOOLEAN                GuardPf;
  BOOLEAN                GuardGp;
  volatile BOOLEAN       Faulted;
  UINTN                  Addr;

  if (((Width != 1) && (Width != 2) && (Width != 4) && (Width != 8)) || ((Address & (Width - 1)) != 0) ||
      (Address + Width - 1 < Address) || (Address + Width - 1 > MAX_ADDRESS))
  {
    return EFI_INVALID_PARAMETER;
  }

  Addr    = (UINTN)Address;
  Faulted = FALSE;
  GuardBegin (&Cpu, &GuardPf, &GuardGp);
  if (SetJump (&mFaultJump) != 0) {
    Faulted = TRUE;
  } else {
    mFaultGuard = TRUE;
    switch (Width) {
      case 1:
        MmioWrite8 (Addr, (UINT8)Value);
        break;
      case 2:
        MmioWrite16 (Addr, (UINT16)Value);
        break;
      case 4:
        MmioWrite32 (Addr, (UINT32)Value);
        break;
      default:
        MmioWrite64 (Addr, Value);
        break;
    }

    mFaultGuard = FALSE;
  }

  GuardEnd (Cpu, GuardPf, GuardGp);
  return Faulted ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtMmioWrite (
  IN UINT64  Address,
  IN UINTN   Width,
  IN UINT64  Value
  )
{
  EFI_STATUS  Status;
  UINT64      Old;
  UINT64      New;
  BOOLEAN     OldValid;
  BOOLEAN     NewValid;

  if ((Width != 8) && (RShiftU64 (Value, Width * 8) != 0)) {
    BdtOutError (L"Value 0x%lx does not fit in %d byte(s)", Value, Width);
    return EFI_INVALID_PARAMETER;
  }

  Old    = 0;
  New    = 0;
  Status = BdtMemorySafeRead (Address, Width, 1, &Old, &OldValid, NULL);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"Address must be aligned to the width (1, 2, 4 or 8)");
    return Status;
  }

  Status = BdtMemorySafeWrite (Address, Width, Value);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"Write to 0x%lx faulted (%s)", Address, BdtStatusName (Status));
    return Status;
  }

  BdtMemorySafeRead (Address, Width, 1, &New, &NewValid, NULL);
  BdtOutFieldHex (L"Address", Address, 0);
  BdtOutFieldNum (L"Width", Width);
  if (OldValid) {
    BdtOutFieldHex (L"Old Value", Old, Width * 2);
  }

  BdtOutFieldHex (L"Written", Value, Width * 2);
  if (NewValid) {
    BdtOutFieldHex (L"Read Back", New, Width * 2);
  }

  return EFI_SUCCESS;
}

UINT32
EFIAPI
BdtIoPortRead (
  IN UINT16  Port,
  IN UINTN   Width
  )
{
  switch (Width) {
    case 1:
      return IoRead8 (Port);
    case 2:
      return IoRead16 (Port);
    default:
      return IoRead32 (Port);
  }
}

VOID
EFIAPI
BdtIoPortWrite (
  IN UINT16  Port,
  IN UINTN   Width,
  IN UINT32  Value
  )
{
  switch (Width) {
    case 1:
      IoWrite8 (Port, (UINT8)Value);
      break;
    case 2:
      IoWrite16 (Port, (UINT16)Value);
      break;
    default:
      IoWrite32 (Port, Value);
      break;
  }
}

STATIC
EFI_STATUS
CheckPort (
  IN UINT16  Port,
  IN UINTN   Width
  )
{
  if (((Width != 1) && (Width != 2) && (Width != 4)) || ((Port & (Width - 1)) != 0) || ((UINT32)Port + Width > 0x10000)) {
    BdtOutError (L"Port must be aligned to the width (1, 2 or 4)");
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtIoPortShow (
  IN UINT16  Port,
  IN UINTN   Width
  )
{
  if (EFI_ERROR (CheckPort (Port, Width))) {
    return EFI_INVALID_PARAMETER;
  }

  BdtOutFieldHex (L"Port", Port, 4);
  BdtOutFieldNum (L"Width", Width);
  BdtOutFieldHex (L"Value", BdtIoPortRead (Port, Width), Width * 2);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtIoPortWriteShow (
  IN UINT16  Port,
  IN UINTN   Width,
  IN UINT32  Value
  )
{
  if (EFI_ERROR (CheckPort (Port, Width))) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Width != 4) && ((Value >> (Width * 8)) != 0)) {
    BdtOutError (L"Value 0x%x does not fit in %d byte(s)", Value, Width);
    return EFI_INVALID_PARAMETER;
  }

  //
  // No read back: reading a port can have side effects (e.g. clear status).
  //
  BdtIoPortWrite (Port, Width, Value);
  BdtOutFieldHex (L"Port", Port, 4);
  BdtOutFieldNum (L"Width", Width);
  BdtOutFieldHex (L"Written", Value, Width * 2);
  return EFI_SUCCESS;
}
