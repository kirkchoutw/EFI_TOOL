/** @file
  BdtBootLib - boot option / boot performance inspection.

  Boot timing comes from the ACPI FPDT: the basic boot performance record and
  the edk2 extended performance records (PERF_START / PERF_END) that
  FirmwarePerformanceDxe copies into the FBPT.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Guid/GlobalVariable.h>
#include <Library/DevicePathLib.h>
#include <Library/BdtAcpiLib.h>
#include <Library/BdtBootLib.h>
#include <Protocol/LoadedImage.h>

#define BDT_FPDT_SIGNATURE         SIGNATURE_32 ('F', 'P', 'D', 'T')
#define BDT_FBPT_SIGNATURE         SIGNATURE_32 ('F', 'B', 'P', 'T')
#define FPDT_TYPE_FBPT_POINTER     0x0000
#define FPDT_TYPE_BASIC_BOOT       0x0002
#define FPDT_TYPE_GUID_EVENT       0x1010
#define FPDT_TYPE_STRING_EVENT     0x1011
#define FPDT_TYPE_DUAL_GUID_STRING 0x1012
#define FPDT_TYPE_GUID_QWORD       0x1013
#define FPDT_TYPE_GUID_QWORD_STR   0x1014

//
// Offsets inside the extended performance records (packed).
//
#define FPDT_REC_PROGRESS_ID       4
#define FPDT_REC_TIMESTAMP         10
#define FPDT_REC_GUID              18
#define FPDT_REC_STRING            34

#define BDT_PERF_MAX_MODULES       512
#define BDT_PERF_MAX_PHASES        16
#define BDT_PERF_NAME_SIZE         40
#define BDT_PERF_KINDS             3       // entry point, LoadImage, DriverBinding.Start
#define BDT_PERF_TOP               40

typedef struct {
  EFI_GUID    Guid;
  CHAR8       Name[BDT_PERF_NAME_SIZE];
  UINT64      Time[BDT_PERF_KINDS];
  UINT64      Start[BDT_PERF_KINDS];
} BDT_PERF_MODULE;

typedef struct {
  CHAR8     Name[BDT_PERF_NAME_SIZE];
  UINT64    Start;
  UINT64    End;
} BDT_PERF_PHASE;

STATIC
BOOLEAN
IsBootOptionName (
  IN CONST CHAR16  *Name,
  OUT UINT16       *Number
  )
{
  UINT64  Value;

  if ((StrLen (Name) != 8) || (StrnCmp (Name, L"Boot", 4) != 0)) {
    return FALSE;
  }

  if (EFI_ERROR (BdtParseHex (Name + 4, &Value))) {
    return FALSE;
  }

  *Number = (UINT16)Value;
  return TRUE;
}

/**
  Parse an EFI_LOAD_OPTION. Pointers returned point into Data.
**/
STATIC
EFI_STATUS
ParseLoadOption (
  IN  UINT8                     *Data,
  IN  UINTN                     Size,
  OUT UINT32                    *Attributes,
  OUT CHAR16                    **Description,
  OUT EFI_DEVICE_PATH_PROTOCOL  **FilePath,
  OUT UINTN                     *FilePathLength,
  OUT UINT8                     **Optional,
  OUT UINTN                     *OptionalSize
  )
{
  UINTN  Offset;
  UINTN  DescSize;

  if (Size < sizeof (UINT32) + sizeof (UINT16) + sizeof (CHAR16)) {
    return EFI_VOLUME_CORRUPTED;
  }

  *Attributes     = ReadUnaligned32 ((UINT32 *)Data);
  *FilePathLength = ReadUnaligned16 ((UINT16 *)(Data + 4));
  *Description    = (CHAR16 *)(Data + 6);

  //
  // Description is a NULL terminated UCS-2 string inside the buffer.
  //
  for (DescSize = 0; 6 + DescSize + 1 < Size; DescSize += 2) {
    if (ReadUnaligned16 ((UINT16 *)(Data + 6 + DescSize)) == 0) {
      break;
    }
  }

  Offset = 6 + DescSize + 2;
  if ((Offset > Size) || (*FilePathLength > Size - Offset)) {
    return EFI_VOLUME_CORRUPTED;
  }

  *FilePath     = (EFI_DEVICE_PATH_PROTOCOL *)(Data + Offset);
  *Optional     = Data + Offset + *FilePathLength;
  *OptionalSize = Size - Offset - *FilePathLength;
  return EFI_SUCCESS;
}

/**
  Emit one table row for Boot####.
**/
STATIC
VOID
BootOptionRow (
  IN UINT16  Number
  )
{
  CHAR16                    Name[16];
  UINT8                     *Data;
  UINTN                     Size;
  UINT32                    Attributes;
  CHAR16                    *Description;
  EFI_DEVICE_PATH_PROTOCOL  *FilePath;
  UINTN                     FilePathLength;
  UINT8                     *Optional;
  UINTN                     OptionalSize;
  CHAR16                    *Text;

  UnicodeSPrint (Name, sizeof (Name), L"Boot%04X", Number);
  BdtOutRowBegin ();
  BdtOutCell (L"%04X", Number);
  if (EFI_ERROR (BdtReadVariable (Name, &gEfiGlobalVariableGuid, (VOID **)&Data, &Size, NULL))) {
    BdtOutCell (L"-");
    BdtOutCell (L"(missing)");
    BdtOutCell (L"");
    BdtOutRowEnd ();
    return;
  }

  if (EFI_ERROR (ParseLoadOption (Data, Size, &Attributes, &Description, &FilePath, &FilePathLength, &Optional, &OptionalSize))) {
    BdtOutCell (L"-");
    BdtOutCell (L"(corrupted)");
    BdtOutCell (L"");
  } else {
    Text = (FilePathLength >= sizeof (EFI_DEVICE_PATH_PROTOCOL)) ? ConvertDevicePathToText (FilePath, FALSE, TRUE) : NULL;
    BdtOutCell (L"%s", ((Attributes & LOAD_OPTION_ACTIVE) != 0) ? L"Yes" : L"No");
    BdtOutCell (L"%s", Description);
    BdtOutCell (L"%s", (Text != NULL) ? Text : L"");
    if (Text != NULL) {
      FreePool (Text);
    }
  }

  BdtOutRowEnd ();
  FreePool (Data);
}

STATIC CONST BDT_COLUMN  mBootColumns[] = {
  { L"option",      L"Option",      8  },
  { L"active",      L"Active",      8  },
  { L"description", L"Description", 32 },
  { L"device_path", L"Device Path", 0  },
};

STATIC
VOID
OutUint16Variable (
  IN CONST CHAR16  *Name
  )
{
  UINT16  Value;
  UINTN   Size;

  Size = sizeof (Value);
  if (!EFI_ERROR (gRT->GetVariable ((CHAR16 *)Name, &gEfiGlobalVariableGuid, NULL, &Size, &Value))) {
    BdtOutField (Name, L"%04X", Value);
  } else {
    BdtOutField (Name, L"Not Set");
  }
}

EFI_STATUS
EFIAPI
BdtBootOrder (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT16      *Order;
  UINTN       Size;
  UINTN       Index;
  UINT16      Timeout;
  UINTN       TimeoutSize;

  OutUint16Variable (L"BootCurrent");
  OutUint16Variable (L"BootNext");
  TimeoutSize = sizeof (Timeout);
  if (!EFI_ERROR (gRT->GetVariable (L"Timeout", &gEfiGlobalVariableGuid, NULL, &TimeoutSize, &Timeout))) {
    BdtOutFieldNum (L"Timeout", Timeout);
  }

  Status = BdtReadVariable (L"BootOrder", &gEfiGlobalVariableGuid, (VOID **)&Order, &Size, NULL);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"BootOrder not found (%s)", BdtStatusName (Status));
    return Status;
  }

  BdtOutTableBegin (L"boot_order", mBootColumns, ARRAY_SIZE (mBootColumns));
  for (Index = 0; Index < Size / sizeof (UINT16); Index++) {
    BootOptionRow (Order[Index]);
  }

  BdtOutTableEnd ();
  FreePool (Order);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtBootOptions (
  VOID
  )
{
  EFI_STATUS  Status;
  CHAR16      *Name;
  CHAR16      *NewName;
  UINTN       NameSize;
  UINTN       Size;
  EFI_GUID    Guid;
  UINT16      Number;

  NameSize = 64 * sizeof (CHAR16);
  Name     = AllocateZeroPool (NameSize);
  if (Name == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (&Guid, sizeof (Guid));
  BdtOutTableBegin (L"boot_options", mBootColumns, ARRAY_SIZE (mBootColumns));
  while (TRUE) {
    Size   = NameSize;
    Status = gRT->GetNextVariableName (&Size, Name, &Guid);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      NewName = ReallocatePool (NameSize, Size, Name);
      if (NewName == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        break;
      }

      Name     = NewName;
      NameSize = Size;
      Status   = gRT->GetNextVariableName (&Size, Name, &Guid);
    }

    if (EFI_ERROR (Status)) {
      break;
    }

    if (CompareGuid (&Guid, &gEfiGlobalVariableGuid) && IsBootOptionName (Name, &Number)) {
      BootOptionRow (Number);
    }
  }

  BdtOutTableEnd ();
  FreePool (Name);
  return (Status == EFI_NOT_FOUND) ? EFI_SUCCESS : Status;
}

EFI_STATUS
EFIAPI
BdtBootDump (
  IN UINT16  Number
  )
{
  EFI_STATUS                Status;
  CHAR16                    Name[16];
  UINT8                     *Data;
  UINTN                     Size;
  UINT32                    Attributes;
  UINT32                    VarAttributes;
  CHAR16                    *Description;
  EFI_DEVICE_PATH_PROTOCOL  *FilePath;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  UINTN                     FilePathLength;
  UINT8                     *Optional;
  UINTN                     OptionalSize;
  CHAR16                    *Text;

  UnicodeSPrint (Name, sizeof (Name), L"Boot%04X", Number);
  Status = BdtReadVariable (Name, &gEfiGlobalVariableGuid, (VOID **)&Data, &Size, &VarAttributes);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"%s not found (%s)", Name, BdtStatusName (Status));
    return Status;
  }

  BdtOutField (L"Option", L"%s", Name);
  BdtOutFieldHex (L"Variable Attributes", VarAttributes, 8);
  BdtOutFieldHex (L"Size", Size, 0);
  Status = ParseLoadOption (Data, Size, &Attributes, &Description, &FilePath, &FilePathLength, &Optional, &OptionalSize);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"%s is not a valid EFI_LOAD_OPTION", Name);
    BdtOutHexDump (L"Raw", 0, Data, Size);
    FreePool (Data);
    return Status;
  }

  BdtOutFieldHex (L"Attributes", Attributes, 8);
  BdtOutFieldBool (L"Active", (Attributes & LOAD_OPTION_ACTIVE) != 0);
  BdtOutFieldBool (L"Force Reconnect", (Attributes & LOAD_OPTION_FORCE_RECONNECT) != 0);
  BdtOutFieldBool (L"Hidden", (Attributes & LOAD_OPTION_HIDDEN) != 0);
  BdtOutField (L"Category", L"%s", ((Attributes & LOAD_OPTION_CATEGORY) == LOAD_OPTION_CATEGORY_APP) ? L"Application" : L"Boot");
  BdtOutField (L"Description", L"%s", Description);
  BdtOutFieldNum (L"File Path List Length", FilePathLength);

  if (FilePathLength >= sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
    Text = ConvertDevicePathToText (FilePath, FALSE, TRUE);
    BdtOutField (L"Device Path", L"%s", (Text != NULL) ? Text : L"?");
    if (Text != NULL) {
      FreePool (Text);
    }

    //
    // The last media file path node is the loader file path.
    //
    for (Node = FilePath; !IsDevicePathEnd (Node) && (UINT8 *)Node < (UINT8 *)FilePath + FilePathLength;
         Node = NextDevicePathNode (Node))
    {
      if (DevicePathNodeLength (Node) < sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
        break;
      }

      if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) && (DevicePathSubType (Node) == MEDIA_FILEPATH_DP)) {
        BdtOutField (L"File Path", L"%s", ((FILEPATH_DEVICE_PATH *)Node)->PathName);
      }
    }
  }

  BdtOutFieldNum (L"Optional Data Size", OptionalSize);
  if (OptionalSize > 0) {
    BdtOutHexDump (L"Optional Data", 0, Optional, OptionalSize);
  }

  FreePool (Data);
  return EFI_SUCCESS;
}

STATIC
VOID
FormatNs (
  IN  UINT64  Ns,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  UINT64  Us;

  Us = DivU64x32 (Ns, 1000);
  UnicodeSPrint (Buffer, BufferSize, L"%lu.%03lu ms", DivU64x32 (Us, 1000), ModU64x32 (Us, 1000));
}

STATIC
BDT_PERF_MODULE *
FindModule (
  IN BDT_PERF_MODULE  *Modules,
  IN UINTN            *Count,
  IN CONST EFI_GUID   *Guid
  )
{
  UINTN  Index;

  for (Index = 0; Index < *Count; Index++) {
    if (CompareGuid (&Modules[Index].Guid, Guid)) {
      return &Modules[Index];
    }
  }

  if (*Count >= BDT_PERF_MAX_MODULES) {
    return NULL;
  }

  ZeroMem (&Modules[*Count], sizeof (BDT_PERF_MODULE));
  CopyGuid (&Modules[*Count].Guid, Guid);
  return &Modules[(*Count)++];
}

/**
  Map a progress ID to a timing kind (0 entry, 1 LoadImage, 2 DB.Start) and
  report whether it is the start or end event. Returns FALSE for others.
**/
STATIC
BOOLEAN
ProgressKind (
  IN  UINT16   ProgressId,
  OUT UINTN    *Kind,
  OUT BOOLEAN  *IsStart
  )
{
  switch (ProgressId) {
    case 0x01:
    case 0x02:
      *Kind = 0;
      break;
    case 0x03:
    case 0x04:
      *Kind = 1;
      break;
    case 0x05:
    case 0x06:
      *Kind = 2;
      break;
    default:
      return FALSE;
  }

  *IsStart = (ProgressId & 1) != 0;
  return TRUE;
}

/**
  Parse FBPT extended records into module and phase timing.
**/
STATIC
VOID
ParseExtendedRecords (
  IN     UINT8            *Fbpt,
  IN     UINT32           Length,
  IN OUT BDT_PERF_MODULE  *Modules,
  IN OUT UINTN            *ModuleCount,
  IN OUT BDT_PERF_PHASE   *Phases,
  IN OUT UINTN            *PhaseCount
  )
{
  UINT32           Offset;
  UINT16           Type;
  UINT8            RecLength;
  UINT16           ProgressId;
  UINT64           Timestamp;
  EFI_GUID         *Guid;
  CHAR8            Name[BDT_PERF_NAME_SIZE];
  UINTN            NameLength;
  UINTN            Index;
  UINTN            Kind;
  BOOLEAN          IsStart;
  BDT_PERF_MODULE  *Module;

  for (Offset = 8; Offset + 4 <= Length; Offset += RecLength) {
    Type      = ReadUnaligned16 ((UINT16 *)(Fbpt + Offset));
    RecLength = Fbpt[Offset + 2];
    if ((RecLength < 4) || (Offset + RecLength > Length)) {
      break;
    }

    if ((Type < FPDT_TYPE_GUID_EVENT) || (Type > FPDT_TYPE_GUID_QWORD_STR) || (RecLength < FPDT_REC_STRING)) {
      continue;
    }

    ProgressId = ReadUnaligned16 ((UINT16 *)(Fbpt + Offset + FPDT_REC_PROGRESS_ID));
    Timestamp  = ReadUnaligned64 ((UINT64 *)(Fbpt + Offset + FPDT_REC_TIMESTAMP));
    Guid       = (EFI_GUID *)(Fbpt + Offset + FPDT_REC_GUID);

    //
    // Optional ASCII string (module or token name).
    //
    Name[0]    = '\0';
    NameLength = 0;
    if ((Type == FPDT_TYPE_STRING_EVENT) || (Type == FPDT_TYPE_DUAL_GUID_STRING) || (Type == FPDT_TYPE_GUID_QWORD_STR)) {
      Index = Offset + FPDT_REC_STRING + ((Type == FPDT_TYPE_STRING_EVENT) ? 0 : 16);
      for ( ; Index < (UINTN)Offset + RecLength && Fbpt[Index] != 0 && NameLength + 1 < sizeof (Name); Index++) {
        Name[NameLength++] = ((Fbpt[Index] >= 0x20) && (Fbpt[Index] < 0x7F)) ? (CHAR8)Fbpt[Index] : '?';
      }

      Name[NameLength] = '\0';
    }

    if (((ProgressId == 0x50) || (ProgressId == 0x51)) && (NameLength > 0)) {
      for (Index = 0; Index < *PhaseCount; Index++) {
        if (AsciiStrCmp (Phases[Index].Name, Name) == 0) {
          break;
        }
      }

      if ((Index == *PhaseCount) && (*PhaseCount < BDT_PERF_MAX_PHASES)) {
        ZeroMem (&Phases[Index], sizeof (Phases[Index]));
        AsciiStrCpyS (Phases[Index].Name, sizeof (Phases[Index].Name), Name);
        (*PhaseCount)++;
      }

      if (Index < *PhaseCount) {
        if ((ProgressId == 0x50) && (Phases[Index].Start == 0)) {
          Phases[Index].Start = Timestamp;
        } else if (ProgressId == 0x51) {
          Phases[Index].End = Timestamp;
        }
      }

      continue;
    }

    if (!ProgressKind (ProgressId, &Kind, &IsStart)) {
      continue;
    }

    Module = FindModule (Modules, ModuleCount, Guid);
    if (Module == NULL) {
      continue;
    }

    if ((Module->Name[0] == '\0') && (NameLength > 0)) {
      AsciiStrCpyS (Module->Name, sizeof (Module->Name), Name);
    }

    if (IsStart) {
      Module->Start[Kind] = Timestamp;
    } else if ((Module->Start[Kind] != 0) && (Timestamp >= Module->Start[Kind])) {
      Module->Time[Kind] += Timestamp - Module->Start[Kind];
      Module->Start[Kind] = 0;
    }
  }
}

EFI_STATUS
EFIAPI
BdtBootTiming (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  PhaseColumns[] = {
    { L"phase",    L"Phase",    10 },
    { L"start",    L"Start",    18 },
    { L"end",      L"End",      18 },
    { L"duration", L"Duration", 0  },
  };
  STATIC CONST BDT_COLUMN  ModuleColumns[] = {
    { L"module",      L"Module",      34 },
    { L"total",       L"Total",       14 },
    { L"entry",       L"Entry",       14 },
    { L"load_image",  L"LoadImage",   14 },
    { L"binding",     L"DB.Start",    0  },
  };
  EFI_STATUS                   Status;
  EFI_ACPI_DESCRIPTION_HEADER  *Fpdt;
  UINT32                       Offset;
  UINT8                        *Record;
  UINT8                        *Fbpt;
  UINT32                       FbptLength;
  UINT8                        *Basic;
  BDT_PERF_MODULE              *Modules;
  UINTN                        ModuleCount;
  BDT_PERF_PHASE               Phases[BDT_PERF_MAX_PHASES];
  UINTN                        PhaseCount;
  UINTN                        Index;
  UINTN                        Best;
  UINTN                        Shown;
  UINT64                       Total;
  UINT64                       BestTotal;
  CHAR16                       Text[4][24];
  CHAR16                       GuidText[40];

  Status = BdtAcpiFindTable (BDT_FPDT_SIGNATURE, 0, &Fpdt);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"ACPI FPDT not found; enable FirmwarePerformanceDxe and a PerformanceLib in the BIOS");
    return Status;
  }

  Fbpt = NULL;
  for (Offset = sizeof (EFI_ACPI_DESCRIPTION_HEADER); Offset + 4 <= Fpdt->Length; Offset += Record[2]) {
    Record = (UINT8 *)Fpdt + Offset;
    if (Record[2] < 4) {
      break;
    }

    if ((ReadUnaligned16 ((UINT16 *)Record) == FPDT_TYPE_FBPT_POINTER) && (Record[2] >= 16)) {
      Fbpt = (UINT8 *)(UINTN)ReadUnaligned64 ((UINT64 *)(Record + 8));
    }
  }

  if ((Fbpt == NULL) || (ReadUnaligned32 ((UINT32 *)Fbpt) != BDT_FBPT_SIGNATURE)) {
    BdtOutError (L"Firmware Basic Boot Performance Table (FBPT) not found");
    return EFI_NOT_FOUND;
  }

  FbptLength = ReadUnaligned32 ((UINT32 *)(Fbpt + 4));
  BdtOutFieldHex (L"FBPT Address", (UINTN)Fbpt, 0);
  BdtOutFieldHex (L"FBPT Length", FbptLength, 0);

  //
  // Basic boot performance record (always the first record of the FBPT).
  //
  Basic = Fbpt + 8;
  if ((FbptLength >= 8 + 48) && (ReadUnaligned16 ((UINT16 *)Basic) == FPDT_TYPE_BASIC_BOOT)) {
    BdtOutSectionBegin (L"Basic Boot Performance");
    FormatNs (ReadUnaligned64 ((UINT64 *)(Basic + 8)), Text[0], sizeof (Text[0]));
    BdtOutField (L"Reset End", L"%s", Text[0]);
    FormatNs (ReadUnaligned64 ((UINT64 *)(Basic + 16)), Text[0], sizeof (Text[0]));
    BdtOutField (L"OS Loader LoadImage", L"%s", Text[0]);
    FormatNs (ReadUnaligned64 ((UINT64 *)(Basic + 24)), Text[0], sizeof (Text[0]));
    BdtOutField (L"OS Loader StartImage", L"%s", Text[0]);
    FormatNs (ReadUnaligned64 ((UINT64 *)(Basic + 32)), Text[0], sizeof (Text[0]));
    BdtOutField (L"ExitBootServices Entry", L"%s", Text[0]);
    FormatNs (ReadUnaligned64 ((UINT64 *)(Basic + 40)), Text[0], sizeof (Text[0]));
    BdtOutField (L"ExitBootServices Exit", L"%s", Text[0]);
    BdtOutSectionEnd ();
  }

  Modules = AllocateZeroPool (BDT_PERF_MAX_MODULES * sizeof (BDT_PERF_MODULE));
  if (Modules == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ModuleCount = 0;
  PhaseCount  = 0;
  ParseExtendedRecords (Fbpt, FbptLength, Modules, &ModuleCount, Phases, &PhaseCount);

  BdtOutTableBegin (L"phases", PhaseColumns, ARRAY_SIZE (PhaseColumns));
  for (Index = 0; Index < PhaseCount; Index++) {
    FormatNs (Phases[Index].Start, Text[0], sizeof (Text[0]));
    FormatNs (Phases[Index].End, Text[1], sizeof (Text[1]));
    if (Phases[Index].End >= Phases[Index].Start) {
      FormatNs (Phases[Index].End - Phases[Index].Start, Text[2], sizeof (Text[2]));
    } else {
      StrCpyS (Text[2], ARRAY_SIZE (Text[2]), L"in progress");
    }

    BdtOutRowBegin ();
    BdtOutCell (L"%a", Phases[Index].Name);
    BdtOutCell (L"%s", Text[0]);
    BdtOutCell (L"%s", Text[1]);
    BdtOutCell (L"%s", Text[2]);
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();

  //
  // Modules, slowest first (selection of the top entries).
  //
  BdtOutTableBegin (L"modules", ModuleColumns, ARRAY_SIZE (ModuleColumns));
  for (Shown = 0; Shown < BDT_PERF_TOP; Shown++) {
    Best      = ModuleCount;
    BestTotal = 0;
    for (Index = 0; Index < ModuleCount; Index++) {
      Total = Modules[Index].Time[0] + Modules[Index].Time[1] + Modules[Index].Time[2];
      if (Total > BestTotal) {
        BestTotal = Total;
        Best      = Index;
      }
    }

    if (Best == ModuleCount) {
      break;
    }

    FormatNs (BestTotal, Text[0], sizeof (Text[0]));
    FormatNs (Modules[Best].Time[0], Text[1], sizeof (Text[1]));
    FormatNs (Modules[Best].Time[1], Text[2], sizeof (Text[2]));
    FormatNs (Modules[Best].Time[2], Text[3], sizeof (Text[3]));
    BdtOutRowBegin ();
    if (Modules[Best].Name[0] != '\0') {
      BdtOutCell (L"%a", Modules[Best].Name);
    } else {
      BdtOutCell (L"%s", BdtGuidToString (&Modules[Best].Guid, GuidText, sizeof (GuidText)));
    }

    BdtOutCell (L"%s", Text[0]);
    BdtOutCell (L"%s", Text[1]);
    BdtOutCell (L"%s", Text[2]);
    BdtOutCell (L"%s", Text[3]);
    BdtOutRowEnd ();
    ZeroMem (Modules[Best].Time, sizeof (Modules[Best].Time));
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Modules With Timing", ModuleCount);
  if (ModuleCount == 0) {
    BdtOutNote (L"No extended performance records; build the BIOS with PcdPerformanceLibraryPropertyMask = 1.");
  }

  FreePool (Modules);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtBootModules (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"order",     L"#",         6  },
    { L"base",      L"Base",      20 },
    { L"size",      L"Size",      12 },
    { L"type",      L"Type",      8  },
    { L"file_path", L"File Path", 0  },
  };
  EFI_STATUS                 Status;
  EFI_HANDLE                 *Handles;
  UINTN                      Count;
  UINTN                      Index;
  EFI_LOADED_IMAGE_PROTOCOL  *Image;
  CHAR16                     *Text;
  CONST CHAR16               *Type;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiLoadedImageProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  BdtOutFieldNum (L"Image Count", Count);
  BdtOutTableBegin (L"images", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiLoadedImageProtocolGuid, (VOID **)&Image))) {
      continue;
    }

    switch (Image->ImageCodeType) {
      case EfiRuntimeServicesCode:
        Type = L"RT";
        break;
      case EfiBootServicesCode:
        Type = L"BS";
        break;
      case EfiLoaderCode:
        Type = L"APP";
        break;
      default:
        Type = L"?";
        break;
    }

    Text = (Image->FilePath != NULL) ? ConvertDevicePathToText (Image->FilePath, FALSE, TRUE) : NULL;
    BdtOutRowBegin ();
    BdtOutCell (L"%d", Index + 1);
    BdtOutCell (L"0x%016lx", (UINT64)(UINTN)Image->ImageBase);
    BdtOutCell (L"0x%lx", Image->ImageSize);
    BdtOutCell (L"%s", Type);
    BdtOutCell (L"%s", (Text != NULL) ? Text : L"");
    BdtOutRowEnd ();
    if (Text != NULL) {
      FreePool (Text);
    }
  }

  BdtOutTableEnd ();
  FreePool (Handles);
  return EFI_SUCCESS;
}
