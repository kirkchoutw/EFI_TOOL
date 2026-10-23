/** @file
  BdtSecurityLib - Secure Boot / TPM 2.0 inspection.

  TPM commands are built by hand (TPM2_GetCapability, TPM2_PCR_Read) and sent
  through EFI_TCG2_PROTOCOL.SubmitCommand, so no SecurityPkg library is needed.
  Only read commands are ever sent.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Guid/GlobalVariable.h>
#include <Guid/ImageAuthentication.h>
#include <Library/BdtSecurityLib.h>
#include <Protocol/Tcg2Protocol.h>

#define BDT_TPM_ST_NO_SESSIONS        0x8001
#define BDT_TPM_CC_GET_CAPABILITY     0x0000017A
#define BDT_TPM_CC_PCR_READ           0x0000017E
#define BDT_TPM_CAP_TPM_PROPERTIES    0x00000006
#define BDT_TPM_PT_MANUFACTURER       0x00000105
#define TPM_PT_PROPERTY_COUNT     8
#define TPM_RESPONSE_HEADER_SIZE  10
#define TPM_PCR_COUNT             24
#define TPM_MAX_BUFFER            1024
#define TCG_LOG_MAX_EVENTS        4096

typedef struct {
  UINT32          Bitmap;
  UINT16          AlgId;
  UINT16          DigestSize;
  CONST CHAR16    *Name;
} BDT_HASH_ALG;

STATIC CONST BDT_HASH_ALG  mHashAlgs[] = {
  { EFI_TCG2_BOOT_HASH_ALG_SHA1,    0x0004, 20, L"SHA1"    },
  { EFI_TCG2_BOOT_HASH_ALG_SHA256,  0x000B, 32, L"SHA256"  },
  { EFI_TCG2_BOOT_HASH_ALG_SHA384,  0x000C, 48, L"SHA384"  },
  { EFI_TCG2_BOOT_HASH_ALG_SHA512,  0x000D, 64, L"SHA512"  },
  { EFI_TCG2_BOOT_HASH_ALG_SM3_256, 0x0012, 32, L"SM3_256" },
};

typedef struct {
  UINT32          Type;
  CONST CHAR16    *Name;
} BDT_EVENT_NAME;

STATIC CONST BDT_EVENT_NAME  mEventNames[] = {
  { 0x00000000, L"EV_PREBOOT_CERT"                  },
  { 0x00000001, L"EV_POST_CODE"                     },
  { 0x00000003, L"EV_NO_ACTION"                     },
  { 0x00000004, L"EV_SEPARATOR"                     },
  { 0x00000005, L"EV_ACTION"                        },
  { 0x00000006, L"EV_EVENT_TAG"                     },
  { 0x00000007, L"EV_S_CRTM_CONTENTS"               },
  { 0x00000008, L"EV_S_CRTM_VERSION"                },
  { 0x00000009, L"EV_CPU_MICROCODE"                 },
  { 0x0000000A, L"EV_PLATFORM_CONFIG_FLAGS"         },
  { 0x0000000B, L"EV_TABLE_OF_DEVICES"              },
  { 0x0000000C, L"EV_COMPACT_HASH"                  },
  { 0x0000000D, L"EV_IPL"                           },
  { 0x0000000E, L"EV_IPL_PARTITION_DATA"            },
  { 0x0000000F, L"EV_NONHOST_CODE"                  },
  { 0x00000010, L"EV_NONHOST_CONFIG"                },
  { 0x00000011, L"EV_NONHOST_INFO"                  },
  { 0x00000012, L"EV_OMIT_BOOT_DEVICE_EVENTS"       },
  { 0x80000001, L"EV_EFI_VARIABLE_DRIVER_CONFIG"    },
  { 0x80000002, L"EV_EFI_VARIABLE_BOOT"             },
  { 0x80000003, L"EV_EFI_BOOT_SERVICES_APPLICATION" },
  { 0x80000004, L"EV_EFI_BOOT_SERVICES_DRIVER"      },
  { 0x80000005, L"EV_EFI_RUNTIME_SERVICES_DRIVER"   },
  { 0x80000006, L"EV_EFI_GPT_EVENT"                 },
  { 0x80000007, L"EV_EFI_ACTION"                    },
  { 0x80000008, L"EV_EFI_PLATFORM_FIRMWARE_BLOB"    },
  { 0x80000009, L"EV_EFI_HANDOFF_TABLES"            },
  { 0x8000000A, L"EV_EFI_PLATFORM_FIRMWARE_BLOB2"   },
  { 0x8000000B, L"EV_EFI_HANDOFF_TABLES2"           },
  { 0x8000000C, L"EV_EFI_VARIABLE_BOOT2"            },
  { 0x80000010, L"EV_EFI_HCRTM_EVENT"               },
  { 0x800000E0, L"EV_EFI_VARIABLE_AUTHORITY"        },
  { 0x800000E1, L"EV_EFI_SPDM_FIRMWARE_BLOB"        },
  { 0x800000E2, L"EV_EFI_SPDM_FIRMWARE_CONFIG"      },
};

//
// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
//

STATIC
UINT8
ReadVariableByte (
  IN CONST CHAR16    *Name,
  IN CONST EFI_GUID  *Guid,
  OUT BOOLEAN        *Present
  )
{
  UINT8  Value;
  UINTN  Size;

  Size     = sizeof (Value);
  Value    = 0;
  *Present = !EFI_ERROR (gRT->GetVariable ((CHAR16 *)Name, (EFI_GUID *)Guid, NULL, &Size, &Value));
  return Value;
}

STATIC
EFI_TCG2_PROTOCOL *
GetTcg2 (
  VOID
  )
{
  EFI_TCG2_PROTOCOL  *Tcg2;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiTcg2ProtocolGuid, NULL, (VOID **)&Tcg2))) {
    return NULL;
  }

  return Tcg2;
}

STATIC
EFI_STATUS
GetTcg2Capability (
  IN  EFI_TCG2_PROTOCOL                 *Tcg2,
  OUT EFI_TCG2_BOOT_SERVICE_CAPABILITY  *Cap
  )
{
  ZeroMem (Cap, sizeof (*Cap));
  Cap->Size = (UINT8)sizeof (*Cap);
  return Tcg2->GetCapability (Tcg2, Cap);
}

STATIC
VOID
FormatHex (
  IN  CONST UINT8  *Data,
  IN  UINTN        Size,
  OUT CHAR16       *Buffer,
  IN  UINTN        BufferSize
  )
{
  UINTN  Index;
  UINTN  Max;

  Max = (BufferSize / sizeof (CHAR16) - 1) / 2;
  for (Index = 0; Index < Size && Index < Max; Index++) {
    UnicodeSPrint (&Buffer[Index * 2], 3 * sizeof (CHAR16), L"%02x", Data[Index]);
  }

  Buffer[MIN (Size, Max) * 2] = L'\0';
}

STATIC
CONST BDT_HASH_ALG *
HashAlgById (
  IN UINT16  AlgId
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mHashAlgs); Index++) {
    if (mHashAlgs[Index].AlgId == AlgId) {
      return &mHashAlgs[Index];
    }
  }

  return NULL;
}

STATIC
CONST CHAR16 *
EventName (
  IN UINT32  Type
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mEventNames); Index++) {
    if (mEventNames[Index].Type == Type) {
      return mEventNames[Index].Name;
    }
  }

  return L"EV_UNKNOWN";
}

/**
  Send a TPM command and validate the response header.
**/
STATIC
EFI_STATUS
TpmCommand (
  IN     EFI_TCG2_PROTOCOL  *Tcg2,
  IN     UINT8              *Command,
  IN     UINT32             CommandSize,
  OUT    UINT8              *Response,
  IN OUT UINT32             *ResponseSize
  )
{
  EFI_STATUS  Status;
  UINT32      Size;
  UINT32      Code;

  ZeroMem (Response, *ResponseSize);
  Status = Tcg2->SubmitCommand (Tcg2, CommandSize, Command, *ResponseSize, Response);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Size = SwapBytes32 (ReadUnaligned32 ((UINT32 *)(Response + 2)));
  Code = SwapBytes32 (ReadUnaligned32 ((UINT32 *)(Response + 6)));
  if ((Size < TPM_RESPONSE_HEADER_SIZE) || (Size > *ResponseSize) || (Code != 0)) {
    return EFI_DEVICE_ERROR;
  }

  *ResponseSize = Size;
  return EFI_SUCCESS;
}

STATIC
UINTN
PutHeader (
  OUT UINT8   *Buffer,
  IN  UINT32  CommandCode
  )
{
  WriteUnaligned16 ((UINT16 *)Buffer, SwapBytes16 (BDT_TPM_ST_NO_SESSIONS));
  WriteUnaligned32 ((UINT32 *)(Buffer + 6), SwapBytes32 (CommandCode));
  return TPM_RESPONSE_HEADER_SIZE;
}

STATIC
VOID
PutSize (
  OUT UINT8  *Buffer,
  IN  UINTN  Size
  )
{
  WriteUnaligned32 ((UINT32 *)(Buffer + 2), SwapBytes32 ((UINT32)Size));
}

//
// ---------------------------------------------------------------------------
// Secure Boot
// ---------------------------------------------------------------------------
//

EFI_STATUS
EFIAPI
BdtSecureBootGetState (
  OUT BOOLEAN  *Enabled,
  OUT BOOLEAN  *SetupMode
  )
{
  BOOLEAN  Present;

  *Enabled   = ReadVariableByte (EFI_SECURE_BOOT_MODE_NAME, &gEfiGlobalVariableGuid, &Present) == 1;
  *SetupMode = ReadVariableByte (EFI_SETUP_MODE_NAME, &gEfiGlobalVariableGuid, &Present) == 1;
  return Present ? EFI_SUCCESS : EFI_NOT_FOUND;
}

BOOLEAN
EFIAPI
BdtTpmIsPresent (
  VOID
  )
{
  EFI_TCG2_PROTOCOL                 *Tcg2;
  EFI_TCG2_BOOT_SERVICE_CAPABILITY  Cap;

  Tcg2 = GetTcg2 ();
  if ((Tcg2 == NULL) || EFI_ERROR (GetTcg2Capability (Tcg2, &Cap))) {
    return FALSE;
  }

  return Cap.TPMPresentFlag;
}

STATIC
CONST EFI_GUID *
DbGuid (
  IN CONST CHAR16  *Name
  )
{
  if ((StrCmp (Name, EFI_PLATFORM_KEY_NAME) == 0) || (StrCmp (Name, EFI_KEY_EXCHANGE_KEY_NAME) == 0)) {
    return &gEfiGlobalVariableGuid;
  }

  return &gEfiImageSecurityDatabaseGuid;
}

/**
  Count the signatures of an EFI_SIGNATURE_LIST array. Returns FALSE when
  the data is malformed.
**/
STATIC
BOOLEAN
CountSignatures (
  IN  CONST UINT8  *Data,
  IN  UINTN        Size,
  OUT UINTN        *Lists,
  OUT UINTN        *Signatures
  )
{
  CONST EFI_SIGNATURE_LIST  *List;
  UINTN                     Offset;

  *Lists      = 0;
  *Signatures = 0;
  for (Offset = 0; Offset + sizeof (EFI_SIGNATURE_LIST) <= Size; Offset += List->SignatureListSize) {
    List = (CONST EFI_SIGNATURE_LIST *)(Data + Offset);
    if ((List->SignatureListSize < sizeof (EFI_SIGNATURE_LIST) + List->SignatureHeaderSize) ||
        (List->SignatureListSize > Size - Offset) ||
        (List->SignatureSize < sizeof (EFI_GUID)))
    {
      return FALSE;
    }

    (*Lists)++;
    *Signatures += (List->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - List->SignatureHeaderSize) / List->SignatureSize;
  }

  return Offset == Size;
}

EFI_STATUS
EFIAPI
BdtSecureBoot (
  VOID
  )
{
  STATIC CONST CHAR16  *Mode[] = { L"SecureBoot", L"SetupMode", L"AuditMode", L"DeployedMode", L"VendorKeys" };
  STATIC CONST CHAR16  *Dbs[]  = { L"PK", L"KEK", L"db", L"dbx", L"dbt", L"dbr" };
  BOOLEAN              Present;
  UINT8                Value;
  UINTN                Index;
  VOID                 *Data;
  UINTN                Size;
  UINTN                Lists;
  UINTN                Signatures;
  BOOLEAN              Valid;

  BdtOutSectionBegin (L"Mode");
  for (Index = 0; Index < ARRAY_SIZE (Mode); Index++) {
    Value = ReadVariableByte (Mode[Index], &gEfiGlobalVariableGuid, &Present);
    if (Present) {
      BdtOutFieldNum (Mode[Index], Value);
    } else {
      BdtOutField (Mode[Index], L"Not Present");
    }
  }

  Value = ReadVariableByte (EFI_SECURE_BOOT_MODE_NAME, &gEfiGlobalVariableGuid, &Present);
  BdtOutField (L"Status", L"%s", (Present && Value == 1) ? L"Enabled" : L"Disabled");
  BdtOutSectionEnd ();

  BdtOutListBegin (L"Key Databases");
  for (Index = 0; Index < ARRAY_SIZE (Dbs); Index++) {
    BdtOutItemBegin (L"%s", Dbs[Index]);
    if (BdtOutGetFormat () != BdtFormatText) {
      BdtOutField (L"name", L"%s", Dbs[Index]);
    }

    if (EFI_ERROR (BdtReadVariable (Dbs[Index], DbGuid (Dbs[Index]), &Data, &Size, NULL))) {
      BdtOutField (L"State", L"Not Installed");
    } else {
      Valid = CountSignatures (Data, Size, &Lists, &Signatures);
      BdtOutField (L"State", L"Installed");
      BdtOutFieldHex (L"Size", Size, 0);
      BdtOutFieldNum (L"Signature Lists", Lists);
      BdtOutFieldNum (L"Signatures", Signatures);
      BdtOutFieldBool (L"Well Formed", Valid);
      FreePool (Data);
    }

    BdtOutItemEnd ();
  }

  BdtOutListEnd ();
  return EFI_SUCCESS;
}

/**
  Extract the subject common name from a DER certificate. The subject is
  normally the last CN (OID 2.5.4.3) in the certificate.
**/
STATIC
BOOLEAN
GetCertCommonName (
  IN  CONST UINT8  *Cert,
  IN  UINTN        Size,
  OUT CHAR16       *Name,
  IN  UINTN        NameSize
  )
{
  STATIC CONST UINT8  CnOid[] = { 0x06, 0x03, 0x55, 0x04, 0x03 };
  UINTN               Offset;
  UINTN               Found;
  UINTN               Length;
  UINTN               Index;
  UINT8               Tag;

  Found = 0;
  for (Offset = 0; Offset + sizeof (CnOid) + 2 < Size; Offset++) {
    if (CompareMem (Cert + Offset, CnOid, sizeof (CnOid)) == 0) {
      Found = Offset + sizeof (CnOid);
    }
  }

  if (Found == 0) {
    return FALSE;
  }

  Tag    = Cert[Found];
  Length = Cert[Found + 1];
  if (((Tag != 0x0C) && (Tag != 0x13) && (Tag != 0x14) && (Tag != 0x16)) || (Length >= 0x80) ||
      (Found + 2 + Length > Size))
  {
    return FALSE;
  }

  for (Index = 0; Index < Length && Index + 1 < NameSize / sizeof (CHAR16); Index++) {
    Name[Index] = ((Cert[Found + 2 + Index] >= 0x20) && (Cert[Found + 2 + Index] < 0x7F)) ? Cert[Found + 2 + Index] : L'?';
  }

  Name[Index] = L'\0';
  return TRUE;
}

EFI_STATUS
EFIAPI
BdtSecurityDb (
  IN CONST CHAR16  *Name
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"list",  L"List",  6  },
    { L"index", L"#",     5  },
    { L"type",  L"Type",  24 },
    { L"owner", L"Owner", 38 },
    { L"size",  L"Size",  8  },
    { L"value", L"Value", 0  },
  };
  EFI_STATUS                Status;
  UINT8                     *Data;
  UINTN                     Size;
  UINT32                    Attributes;
  UINTN                     Offset;
  UINTN                     ListIndex;
  UINTN                     SigIndex;
  UINTN                     SigCount;
  EFI_SIGNATURE_LIST        *List;
  EFI_SIGNATURE_DATA        *Sig;
  UINTN                     DataSize;
  CHAR16                    TypeText[40];
  CHAR16                    Value[132];

  Status = BdtReadVariable (Name, DbGuid (Name), (VOID **)&Data, &Size, &Attributes);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"%s is not installed (%s)", Name, BdtStatusName (Status));
    return Status;
  }

  BdtOutField (L"Variable", L"%s", Name);
  BdtOutField (L"GUID", L"%g", DbGuid (Name));
  BdtOutFieldHex (L"Attributes", Attributes, 8);
  BdtOutFieldHex (L"Size", Size, 0);

  BdtOutTableBegin (L"signatures", Columns, ARRAY_SIZE (Columns));
  ListIndex = 0;
  for (Offset = 0; Offset + sizeof (EFI_SIGNATURE_LIST) <= Size; Offset += List->SignatureListSize, ListIndex++) {
    List = (EFI_SIGNATURE_LIST *)(Data + Offset);
    if ((List->SignatureListSize < sizeof (EFI_SIGNATURE_LIST) + List->SignatureHeaderSize) ||
        (List->SignatureListSize > Size - Offset) || (List->SignatureSize < sizeof (EFI_GUID)))
    {
      BdtOutError (L"Malformed EFI_SIGNATURE_LIST at offset 0x%x", Offset);
      break;
    }

    BdtGuidToString (&List->SignatureType, TypeText, sizeof (TypeText));
    SigCount = (List->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - List->SignatureHeaderSize) / List->SignatureSize;
    for (SigIndex = 0; SigIndex < SigCount; SigIndex++) {
      Sig = (EFI_SIGNATURE_DATA *)((UINT8 *)List + sizeof (EFI_SIGNATURE_LIST) + List->SignatureHeaderSize +
                                   SigIndex * List->SignatureSize);
      DataSize = List->SignatureSize - sizeof (EFI_GUID);
      if (CompareGuid (&List->SignatureType, &gEfiCertX509Guid)) {
        if (!GetCertCommonName (Sig->SignatureData, DataSize, Value, sizeof (Value))) {
          StrCpyS (Value, ARRAY_SIZE (Value), L"(X.509 certificate)");
        }
      } else {
        FormatHex (Sig->SignatureData, DataSize, Value, sizeof (Value));
      }

      BdtOutRowBegin ();
      BdtOutCell (L"%d", ListIndex);
      BdtOutCell (L"%d", SigIndex);
      BdtOutCell (L"%s", TypeText);
      BdtOutCell (L"%g", &Sig->SignatureOwner);
      BdtOutCell (L"%d", DataSize);
      BdtOutCell (L"%s", Value);
      BdtOutRowEnd ();
    }
  }

  BdtOutTableEnd ();
  FreePool (Data);
  return EFI_SUCCESS;
}

//
// ---------------------------------------------------------------------------
// TPM
// ---------------------------------------------------------------------------
//

STATIC
VOID
OutTpmProperties (
  IN EFI_TCG2_PROTOCOL  *Tcg2
  )
{
  UINT8   Command[32];
  UINT8   Response[TPM_MAX_BUFFER];
  UINT32  ResponseSize;
  UINTN   Size;
  UINT32  Count;
  UINT32  Index;
  UINT32  Property;
  UINT32  Value;
  UINT32  Vendor[4];
  UINT32  Fw[2];
  CHAR8   VendorText[17];

  ZeroMem (Command, sizeof (Command));
  Size = PutHeader (Command, BDT_TPM_CC_GET_CAPABILITY);
  WriteUnaligned32 ((UINT32 *)(Command + Size), SwapBytes32 (BDT_TPM_CAP_TPM_PROPERTIES));
  Size += 4;
  WriteUnaligned32 ((UINT32 *)(Command + Size), SwapBytes32 (BDT_TPM_PT_MANUFACTURER));
  Size += 4;
  WriteUnaligned32 ((UINT32 *)(Command + Size), SwapBytes32 (TPM_PT_PROPERTY_COUNT));
  Size += 4;
  PutSize (Command, Size);

  ResponseSize = sizeof (Response);
  if (EFI_ERROR (TpmCommand (Tcg2, Command, (UINT32)Size, Response, &ResponseSize)) ||
      (ResponseSize < TPM_RESPONSE_HEADER_SIZE + 9))
  {
    return;
  }

  //
  // Response: header, moreData (1), capability (4), count (4), {property, value}[]
  //
  ZeroMem (Vendor, sizeof (Vendor));
  ZeroMem (Fw, sizeof (Fw));
  Count = SwapBytes32 (ReadUnaligned32 ((UINT32 *)(Response + TPM_RESPONSE_HEADER_SIZE + 5)));
  for (Index = 0; Index < Count && TPM_RESPONSE_HEADER_SIZE + 9 + (Index + 1) * 8 <= ResponseSize; Index++) {
    Property = SwapBytes32 (ReadUnaligned32 ((UINT32 *)(Response + TPM_RESPONSE_HEADER_SIZE + 9 + Index * 8)));
    Value    = ReadUnaligned32 ((UINT32 *)(Response + TPM_RESPONSE_HEADER_SIZE + 9 + Index * 8 + 4));
    if ((Property >= 0x106) && (Property <= 0x109)) {
      Vendor[Property - 0x106] = Value;           // keep big endian byte order for text
    } else if ((Property == 0x10B) || (Property == 0x10C)) {
      Fw[Property - 0x10B] = SwapBytes32 (Value);
    }
  }

  CopyMem (VendorText, Vendor, sizeof (Vendor));
  VendorText[16] = '\0';
  for (Index = 0; Index < 16; Index++) {
    if ((VendorText[Index] != 0) && ((VendorText[Index] < 0x20) || (VendorText[Index] >= 0x7F))) {
      VendorText[Index] = ' ';
    }
  }

  BdtOutField (L"Vendor String", L"%a", VendorText);
  BdtOutField (L"Firmware Version", L"%d.%d.%d.%d", Fw[0] >> 16, Fw[0] & 0xFFFF, Fw[1] >> 16, Fw[1] & 0xFFFF);
}

EFI_STATUS
EFIAPI
BdtTpmInfo (
  VOID
  )
{
  EFI_TCG2_PROTOCOL                 *Tcg2;
  EFI_TCG2_BOOT_SERVICE_CAPABILITY  Cap;
  EFI_STATUS                        Status;
  UINTN                             Index;
  CHAR8                             Manufacturer[5];
  CHAR16                            Banks[64];

  Tcg2 = GetTcg2 ();
  if (Tcg2 == NULL) {
    BdtOutFieldBool (L"TPM Present", FALSE);
    BdtOutField (L"Detail", L"EFI_TCG2_PROTOCOL not found");
    return EFI_SUCCESS;
  }

  Status = GetTcg2Capability (Tcg2, &Cap);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"TCG2 GetCapability failed (%s)", BdtStatusName (Status));
    return Status;
  }

  for (Index = 0; Index < 4; Index++) {
    Manufacturer[Index] = (CHAR8)(Cap.ManufacturerID >> (Index * 8));
    if ((Manufacturer[Index] < 0x20) || (Manufacturer[Index] >= 0x7F)) {
      Manufacturer[Index] = ' ';
    }
  }

  Manufacturer[4] = '\0';

  Banks[0] = L'\0';
  for (Index = 0; Index < ARRAY_SIZE (mHashAlgs); Index++) {
    if ((Cap.ActivePcrBanks & mHashAlgs[Index].Bitmap) != 0) {
      if (Banks[0] != L'\0') {
        StrCatS (Banks, ARRAY_SIZE (Banks), L" ");
      }

      StrCatS (Banks, ARRAY_SIZE (Banks), mHashAlgs[Index].Name);
    }
  }

  BdtOutFieldBool (L"TPM Present", Cap.TPMPresentFlag);
  BdtOutField (L"TPM Version", L"2.0");
  BdtOutField (L"Protocol Version", L"%d.%d", Cap.ProtocolVersion.Major, Cap.ProtocolVersion.Minor);
  BdtOutFieldHex (L"Manufacturer ID", Cap.ManufacturerID, 8);
  BdtOutField (L"Manufacturer", L"%a", Manufacturer);
  BdtOutFieldHex (L"Hash Algorithm Bitmap", Cap.HashAlgorithmBitmap, 8);
  BdtOutFieldHex (L"Supported Event Logs", Cap.SupportedEventLogs, 8);
  BdtOutFieldNum (L"Number of PCR Banks", Cap.NumberOfPCRBanks);
  BdtOutField (L"Active PCR Banks", L"%s", Banks);
  BdtOutFieldNum (L"Max Command Size", Cap.MaxCommandSize);
  BdtOutFieldNum (L"Max Response Size", Cap.MaxResponseSize);
  if (Cap.TPMPresentFlag) {
    OutTpmProperties (Tcg2);
  }

  return EFI_SUCCESS;
}

/**
  Read one PCR of one bank with TPM2_PCR_Read.
**/
STATIC
EFI_STATUS
ReadPcr (
  IN  EFI_TCG2_PROTOCOL  *Tcg2,
  IN  UINT16             AlgId,
  IN  UINT32             Pcr,
  OUT UINT8              *Digest,
  OUT UINTN              *DigestSize
  )
{
  EFI_STATUS  Status;
  UINT8       Command[32];
  UINT8       Response[TPM_MAX_BUFFER];
  UINT32      ResponseSize;
  UINTN       Size;
  UINTN       Offset;
  UINT32      Selections;
  UINT32      Index;
  UINT16      Length;

  ZeroMem (Command, sizeof (Command));
  Size = PutHeader (Command, BDT_TPM_CC_PCR_READ);
  WriteUnaligned32 ((UINT32 *)(Command + Size), SwapBytes32 (1));
  Size += 4;
  WriteUnaligned16 ((UINT16 *)(Command + Size), SwapBytes16 (AlgId));
  Size               += 2;
  Command[Size++]     = 3;                         // sizeofSelect
  Command[Size + Pcr / 8] = (UINT8)(1 << (Pcr % 8));
  Size               += 3;
  PutSize (Command, Size);

  ResponseSize = sizeof (Response);
  Status       = TpmCommand (Tcg2, Command, (UINT32)Size, Response, &ResponseSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Response: header, pcrUpdateCounter (4), TPML_PCR_SELECTION, TPML_DIGEST.
  //
  Offset = TPM_RESPONSE_HEADER_SIZE + 4;
  if (Offset + 4 > ResponseSize) {
    return EFI_DEVICE_ERROR;
  }

  Selections = SwapBytes32 (ReadUnaligned32 ((UINT32 *)(Response + Offset)));
  Offset    += 4;
  for (Index = 0; Index < Selections; Index++) {
    if (Offset + 3 > ResponseSize) {
      return EFI_DEVICE_ERROR;
    }

    Offset += 3 + Response[Offset + 2];
  }

  if ((Offset + 6 > ResponseSize) || (SwapBytes32 (ReadUnaligned32 ((UINT32 *)(Response + Offset))) == 0)) {
    return EFI_NOT_FOUND;
  }

  Offset += 4;
  Length  = SwapBytes16 (ReadUnaligned16 ((UINT16 *)(Response + Offset)));
  Offset += 2;
  if ((Length > 64) || (Offset + Length > ResponseSize)) {
    return EFI_DEVICE_ERROR;
  }

  CopyMem (Digest, Response + Offset, Length);
  *DigestSize = Length;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtTpmPcr (
  IN UINT32  Index
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"pcr",    L"PCR",    5  },
    { L"bank",   L"Bank",   9  },
    { L"digest", L"Digest", 0  },
  };
  EFI_TCG2_PROTOCOL                 *Tcg2;
  EFI_TCG2_BOOT_SERVICE_CAPABILITY  Cap;
  UINT32                            First;
  UINT32                            Last;
  UINT32                            Pcr;
  UINTN                             Alg;
  UINT8                             Digest[64];
  UINTN                             DigestSize;
  CHAR16                            Hex[132];
  EFI_STATUS                        Status;

  if ((Index != MAX_UINT32) && (Index >= TPM_PCR_COUNT)) {
    BdtOutError (L"PCR index must be 0 - %d", TPM_PCR_COUNT - 1);
    return EFI_INVALID_PARAMETER;
  }

  Tcg2 = GetTcg2 ();
  if ((Tcg2 == NULL) || EFI_ERROR (GetTcg2Capability (Tcg2, &Cap)) || !Cap.TPMPresentFlag) {
    BdtOutError (L"TPM 2.0 not present");
    return EFI_NOT_FOUND;
  }

  First = (Index == MAX_UINT32) ? 0 : Index;
  Last  = (Index == MAX_UINT32) ? TPM_PCR_COUNT - 1 : Index;
  BdtOutTableBegin (L"pcrs", Columns, ARRAY_SIZE (Columns));
  for (Alg = 0; Alg < ARRAY_SIZE (mHashAlgs); Alg++) {
    if ((Cap.ActivePcrBanks & mHashAlgs[Alg].Bitmap) == 0) {
      continue;
    }

    for (Pcr = First; Pcr <= Last; Pcr++) {
      Status = ReadPcr (Tcg2, mHashAlgs[Alg].AlgId, Pcr, Digest, &DigestSize);
      if (EFI_ERROR (Status)) {
        UnicodeSPrint (Hex, sizeof (Hex), L"(%s)", BdtStatusName (Status));
      } else {
        FormatHex (Digest, DigestSize, Hex, sizeof (Hex));
      }

      BdtOutRowBegin ();
      BdtOutCell (L"%d", Pcr);
      BdtOutCell (L"%s", mHashAlgs[Alg].Name);
      BdtOutCell (L"%s", Hex);
      BdtOutRowEnd ();
    }
  }

  BdtOutTableEnd ();
  return EFI_SUCCESS;
}

/**
  Short human readable description of an event's data.
**/
STATIC
VOID
DescribeEvent (
  IN  UINT32       Type,
  IN  CONST UINT8  *Data,
  IN  UINT32       Size,
  OUT CHAR16       *Text,
  IN  UINTN        TextSize
  )
{
  UINTN    Index;
  BOOLEAN  Printable;
  UINT64   NameLength;
  CHAR16   *Name;

  Text[0] = L'\0';
  if (Size == 0) {
    return;
  }

  //
  // UEFI_VARIABLE_DATA: GUID, UnicodeNameLength, VariableDataLength, Name.
  //
  if (((Type == 0x80000001) || (Type == 0x80000002) || (Type == 0x8000000C) || (Type == 0x800000E0)) && (Size >= 32)) {
    NameLength = ReadUnaligned64 ((UINT64 *)(Data + 16));
    if ((NameLength > 0) && (NameLength < 128) && (32 + NameLength * 2 <= Size)) {
      Name = AllocateZeroPool ((UINTN)(NameLength + 1) * sizeof (CHAR16));
      if (Name != NULL) {
        CopyMem (Name, Data + 32, (UINTN)NameLength * 2);
        UnicodeSPrint (Text, TextSize, L"%s (%g)", Name, (EFI_GUID *)Data);
        FreePool (Name);
        return;
      }
    }
  }

  //
  // NULL terminated UCS-2 string (e.g. EV_S_CRTM_VERSION).
  //
  Printable = (Size >= 4) && (Size % 2 == 0) && (ReadUnaligned16 ((UINT16 *)(Data + Size - 2)) == 0);
  for (Index = 0; Index + 2 < Size && Printable; Index += 2) {
    Printable = (Data[Index] >= 0x20) && (Data[Index] < 0x7F) && (Data[Index + 1] == 0);
  }

  if (Printable) {
    for (Index = 0; Index + 2 < Size && Index / 2 + 1 < TextSize / sizeof (CHAR16); Index += 2) {
      Text[Index / 2] = Data[Index];
    }

    Text[Index / 2] = L'\0';
    return;
  }

  Printable = TRUE;
  for (Index = 0; Index < Size && Printable; Index++) {
    Printable = ((Data[Index] >= 0x20) && (Data[Index] < 0x7F)) || ((Data[Index] == 0) && (Index == Size - 1));
  }

  if (Printable) {
    for (Index = 0; Index < Size && Data[Index] != 0 && Index + 1 < TextSize / sizeof (CHAR16); Index++) {
      Text[Index] = Data[Index];
    }

    Text[Index] = L'\0';
  } else {
    FormatHex (Data, MIN (Size, 32), Text, TextSize);
  }
}

EFI_STATUS
EFIAPI
BdtTcgLog (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"number", L"#",          6  },
    { L"pcr",    L"PCR",        5  },
    { L"type",   L"Event Type", 34 },
    { L"size",   L"Size",       7  },
    { L"digest", L"Digest",     66 },
    { L"data",   L"Event Data", 0  },
  };
  EFI_TCG2_PROTOCOL     *Tcg2;
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Location;
  EFI_PHYSICAL_ADDRESS  LastEntry;
  BOOLEAN               Truncated;
  UINT8                 *Walk;
  UINT8                 *Last;
  UINT8                 *Spec;
  UINT32                SpecSize;
  UINT32                AlgCount;
  UINT16                AlgIds[8];
  UINT16                AlgSizes[8];
  UINT32                Index;
  UINT32                Pcr;
  UINT32                Type;
  UINT32                DigestCount;
  UINT32                EventSize;
  UINT16                AlgId;
  UINTN                 Number;
  UINT8                 *Digest;
  UINTN                 DigestSize;
  UINTN                 Alg;
  CONST BDT_HASH_ALG    *Hash;
  CHAR16                DigestText[132];
  CHAR16                DataText[132];
  BOOLEAN               Malformed;

  Tcg2 = GetTcg2 ();
  if (Tcg2 == NULL) {
    BdtOutError (L"EFI_TCG2_PROTOCOL not found");
    return EFI_NOT_FOUND;
  }

  Status = Tcg2->GetEventLog (Tcg2, EFI_TCG2_EVENT_LOG_FORMAT_TCG_2, &Location, &LastEntry, &Truncated);
  if (EFI_ERROR (Status) || (Location == 0)) {
    BdtOutError (L"TCG2 event log not available (%s)", BdtStatusName (Status));
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  BdtOutFieldHex (L"Log Address", Location, 0);
  BdtOutFieldHex (L"Last Entry", LastEntry, 0);
  BdtOutFieldBool (L"Truncated", Truncated);

  //
  // First event: TCG_PCR_EVENT (SHA1 format) carrying TCG_EfiSpecIDEventStruct.
  //
  Walk     = (UINT8 *)(UINTN)Location;
  SpecSize = ReadUnaligned32 ((UINT32 *)(Walk + 28));
  Spec     = Walk + 32;
  if ((SpecSize < 28) || (CompareMem (Spec, "Spec ID Event03", 16) != 0)) {
    BdtOutError (L"Event log does not start with a Spec ID Event03");
    return EFI_VOLUME_CORRUPTED;
  }

  AlgCount = ReadUnaligned32 ((UINT32 *)(Spec + 24));
  if ((AlgCount == 0) || (AlgCount > ARRAY_SIZE (AlgIds)) || (28 + AlgCount * 4 > SpecSize)) {
    BdtOutError (L"Invalid algorithm count %d in Spec ID event", AlgCount);
    return EFI_VOLUME_CORRUPTED;
  }

  BdtOutListBegin (L"Algorithms");
  for (Index = 0; Index < AlgCount; Index++) {
    AlgIds[Index]   = ReadUnaligned16 ((UINT16 *)(Spec + 28 + Index * 4));
    AlgSizes[Index] = ReadUnaligned16 ((UINT16 *)(Spec + 30 + Index * 4));
    Hash            = HashAlgById (AlgIds[Index]);
    BdtOutItemBegin (L"%s (0x%04x) %d bytes", (Hash != NULL) ? Hash->Name : L"Unknown", AlgIds[Index], AlgSizes[Index]);
    if (BdtOutGetFormat () != BdtFormatText) {
      BdtOutFieldHex (L"alg_id", AlgIds[Index], 4);
      BdtOutFieldNum (L"digest_size", AlgSizes[Index]);
    }

    BdtOutItemEnd ();
  }

  BdtOutListEnd ();

  //
  // TCG_PCR_EVENT2 records. Show the SHA256 digest when present, else the first.
  //
  Last      = (UINT8 *)(UINTN)LastEntry;
  Walk      = Spec + SpecSize;
  Number    = 1;
  Malformed = FALSE;
  BdtOutTableBegin (L"events", Columns, ARRAY_SIZE (Columns));
  while (Walk <= Last && Number < TCG_LOG_MAX_EVENTS && !Malformed) {
    Pcr         = ReadUnaligned32 ((UINT32 *)Walk);
    Type        = ReadUnaligned32 ((UINT32 *)(Walk + 4));
    DigestCount = ReadUnaligned32 ((UINT32 *)(Walk + 8));
    Walk       += 12;
    Digest      = NULL;
    DigestSize  = 0;
    if (DigestCount > AlgCount) {
      Malformed = TRUE;
      break;
    }

    for (Index = 0; Index < DigestCount; Index++) {
      AlgId = ReadUnaligned16 ((UINT16 *)Walk);
      for (Alg = 0; Alg < AlgCount && AlgIds[Alg] != AlgId; Alg++) {
      }

      if (Alg == AlgCount) {
        Malformed = TRUE;
        break;
      }

      if ((Digest == NULL) || (AlgId == 0x000B)) {
        Digest     = Walk + 2;
        DigestSize = AlgSizes[Alg];
      }

      Walk += 2 + AlgSizes[Alg];
    }

    if (Malformed) {
      break;
    }

    EventSize = ReadUnaligned32 ((UINT32 *)Walk);
    Walk     += 4;
    if (Digest != NULL) {
      FormatHex (Digest, DigestSize, DigestText, sizeof (DigestText));
    } else {
      DigestText[0] = L'\0';
    }

    DescribeEvent (Type, Walk, EventSize, DataText, sizeof (DataText));
    BdtOutRowBegin ();
    BdtOutCell (L"%d", Number);
    BdtOutCell (L"%d", Pcr);
    BdtOutCell (L"%s", EventName (Type));
    BdtOutCell (L"%d", EventSize);
    BdtOutCell (L"%s", DigestText);
    BdtOutCell (L"%s", DataText);
    BdtOutRowEnd ();
    Walk += EventSize;
    Number++;
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Event Count", Number - 1);
  if (Malformed) {
    BdtOutError (L"Malformed event record after event %d", Number - 1);
  }

  return EFI_SUCCESS;
}
