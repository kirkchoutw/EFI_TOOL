/** @file
  BdtLib - common helpers (argument parsing, GUID names, variables).

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>

#include <Guid/Acpi.h>
#include <Guid/DebugImageInfoTable.h>
#include <Guid/DxeServices.h>
#include <Guid/GlobalVariable.h>
#include <Guid/HobList.h>
#include <Guid/ImageAuthentication.h>
#include <Guid/MemoryAttributesTable.h>
#include <Guid/SmBios.h>
#include <Guid/WinCertificate.h>

#include <Protocol/AcpiTable.h>
#include <Protocol/AtaPassThru.h>
#include <Protocol/BlockIo.h>
#include <Protocol/BlockIo2.h>
#include <Protocol/BusSpecificDriverOverride.h>
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/Cpu.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DevicePathToText.h>
#include <Protocol/DevicePathUtilities.h>
#include <Protocol/DiskIo.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/DriverDiagnostics2.h>
#include <Protocol/EdidActive.h>
#include <Protocol/EdidDiscovered.h>
#include <Protocol/ScsiPassThruExt.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/FirmwareVolumeBlock.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiConfigRouting.h>
#include <Protocol/HiiDatabase.h>
#include <Protocol/HiiPackageList.h>
#include <Protocol/LoadFile.h>
#include <Protocol/LoadFile2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/MpService.h>
#include <Protocol/NvmExpressPassthru.h>
#include <Protocol/PciIo.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Protocol/Rng.h>
#include <Protocol/SerialIo.h>
#include <Protocol/Shell.h>
#include <Protocol/ShellParameters.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/Smbios.h>
#include <Protocol/StorageSecurityCommand.h>
#include <Protocol/Tcg2Protocol.h>
#include <Protocol/Timer.h>
#include <Protocol/UsbIo.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/Variable.h>
#include <Protocol/VariableWrite.h>
#include <Protocol/Security.h>
#include <Protocol/Security2.h>
#include <Protocol/Runtime.h>
#include <Protocol/Bds.h>
#include <Protocol/Reset.h>
#include <Protocol/RealTimeClock.h>
#include <Protocol/MonotonicCounter.h>
#include <Protocol/WatchdogTimer.h>
#include <Protocol/Capsule.h>
#include <Protocol/Metronome.h>
#include <Protocol/SmmBase2.h>

typedef struct {
  EFI_GUID        Guid;
  CONST CHAR16    *Name;
} BDT_KNOWN_GUID;

STATIC CONST BDT_KNOWN_GUID  mKnownGuids[] = {
  //
  // Protocols
  //
  { EFI_LOADED_IMAGE_PROTOCOL_GUID,                L"EFI_LOADED_IMAGE_PROTOCOL"                },
  { EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID,    L"EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL"    },
  { EFI_DEVICE_PATH_PROTOCOL_GUID,                 L"EFI_DEVICE_PATH_PROTOCOL"                 },
  { EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID,         L"EFI_DEVICE_PATH_TO_TEXT_PROTOCOL"         },
  { EFI_DEVICE_PATH_UTILITIES_PROTOCOL_GUID,       L"EFI_DEVICE_PATH_UTILITIES_PROTOCOL"       },
  { EFI_DRIVER_BINDING_PROTOCOL_GUID,              L"EFI_DRIVER_BINDING_PROTOCOL"              },
  { EFI_COMPONENT_NAME_PROTOCOL_GUID,              L"EFI_COMPONENT_NAME_PROTOCOL"              },
  { EFI_COMPONENT_NAME2_PROTOCOL_GUID,             L"EFI_COMPONENT_NAME2_PROTOCOL"             },
  { EFI_DRIVER_DIAGNOSTICS2_PROTOCOL_GUID,         L"EFI_DRIVER_DIAGNOSTICS2_PROTOCOL"         },
  { EFI_BUS_SPECIFIC_DRIVER_OVERRIDE_PROTOCOL_GUID, L"EFI_BUS_SPECIFIC_DRIVER_OVERRIDE_PROTOCOL" },
  { EFI_PCI_IO_PROTOCOL_GUID,                      L"EFI_PCI_IO_PROTOCOL"                      },
  { EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GUID,          L"EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL"          },
  { EFI_BLOCK_IO_PROTOCOL_GUID,                    L"EFI_BLOCK_IO_PROTOCOL"                    },
  { EFI_BLOCK_IO2_PROTOCOL_GUID,                   L"EFI_BLOCK_IO2_PROTOCOL"                   },
  { EFI_DISK_IO_PROTOCOL_GUID,                     L"EFI_DISK_IO_PROTOCOL"                     },
  { EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,          L"EFI_SIMPLE_FILE_SYSTEM_PROTOCOL"          },
  { EFI_STORAGE_SECURITY_COMMAND_PROTOCOL_GUID,    L"EFI_STORAGE_SECURITY_COMMAND_PROTOCOL"    },
  { EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID,       L"EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL"       },
  { EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID,          L"EFI_EXT_SCSI_PASS_THRU_PROTOCOL"          },
  { EFI_ATA_PASS_THRU_PROTOCOL_GUID,               L"EFI_ATA_PASS_THRU_PROTOCOL"               },
  { EFI_USB_IO_PROTOCOL_GUID,                      L"EFI_USB_IO_PROTOCOL"                      },
  { EFI_USB2_HC_PROTOCOL_GUID,                     L"EFI_USB2_HC_PROTOCOL"                     },
  { EFI_SIMPLE_NETWORK_PROTOCOL_GUID,              L"EFI_SIMPLE_NETWORK_PROTOCOL"              },
  { EFI_LOAD_FILE_PROTOCOL_GUID,                   L"EFI_LOAD_FILE_PROTOCOL"                   },
  { EFI_LOAD_FILE2_PROTOCOL_GUID,                  L"EFI_LOAD_FILE2_PROTOCOL"                  },
  { EFI_SERIAL_IO_PROTOCOL_GUID,                   L"EFI_SERIAL_IO_PROTOCOL"                   },
  { EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID,           L"EFI_SIMPLE_TEXT_INPUT_PROTOCOL"           },
  { EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID,        L"EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL"        },
  { EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,          L"EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL"          },
  { EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,             L"EFI_GRAPHICS_OUTPUT_PROTOCOL"             },
  { EFI_EDID_ACTIVE_PROTOCOL_GUID,                 L"EFI_EDID_ACTIVE_PROTOCOL"                 },
  { EFI_EDID_DISCOVERED_PROTOCOL_GUID,             L"EFI_EDID_DISCOVERED_PROTOCOL"             },
  { EFI_HII_DATABASE_PROTOCOL_GUID,                L"EFI_HII_DATABASE_PROTOCOL"                },
  { EFI_HII_CONFIG_ROUTING_PROTOCOL_GUID,          L"EFI_HII_CONFIG_ROUTING_PROTOCOL"          },
  { EFI_HII_PACKAGE_LIST_PROTOCOL_GUID,            L"EFI_HII_PACKAGE_LIST_PROTOCOL"            },
  { EFI_ACPI_TABLE_PROTOCOL_GUID,                  L"EFI_ACPI_TABLE_PROTOCOL"                  },
  { EFI_SMBIOS_PROTOCOL_GUID,                      L"EFI_SMBIOS_PROTOCOL"                      },
  { EFI_FIRMWARE_VOLUME2_PROTOCOL_GUID,            L"EFI_FIRMWARE_VOLUME2_PROTOCOL"            },
  { EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL_GUID,       L"EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL"       },
  { EFI_SHELL_PROTOCOL_GUID,                       L"EFI_SHELL_PROTOCOL"                       },
  { EFI_SHELL_PARAMETERS_PROTOCOL_GUID,            L"EFI_SHELL_PARAMETERS_PROTOCOL"            },
  { EFI_TCG2_PROTOCOL_GUID,                        L"EFI_TCG2_PROTOCOL"                        },
  { EFI_MP_SERVICES_PROTOCOL_GUID,                 L"EFI_MP_SERVICES_PROTOCOL"                 },
  { EFI_RNG_PROTOCOL_GUID,                         L"EFI_RNG_PROTOCOL"                         },
  { EFI_CPU_ARCH_PROTOCOL_GUID,                    L"EFI_CPU_ARCH_PROTOCOL"                    },
  { EFI_TIMER_ARCH_PROTOCOL_GUID,                  L"EFI_TIMER_ARCH_PROTOCOL"                  },
  { EFI_METRONOME_ARCH_PROTOCOL_GUID,              L"EFI_METRONOME_ARCH_PROTOCOL"              },
  { EFI_VARIABLE_ARCH_PROTOCOL_GUID,               L"EFI_VARIABLE_ARCH_PROTOCOL"               },
  { EFI_VARIABLE_WRITE_ARCH_PROTOCOL_GUID,         L"EFI_VARIABLE_WRITE_ARCH_PROTOCOL"         },
  { EFI_SECURITY_ARCH_PROTOCOL_GUID,               L"EFI_SECURITY_ARCH_PROTOCOL"               },
  { EFI_SECURITY2_ARCH_PROTOCOL_GUID,              L"EFI_SECURITY2_ARCH_PROTOCOL"              },
  { EFI_RUNTIME_ARCH_PROTOCOL_GUID,                L"EFI_RUNTIME_ARCH_PROTOCOL"                },
  { EFI_BDS_ARCH_PROTOCOL_GUID,                    L"EFI_BDS_ARCH_PROTOCOL"                    },
  { EFI_RESET_ARCH_PROTOCOL_GUID,                  L"EFI_RESET_ARCH_PROTOCOL"                  },
  { EFI_REAL_TIME_CLOCK_ARCH_PROTOCOL_GUID,        L"EFI_REAL_TIME_CLOCK_ARCH_PROTOCOL"        },
  { EFI_MONOTONIC_COUNTER_ARCH_PROTOCOL_GUID,      L"EFI_MONOTONIC_COUNTER_ARCH_PROTOCOL"      },
  { EFI_WATCHDOG_TIMER_ARCH_PROTOCOL_GUID,         L"EFI_WATCHDOG_TIMER_ARCH_PROTOCOL"         },
  { EFI_CAPSULE_ARCH_PROTOCOL_GUID,                L"EFI_CAPSULE_ARCH_PROTOCOL"                },
  { EFI_SMM_BASE2_PROTOCOL_GUID,                   L"EFI_SMM_BASE2_PROTOCOL"                   },

  //
  // GUIDs: variables, configuration tables, signature types
  //
  { EFI_GLOBAL_VARIABLE,                           L"EFI_GLOBAL_VARIABLE"                      },
  { EFI_IMAGE_SECURITY_DATABASE_GUID,              L"EFI_IMAGE_SECURITY_DATABASE"              },
  { EFI_ACPI_20_TABLE_GUID,                        L"EFI_ACPI_20_TABLE"                        },
  { ACPI_TABLE_GUID,                               L"ACPI_TABLE"                               },
  { SMBIOS_TABLE_GUID,                             L"SMBIOS_TABLE"                             },
  { SMBIOS3_TABLE_GUID,                            L"SMBIOS3_TABLE"                            },
  { EFI_DEBUG_IMAGE_INFO_TABLE_GUID,               L"EFI_DEBUG_IMAGE_INFO_TABLE"               },
  { EFI_MEMORY_ATTRIBUTES_TABLE_GUID,              L"EFI_MEMORY_ATTRIBUTES_TABLE"              },
  { HOB_LIST_GUID,                                 L"HOB_LIST"                                 },
  { DXE_SERVICES_TABLE_GUID,                       L"DXE_SERVICES_TABLE"                       },
  { EFI_TCG2_FINAL_EVENTS_TABLE_GUID,              L"EFI_TCG2_FINAL_EVENTS_TABLE"              },
  { EFI_CERT_SHA1_GUID,                            L"EFI_CERT_SHA1"                            },
  { EFI_CERT_SHA256_GUID,                          L"EFI_CERT_SHA256"                          },
  { EFI_CERT_SHA384_GUID,                          L"EFI_CERT_SHA384"                          },
  { EFI_CERT_SHA512_GUID,                          L"EFI_CERT_SHA512"                          },
  { EFI_CERT_RSA2048_GUID,                         L"EFI_CERT_RSA2048"                         },
  { EFI_CERT_RSA2048_SHA256_GUID,                  L"EFI_CERT_RSA2048_SHA256"                  },
  { EFI_CERT_X509_GUID,                            L"EFI_CERT_X509"                            },
  { EFI_CERT_X509_SHA256_GUID,                     L"EFI_CERT_X509_SHA256"                     },
  { EFI_CERT_X509_SHA384_GUID,                     L"EFI_CERT_X509_SHA384"                     },
  { EFI_CERT_X509_SHA512_GUID,                     L"EFI_CERT_X509_SHA512"                     },
  { EFI_CERT_TYPE_PKCS7_GUID,                      L"EFI_CERT_TYPE_PKCS7"                      },
};

typedef struct {
  EFI_STATUS      Status;
  CONST CHAR16    *Name;
} BDT_STATUS_NAME;

STATIC CONST BDT_STATUS_NAME  mStatusNames[] = {
  { EFI_SUCCESS,              L"EFI_SUCCESS"              },
  { EFI_LOAD_ERROR,           L"EFI_LOAD_ERROR"           },
  { EFI_INVALID_PARAMETER,    L"EFI_INVALID_PARAMETER"    },
  { EFI_UNSUPPORTED,          L"EFI_UNSUPPORTED"          },
  { EFI_BAD_BUFFER_SIZE,      L"EFI_BAD_BUFFER_SIZE"      },
  { EFI_BUFFER_TOO_SMALL,     L"EFI_BUFFER_TOO_SMALL"     },
  { EFI_NOT_READY,            L"EFI_NOT_READY"            },
  { EFI_DEVICE_ERROR,         L"EFI_DEVICE_ERROR"         },
  { EFI_WRITE_PROTECTED,      L"EFI_WRITE_PROTECTED"      },
  { EFI_OUT_OF_RESOURCES,     L"EFI_OUT_OF_RESOURCES"     },
  { EFI_VOLUME_CORRUPTED,     L"EFI_VOLUME_CORRUPTED"     },
  { EFI_VOLUME_FULL,          L"EFI_VOLUME_FULL"          },
  { EFI_NO_MEDIA,             L"EFI_NO_MEDIA"             },
  { EFI_MEDIA_CHANGED,        L"EFI_MEDIA_CHANGED"        },
  { EFI_NOT_FOUND,            L"EFI_NOT_FOUND"            },
  { EFI_ACCESS_DENIED,        L"EFI_ACCESS_DENIED"        },
  { EFI_NO_RESPONSE,          L"EFI_NO_RESPONSE"          },
  { EFI_NO_MAPPING,           L"EFI_NO_MAPPING"           },
  { EFI_TIMEOUT,              L"EFI_TIMEOUT"              },
  { EFI_NOT_STARTED,          L"EFI_NOT_STARTED"          },
  { EFI_ALREADY_STARTED,      L"EFI_ALREADY_STARTED"      },
  { EFI_ABORTED,              L"EFI_ABORTED"              },
  { EFI_ICMP_ERROR,           L"EFI_ICMP_ERROR"           },
  { EFI_TFTP_ERROR,           L"EFI_TFTP_ERROR"           },
  { EFI_PROTOCOL_ERROR,       L"EFI_PROTOCOL_ERROR"       },
  { EFI_INCOMPATIBLE_VERSION, L"EFI_INCOMPATIBLE_VERSION" },
  { EFI_SECURITY_VIOLATION,   L"EFI_SECURITY_VIOLATION"   },
  { EFI_CRC_ERROR,            L"EFI_CRC_ERROR"            },
  { EFI_END_OF_MEDIA,         L"EFI_END_OF_MEDIA"         },
  { EFI_END_OF_FILE,          L"EFI_END_OF_FILE"          },
  { EFI_INVALID_LANGUAGE,     L"EFI_INVALID_LANGUAGE"     },
  { EFI_COMPROMISED_DATA,     L"EFI_COMPROMISED_DATA"     },
};

STATIC
BOOLEAN
HexDigit (
  IN  CHAR16  Char,
  OUT UINTN   *Value
  )
{
  if ((Char >= L'0') && (Char <= L'9')) {
    *Value = Char - L'0';
  } else if ((Char >= L'a') && (Char <= L'f')) {
    *Value = Char - L'a' + 10;
  } else if ((Char >= L'A') && (Char <= L'F')) {
    *Value = Char - L'A' + 10;
  } else {
    return FALSE;
  }

  return TRUE;
}

/**
  Parse Length characters of String as hex.
**/
STATIC
EFI_STATUS
ParseHexN (
  IN  CONST CHAR16  *String,
  IN  UINTN         Length,
  OUT UINT64        *Value
  )
{
  UINTN   Digit;
  UINT64  Result;

  if ((Length == 0) || (Length > 16)) {
    return EFI_INVALID_PARAMETER;
  }

  Result = 0;
  for ( ; Length > 0; Length--, String++) {
    if (!HexDigit (*String, &Digit)) {
      return EFI_INVALID_PARAMETER;
    }

    Result = LShiftU64 (Result, 4) | Digit;
  }

  *Value = Result;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtParseHex (
  IN  CONST CHAR16  *String,
  OUT UINT64        *Value
  )
{
  UINTN  Length;

  if ((String == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((String[0] == L'0') && ((String[1] == L'x') || (String[1] == L'X'))) {
    String += 2;
  }

  Length = StrLen (String);
  if ((Length > 0) && ((String[Length - 1] == L'h') || (String[Length - 1] == L'H'))) {
    Length--;
  }

  return ParseHexN (String, Length, Value);
}

EFI_STATUS
EFIAPI
BdtParseNumber (
  IN  CONST CHAR16  *String,
  OUT UINT64        *Value
  )
{
  UINTN   Length;
  UINT64  Result;

  if ((String == NULL) || (Value == NULL) || (*String == L'\0')) {
    return EFI_INVALID_PARAMETER;
  }

  Length = StrLen (String);
  if (((String[0] == L'0') && ((String[1] == L'x') || (String[1] == L'X'))) ||
      (String[Length - 1] == L'h') || (String[Length - 1] == L'H'))
  {
    return BdtParseHex (String, Value);
  }

  Result = 0;
  for ( ; *String != L'\0'; String++) {
    if ((*String < L'0') || (*String > L'9') || (Result > DivU64x32 (MAX_UINT64 - 9, 10))) {
      return EFI_INVALID_PARAMETER;
    }

    Result = MultU64x32 (Result, 10) + (*String - L'0');
  }

  *Value = Result;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtParseBdf (
  IN  CONST CHAR16  *String,
  OUT UINT16        *Segment,
  OUT UINT8         *Bus,
  OUT UINT8         *Device,
  OUT UINT8         *Function
  )
{
  UINT64        Parts[4];
  UINTN         Count;
  CONST CHAR16  *Start;
  CONST CHAR16  *Walk;

  if (String == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Split on ':' and '.'; the last separator must be '.'.
  //
  Count = 0;
  Start = String;
  for (Walk = String; ; Walk++) {
    if ((*Walk == L':') || (*Walk == L'.') || (*Walk == L'\0')) {
      if ((Count == 4) || EFI_ERROR (ParseHexN (Start, Walk - Start, &Parts[Count]))) {
        return EFI_INVALID_PARAMETER;
      }

      Count++;
      if (*Walk == L'\0') {
        break;
      }

      if ((*Walk == L'.') && (Count < 2)) {
        return EFI_INVALID_PARAMETER;
      }

      Start = Walk + 1;
    }
  }

  if (Count == 3) {
    Parts[3] = Parts[2];
    Parts[2] = Parts[1];
    Parts[1] = Parts[0];
    Parts[0] = 0;
  } else if (Count != 4) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Parts[0] > 0xFFFF) || (Parts[1] > 0xFF) || (Parts[2] > 0x1F) || (Parts[3] > 0x7)) {
    return EFI_INVALID_PARAMETER;
  }

  *Segment  = (UINT16)Parts[0];
  *Bus      = (UINT8)Parts[1];
  *Device   = (UINT8)Parts[2];
  *Function = (UINT8)Parts[3];
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtParseGuid (
  IN  CONST CHAR16  *String,
  OUT EFI_GUID      *Guid
  )
{
  UINTN  Index;

  if ((String == NULL) || (Guid == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!RETURN_ERROR (StrToGuid (String, Guid)) && (StrLen (String) == 36)) {
    return EFI_SUCCESS;
  }

  //
  // Accept "EFI_GLOBAL_VARIABLE", "EFI_GLOBAL_VARIABLE_GUID" and short aliases.
  //
  if (BdtStriCmp (String, L"global") == 0) {
    String = L"EFI_GLOBAL_VARIABLE";
  } else if (BdtStriCmp (String, L"security") == 0) {
    String = L"EFI_IMAGE_SECURITY_DATABASE";
  }

  for (Index = 0; Index < ARRAY_SIZE (mKnownGuids); Index++) {
    if ((BdtStriCmp (String, mKnownGuids[Index].Name) == 0) ||
        ((StrnCmp (String, mKnownGuids[Index].Name, StrLen (mKnownGuids[Index].Name)) == 0) &&
         (BdtStriCmp (String + StrLen (mKnownGuids[Index].Name), L"_GUID") == 0)))
    {
      CopyGuid (Guid, &mKnownGuids[Index].Guid);
      return EFI_SUCCESS;
    }
  }

  return EFI_INVALID_PARAMETER;
}

CONST CHAR16 *
EFIAPI
BdtGuidName (
  IN CONST EFI_GUID  *Guid
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mKnownGuids); Index++) {
    if (CompareGuid (Guid, &mKnownGuids[Index].Guid)) {
      return mKnownGuids[Index].Name;
    }
  }

  return NULL;
}

CONST CHAR16 *
EFIAPI
BdtGuidToString (
  IN  CONST EFI_GUID  *Guid,
  OUT CHAR16          *Buffer,
  IN  UINTN           BufferSize
  )
{
  CONST CHAR16  *Name;

  Name = BdtGuidName (Guid);
  if (Name != NULL) {
    return Name;
  }

  UnicodeSPrint (Buffer, BufferSize, L"%g", Guid);
  return Buffer;
}

BOOLEAN
EFIAPI
BdtGetKnownGuid (
  IN  UINTN           Index,
  OUT CONST EFI_GUID  **Guid,
  OUT CONST CHAR16    **Name
  )
{
  if (Index >= ARRAY_SIZE (mKnownGuids)) {
    return FALSE;
  }

  *Guid = &mKnownGuids[Index].Guid;
  *Name = mKnownGuids[Index].Name;
  return TRUE;
}

EFI_STATUS
EFIAPI
BdtReadVariable (
  IN  CONST CHAR16    *Name,
  IN  CONST EFI_GUID  *Guid,
  OUT VOID            **Data,
  OUT UINTN           *DataSize,
  OUT UINT32          *Attributes OPTIONAL
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  VOID        *Buffer;

  *Data     = NULL;
  *DataSize = 0;

  Size   = 0;
  Status = gRT->GetVariable ((CHAR16 *)Name, (EFI_GUID *)Guid, Attributes, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  Buffer = AllocateZeroPool (Size);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gRT->GetVariable ((CHAR16 *)Name, (EFI_GUID *)Guid, Attributes, &Size, Buffer);
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return Status;
  }

  *Data     = Buffer;
  *DataSize = Size;
  return EFI_SUCCESS;
}

VOID
EFIAPI
BdtFormatSize (
  IN  UINT64  Bytes,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  if ((Bytes >= SIZE_1GB) && ((Bytes & (SIZE_1GB - 1)) == 0 || Bytes >= SIZE_16GB)) {
    UnicodeSPrint (Buffer, BufferSize, L"%lu GB", RShiftU64 (Bytes, 30));
  } else if (Bytes >= SIZE_1MB) {
    UnicodeSPrint (Buffer, BufferSize, L"%lu MB", RShiftU64 (Bytes, 20));
  } else if (Bytes >= SIZE_1KB) {
    UnicodeSPrint (Buffer, BufferSize, L"%lu KB", RShiftU64 (Bytes, 10));
  } else {
    UnicodeSPrint (Buffer, BufferSize, L"%lu B", Bytes);
  }
}

CONST CHAR16 *
EFIAPI
BdtStatusName (
  IN EFI_STATUS  Status
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mStatusNames); Index++) {
    if (mStatusNames[Index].Status == Status) {
      return mStatusNames[Index].Name;
    }
  }

  return L"EFI_UNKNOWN_STATUS";
}

INTN
EFIAPI
BdtStriCmp (
  IN CONST CHAR16  *First,
  IN CONST CHAR16  *Second
  )
{
  CHAR16  A;
  CHAR16  B;

  do {
    A = *First++;
    B = *Second++;
    if ((A >= L'A') && (A <= L'Z')) {
      A = A - L'A' + L'a';
    }

    if ((B >= L'A') && (B <= L'Z')) {
      B = B - L'A' + L'a';
    }
  } while ((A == B) && (A != L'\0'));

  return (INTN)A - (INTN)B;
}
