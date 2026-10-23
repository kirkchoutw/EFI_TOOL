/** @file
  BdtPciLib - PCI / PCIe inspection through EFI_PCI_IO_PROTOCOL.

  Everything is read only: BAR sizes come from GetBarAttributes() instead of
  the write-ones sizing method.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/Pci.h>
#include <Library/DevicePathLib.h>
#include <Library/BdtPciLib.h>
#include <Protocol/PciIo.h>

#define PCI_CFG_SIZE           0x100
#define PCIE_CFG_SIZE          0x1000
#define PCI_CAP_MAX_WALK       48
#define PCIE_EXT_CAP_MAX_WALK  (PCIE_CFG_SIZE / 4)

//
// Offsets inside the PCI Express capability structure.
//
#define PCIE_CAP_REG           0x02
#define PCIE_DEVICE_STATUS     0x0A
#define PCIE_LINK_CAP          0x0C
#define PCIE_LINK_CONTROL      0x10
#define PCIE_LINK_STATUS       0x12
#define PCIE_LINK_CAP2         0x2C

//
// Offsets inside the AER extended capability.
//
#define AER_UNCORR_STATUS      0x04
#define AER_UNCORR_MASK        0x08
#define AER_UNCORR_SEVERITY    0x0C
#define AER_CORR_STATUS        0x10
#define AER_CORR_MASK          0x14
#define AER_CAP_CONTROL        0x18
#define AER_HEADER_LOG         0x1C
#define AER_ROOT_ERR_STATUS    0x30
#define AER_ERR_SOURCE_ID      0x34
#define PCIE_EXT_CAP_ID_AER    0x0001

typedef struct {
  UINT8           BaseClass;
  UINT8           SubClass;    ///< 0xFF = any
  CONST CHAR16    *Name;
} BDT_PCI_CLASS_NAME;

typedef struct {
  UINT16          Id;
  CONST CHAR16    *Name;
} BDT_ID_NAME;

typedef struct {
  UINT8           Bit;
  CONST CHAR16    *Name;
} BDT_BIT_NAME;

STATIC CONST BDT_PCI_CLASS_NAME  mClassNames[] = {
  { 0x00, 0xFF, L"Unclassified"                 },
  { 0x01, 0x00, L"SCSI Controller"              },
  { 0x01, 0x01, L"IDE Controller"               },
  { 0x01, 0x04, L"RAID Controller"              },
  { 0x01, 0x06, L"SATA Controller"              },
  { 0x01, 0x07, L"SAS Controller"               },
  { 0x01, 0x08, L"NVMe Controller"              },
  { 0x01, 0xFF, L"Mass Storage Controller"      },
  { 0x02, 0x00, L"Ethernet Controller"          },
  { 0x02, 0x80, L"Network Controller"           },
  { 0x02, 0xFF, L"Network Controller"           },
  { 0x03, 0x00, L"VGA Controller"               },
  { 0x03, 0xFF, L"Display Controller"           },
  { 0x04, 0x03, L"Audio Device"                 },
  { 0x04, 0xFF, L"Multimedia Controller"        },
  { 0x05, 0xFF, L"Memory Controller"            },
  { 0x06, 0x00, L"Host Bridge"                  },
  { 0x06, 0x01, L"ISA Bridge"                   },
  { 0x06, 0x04, L"PCI Bridge / Root Port"       },
  { 0x06, 0x80, L"Other Bridge"                 },
  { 0x06, 0xFF, L"Bridge"                       },
  { 0x07, 0xFF, L"Communication Controller"     },
  { 0x08, 0xFF, L"System Peripheral"            },
  { 0x09, 0xFF, L"Input Device"                 },
  { 0x0A, 0xFF, L"Docking Station"              },
  { 0x0B, 0xFF, L"Processor"                    },
  { 0x0C, 0x03, L"USB Controller"               },
  { 0x0C, 0x05, L"SMBus Controller"             },
  { 0x0C, 0x80, L"Serial Bus Controller"        },
  { 0x0C, 0xFF, L"Serial Bus Controller"        },
  { 0x0D, 0xFF, L"Wireless Controller"          },
  { 0x0E, 0xFF, L"Intelligent I/O Controller"   },
  { 0x0F, 0xFF, L"Satellite Controller"         },
  { 0x10, 0xFF, L"Encryption Controller"        },
  { 0x11, 0xFF, L"Signal Processing Controller" },
  { 0x12, 0xFF, L"Processing Accelerator"       },
  { 0x13, 0xFF, L"Non-Essential Instrumentation" },
  { 0xFF, 0xFF, L"Unassigned Class"             },
};

STATIC CONST BDT_ID_NAME  mCapNames[] = {
  { 0x01, L"Power Management"     },
  { 0x02, L"AGP"                  },
  { 0x03, L"VPD"                  },
  { 0x04, L"Slot Identification"  },
  { 0x05, L"MSI"                  },
  { 0x06, L"CompactPCI Hot Swap"  },
  { 0x07, L"PCI-X"                },
  { 0x08, L"HyperTransport"       },
  { 0x09, L"Vendor Specific"      },
  { 0x0A, L"Debug Port"           },
  { 0x0B, L"CompactPCI Central Resource Control" },
  { 0x0C, L"PCI Hot Plug"         },
  { 0x0D, L"Bridge Subsystem Vendor ID" },
  { 0x0E, L"AGP 8x"               },
  { 0x0F, L"Secure Device"        },
  { 0x10, L"PCI Express"          },
  { 0x11, L"MSI-X"                },
  { 0x12, L"SATA Data/Index"      },
  { 0x13, L"Advanced Features"    },
  { 0x14, L"Enhanced Allocation"  },
  { 0x15, L"Flattening Portal Bridge" },
};

STATIC CONST BDT_ID_NAME  mExtCapNames[] = {
  { 0x0001, L"Advanced Error Reporting"         },
  { 0x0002, L"Virtual Channel"                  },
  { 0x0003, L"Device Serial Number"             },
  { 0x0004, L"Power Budgeting"                  },
  { 0x0005, L"Root Complex Link Declaration"    },
  { 0x0006, L"Root Complex Internal Link Control" },
  { 0x0007, L"Root Complex Event Collector Endpoint Association" },
  { 0x0008, L"Multi-Function Virtual Channel"   },
  { 0x0009, L"Virtual Channel (MFVC)"           },
  { 0x000A, L"RCRB Header"                      },
  { 0x000B, L"Vendor Specific"                  },
  { 0x000D, L"Access Control Services"          },
  { 0x000E, L"Alternative Routing-ID"           },
  { 0x000F, L"Address Translation Services"     },
  { 0x0010, L"SR-IOV"                           },
  { 0x0011, L"MR-IOV"                           },
  { 0x0012, L"Multicast"                        },
  { 0x0013, L"Page Request"                     },
  { 0x0015, L"Resizable BAR"                    },
  { 0x0016, L"Dynamic Power Allocation"         },
  { 0x0017, L"TPH Requester"                    },
  { 0x0018, L"Latency Tolerance Reporting"      },
  { 0x0019, L"Secondary PCI Express"            },
  { 0x001A, L"Protocol Multiplexing"            },
  { 0x001B, L"PASID"                            },
  { 0x001C, L"LN Requester"                     },
  { 0x001D, L"Downstream Port Containment"      },
  { 0x001E, L"L1 PM Substates"                  },
  { 0x001F, L"Precision Time Measurement"       },
  { 0x0023, L"Designated Vendor Specific"       },
  { 0x0024, L"VF Resizable BAR"                 },
  { 0x0025, L"Data Link Feature"                },
  { 0x0026, L"Physical Layer 16.0 GT/s"         },
  { 0x0027, L"Lane Margining at the Receiver"   },
  { 0x002A, L"Physical Layer 32.0 GT/s"         },
  { 0x0031, L"Physical Layer 64.0 GT/s"         },
};

STATIC CONST BDT_BIT_NAME  mUncorrectableErrors[] = {
  { 0,  L"Undefined"                      },
  { 4,  L"Data Link Protocol Error"       },
  { 5,  L"Surprise Down Error"            },
  { 12, L"Poisoned TLP"                   },
  { 13, L"Flow Control Protocol Error"    },
  { 14, L"Completion Timeout"             },
  { 15, L"Completer Abort"                },
  { 16, L"Unexpected Completion"          },
  { 17, L"Receiver Overflow"              },
  { 18, L"Malformed TLP"                  },
  { 19, L"ECRC Error"                     },
  { 20, L"Unsupported Request"            },
  { 21, L"ACS Violation"                  },
  { 22, L"Uncorrectable Internal Error"   },
  { 23, L"MC Blocked TLP"                 },
  { 24, L"AtomicOp Egress Blocked"        },
  { 25, L"TLP Prefix Blocked"             },
  { 26, L"Poisoned TLP Egress Blocked"    },
};

STATIC CONST BDT_BIT_NAME  mCorrectableErrors[] = {
  { 0,  L"Receiver Error"                 },
  { 6,  L"Bad TLP"                        },
  { 7,  L"Bad DLLP"                       },
  { 8,  L"REPLAY_NUM Rollover"            },
  { 12, L"Replay Timer Timeout"           },
  { 13, L"Advisory Non-Fatal Error"       },
  { 14, L"Corrected Internal Error"       },
  { 15, L"Header Log Overflow"            },
};

STATIC CONST CHAR16  *mPortTypeNames[] = {
  L"PCI Express Endpoint",
  L"Legacy PCI Express Endpoint",
  L"Reserved",
  L"Reserved",
  L"Root Port",
  L"Upstream Switch Port",
  L"Downstream Switch Port",
  L"PCIe to PCI/PCI-X Bridge",
  L"PCI/PCI-X to PCIe Bridge",
  L"Root Complex Integrated Endpoint",
  L"Root Complex Event Collector",
};

//
// ---------------------------------------------------------------------------
// Enumeration helpers
// ---------------------------------------------------------------------------
//

STATIC
CONST CHAR16 *
ClassName (
  IN UINT8  BaseClass,
  IN UINT8  SubClass
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mClassNames); Index++) {
    if ((mClassNames[Index].BaseClass == BaseClass) &&
        ((mClassNames[Index].SubClass == SubClass) || (mClassNames[Index].SubClass == 0xFF)))
    {
      return mClassNames[Index].Name;
    }
  }

  return L"Unknown";
}

STATIC
CONST CHAR16 *
IdName (
  IN CONST BDT_ID_NAME  *Table,
  IN UINTN              Count,
  IN UINT16             Id
  )
{
  UINTN  Index;

  for (Index = 0; Index < Count; Index++) {
    if (Table[Index].Id == Id) {
      return Table[Index].Name;
    }
  }

  return L"Unknown";
}

STATIC
INTN
CompareAddress (
  IN CONST BDT_PCI_ADDRESS  *A,
  IN CONST BDT_PCI_ADDRESS  *B
  )
{
  UINT32  KeyA;
  UINT32  KeyB;

  if (A->Segment != B->Segment) {
    return (A->Segment < B->Segment) ? -1 : 1;
  }

  KeyA = ((UINT32)A->Bus << 8) | ((UINT32)A->Device << 3) | A->Function;
  KeyB = ((UINT32)B->Bus << 8) | ((UINT32)B->Device << 3) | B->Function;
  return (KeyA < KeyB) ? -1 : ((KeyA > KeyB) ? 1 : 0);
}

/**
  Collect all PCI functions published by the PCI bus driver, sorted by address.
**/
STATIC
EFI_STATUS
GetDevices (
  OUT BDT_PCI_INFO  **Devices,
  OUT UINTN           *Count
  )
{
  EFI_STATUS           Status;
  EFI_HANDLE           *Handles;
  UINTN                HandleCount;
  UINTN                Index;
  UINTN                Valid;
  UINTN                Seg;
  UINTN                Bus;
  UINTN                Dev;
  UINTN                Func;
  UINT32               Header[4];
  UINT32               BusNumbers;
  BDT_PCI_INFO       *List;
  BDT_PCI_INFO       Temp;
  UINTN                Inner;
  EFI_PCI_IO_PROTOCOL  *PciIo;

  *Devices = NULL;
  *Count   = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  List = AllocateZeroPool (HandleCount * sizeof (BDT_PCI_INFO));
  if (List == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Valid = 0;
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = PciIo->GetLocation (PciIo, &Seg, &Bus, &Dev, &Func);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0, ARRAY_SIZE (Header), Header);
    if (EFI_ERROR (Status)) {
      continue;
    }

    List[Valid].Handle           = Handles[Index];
    List[Valid].PciIo            = PciIo;
    List[Valid].Address.Segment  = (UINT16)Seg;
    List[Valid].Address.Bus      = (UINT8)Bus;
    List[Valid].Address.Device   = (UINT8)Dev;
    List[Valid].Address.Function = (UINT8)Func;
    List[Valid].VendorId         = (UINT16)Header[0];
    List[Valid].DeviceId         = (UINT16)(Header[0] >> 16);
    List[Valid].Revision         = (UINT8)Header[2];
    List[Valid].ProgIf           = (UINT8)(Header[2] >> 8);
    List[Valid].SubClass         = (UINT8)(Header[2] >> 16);
    List[Valid].BaseClass        = (UINT8)(Header[2] >> 24);
    List[Valid].HeaderType       = (UINT8)(Header[3] >> 16);

    if ((List[Valid].HeaderType & HEADER_LAYOUT_CODE) == HEADER_TYPE_PCI_TO_PCI_BRIDGE) {
      if (!EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0x18, 1, &BusNumbers))) {
        List[Valid].SecondaryBus   = (UINT8)(BusNumbers >> 8);
        List[Valid].SubordinateBus = (UINT8)(BusNumbers >> 16);
      }
    }

    Valid++;
  }

  FreePool (Handles);

  //
  // Insertion sort; device counts are small.
  //
  for (Index = 1; Index < Valid; Index++) {
    CopyMem (&Temp, &List[Index], sizeof (Temp));
    for (Inner = Index; Inner > 0 && CompareAddress (&List[Inner - 1].Address, &Temp.Address) > 0; Inner--) {
      CopyMem (&List[Inner], &List[Inner - 1], sizeof (Temp));
    }

    CopyMem (&List[Inner], &Temp, sizeof (Temp));
  }

  *Devices = List;
  *Count   = Valid;
  return (Valid == 0) ? EFI_NOT_FOUND : EFI_SUCCESS;
}

STATIC
EFI_STATUS
FindDevice (
  IN  CONST BDT_PCI_ADDRESS  *Address,
  OUT BDT_PCI_INFO         **Devices,
  OUT BDT_PCI_INFO         **Device
  )
{
  EFI_STATUS  Status;
  UINTN       Count;
  UINTN       Index;

  *Device = NULL;
  Status  = GetDevices (Devices, &Count);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"No PCI device found (%s)", BdtStatusName (Status));
    return Status;
  }

  for (Index = 0; Index < Count; Index++) {
    if (CompareAddress (&(*Devices)[Index].Address, Address) == 0) {
      *Device = &(*Devices)[Index];
      return EFI_SUCCESS;
    }
  }

  BdtOutError (
    L"PCI device %04x:%02x:%02x.%x not found",
    Address->Segment,
    Address->Bus,
    Address->Device,
    Address->Function
    );
  FreePool (*Devices);
  *Devices = NULL;
  return EFI_NOT_FOUND;
}

STATIC
VOID
FormatBdf (
  IN  CONST BDT_PCI_ADDRESS  *Address,
  OUT CHAR16                 *Buffer,
  IN  UINTN                  BufferSize
  )
{
  if (Address->Segment != 0) {
    UnicodeSPrint (Buffer, BufferSize, L"%04x:%02x:%02x.%x", Address->Segment, Address->Bus, Address->Device, Address->Function);
  } else {
    UnicodeSPrint (Buffer, BufferSize, L"%02x:%02x.%x", Address->Bus, Address->Device, Address->Function);
  }
}

STATIC
UINT32
ReadConfig32 (
  IN BDT_PCI_INFO  *Device,
  IN UINT32          Offset
  )
{
  UINT32  Value;

  if (EFI_ERROR (Device->PciIo->Pci.Read (Device->PciIo, EfiPciIoWidthUint32, Offset, 1, &Value))) {
    return MAX_UINT32;
  }

  return Value;
}

STATIC
UINT16
ReadConfig16 (
  IN BDT_PCI_INFO  *Device,
  IN UINT32          Offset
  )
{
  UINT16  Value;

  if (EFI_ERROR (Device->PciIo->Pci.Read (Device->PciIo, EfiPciIoWidthUint16, Offset, 1, &Value))) {
    return MAX_UINT16;
  }

  return Value;
}

STATIC
UINT8
ReadConfig8 (
  IN BDT_PCI_INFO  *Device,
  IN UINT32          Offset
  )
{
  UINT8  Value;

  if (EFI_ERROR (Device->PciIo->Pci.Read (Device->PciIo, EfiPciIoWidthUint8, Offset, 1, &Value))) {
    return MAX_UINT8;
  }

  return Value;
}

/**
  Find a standard capability. Returns 0 when absent.
**/
STATIC
UINT8
FindCapability (
  IN BDT_PCI_INFO  *Device,
  IN UINT8           CapId
  )
{
  UINT8  Offset;
  UINTN  Walk;

  if ((ReadConfig16 (Device, PCI_PRIMARY_STATUS_OFFSET) & EFI_PCI_STATUS_CAPABILITY) == 0) {
    return 0;
  }

  Offset = ReadConfig8 (
             Device,
             ((Device->HeaderType & HEADER_LAYOUT_CODE) == HEADER_TYPE_CARDBUS_BRIDGE) ? 0x14 : PCI_CAPBILITY_POINTER_OFFSET
             );
  for (Walk = 0; Walk < PCI_CAP_MAX_WALK && Offset >= 0x40 && Offset != 0xFF; Walk++) {
    Offset &= 0xFC;
    if (ReadConfig8 (Device, Offset) == CapId) {
      return Offset;
    }

    Offset = ReadConfig8 (Device, Offset + 1);
  }

  return 0;
}

/**
  Find a PCIe extended capability. Returns 0 when absent.
**/
STATIC
UINT32
FindExtCapability (
  IN BDT_PCI_INFO  *Device,
  IN UINT16          CapId
  )
{
  UINT32  Offset;
  UINT32  Header;
  UINTN   Walk;

  if (FindCapability (Device, EFI_PCI_CAPABILITY_ID_PCIEXP) == 0) {
    return 0;
  }

  Offset = 0x100;
  for (Walk = 0; Walk < PCIE_EXT_CAP_MAX_WALK && Offset >= 0x100; Walk++) {
    Header = ReadConfig32 (Device, Offset);
    if ((Header == 0) || (Header == MAX_UINT32)) {
      return 0;
    }

    if ((Header & 0xFFFF) == CapId) {
      return Offset;
    }

    Offset = (Header >> 20) & 0xFFC;
  }

  return 0;
}

STATIC
CONST CHAR16 *
LinkSpeedName (
  IN UINTN  Speed
  )
{
  STATIC CONST CHAR16  *Names[] = {
    L"Unknown", L"Gen1 (2.5 GT/s)", L"Gen2 (5.0 GT/s)", L"Gen3 (8.0 GT/s)",
    L"Gen4 (16.0 GT/s)", L"Gen5 (32.0 GT/s)", L"Gen6 (64.0 GT/s)", L"Gen7 (128.0 GT/s)"
  };

  return (Speed < ARRAY_SIZE (Names)) ? Names[Speed] : Names[0];
}

STATIC
VOID
OutErrorBits (
  IN CONST CHAR16        *ListName,
  IN UINT32              Value,
  IN CONST BDT_BIT_NAME  *Table,
  IN UINTN               Count
  )
{
  UINTN  Index;

  BdtOutListBegin (ListName);
  for (Index = 0; Index < Count; Index++) {
    if ((Value & (1u << Table[Index].Bit)) != 0) {
      BdtOutItemBegin (L"[bit %d] %s", Table[Index].Bit, Table[Index].Name);
      if (BdtOutGetFormat () != BdtFormatText) {
        BdtOutFieldNum (L"bit", Table[Index].Bit);
        BdtOutField (L"name", L"%s", Table[Index].Name);
      }

      BdtOutItemEnd ();
    }
  }

  BdtOutListEnd ();
}

//
// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
//

EFI_STATUS
EFIAPI
BdtPciGetDeviceCount (
  OUT UINTN  *Count
  )
{
  EFI_STATUS      Status;
  BDT_PCI_INFO  *Devices;

  Status = GetDevices (&Devices, Count);
  if (Devices != NULL) {
    FreePool (Devices);
  }

  return Status;
}

EFI_STATUS
EFIAPI
BdtPciList (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"bdf",         L"BDF",          14 },
    { L"vendor",      L"VID",          6  },
    { L"device",      L"DID",          6  },
    { L"class",       L"Class",        8  },
    { L"rev",         L"Rev",          5  },
    { L"header",      L"Hdr",          5  },
    { L"description", L"Description",  0  },
  };
  EFI_STATUS      Status;
  BDT_PCI_INFO  *Devices;
  UINTN           Count;
  UINTN           Index;
  CHAR16          Bdf[20];

  Status = GetDevices (&Devices, &Count);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"No PCI device found (%s)", BdtStatusName (Status));
    return Status;
  }

  BdtOutFieldNum (L"Device Count", Count);
  BdtOutTableBegin (L"devices", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < Count; Index++) {
    FormatBdf (&Devices[Index].Address, Bdf, sizeof (Bdf));
    BdtOutRowBegin ();
    BdtOutCell (L"%s", Bdf);
    BdtOutCell (L"%04x", Devices[Index].VendorId);
    BdtOutCell (L"%04x", Devices[Index].DeviceId);
    BdtOutCell (L"%02x%02x%02x", Devices[Index].BaseClass, Devices[Index].SubClass, Devices[Index].ProgIf);
    BdtOutCell (L"%02x", Devices[Index].Revision);
    BdtOutCell (L"%02x", Devices[Index].HeaderType);
    BdtOutCell (L"%s", ClassName (Devices[Index].BaseClass, Devices[Index].SubClass));
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  FreePool (Devices);
  return EFI_SUCCESS;
}

STATIC
VOID
TreeNode (
  IN BDT_PCI_INFO  *Devices,
  IN UINTN           Count,
  IN UINTN           Index,
  IN UINTN           Depth
  )
{
  BDT_PCI_INFO  *Device;
  UINTN           Child;
  BOOLEAN         IsBridge;
  CHAR16          Bdf[20];

  Device   = &Devices[Index];
  IsBridge = (Device->HeaderType & HEADER_LAYOUT_CODE) == HEADER_TYPE_PCI_TO_PCI_BRIDGE;
  FormatBdf (&Device->Address, Bdf, sizeof (Bdf));

  BdtOutItemBegin (
    L"%s%s [%04x:%04x] %s",
    (Depth == 0) ? L"" : L"+-- ",
    Bdf,
    Device->VendorId,
    Device->DeviceId,
    ClassName (Device->BaseClass, Device->SubClass)
    );
  if (BdtOutGetFormat () != BdtFormatText) {
    BdtOutField (L"bdf", L"%s", Bdf);
    BdtOutField (L"vendor", L"%04x", Device->VendorId);
    BdtOutField (L"device", L"%04x", Device->DeviceId);
    BdtOutField (L"description", L"%s", ClassName (Device->BaseClass, Device->SubClass));
  }

  if (IsBridge && (Device->SecondaryBus != 0) && (Depth < 32)) {
    if (BdtOutGetFormat () == BdtFormatText) {
      BdtOutNote (L"bus %02x-%02x", Device->SecondaryBus, Device->SubordinateBus);
    } else {
      BdtOutFieldHex (L"secondary_bus", Device->SecondaryBus, 2);
      BdtOutFieldHex (L"subordinate_bus", Device->SubordinateBus, 2);
    }

    BdtOutListBegin (L"children");
    for (Child = 0; Child < Count; Child++) {
      if ((Devices[Child].Address.Segment == Device->Address.Segment) &&
          (Devices[Child].Address.Bus == Device->SecondaryBus))
      {
        TreeNode (Devices, Count, Child, Depth + 1);
      }
    }

    BdtOutListEnd ();
  }

  BdtOutItemEnd ();
}

EFI_STATUS
EFIAPI
BdtPciTree (
  VOID
  )
{
  EFI_STATUS      Status;
  BDT_PCI_INFO  *Devices;
  UINTN           Count;
  UINTN           Index;
  UINTN           Bridge;
  BOOLEAN         HasParent;

  Status = GetDevices (&Devices, &Count);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"No PCI device found (%s)", BdtStatusName (Status));
    return Status;
  }

  BdtOutListBegin (L"PCI Topology");
  for (Index = 0; Index < Count; Index++) {
    HasParent = FALSE;
    for (Bridge = 0; Bridge < Count; Bridge++) {
      if (((Devices[Bridge].HeaderType & HEADER_LAYOUT_CODE) == HEADER_TYPE_PCI_TO_PCI_BRIDGE) &&
          (Devices[Bridge].SecondaryBus != 0) &&
          (Devices[Bridge].Address.Segment == Devices[Index].Address.Segment) &&
          (Devices[Bridge].SecondaryBus == Devices[Index].Address.Bus))
      {
        HasParent = TRUE;
        break;
      }
    }

    if (!HasParent) {
      TreeNode (Devices, Count, Index, 0);
    }
  }

  BdtOutListEnd ();
  FreePool (Devices);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtPciDump (
  IN CONST BDT_PCI_ADDRESS  *Address,
  IN BOOLEAN                Extended
  )
{
  EFI_STATUS      Status;
  BDT_PCI_INFO  *Devices;
  BDT_PCI_INFO  *Device;
  UINT8           *Config;
  UINTN           Size;
  CHAR16          Bdf[20];

  Status = FindDevice (Address, &Devices, &Device);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Config = AllocateZeroPool (PCIE_CFG_SIZE);
  if (Config == NULL) {
    FreePool (Devices);
    return EFI_OUT_OF_RESOURCES;
  }

  Size = PCI_CFG_SIZE;
  if (Extended && (FindCapability (Device, EFI_PCI_CAPABILITY_ID_PCIEXP) != 0)) {
    Size = PCIE_CFG_SIZE;
  }

  Status = Device->PciIo->Pci.Read (Device->PciIo, EfiPciIoWidthUint32, 0, Size / 4, Config);
  if (EFI_ERROR (Status) && (Size == PCIE_CFG_SIZE)) {
    Size   = PCI_CFG_SIZE;
    Status = Device->PciIo->Pci.Read (Device->PciIo, EfiPciIoWidthUint32, 0, Size / 4, Config);
  }

  if (!EFI_ERROR (Status)) {
    FormatBdf (&Device->Address, Bdf, sizeof (Bdf));
    BdtOutField (L"BDF", L"%s", Bdf);
    BdtOutField (L"Vendor ID", L"%04x", Device->VendorId);
    BdtOutField (L"Device ID", L"%04x", Device->DeviceId);
    BdtOutField (L"Description", L"%s", ClassName (Device->BaseClass, Device->SubClass));
    if (Extended && (Size == PCI_CFG_SIZE)) {
      BdtOutNote (L"Extended configuration space not available, showing 256 bytes.");
    }

    BdtOutHexDump (L"Configuration Space", 0, Config, Size);
  } else {
    BdtOutError (L"Config read failed (%s)", BdtStatusName (Status));
  }

  FreePool (Config);
  FreePool (Devices);
  return Status;
}

EFI_STATUS
EFIAPI
BdtPciCap (
  IN CONST BDT_PCI_ADDRESS  *Address
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"offset",  L"Offset", 8  },
    { L"id",      L"ID",     6  },
    { L"version", L"Ver",    5  },
    { L"name",    L"Name",   0  },
  };
  EFI_STATUS      Status;
  BDT_PCI_INFO  *Devices;
  BDT_PCI_INFO  *Device;
  UINT32          Offset;
  UINT32          Header;
  UINT8           CapId;
  UINTN           Walk;
  CHAR16          Bdf[20];

  Status = FindDevice (Address, &Devices, &Device);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  FormatBdf (&Device->Address, Bdf, sizeof (Bdf));
  BdtOutField (L"BDF", L"%s", Bdf);

  BdtOutTableBegin (L"capabilities", Columns, ARRAY_SIZE (Columns));
  if ((ReadConfig16 (Device, PCI_PRIMARY_STATUS_OFFSET) & EFI_PCI_STATUS_CAPABILITY) != 0) {
    Offset = ReadConfig8 (
               Device,
               ((Device->HeaderType & HEADER_LAYOUT_CODE) == HEADER_TYPE_CARDBUS_BRIDGE) ? 0x14 : PCI_CAPBILITY_POINTER_OFFSET
               );
    for (Walk = 0; Walk < PCI_CAP_MAX_WALK && Offset >= 0x40 && Offset != 0xFF; Walk++) {
      Offset &= 0xFC;
      CapId   = ReadConfig8 (Device, Offset);
      BdtOutRowBegin ();
      BdtOutCell (L"0x%02x", Offset);
      BdtOutCell (L"%02x", CapId);
      BdtOutCell (L"-");
      BdtOutCell (L"%s", IdName (mCapNames, ARRAY_SIZE (mCapNames), CapId));
      BdtOutRowEnd ();
      Offset = ReadConfig8 (Device, Offset + 1);
    }
  }

  BdtOutTableEnd ();

  if (FindCapability (Device, EFI_PCI_CAPABILITY_ID_PCIEXP) != 0) {
    BdtOutTableBegin (L"extended_capabilities", Columns, ARRAY_SIZE (Columns));
    Offset = 0x100;
    for (Walk = 0; Walk < PCIE_EXT_CAP_MAX_WALK && Offset >= 0x100; Walk++) {
      Header = ReadConfig32 (Device, Offset);
      if ((Header == 0) || (Header == MAX_UINT32)) {
        break;
      }

      BdtOutRowBegin ();
      BdtOutCell (L"0x%03x", Offset);
      BdtOutCell (L"%04x", Header & 0xFFFF);
      BdtOutCell (L"%d", (Header >> 16) & 0xF);
      BdtOutCell (L"%s", IdName (mExtCapNames, ARRAY_SIZE (mExtCapNames), (UINT16)(Header & 0xFFFF)));
      BdtOutRowEnd ();
      Offset = (Header >> 20) & 0xFFC;
    }

    BdtOutTableEnd ();
  }

  FreePool (Devices);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtPciBar (
  IN CONST BDT_PCI_ADDRESS  *Address
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"bar",          L"BAR",     5  },
    { L"offset",       L"Offset",  8  },
    { L"raw",          L"Raw",     12 },
    { L"type",         L"Type",    8  },
    { L"prefetchable", L"Pref",    6  },
    { L"address",      L"Address", 20 },
    { L"size",         L"Size",    0  },
  };
  EFI_STATUS                         Status;
  BDT_PCI_INFO                     *Devices;
  BDT_PCI_INFO                     *Device;
  UINTN                              BarCount;
  UINTN                              Bar;
  UINT32                             Raw;
  UINT64                             Base;
  UINT64                             Length;
  BOOLEAN                            Is64;
  BOOLEAN                            IsIo;
  VOID                               *Resources;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Desc;
  CHAR16                             Bdf[20];
  CHAR16                             SizeText[24];

  Status = FindDevice (Address, &Devices, &Device);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  FormatBdf (&Device->Address, Bdf, sizeof (Bdf));
  BdtOutField (L"BDF", L"%s", Bdf);

  switch (Device->HeaderType & HEADER_LAYOUT_CODE) {
    case HEADER_TYPE_DEVICE:
      BarCount = PCI_MAX_BAR;
      break;
    case HEADER_TYPE_PCI_TO_PCI_BRIDGE:
      BarCount = PCI_MAX_BAR - 4;
      break;
    default:
      BarCount = 0;
      break;
  }

  BdtOutTableBegin (L"bars", Columns, ARRAY_SIZE (Columns));
  for (Bar = 0; Bar < BarCount; Bar++) {
    Raw = ReadConfig32 (Device, PCI_BASE_ADDRESSREG_OFFSET + (UINT32)Bar * 4);
    if (Raw == 0) {
      continue;
    }

    IsIo = (Raw & BIT0) != 0;
    Is64 = !IsIo && ((Raw & (BIT2 | BIT1)) == BIT2);
    Base = IsIo ? (Raw & ~(UINT32)0x3) : (Raw & ~(UINT32)0xF);
    if (Is64 && (Bar + 1 < BarCount)) {
      Base |= LShiftU64 (ReadConfig32 (Device, PCI_BASE_ADDRESSREG_OFFSET + (UINT32)(Bar + 1) * 4), 32);
    }

    Length    = 0;
    Resources = NULL;
    if (!EFI_ERROR (Device->PciIo->GetBarAttributes (Device->PciIo, (UINT8)Bar, NULL, &Resources)) && (Resources != NULL)) {
      Desc = (EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *)Resources;
      if (Desc->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR) {
        Length = Desc->AddrLen;
      }

      FreePool (Resources);
    }

    if (Length != 0) {
      BdtFormatSize (Length, SizeText, sizeof (SizeText));
    } else {
      StrCpyS (SizeText, ARRAY_SIZE (SizeText), L"unknown");
    }

    BdtOutRowBegin ();
    BdtOutCell (L"%d", Bar);
    BdtOutCell (L"0x%02x", PCI_BASE_ADDRESSREG_OFFSET + Bar * 4);
    BdtOutCell (L"%08x", Raw);
    BdtOutCell (L"%s", IsIo ? L"IO" : (Is64 ? L"MEM64" : L"MEM32"));
    BdtOutCell (L"%s", (!IsIo && ((Raw & BIT3) != 0)) ? L"Yes" : L"No");
    BdtOutCell (L"0x%lx", Base);
    BdtOutCell (L"%s", SizeText);
    BdtOutRowEnd ();

    if (Is64) {
      Bar++;
    }
  }

  BdtOutTableEnd ();
  FreePool (Devices);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtPciLink (
  IN CONST BDT_PCI_ADDRESS  *Address
  )
{
  EFI_STATUS      Status;
  BDT_PCI_INFO  *Devices;
  BDT_PCI_INFO  *Device;
  UINT8           Cap;
  UINT16          CapReg;
  UINT32          LinkCap;
  UINT32          LinkCap2;
  UINT16          LinkControl;
  UINT16          LinkStatus;
  UINT16          DevStatus;
  UINTN           PortType;
  UINTN           MaxSpeed;
  UINTN           CurSpeed;
  UINTN           MaxWidth;
  UINTN           CurWidth;
  CHAR16          Bdf[20];

  STATIC CONST CHAR16  *AspmNames[] = { L"Disabled", L"L0s", L"L1", L"L0s and L1" };

  Status = FindDevice (Address, &Devices, &Device);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Cap = FindCapability (Device, EFI_PCI_CAPABILITY_ID_PCIEXP);
  if (Cap == 0) {
    BdtOutError (L"Device is not a PCI Express device");
    FreePool (Devices);
    return EFI_UNSUPPORTED;
  }

  CapReg      = ReadConfig16 (Device, Cap + PCIE_CAP_REG);
  DevStatus   = ReadConfig16 (Device, Cap + PCIE_DEVICE_STATUS);
  LinkCap     = ReadConfig32 (Device, Cap + PCIE_LINK_CAP);
  LinkControl = ReadConfig16 (Device, Cap + PCIE_LINK_CONTROL);
  LinkStatus  = ReadConfig16 (Device, Cap + PCIE_LINK_STATUS);
  LinkCap2    = ((CapReg & 0xF) >= 2) ? ReadConfig32 (Device, Cap + PCIE_LINK_CAP2) : 0;
  PortType    = (CapReg >> 4) & 0xF;
  MaxSpeed    = LinkCap & 0xF;
  MaxWidth    = (LinkCap >> 4) & 0x3F;
  CurSpeed    = LinkStatus & 0xF;
  CurWidth    = (LinkStatus >> 4) & 0x3F;

  FormatBdf (&Device->Address, Bdf, sizeof (Bdf));
  BdtOutField (L"Device", L"%s", Bdf);
  BdtOutField (L"Port Type", L"%s", (PortType < ARRAY_SIZE (mPortTypeNames)) ? mPortTypeNames[PortType] : L"Reserved");
  BdtOutFieldNum (L"Capability Version", CapReg & 0xF);
  BdtOutFieldNum (L"Port Number", LinkCap >> 24);
  BdtOutField (L"Max Speed", L"%s", LinkSpeedName (MaxSpeed));
  BdtOutField (L"Current Speed", L"%s", LinkSpeedName (CurSpeed));
  BdtOutField (L"Max Width", L"x%d", MaxWidth);
  BdtOutField (L"Current Width", L"x%d", CurWidth);
  if (LinkCap2 != 0) {
    BdtOutFieldHex (L"Supported Speeds Vector", (LinkCap2 >> 1) & 0x7F, 2);
  }

  BdtOutField (L"ASPM Support", L"%s", AspmNames[(LinkCap >> 10) & 0x3]);
  BdtOutField (L"ASPM Control", L"%s", AspmNames[LinkControl & 0x3]);
  BdtOutFieldBool (L"Link Disabled", (LinkControl & BIT4) != 0);
  BdtOutFieldBool (L"Link Training", (LinkStatus & BIT11) != 0);
  BdtOutFieldBool (L"Data Link Layer Active", (LinkStatus & BIT13) != 0);
  BdtOutFieldBool (L"Speed Downgraded", CurSpeed < MaxSpeed);
  BdtOutFieldBool (L"Width Downgraded", CurWidth < MaxWidth);
  BdtOutFieldBool (L"Correctable Error Detected", (DevStatus & BIT0) != 0);
  BdtOutFieldBool (L"Non-Fatal Error Detected", (DevStatus & BIT1) != 0);
  BdtOutFieldBool (L"Fatal Error Detected", (DevStatus & BIT2) != 0);
  BdtOutFieldBool (L"Unsupported Request Detected", (DevStatus & BIT3) != 0);

  FreePool (Devices);
  return EFI_SUCCESS;
}

STATIC
VOID
AerDetail (
  IN BDT_PCI_INFO  *Device,
  IN UINT32          Aer
  )
{
  UINT32  Uncorr;
  UINT32  Severity;
  UINT32  Corr;
  UINT32  Control;
  UINT8   PortType;
  UINT8   Cap;

  Uncorr   = ReadConfig32 (Device, Aer + AER_UNCORR_STATUS);
  Severity = ReadConfig32 (Device, Aer + AER_UNCORR_SEVERITY);
  Corr     = ReadConfig32 (Device, Aer + AER_CORR_STATUS);
  Control  = ReadConfig32 (Device, Aer + AER_CAP_CONTROL);

  BdtOutFieldHex (L"AER Offset", Aer, 3);
  BdtOutFieldHex (L"Uncorrectable Status", Uncorr, 8);
  BdtOutFieldHex (L"Uncorrectable Mask", ReadConfig32 (Device, Aer + AER_UNCORR_MASK), 8);
  BdtOutFieldHex (L"Uncorrectable Severity", Severity, 8);
  BdtOutFieldHex (L"Correctable Status", Corr, 8);
  BdtOutFieldHex (L"Correctable Mask", ReadConfig32 (Device, Aer + AER_CORR_MASK), 8);
  BdtOutFieldNum (L"First Error Pointer", Control & 0x1F);
  BdtOutFieldBool (L"Correctable Error", Corr != 0);
  BdtOutFieldBool (L"Uncorrectable Error", (Uncorr & ~Severity) != 0);
  BdtOutFieldBool (L"Fatal Error", (Uncorr & Severity) != 0);
  BdtOutField (
    L"Header Log",
    L"%08x %08x %08x %08x",
    ReadConfig32 (Device, Aer + AER_HEADER_LOG),
    ReadConfig32 (Device, Aer + AER_HEADER_LOG + 4),
    ReadConfig32 (Device, Aer + AER_HEADER_LOG + 8),
    ReadConfig32 (Device, Aer + AER_HEADER_LOG + 12)
    );

  Cap      = FindCapability (Device, EFI_PCI_CAPABILITY_ID_PCIEXP);
  PortType = (Cap != 0) ? (UINT8)((ReadConfig16 (Device, Cap + PCIE_CAP_REG) >> 4) & 0xF) : 0;
  if ((PortType == 4) || (PortType == 0xA)) {
    BdtOutFieldHex (L"Root Error Status", ReadConfig32 (Device, Aer + AER_ROOT_ERR_STATUS), 8);
    BdtOutFieldHex (L"Error Source ID", ReadConfig32 (Device, Aer + AER_ERR_SOURCE_ID), 8);
  }

  OutErrorBits (L"Uncorrectable Errors", Uncorr, mUncorrectableErrors, ARRAY_SIZE (mUncorrectableErrors));
  OutErrorBits (L"Correctable Errors", Corr, mCorrectableErrors, ARRAY_SIZE (mCorrectableErrors));
}

EFI_STATUS
EFIAPI
BdtPciAer (
  IN CONST BDT_PCI_ADDRESS  *Address OPTIONAL
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"bdf",           L"BDF",           14 },
    { L"uncorrectable", L"UncorrStatus",  14 },
    { L"severity",      L"Severity",      10 },
    { L"correctable",   L"CorrStatus",    12 },
    { L"result",        L"Result",        0  },
  };
  EFI_STATUS      Status;
  BDT_PCI_INFO  *Devices;
  BDT_PCI_INFO  *Device;
  UINTN           Count;
  UINTN           Index;
  UINT32          Aer;
  UINT32          Uncorr;
  UINT32          Severity;
  UINT32          Corr;
  CHAR16          Bdf[20];

  if (Address != NULL) {
    Status = FindDevice (Address, &Devices, &Device);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    FormatBdf (&Device->Address, Bdf, sizeof (Bdf));
    BdtOutField (L"Device", L"%s", Bdf);
    Aer = FindExtCapability (Device, PCIE_EXT_CAP_ID_AER);
    if (Aer == 0) {
      BdtOutError (L"Device %s has no AER capability", Bdf);
      FreePool (Devices);
      return EFI_UNSUPPORTED;
    }

    AerDetail (Device, Aer);
    FreePool (Devices);
    return EFI_SUCCESS;
  }

  Status = GetDevices (&Devices, &Count);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"No PCI device found (%s)", BdtStatusName (Status));
    return Status;
  }

  BdtOutTableBegin (L"aer", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < Count; Index++) {
    Aer = FindExtCapability (&Devices[Index], PCIE_EXT_CAP_ID_AER);
    if (Aer == 0) {
      continue;
    }

    Uncorr   = ReadConfig32 (&Devices[Index], Aer + AER_UNCORR_STATUS);
    Severity = ReadConfig32 (&Devices[Index], Aer + AER_UNCORR_SEVERITY);
    Corr     = ReadConfig32 (&Devices[Index], Aer + AER_CORR_STATUS);
    FormatBdf (&Devices[Index].Address, Bdf, sizeof (Bdf));
    BdtOutRowBegin ();
    BdtOutCell (L"%s", Bdf);
    BdtOutCell (L"%08x", Uncorr);
    BdtOutCell (L"%08x", Severity);
    BdtOutCell (L"%08x", Corr);
    BdtOutCell (
      L"%s",
      ((Uncorr & Severity) != 0) ? L"FATAL" : ((Uncorr != 0) ? L"UNCORRECTABLE" : ((Corr != 0) ? L"CORRECTABLE" : L"CLEAN"))
      );
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  FreePool (Devices);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtPciReadRegister (
  IN CONST BDT_PCI_ADDRESS  *Address,
  IN UINT32                 Offset,
  IN UINTN                  Width
  )
{
  EFI_STATUS      Status;
  BDT_PCI_INFO  *Devices;
  BDT_PCI_INFO  *Device;
  UINT32          Value;
  CHAR16          Bdf[20];

  if (((Width != 1) && (Width != 2) && (Width != 4)) || ((Offset & (Width - 1)) != 0) || (Offset + Width > PCIE_CFG_SIZE)) {
    BdtOutError (L"Offset must be aligned to the width (1, 2 or 4) and below 0x1000");
    return EFI_INVALID_PARAMETER;
  }

  Status = FindDevice (Address, &Devices, &Device);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Value  = 0;
  Status = Device->PciIo->Pci.Read (
                                Device->PciIo,
                                (Width == 1) ? EfiPciIoWidthUint8 : ((Width == 2) ? EfiPciIoWidthUint16 : EfiPciIoWidthUint32),
                                Offset,
                                1,
                                &Value
                                );
  if (EFI_ERROR (Status)) {
    BdtOutError (L"Config read failed (%s)", BdtStatusName (Status));
  } else {
    FormatBdf (&Device->Address, Bdf, sizeof (Bdf));
    BdtOutField (L"BDF", L"%s", Bdf);
    BdtOutFieldHex (L"Offset", Offset, 3);
    BdtOutFieldNum (L"Width", Width);
    BdtOutFieldHex (L"Value", Value, Width * 2);
  }

  FreePool (Devices);
  return Status;
}

EFI_STATUS
EFIAPI
BdtPciGetList (
  OUT BDT_PCI_INFO  **List,
  OUT UINTN         *Count
  )
{
  return GetDevices (List, Count);
}

CONST CHAR16 *
EFIAPI
BdtPciClassName (
  IN UINT8  BaseClass,
  IN UINT8  SubClass
  )
{
  return ClassName (BaseClass, SubClass);
}

UINTN
EFIAPI
BdtPciConfigSize (
  IN BDT_PCI_INFO  *Info
  )
{
  return (FindCapability (Info, EFI_PCI_CAPABILITY_ID_PCIEXP) != 0) ? PCIE_CFG_SIZE : PCI_CFG_SIZE;
}

EFI_STATUS
EFIAPI
BdtPciConfigRead (
  IN  BDT_PCI_INFO  *Info,
  IN  UINT32        Offset,
  IN  UINTN         Width,
  IN  UINTN         Count,
  OUT VOID          *Buffer
  )
{
  EFI_PCI_IO_PROTOCOL_WIDTH  IoWidth;

  switch (Width) {
    case 1:
      IoWidth = EfiPciIoWidthUint8;
      break;
    case 2:
      IoWidth = EfiPciIoWidthUint16;
      break;
    case 4:
      IoWidth = EfiPciIoWidthUint32;
      break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  if (((Offset & (Width - 1)) != 0) || (Offset + Width * Count > PCIE_CFG_SIZE)) {
    return EFI_INVALID_PARAMETER;
  }

  return Info->PciIo->Pci.Read (Info->PciIo, IoWidth, Offset, Count, Buffer);
}

typedef struct {
  UINT8           Offset;
  UINT8           Size;
  CONST CHAR16    *Name;
} BDT_PCI_FIELD;

STATIC CONST BDT_PCI_FIELD  mCommonFields[] = {
  { 0x00, 2, L"Vendor ID"       },
  { 0x02, 2, L"Device ID"       },
  { 0x04, 2, L"Command"         },
  { 0x06, 2, L"Status"          },
  { 0x08, 1, L"Revision ID"     },
  { 0x09, 1, L"Prog Interface"  },
  { 0x0A, 1, L"Sub Class"       },
  { 0x0B, 1, L"Base Class"      },
  { 0x0C, 1, L"Cache Line Size" },
  { 0x0D, 1, L"Latency Timer"   },
  { 0x0E, 1, L"Header Type"     },
  { 0x0F, 1, L"BIST"            },
  { 0x10, 4, L"BAR0"            },
  { 0x14, 4, L"BAR1"            },
  { 0x34, 1, L"Capabilities Pointer" },
  { 0x3C, 1, L"Interrupt Line"  },
  { 0x3D, 1, L"Interrupt Pin"   },
};

STATIC CONST BDT_PCI_FIELD  mType0Fields[] = {
  { 0x18, 4, L"BAR2"                  },
  { 0x1C, 4, L"BAR3"                  },
  { 0x20, 4, L"BAR4"                  },
  { 0x24, 4, L"BAR5"                  },
  { 0x28, 4, L"CardBus CIS Pointer"   },
  { 0x2C, 2, L"Subsystem Vendor ID"   },
  { 0x2E, 2, L"Subsystem ID"          },
  { 0x30, 4, L"Expansion ROM Base"    },
  { 0x3E, 1, L"Min Grant"             },
  { 0x3F, 1, L"Max Latency"           },
};

STATIC CONST BDT_PCI_FIELD  mType1Fields[] = {
  { 0x18, 1, L"Primary Bus"           },
  { 0x19, 1, L"Secondary Bus"         },
  { 0x1A, 1, L"Subordinate Bus"       },
  { 0x1B, 1, L"Secondary Latency"     },
  { 0x1C, 1, L"I/O Base"              },
  { 0x1D, 1, L"I/O Limit"             },
  { 0x1E, 2, L"Secondary Status"      },
  { 0x20, 2, L"Memory Base"           },
  { 0x22, 2, L"Memory Limit"          },
  { 0x24, 2, L"Prefetchable Base"     },
  { 0x26, 2, L"Prefetchable Limit"    },
  { 0x28, 4, L"Prefetchable Base Upper 32" },
  { 0x2C, 4, L"Prefetchable Limit Upper 32" },
  { 0x30, 2, L"I/O Base Upper 16"     },
  { 0x32, 2, L"I/O Limit Upper 16"    },
  { 0x38, 4, L"Expansion ROM Base"    },
  { 0x3E, 2, L"Bridge Control"        },
};

STATIC
CONST CHAR16 *
FieldName (
  IN CONST BDT_PCI_FIELD  *Fields,
  IN UINTN                Count,
  IN UINT32               Offset
  )
{
  UINTN  Index;

  for (Index = 0; Index < Count; Index++) {
    if ((Offset >= Fields[Index].Offset) && (Offset < (UINT32)Fields[Index].Offset + Fields[Index].Size)) {
      return Fields[Index].Name;
    }
  }

  return NULL;
}

VOID
EFIAPI
BdtPciDescribeRegister (
  IN  BDT_PCI_INFO  *Info,
  IN  UINT32        Offset,
  OUT CHAR16        *Buffer,
  IN  UINTN         BufferSize
  )
{
  CONST CHAR16  *Name;
  UINT32        Cap;
  UINT32        Next;
  UINT32        Header;
  UINTN         Walk;

  Buffer[0] = L'\0';
  if (Offset < 0x40) {
    Name = FieldName (mCommonFields, ARRAY_SIZE (mCommonFields), Offset);
    if (Name == NULL) {
      if ((Info->HeaderType & HEADER_LAYOUT_CODE) == HEADER_TYPE_PCI_TO_PCI_BRIDGE) {
        Name = FieldName (mType1Fields, ARRAY_SIZE (mType1Fields), Offset);
      } else if ((Info->HeaderType & HEADER_LAYOUT_CODE) == HEADER_TYPE_DEVICE) {
        Name = FieldName (mType0Fields, ARRAY_SIZE (mType0Fields), Offset);
      }
    }

    StrCpyS (Buffer, BufferSize / sizeof (CHAR16), (Name != NULL) ? Name : L"Reserved");
    return;
  }

  //
  // Standard capabilities: the capability containing Offset is the one with
  // the highest start below Offset.
  //
  if (Offset < 0x100) {
    if ((ReadConfig16 (Info, PCI_PRIMARY_STATUS_OFFSET) & EFI_PCI_STATUS_CAPABILITY) == 0) {
      return;
    }

    Cap  = 0;
    Next = ReadConfig8 (Info, PCI_CAPBILITY_POINTER_OFFSET);
    for (Walk = 0; Walk < PCI_CAP_MAX_WALK && Next >= 0x40 && Next != 0xFF; Walk++) {
      Next &= 0xFC;
      if ((Next <= Offset) && (Next > Cap)) {
        Cap = Next;
      }

      Next = ReadConfig8 (Info, Next + 1);
    }

    if (Cap != 0) {
      UnicodeSPrint (
        Buffer,
        BufferSize,
        L"%s capability +0x%02x",
        IdName (mCapNames, ARRAY_SIZE (mCapNames), ReadConfig8 (Info, Cap)),
        Offset - Cap
        );
    }

    return;
  }

  if (FindCapability (Info, EFI_PCI_CAPABILITY_ID_PCIEXP) == 0) {
    return;
  }

  Cap  = 0;
  Next = 0x100;
  for (Walk = 0; Walk < PCIE_EXT_CAP_MAX_WALK && Next >= 0x100; Walk++) {
    Header = ReadConfig32 (Info, Next);
    if ((Header == 0) || (Header == MAX_UINT32)) {
      break;
    }

    if ((Next <= Offset) && (Next > Cap)) {
      Cap = Next;
    }

    Next = (Header >> 20) & 0xFFC;
  }

  if (Cap != 0) {
    UnicodeSPrint (
      Buffer,
      BufferSize,
      L"%s +0x%03x",
      IdName (mExtCapNames, ARRAY_SIZE (mExtCapNames), (UINT16)(ReadConfig32 (Info, Cap) & 0xFFFF)),
      Offset - Cap
      );
  }
}
