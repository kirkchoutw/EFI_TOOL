/** @file
  BdtCpuLib - CPUID / MSR / MTRR / MCA inspection.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_CPU_LIB_H_
#define BDT_CPU_LIB_H_

#include <Uefi.h>

typedef struct {
  CHAR8     Vendor[13];
  CHAR8     Brand[49];
  UINT32    Family;
  UINT32    Model;
  UINT32    Stepping;
  UINT32    Signature;        ///< CPUID.1:EAX
  UINTN     Packages;         ///< 0 when unknown
  UINTN     Cores;            ///< 0 when unknown
  UINTN     Threads;          ///< logical processors, 0 when unknown
  UINTN     EnabledThreads;   ///< 0 when unknown
} BDT_CPU_INFO;

EFI_STATUS EFIAPI BdtCpuGetInfo (OUT BDT_CPU_INFO *Info);
EFI_STATUS EFIAPI BdtCpuInfo (VOID);

/**
  Show all standard and extended CPUID leaves (All = TRUE) or one leaf.
**/
EFI_STATUS EFIAPI BdtCpuCpuid (IN BOOLEAN All, IN UINT32 Leaf, IN UINT32 SubLeaf);

/**
  Read an MSR. A #GP caused by an unsupported MSR is caught and reported as
  EFI_UNSUPPORTED instead of hanging the system (when the CPU protocol allows).
**/
EFI_STATUS EFIAPI BdtCpuReadMsr (IN UINT32 Index, OUT UINT64 *Value);
EFI_STATUS EFIAPI BdtCpuMsr (IN UINT32 Index);

/**
  Write an MSR. A #GP (unsupported MSR or reserved bits) is caught and
  reported as EFI_DEVICE_ERROR when the CPU protocol allows it.
**/
EFI_STATUS EFIAPI BdtCpuWriteMsr (IN UINT32 Index, IN UINT64 Value);

/**
  Write an MSR and show the old, written and read back values.
**/
EFI_STATUS EFIAPI BdtCpuMsrWrite (IN UINT32 Index, IN UINT64 Value);
EFI_STATUS EFIAPI BdtCpuMtrr (VOID);
EFI_STATUS EFIAPI BdtCpuMca (VOID);

#endif
