/** @file
  BdtPciLib - PCI / PCIe inspection through EFI_PCI_IO_PROTOCOL.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_PCI_LIB_H_
#define BDT_PCI_LIB_H_

#include <Uefi.h>

typedef struct {
  UINT16    Segment;
  UINT8     Bus;
  UINT8     Device;
  UINT8     Function;
} BDT_PCI_ADDRESS;

EFI_STATUS EFIAPI BdtPciGetDeviceCount (OUT UINTN *Count);

EFI_STATUS EFIAPI BdtPciList (VOID);
EFI_STATUS EFIAPI BdtPciTree (VOID);

/**
  Dump configuration space. Extended = TRUE dumps 4 KB (PCIe), otherwise 256 B.
**/
EFI_STATUS EFIAPI BdtPciDump (IN CONST BDT_PCI_ADDRESS *Address, IN BOOLEAN Extended);
EFI_STATUS EFIAPI BdtPciCap (IN CONST BDT_PCI_ADDRESS *Address);
EFI_STATUS EFIAPI BdtPciBar (IN CONST BDT_PCI_ADDRESS *Address);
EFI_STATUS EFIAPI BdtPciLink (IN CONST BDT_PCI_ADDRESS *Address);

/**
  Show the AER registers of one device, or of all devices when Address is NULL.
**/
EFI_STATUS EFIAPI BdtPciAer (IN CONST BDT_PCI_ADDRESS *Address OPTIONAL);

/**
  Read one configuration register (Width = 1, 2 or 4 bytes) and show it.
**/
EFI_STATUS EFIAPI BdtPciReadRegister (IN CONST BDT_PCI_ADDRESS *Address, IN UINT32 Offset, IN UINTN Width);

#endif
