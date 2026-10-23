/** @file
  BdtCpuLib - CPUID / MSR / MTRR / MCA inspection (IA32 / X64).

  MSR reads are protected: a temporary #GP handler is registered through
  EFI_CPU_ARCH_PROTOCOL so reading an unsupported MSR returns an error instead
  of hanging the platform. Nothing is ever written to an MSR.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Library/BdtCpuLib.h>
#include <Protocol/Cpu.h>
#include <Protocol/DebugSupport.h>
#include <Protocol/MpService.h>

#define MSR_IA32_BIOS_SIGN_ID       0x0000008B
#define MSR_IA32_MTRRCAP            0x000000FE
#define MSR_IA32_MCG_CAP            0x00000179
#define MSR_IA32_MCG_STATUS         0x0000017A
#define MSR_IA32_MTRR_PHYSBASE0     0x00000200
#define MSR_IA32_MTRR_DEF_TYPE      0x000002FF
#define MSR_IA32_MC0_CTL            0x00000400

#define CPUID_STD_LEAF_LIMIT        0x40
#define CPUID_EXT_LEAF_LIMIT        0x80000040
#define CPUID_MAX_SUBLEAF           8

STATIC volatile BOOLEAN  mMsrFault;

STATIC CONST UINT32  mFixedMtrrs[] = {
  0x250, 0x258, 0x259, 0x268, 0x269, 0x26A, 0x26B, 0x26C, 0x26D, 0x26E, 0x26F
};

STATIC CONST CHAR16  *mMemTypeNames[] = {
  L"UC", L"WC", L"Reserved", L"Reserved", L"WT", L"WP", L"WB", L"UC-"
};

typedef struct {
  UINT8           Reg;        ///< 0 = leaf1 ECX, 1 = leaf1 EDX, 2 = leaf7 EBX, 3 = leaf7 ECX, 4 = leaf7 EDX
  UINT8           Bit;
  CONST CHAR16    *Name;
} BDT_CPU_FEATURE;

STATIC CONST BDT_CPU_FEATURE  mFeatures[] = {
  { 1, 0,  L"FPU"      }, { 1, 4,  L"TSC"     }, { 1, 5,  L"MSR"      }, { 1, 6,  L"PAE"     },
  { 1, 7,  L"MCE"      }, { 1, 9,  L"APIC"    }, { 1, 12, L"MTRR"     }, { 1, 13, L"PGE"     },
  { 1, 14, L"MCA"      }, { 1, 16, L"PAT"     }, { 1, 19, L"CLFSH"    }, { 1, 25, L"SSE"     },
  { 1, 26, L"SSE2"     }, { 1, 28, L"HTT"     }, { 0, 0,  L"SSE3"     }, { 0, 1,  L"PCLMULQDQ" },
  { 0, 5,  L"VMX"      }, { 0, 6,  L"SMX"     }, { 0, 7,  L"EIST"     }, { 0, 9,  L"SSSE3"   },
  { 0, 19, L"SSE4.1"   }, { 0, 20, L"SSE4.2"  }, { 0, 21, L"x2APIC"   }, { 0, 24, L"TSC-Deadline" },
  { 0, 25, L"AES"      }, { 0, 26, L"XSAVE"   }, { 0, 28, L"AVX"      }, { 0, 30, L"RDRAND"  },
  { 0, 31, L"Hypervisor" },
  { 2, 0,  L"FSGSBASE" }, { 2, 3,  L"BMI1"    }, { 2, 5,  L"AVX2"     }, { 2, 7,  L"SMEP"    },
  { 2, 8,  L"BMI2"     }, { 2, 11, L"RTM"     }, { 2, 14, L"MPX"      }, { 2, 16, L"AVX512F" },
  { 2, 18, L"RDSEED"   }, { 2, 20, L"SMAP"    }, { 2, 29, L"SHA"      },
  { 3, 2,  L"UMIP"     }, { 3, 7,  L"CET-SS"  }, { 3, 13, L"TME"      },
  { 4, 20, L"CET-IBT"  },
};

STATIC
VOID
EFIAPI
GpFaultHandler (
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  UINT8  *Ip;

 #if defined (MDE_CPU_X64)
  Ip = (UINT8 *)(UINTN)SystemContext.SystemContextX64->Rip;
 #else
  Ip = (UINT8 *)(UINTN)SystemContext.SystemContextIa32->Eip;
 #endif

  //
  // Only recover from a faulting RDMSR (0F 32): skip it and return zero.
  //
  if ((Ip[0] == 0x0F) && (Ip[1] == 0x32)) {
    mMsrFault = TRUE;
 #if defined (MDE_CPU_X64)
    SystemContext.SystemContextX64->Rip += 2;
    SystemContext.SystemContextX64->Rax  = 0;
    SystemContext.SystemContextX64->Rdx  = 0;
 #else
    SystemContext.SystemContextIa32->Eip += 2;
    SystemContext.SystemContextIa32->Eax  = 0;
    SystemContext.SystemContextIa32->Edx  = 0;
 #endif
    return;
  }

  CpuDeadLoop ();
}

EFI_STATUS
EFIAPI
BdtCpuReadMsr (
  IN  UINT32  Index,
  OUT UINT64  *Value
  )
{
  EFI_CPU_ARCH_PROTOCOL  *Cpu;
  BOOLEAN                Protected;
  UINT32                 Edx;

  //
  // CPUID.1:EDX[5] - RDMSR support.
  //
  AsmCpuid (1, NULL, NULL, NULL, &Edx);
  if ((Edx & BIT5) == 0) {
    return EFI_UNSUPPORTED;
  }

  Protected = FALSE;
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu))) {
    Protected = !EFI_ERROR (Cpu->RegisterInterruptHandler (Cpu, EXCEPT_IA32_GP_FAULT, GpFaultHandler));
  }

  mMsrFault = FALSE;
  *Value    = AsmReadMsr64 (Index);

  if (Protected) {
    Cpu->RegisterInterruptHandler (Cpu, EXCEPT_IA32_GP_FAULT, NULL);
  }

  if (mMsrFault) {
    *Value = 0;
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
GetSignature (
  OUT BDT_CPU_INFO  *Info
  )
{
  UINT32  Eax;
  UINT32  Base;

  AsmCpuid (1, &Eax, NULL, NULL, NULL);
  Info->Signature = Eax;
  Base            = (Eax >> 8) & 0xF;
  Info->Family    = (Base == 0xF) ? Base + ((Eax >> 20) & 0xFF) : Base;
  Info->Model     = (Eax >> 4) & 0xF;
  if ((Base == 0x6) || (Base == 0xF)) {
    Info->Model |= ((Eax >> 16) & 0xF) << 4;
  }

  Info->Stepping = Eax & 0xF;
}

STATIC
VOID
TrimLeft (
  IN OUT CHAR8  *String
  )
{
  CHAR8  *Walk;

  for (Walk = String; *Walk == ' '; Walk++) {
  }

  if (Walk != String) {
    CopyMem (String, Walk, AsciiStrSize (Walk));
  }
}

EFI_STATUS
EFIAPI
BdtCpuGetInfo (
  OUT BDT_CPU_INFO  *Info
  )
{
  EFI_MP_SERVICES_PROTOCOL   *Mp;
  EFI_PROCESSOR_INFORMATION  ProcInfo;
  UINT32                     MaxExt;
  UINT32                     *Regs;
  UINTN                      Leaf;
  UINTN                      Total;
  UINTN                      Enabled;
  UINTN                      Index;
  UINTN                      Packages[64];
  UINTN                      PackageCount;
  UINTN                      Check;

  ZeroMem (Info, sizeof (*Info));
  AsmCpuid (0, NULL, (UINT32 *)&Info->Vendor[0], (UINT32 *)&Info->Vendor[8], (UINT32 *)&Info->Vendor[4]);
  Info->Vendor[12] = '\0';
  GetSignature (Info);

  AsmCpuid (0x80000000, &MaxExt, NULL, NULL, NULL);
  if (MaxExt >= 0x80000004) {
    Regs = (UINT32 *)Info->Brand;
    for (Leaf = 0; Leaf < 3; Leaf++) {
      AsmCpuid ((UINT32)(0x80000002 + Leaf), &Regs[Leaf * 4], &Regs[Leaf * 4 + 1], &Regs[Leaf * 4 + 2], &Regs[Leaf * 4 + 3]);
    }

    Info->Brand[48] = '\0';
    TrimLeft (Info->Brand);
  }

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiMpServiceProtocolGuid, NULL, (VOID **)&Mp)) ||
      EFI_ERROR (Mp->GetNumberOfProcessors (Mp, &Total, &Enabled)))
  {
    return EFI_SUCCESS;
  }

  Info->Threads        = Total;
  Info->EnabledThreads = Enabled;
  PackageCount         = 0;
  for (Index = 0; Index < Total; Index++) {
    if (EFI_ERROR (Mp->GetProcessorInfo (Mp, Index, &ProcInfo))) {
      continue;
    }

    if (ProcInfo.Location.Thread == 0) {
      Info->Cores++;
    }

    for (Check = 0; Check < PackageCount; Check++) {
      if (Packages[Check] == ProcInfo.Location.Package) {
        break;
      }
    }

    if ((Check == PackageCount) && (PackageCount < ARRAY_SIZE (Packages))) {
      Packages[PackageCount++] = ProcInfo.Location.Package;
    }
  }

  Info->Packages = PackageCount;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtCpuInfo (
  VOID
  )
{
  BDT_CPU_INFO  Info;
  UINT32        MaxLeaf;
  UINT32        MaxExt;
  UINT32        Regs[5];
  UINT32        Ebx;
  UINT64        Microcode;
  UINTN         Index;
  CHAR16        *Features;

  BdtCpuGetInfo (&Info);
  AsmCpuid (0, &MaxLeaf, NULL, NULL, NULL);
  AsmCpuid (0x80000000, &MaxExt, NULL, NULL, NULL);

  BdtOutField (L"Vendor", L"%a", Info.Vendor);
  BdtOutField (L"Brand", L"%a", Info.Brand);
  BdtOutFieldHex (L"Signature", Info.Signature, 8);
  BdtOutFieldHex (L"Family", Info.Family, 2);
  BdtOutFieldHex (L"Model", Info.Model, 2);
  BdtOutFieldHex (L"Stepping", Info.Stepping, 1);
  BdtOutFieldHex (L"Max Standard Leaf", MaxLeaf, 8);
  BdtOutFieldHex (L"Max Extended Leaf", MaxExt, 8);

  if (!EFI_ERROR (BdtCpuReadMsr (MSR_IA32_BIOS_SIGN_ID, &Microcode))) {
    BdtOutFieldHex (
      L"Microcode Revision",
      (AsciiStrCmp (Info.Vendor, "GenuineIntel") == 0) ? RShiftU64 (Microcode, 32) : (Microcode & MAX_UINT32),
      8
      );
  }

  AsmCpuid (1, NULL, &Ebx, NULL, NULL);
  BdtOutFieldHex (L"Initial APIC ID (BSP)", Ebx >> 24, 2);
  if (Info.Threads != 0) {
    BdtOutFieldNum (L"Packages", Info.Packages);
    BdtOutFieldNum (L"Cores", Info.Cores);
    BdtOutFieldNum (L"Threads", Info.Threads);
    BdtOutFieldNum (L"Enabled Threads", Info.EnabledThreads);
  } else {
    BdtOutNote (L"EFI_MP_SERVICES_PROTOCOL not available; core count unknown.");
  }

  //
  // Feature flags: leaf 1 ECX/EDX, leaf 7.0 EBX/ECX/EDX.
  //
  ZeroMem (Regs, sizeof (Regs));
  AsmCpuid (1, NULL, NULL, &Regs[0], &Regs[1]);
  if (MaxLeaf >= 7) {
    AsmCpuidEx (7, 0, NULL, &Regs[2], &Regs[3], &Regs[4]);
  }

  Features = AllocateZeroPool (BDT_MAX_VALUE_LENGTH * sizeof (CHAR16));
  if (Features != NULL) {
    for (Index = 0; Index < ARRAY_SIZE (mFeatures); Index++) {
      if ((Regs[mFeatures[Index].Reg] & (1u << mFeatures[Index].Bit)) != 0) {
        if (Features[0] != L'\0') {
          StrCatS (Features, BDT_MAX_VALUE_LENGTH, L" ");
        }

        StrCatS (Features, BDT_MAX_VALUE_LENGTH, mFeatures[Index].Name);
      }
    }

    BdtOutField (L"Features", L"%s", Features);
    FreePool (Features);
  }

  return EFI_SUCCESS;
}

STATIC
VOID
CpuidRow (
  IN UINT32  Leaf,
  IN UINT32  SubLeaf
  )
{
  UINT32  Eax;
  UINT32  Ebx;
  UINT32  Ecx;
  UINT32  Edx;

  AsmCpuidEx (Leaf, SubLeaf, &Eax, &Ebx, &Ecx, &Edx);
  BdtOutRowBegin ();
  BdtOutCell (L"%08x", Leaf);
  BdtOutCell (L"%x", SubLeaf);
  BdtOutCell (L"%08x", Eax);
  BdtOutCell (L"%08x", Ebx);
  BdtOutCell (L"%08x", Ecx);
  BdtOutCell (L"%08x", Edx);
  BdtOutRowEnd ();
}

/**
  TRUE for leaves whose output depends on the sub-leaf index in ECX.
**/
STATIC
BOOLEAN
HasSubLeaves (
  IN UINT32  Leaf
  )
{
  return (Leaf == 0x4) || (Leaf == 0x7) || (Leaf == 0xB) || (Leaf == 0xD) || (Leaf == 0xF) ||
         (Leaf == 0x10) || (Leaf == 0x12) || (Leaf == 0x14) || (Leaf == 0x17) || (Leaf == 0x18) ||
         (Leaf == 0x1F) || (Leaf == 0x8000001D);
}

EFI_STATUS
EFIAPI
BdtCpuCpuid (
  IN BOOLEAN  All,
  IN UINT32   Leaf,
  IN UINT32   SubLeaf
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"leaf",    L"Leaf",     10 },
    { L"subleaf", L"Sub",      5  },
    { L"eax",     L"EAX",      10 },
    { L"ebx",     L"EBX",      10 },
    { L"ecx",     L"ECX",      10 },
    { L"edx",     L"EDX",      0  },
  };
  UINT32  Max;
  UINT32  MaxExt;
  UINT32  Current;
  UINT32  Sub;
  UINT32  Eax;

  BdtOutTableBegin (L"cpuid", Columns, ARRAY_SIZE (Columns));
  if (!All) {
    CpuidRow (Leaf, SubLeaf);
    BdtOutTableEnd ();
    return EFI_SUCCESS;
  }

  AsmCpuid (0, &Max, NULL, NULL, NULL);
  AsmCpuid (0x80000000, &MaxExt, NULL, NULL, NULL);
  Max    = MIN (Max, CPUID_STD_LEAF_LIMIT);
  MaxExt = MIN (MaxExt, CPUID_EXT_LEAF_LIMIT);

  for (Current = 0; Current <= Max; Current++) {
    CpuidRow (Current, 0);
    if (HasSubLeaves (Current)) {
      for (Sub = 1; Sub < CPUID_MAX_SUBLEAF; Sub++) {
        AsmCpuidEx (Current, Sub, &Eax, NULL, NULL, NULL);
        if ((Eax == 0) && (Current != 0xD)) {
          break;
        }

        CpuidRow (Current, Sub);
      }
    }
  }

  for (Current = 0x80000000; Current <= MaxExt && Current >= 0x80000000; Current++) {
    CpuidRow (Current, 0);
  }

  BdtOutTableEnd ();
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtCpuMsr (
  IN UINT32  Index
  )
{
  EFI_STATUS  Status;
  UINT64      Value;
  UINTN       Bit;

  Status = BdtCpuReadMsr (Index, &Value);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"MSR 0x%x is not readable (%s)", Index, BdtStatusName (Status));
    return Status;
  }

  BdtOutFieldHex (L"MSR", Index, 0);
  BdtOutFieldHex (L"Value", Value, 16);
  BdtOutFieldHex (L"High", RShiftU64 (Value, 32), 8);
  BdtOutFieldHex (L"Low", Value & MAX_UINT32, 8);
  BdtOutListBegin (L"Set Bits");
  for (Bit = 0; Bit < 64; Bit++) {
    if ((RShiftU64 (Value, Bit) & 1) != 0) {
      BdtOutItemBegin (L"Bit %d : 1", Bit);
      if (BdtOutGetFormat () != BdtFormatText) {
        BdtOutFieldNum (L"bit", Bit);
      }

      BdtOutItemEnd ();
    }
  }

  BdtOutListEnd ();
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtCpuMtrr (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  VarColumns[] = {
    { L"index", L"#",       4  },
    { L"base",  L"Base",    20 },
    { L"mask",  L"Mask",    20 },
    { L"type",  L"Type",    6  },
    { L"size",  L"Size",    0  },
  };
  STATIC CONST BDT_COLUMN  FixedColumns[] = {
    { L"msr",   L"MSR",     8  },
    { L"value", L"Value",   0  },
  };
  UINT32  Edx;
  UINT32  MaxExt;
  UINT32  AddrBits;
  UINT64  Cap;
  UINT64  DefType;
  UINT64  Base;
  UINT64  Mask;
  UINT64  AddrMask;
  UINT64  Size;
  UINT64  Value;
  UINTN   Count;
  UINTN   Index;
  CHAR16  SizeText[24];

  AsmCpuid (1, NULL, NULL, NULL, &Edx);
  if ((Edx & BIT12) == 0) {
    BdtOutError (L"CPU does not support MTRRs");
    return EFI_UNSUPPORTED;
  }

  AsmCpuid (0x80000000, &MaxExt, NULL, NULL, NULL);
  AddrBits = 36;
  if (MaxExt >= 0x80000008) {
    AsmCpuid (0x80000008, &AddrBits, NULL, NULL, NULL);
    AddrBits &= 0xFF;
  }

  AddrMask = LShiftU64 (1, AddrBits) - 1;

  if (EFI_ERROR (BdtCpuReadMsr (MSR_IA32_MTRRCAP, &Cap)) || EFI_ERROR (BdtCpuReadMsr (MSR_IA32_MTRR_DEF_TYPE, &DefType))) {
    BdtOutError (L"MTRR MSRs are not readable");
    return EFI_UNSUPPORTED;
  }

  Count = (UINTN)(Cap & 0xFF);
  BdtOutFieldHex (L"MTRRCAP", Cap, 16);
  BdtOutFieldNum (L"Variable MTRR Count", Count);
  BdtOutFieldBool (L"Fixed MTRR Supported", (Cap & BIT8) != 0);
  BdtOutFieldBool (L"WC Supported", (Cap & BIT10) != 0);
  BdtOutFieldBool (L"SMRR Supported", (Cap & BIT11) != 0);
  BdtOutFieldHex (L"MTRR_DEF_TYPE", DefType, 16);
  BdtOutField (L"Default Type", L"%s", mMemTypeNames[DefType & 0x7]);
  BdtOutFieldBool (L"MTRRs Enabled", (DefType & BIT11) != 0);
  BdtOutFieldBool (L"Fixed MTRRs Enabled", (DefType & BIT10) != 0);
  BdtOutFieldNum (L"Physical Address Bits", AddrBits);

  BdtOutTableBegin (L"variable", VarColumns, ARRAY_SIZE (VarColumns));
  for (Index = 0; Index < Count && Index < 32; Index++) {
    if (EFI_ERROR (BdtCpuReadMsr ((UINT32)(MSR_IA32_MTRR_PHYSBASE0 + Index * 2), &Base)) ||
        EFI_ERROR (BdtCpuReadMsr ((UINT32)(MSR_IA32_MTRR_PHYSBASE0 + Index * 2 + 1), &Mask)))
    {
      continue;
    }

    if ((Mask & BIT11) == 0) {
      continue;
    }

    Size = ((~(Mask & ~(UINT64)0xFFF)) & AddrMask) + 1;
    BdtFormatSize (Size, SizeText, sizeof (SizeText));
    BdtOutRowBegin ();
    BdtOutCell (L"%d", Index);
    BdtOutCell (L"0x%016lx", Base & ~(UINT64)0xFFF);
    BdtOutCell (L"0x%016lx", Mask & ~(UINT64)0xFFF);
    BdtOutCell (L"%s", mMemTypeNames[Base & 0x7]);
    BdtOutCell (L"%s", SizeText);
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();

  if ((Cap & BIT8) != 0) {
    BdtOutTableBegin (L"fixed", FixedColumns, ARRAY_SIZE (FixedColumns));
    for (Index = 0; Index < ARRAY_SIZE (mFixedMtrrs); Index++) {
      if (EFI_ERROR (BdtCpuReadMsr (mFixedMtrrs[Index], &Value))) {
        continue;
      }

      BdtOutRowBegin ();
      BdtOutCell (L"0x%03x", mFixedMtrrs[Index]);
      BdtOutCell (L"0x%016lx", Value);
      BdtOutRowEnd ();
    }

    BdtOutTableEnd ();
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtCpuMca (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"bank",   L"Bank",   6  },
    { L"ctl",    L"CTL",    20 },
    { L"status", L"STATUS", 20 },
    { L"addr",   L"ADDR",   20 },
    { L"misc",   L"MISC",   20 },
    { L"flags",  L"Flags",  0  },
  };
  UINT32  Edx;
  UINT64  Cap;
  UINT64  McgStatus;
  UINT64  Ctl;
  UINT64  Status;
  UINT64  Addr;
  UINT64  Misc;
  UINTN   Banks;
  UINTN   Bank;
  UINTN   Valid;
  UINT32  Msr;
  CHAR16  Flags[64];

  AsmCpuid (1, NULL, NULL, NULL, &Edx);
  if ((Edx & (BIT7 | BIT14)) != (BIT7 | BIT14)) {
    BdtOutError (L"CPU does not support MCE/MCA");
    return EFI_UNSUPPORTED;
  }

  if (EFI_ERROR (BdtCpuReadMsr (MSR_IA32_MCG_CAP, &Cap))) {
    BdtOutError (L"IA32_MCG_CAP is not readable");
    return EFI_UNSUPPORTED;
  }

  BdtCpuReadMsr (MSR_IA32_MCG_STATUS, &McgStatus);
  Banks = (UINTN)(Cap & 0xFF);
  BdtOutFieldHex (L"MCG_CAP", Cap, 16);
  BdtOutFieldHex (L"MCG_STATUS", McgStatus, 16);
  BdtOutFieldNum (L"Bank Count", Banks);
  BdtOutFieldBool (L"MCIP (Machine Check In Progress)", (McgStatus & BIT2) != 0);

  Valid = 0;
  BdtOutTableBegin (L"banks", Columns, ARRAY_SIZE (Columns));
  for (Bank = 0; Bank < Banks && Bank < 64; Bank++) {
    Msr = (UINT32)(MSR_IA32_MC0_CTL + Bank * 4);
    Ctl = 0;
    BdtCpuReadMsr (Msr, &Ctl);
    if (EFI_ERROR (BdtCpuReadMsr (Msr + 1, &Status))) {
      continue;
    }

    Addr = 0;
    Misc = 0;
    if ((Status & BIT58) != 0) {
      BdtCpuReadMsr (Msr + 2, &Addr);
    }

    if ((Status & BIT59) != 0) {
      BdtCpuReadMsr (Msr + 3, &Misc);
    }

    Flags[0] = L'\0';
    if ((Status & BIT63) != 0) {
      Valid++;
      UnicodeSPrint (
        Flags,
        sizeof (Flags),
        L"VAL%s%s%s%s code=0x%04x",
        ((Status & BIT62) != 0) ? L" OVER" : L"",
        ((Status & BIT61) != 0) ? L" UC" : L" CE",
        ((Status & BIT60) != 0) ? L" EN" : L"",
        ((Status & BIT57) != 0) ? L" PCC" : L"",
        (UINT32)(Status & 0xFFFF)
        );
    }

    BdtOutRowBegin ();
    BdtOutCell (L"%d", Bank);
    BdtOutCell (L"0x%016lx", Ctl);
    BdtOutCell (L"0x%016lx", Status);
    BdtOutCell (L"0x%016lx", Addr);
    BdtOutCell (L"0x%016lx", Misc);
    BdtOutCell (L"%s", Flags);
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Banks With Valid Error", Valid);
  return EFI_SUCCESS;
}
