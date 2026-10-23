/** @file
  BdtHandleLib - handle / protocol / device path inspection.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_HANDLE_LIB_H_
#define BDT_HANDLE_LIB_H_

#include <Uefi.h>

EFI_STATUS EFIAPI BdtHandleList (VOID);

/**
  Show one handle. Index is the 1-based index shown by "handle list".
**/
EFI_STATUS EFIAPI BdtHandleInfo (IN UINTN Index);
EFI_STATUS EFIAPI BdtHandleByProtocol (IN CONST EFI_GUID *Protocol);
EFI_STATUS EFIAPI BdtHandleTree (VOID);
EFI_STATUS EFIAPI BdtProtocolList (VOID);
EFI_STATUS EFIAPI BdtDevicePathList (VOID);
EFI_STATUS EFIAPI BdtDevicePathDecode (IN UINTN Index);

#endif
