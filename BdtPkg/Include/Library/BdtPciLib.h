/** @file
  BdtPciLib - PCI / PCIe inspection through EFI_PCI_IO_PROTOCOL.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_PCI_LIB_H_
#define BDT_PCI_LIB_H_

#include <Uefi.h>
#include <Protocol/PciIo.h>

typedef struct {
  UINT16    Segment;
  UINT8     Bus;
  UINT8     Device;
  UINT8     Function;
} BDT_PCI_ADDRESS;

///
/// One PCI function published by the PCI bus driver.
///
typedef struct {
  EFI_HANDLE             Handle;
  EFI_PCI_IO_PROTOCOL    *PciIo;
  BDT_PCI_ADDRESS        Address;
  UINT16                 VendorId;
  UINT16                 DeviceId;
  UINT8                  BaseClass;
  UINT8                  SubClass;
  UINT8                  ProgIf;
  UINT8                  Revision;
  UINT8                  HeaderType;
  UINT8                  SecondaryBus;     ///< Bridges only.
  UINT8                  SubordinateBus;   ///< Bridges only.
} BDT_PCI_INFO;

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

/**
  Return all PCI functions sorted by address. Free *List with FreePool.
**/
EFI_STATUS EFIAPI BdtPciGetList (OUT BDT_PCI_INFO **List, OUT UINTN *Count);

CONST CHAR16 * EFIAPI BdtPciClassName (IN UINT8 BaseClass, IN UINT8 SubClass);

/**
  Size of the configuration space: 0x1000 for PCI Express, else 0x100.
**/
UINTN EFIAPI BdtPciConfigSize (IN BDT_PCI_INFO *Info);

/**
  Read Count registers of Width (1, 2 or 4) bytes starting at Offset.
**/
EFI_STATUS EFIAPI BdtPciConfigRead (IN BDT_PCI_INFO *Info, IN UINT32 Offset, IN UINTN Width, IN UINTN Count, OUT VOID *Buffer);

/**
  Write one register of Width (1, 2 or 4) bytes at Offset.
**/
EFI_STATUS EFIAPI BdtPciConfigWrite (IN BDT_PCI_INFO *Info, IN UINT32 Offset, IN UINTN Width, IN UINT32 Value);

/**
  Write one configuration register of a device and show old / new values.
**/
EFI_STATUS EFIAPI BdtPciWriteRegister (IN CONST BDT_PCI_ADDRESS *Address, IN UINT32 Offset, IN UINTN Width, IN UINT32 Value);

/**
  Describe the register at Offset: header field name or capability name.
**/
VOID EFIAPI BdtPciDescribeRegister (IN BDT_PCI_INFO *Info, IN UINT32 Offset, OUT CHAR16 *Buffer, IN UINTN BufferSize);

#endif
