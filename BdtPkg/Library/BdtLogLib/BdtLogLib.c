/** @file
  BdtLogLib - firmware debug log: backend selection, line splitting and the
  log show / summary / export / clear / info commands.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include "BdtLogInternal.h"
#include <Guid/HobList.h>
#include <Library/HobLib.h>     // HOB walking macros only
#include <Library/ShellLib.h>
#include <Library/BdtFileLib.h>

#define LOG_MAX_ISSUES  200

//
// Backends in priority order. Add new ones here.
//
STATIC CONST BDT_LOG_BACKEND  *mBackends[] = {
  &gBdtLogAdvancedLogger,
  &gBdtLogMemDebugLog,
};

typedef struct {
  UINTN           Offset;
  UINTN           Length;
  BDT_LOG_ATTR    Attr;
} LOG_LINE;

typedef struct {
  CONST BDT_LOG_BACKEND    *Backend;
  BDT_LOG_SOURCE           Source;
  CHAR8                    *Text;
  UINTN                    TextUsed;
  UINTN                    TextSize;
  LOG_LINE                 *Lines;
  UINTN                    Count;
  UINTN                    Capacity;
  BOOLEAN                  Open;
  BOOLEAN                  OutOfMemory;
  EFI_STATUS               ReadStatus;
} LOG_DATA;

VOID *
BdtLogFindGuidHob (
  IN  CONST EFI_GUID  *Guid,
  OUT UINTN           *DataSize OPTIONAL
  )
{
  EFI_PEI_HOB_POINTERS  Hob;
  VOID                  *HobList;

  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiHobListGuid, &HobList)) || (HobList == NULL)) {
    return NULL;
  }

  for (Hob.Raw = HobList; !END_OF_HOB_LIST (Hob); Hob.Raw = GET_NEXT_HOB (Hob)) {
    if (Hob.Header->HobLength < sizeof (EFI_HOB_GENERIC_HEADER)) {
      break;
    }

    if ((GET_HOB_TYPE (Hob) == EFI_HOB_TYPE_GUID_EXTENSION) &&
        (Hob.Header->HobLength >= sizeof (EFI_HOB_GUID_TYPE)) && CompareGuid (&Hob.Guid->Name, Guid))
    {
      if (DataSize != NULL) {
        *DataSize = GET_GUID_HOB_DATA_SIZE (Hob.Guid);
      }

      return GET_GUID_HOB_DATA (Hob.Guid);
    }
  }

  return NULL;
}

//
// ---------------------------------------------------------------------------
// Collecting lines
// ---------------------------------------------------------------------------
//

STATIC
BOOLEAN
Grow (
  IN OUT VOID   **Buffer,
  IN OUT UINTN  *Capacity,
  IN     UINTN  Needed,
  IN     UINTN  ElementSize
  )
{
  VOID   *Grown;
  UINTN  NewCapacity;

  if (Needed <= *Capacity) {
    return TRUE;
  }

  NewCapacity = MAX (*Capacity * 2, MAX (Needed, 1024));
  Grown       = ReallocatePool (*Capacity * ElementSize, NewCapacity * ElementSize, *Buffer);
  if (Grown == NULL) {
    return FALSE;
  }

  *Buffer   = Grown;
  *Capacity = NewCapacity;
  return TRUE;
}

STATIC
VOID
CloseLine (
  IN OUT LOG_DATA  *Data
  )
{
  if (Data->Open) {
    Data->Lines[Data->Count - 1].Length = Data->TextUsed - Data->Lines[Data->Count - 1].Offset;
    Data->Open                          = FALSE;
  }
}

/**
  BDT_LOG_SINK: append a chunk, starting a new line after every '\n'. A line
  takes the attributes of the chunk that started it.
**/
STATIC
VOID
CollectSink (
  IN VOID                *Context,
  IN CONST CHAR8         *Text,
  IN UINTN               Length,
  IN CONST BDT_LOG_ATTR  *Attr
  )
{
  LOG_DATA  *Data;
  UINTN     Index;
  CHAR8     Char;

  Data = Context;
  if (Data->OutOfMemory ||
      !Grow ((VOID **)&Data->Text, &Data->TextSize, Data->TextUsed + Length + 1, sizeof (CHAR8)))
  {
    Data->OutOfMemory = TRUE;
    return;
  }

  for (Index = 0; Index < Length; Index++) {
    Char = Text[Index];
    if ((Char == '\r') || (Char == '\0')) {
      continue;
    }

    if (!Data->Open) {
      if (!Grow ((VOID **)&Data->Lines, &Data->Capacity, Data->Count + 1, sizeof (LOG_LINE))) {
        Data->OutOfMemory = TRUE;
        return;
      }

      Data->Lines[Data->Count].Offset = Data->TextUsed;
      Data->Lines[Data->Count].Length = 0;
      CopyMem (&Data->Lines[Data->Count].Attr, Attr, sizeof (BDT_LOG_ATTR));
      Data->Count++;
      Data->Open = TRUE;
    }

    if (Char == '\n') {
      CloseLine (Data);
      continue;
    }

    Data->Text[Data->TextUsed++] = ((Char >= 0x20) && (Char < 0x7F)) || (Char == '\t') ? Char : '.';
  }
}

STATIC
VOID
FreeData (
  IN OUT LOG_DATA  *Data
  )
{
  if (Data->Text != NULL) {
    FreePool (Data->Text);
  }

  if (Data->Lines != NULL) {
    FreePool (Data->Lines);
  }

  ZeroMem (Data, sizeof (*Data));
}

/**
  Find the first backend with a log. Read: also load its lines.
**/
STATIC
EFI_STATUS
LoadLog (
  OUT LOG_DATA  *Data,
  IN  BOOLEAN   Read
  )
{
  UINTN       Index;
  EFI_STATUS  Status;

  ZeroMem (Data, sizeof (*Data));
  for (Index = 0; Index < ARRAY_SIZE (mBackends); Index++) {
    ZeroMem (&Data->Source, sizeof (Data->Source));
    Data->Source.Backend = mBackends[Index]->Name;
    if (!EFI_ERROR (mBackends[Index]->Locate (&Data->Source))) {
      Data->Backend = mBackends[Index];
      break;
    }
  }

  if (Data->Backend == NULL) {
    BdtOutError (L"No firmware debug log found (run \"log info\" for the supported formats)");
    return EFI_NOT_FOUND;
  }

  if (!Read) {
    return EFI_SUCCESS;
  }

  Status           = Data->Backend->Read (&Data->Source, CollectSink, Data);
  Data->ReadStatus = Status;
  CloseLine (Data);
  if (Data->OutOfMemory) {
    FreeData (Data);
    return EFI_OUT_OF_RESOURCES;
  }

  if (EFI_ERROR (Status) && (Status != EFI_COMPROMISED_DATA)) {
    BdtOutError (L"Cannot read the %s log (%s)", Data->Backend->Name, BdtStatusName (Status));
    FreeData (Data);
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
CONST CHAR16 *
LevelName (
  IN  CONST BDT_LOG_ATTR  *Attr,
  OUT CHAR16              *Buffer,
  IN  UINTN               BufferSize
  )
{
  if (!Attr->HasLevel) {
    return L"-";
  }

  if ((Attr->Level & BDT_LOG_LEVEL_ERROR) != 0) {
    return L"ERROR";
  }

  if ((Attr->Level & BDT_LOG_LEVEL_WARN) != 0) {
    return L"WARN";
  }

  if ((Attr->Level & BDT_LOG_LEVEL_INFO) != 0) {
    return L"INFO";
  }

  if ((Attr->Level & BDT_LOG_LEVEL_VERBOSE) != 0) {
    return L"VERBOSE";
  }

  UnicodeSPrint (Buffer, BufferSize, L"0x%x", Attr->Level);
  return Buffer;
}

STATIC
CONST CHAR16 *
PhaseText (
  IN  LOG_DATA            *Data,
  IN  CONST BDT_LOG_ATTR  *Attr
  )
{
  CONST CHAR16  *Name;

  if (!Attr->HasLevel || (Data->Backend->PhaseName == NULL)) {
    return L"-";
  }

  Name = Data->Backend->PhaseName (Attr->Phase);
  return (Name != NULL) ? Name : L"?";
}

/**
  Copy a line into a CHAR16 buffer (truncated to fit).
**/
STATIC
VOID
LineText (
  IN  LOG_DATA        *Data,
  IN  CONST LOG_LINE  *Line,
  OUT CHAR16          *Buffer,
  IN  UINTN           Count
  )
{
  UINTN  Index;

  for (Index = 0; (Index < Line->Length) && (Index + 1 < Count); Index++) {
    Buffer[Index] = (CHAR16)(UINT8)Data->Text[Line->Offset + Index];
  }

  Buffer[Index] = L'\0';
}

/**
  Case insensitive search of an ASCII word in a line.
**/
STATIC
BOOLEAN
LineContains (
  IN LOG_DATA        *Data,
  IN CONST LOG_LINE  *Line,
  IN CONST CHAR8     *Word,
  IN BOOLEAN         IgnoreCase
  )
{
  UINTN  WordLength;
  UINTN  Start;
  UINTN  Index;
  CHAR8  A;
  CHAR8  B;

  WordLength = AsciiStrLen (Word);
  for (Start = 0; Start + WordLength <= Line->Length; Start++) {
    for (Index = 0; Index < WordLength; Index++) {
      A = Data->Text[Line->Offset + Start + Index];
      B = Word[Index];
      if (IgnoreCase) {
        A = ((A >= 'A') && (A <= 'Z')) ? (CHAR8)(A + 32) : A;
        B = ((B >= 'A') && (B <= 'Z')) ? (CHAR8)(B + 32) : B;
      }

      if (A != B) {
        break;
      }
    }

    if (Index == WordLength) {
      return TRUE;
    }
  }

  return FALSE;
}

typedef enum {
  IssueNone,
  IssueWarning,
  IssueError,
  IssueAssert
} LOG_ISSUE;

STATIC CONST CHAR16  *mIssueNames[] = { L"", L"WARNING", L"ERROR", L"ASSERT" };

/**
  ASSERT lines are found by text. Otherwise the debug level decides when the
  backend has one; for plain text logs "error" / "fail" / "warning" in the
  line (any case) are used.
**/
STATIC
LOG_ISSUE
Classify (
  IN LOG_DATA        *Data,
  IN CONST LOG_LINE  *Line
  )
{
  if (LineContains (Data, Line, "ASSERT", FALSE)) {
    return IssueAssert;
  }

  if (Line->Attr.HasLevel) {
    if ((Line->Attr.Level & BDT_LOG_LEVEL_ERROR) != 0) {
      return IssueError;
    }

    return ((Line->Attr.Level & BDT_LOG_LEVEL_WARN) != 0) ? IssueWarning : IssueNone;
  }

  if (LineContains (Data, Line, "error", TRUE) || LineContains (Data, Line, "fail", TRUE)) {
    return IssueError;
  }

  return LineContains (Data, Line, "warning", TRUE) ? IssueWarning : IssueNone;
}

STATIC
VOID
SourceFields (
  IN LOG_DATA  *Data
  )
{
  BdtOutField (L"Backend", L"%s", Data->Backend->Name);
  BdtOutField (L"Found Through", L"%s", Data->Source.Found);
  BdtOutFieldHex (L"Address", Data->Source.Address, 0);
  BdtOutFieldHex (L"Capacity", Data->Source.Capacity, 0);
  BdtOutFieldHex (L"Used", Data->Source.Used, 0);
  if (Data->Source.LostBytes != 0) {
    BdtOutFieldHex (L"Lost Bytes", Data->Source.LostBytes, 0);
  }

  BdtOutFieldBool (L"Messages Lost", Data->Source.Lost);
}

STATIC
VOID
LostNote (
  IN LOG_DATA  *Data
  )
{
  if (Data->Source.Lost) {
    BdtOutNote (
      L"The %s buffer was full: %s messages are missing.",
      Data->Backend->Name,
      (Data->Source.LostBytes != 0) ? L"newer" : L"older"
      );
  }

  if (Data->ReadStatus == EFI_COMPROMISED_DATA) {
    BdtOutNote (L"The log contains a damaged entry; the rest of the buffer was skipped.");
  }
}

//
// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
//

EFI_STATUS
EFIAPI
BdtLogInfo (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"backend",  L"Backend",  16 },
    { L"status",   L"Status",   14 },
    { L"address",  L"Address",  20 },
    { L"version",  L"Version",  9  },
    { L"capacity", L"Capacity", 10 },
    { L"used",     L"Used",     10 },
    { L"lost",     L"Lost",     6  },
    { L"found",    L"Found Through", 0 },
  };
  UINTN           Index;
  UINTN           Found;
  EFI_STATUS      Status;
  BDT_LOG_SOURCE  Source;

  Found = 0;
  BdtOutTableBegin (L"backends", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < ARRAY_SIZE (mBackends); Index++) {
    ZeroMem (&Source, sizeof (Source));
    Status = mBackends[Index]->Locate (&Source);
    BdtOutRowBegin ();
    BdtOutCell (L"%s", mBackends[Index]->Name);
    if (EFI_ERROR (Status)) {
      BdtOutCell (L"%s", (Status == EFI_NOT_FOUND) ? L"not present" : L"invalid");
      BdtOutCell (L"-");
      BdtOutCell (L"-");
      BdtOutCell (L"-");
      BdtOutCell (L"-");
      BdtOutCell (L"-");
      BdtOutCell (L"");
    } else {
      BdtOutCell (L"%s", (Found == 0) ? L"found (used)" : L"found");
      BdtOutCell (L"0x%016lx", Source.Address);
      BdtOutCell (L"%s", Source.Version);
      BdtOutCell (L"0x%lx", Source.Capacity);
      BdtOutCell (L"0x%lx", Source.Used);
      BdtOutCell (L"%s", Source.Lost ? L"yes" : L"no");
      BdtOutCell (L"%s", Source.Found);
      Found++;
    }

    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Logs Found", Found);
  if (Found == 0) {
    BdtOutNote (L"Enable an in memory debug log in the BIOS: Project Mu AdvLoggerPkg, or OVMF DEBUG_TO_MEM=TRUE.");
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtLogShow (
  IN UINTN  Last
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"line",    L"Line",  7  },
    { L"phase",   L"Phase", 9  },
    { L"level",   L"Level", 8  },
    { L"time_ms", L"ms",    10 },
    { L"text",    L"Text",  0  },
  };
  EFI_STATUS  Status;
  LOG_DATA    Data;
  UINTN       First;
  UINTN       Index;
  LOG_LINE    *Line;
  CHAR16      Text[512];
  CHAR16      Level[16];

  Status = LoadLog (&Data, TRUE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  First = ((Last != 0) && (Last < Data.Count)) ? Data.Count - Last : 0;
  if (BdtOutGetFormat () == BdtFormatText) {
    //
    // Text: the log as the firmware wrote it.
    //
    LostNote (&Data);
    for (Index = First; Index < Data.Count; Index++) {
      LineText (&Data, &Data.Lines[Index], Text, ARRAY_SIZE (Text));
      BdtOutRaw (Text);
      BdtOutRaw (L"\r\n");
    }
  } else {
    SourceFields (&Data);
    BdtOutTableBegin (L"lines", Columns, ARRAY_SIZE (Columns));
    for (Index = First; Index < Data.Count; Index++) {
      Line = &Data.Lines[Index];
      LineText (&Data, Line, Text, ARRAY_SIZE (Text));
      BdtOutRowBegin ();
      BdtOutCell (L"%d", Index + 1);
      BdtOutCell (L"%s", PhaseText (&Data, &Line->Attr));
      BdtOutCell (L"%s", LevelName (&Line->Attr, Level, sizeof (Level)));
      if (Line->Attr.HasLevel && (Data.Source.TimerFrequency != 0)) {
        BdtOutCell (L"%ld", DivU64x64Remainder (MultU64x32 (Line->Attr.TimeStamp, 1000), Data.Source.TimerFrequency, NULL));
      } else {
        BdtOutCell (L"-");
      }

      BdtOutCell (L"%s", Text);
      BdtOutRowEnd ();
    }

    BdtOutTableEnd ();
  }

  BdtOutFieldNum (L"Lines", Data.Count);
  FreeData (&Data);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtLogSummary (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  IssueColumns[] = {
    { L"line",  L"Line",  7  },
    { L"kind",  L"Kind",  9  },
    { L"phase", L"Phase", 9  },
    { L"text",  L"Text",  0  },
  };
  STATIC CONST BDT_COLUMN  PhaseColumns[] = {
    { L"phase",    L"Phase",    10 },
    { L"lines",    L"Lines",    8  },
    { L"errors",   L"Errors",   8  },
    { L"warnings", L"Warnings", 0  },
  };
  EFI_STATUS  Status;
  LOG_DATA    Data;
  UINTN       Index;
  UINTN       Phase;
  UINTN       Counts[4];
  UINTN       PhaseLines[16];
  UINTN       PhaseErrors[16];
  UINTN       PhaseWarnings[16];
  UINTN       Shown;
  LOG_ISSUE   Issue;
  BOOLEAN     HasPhases;
  CHAR16      Text[512];
  CHAR16      PhaseNumber[8];

  Status = LoadLog (&Data, TRUE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (Counts, sizeof (Counts));
  ZeroMem (PhaseLines, sizeof (PhaseLines));
  ZeroMem (PhaseErrors, sizeof (PhaseErrors));
  ZeroMem (PhaseWarnings, sizeof (PhaseWarnings));
  HasPhases = FALSE;
  SourceFields (&Data);
  BdtOutFieldNum (L"Lines", Data.Count);

  Shown = 0;
  BdtOutTableBegin (L"issues", IssueColumns, ARRAY_SIZE (IssueColumns));
  for (Index = 0; Index < Data.Count; Index++) {
    Issue = Classify (&Data, &Data.Lines[Index]);
    Counts[Issue]++;
    if (Data.Lines[Index].Attr.HasLevel && (Data.Backend->PhaseName != NULL)) {
      HasPhases = TRUE;
      Phase     = MIN (Data.Lines[Index].Attr.Phase, ARRAY_SIZE (PhaseLines) - 1);
      PhaseLines[Phase]++;
      if (Issue >= IssueError) {
        PhaseErrors[Phase]++;
      } else if (Issue == IssueWarning) {
        PhaseWarnings[Phase]++;
      }
    }

    if ((Issue == IssueNone) || (Shown >= LOG_MAX_ISSUES)) {
      continue;
    }

    LineText (&Data, &Data.Lines[Index], Text, ARRAY_SIZE (Text));
    BdtOutRowBegin ();
    BdtOutCell (L"%d", Index + 1);
    BdtOutCell (L"%s", mIssueNames[Issue]);
    BdtOutCell (L"%s", PhaseText (&Data, &Data.Lines[Index].Attr));
    BdtOutCell (L"%s", Text);
    BdtOutRowEnd ();
    Shown++;
  }

  BdtOutTableEnd ();
  if (Counts[IssueWarning] + Counts[IssueError] + Counts[IssueAssert] > Shown) {
    BdtOutNote (L"Only the first %d issues are listed.", LOG_MAX_ISSUES);
  }

  if (HasPhases) {
    BdtOutTableBegin (L"phases", PhaseColumns, ARRAY_SIZE (PhaseColumns));
    for (Phase = 0; Phase < ARRAY_SIZE (PhaseLines); Phase++) {
      if (PhaseLines[Phase] == 0) {
        continue;
      }

      UnicodeSPrint (PhaseNumber, sizeof (PhaseNumber), L"%d", Phase);
      BdtOutRowBegin ();
      BdtOutCell (
        L"%s",
        (Data.Backend->PhaseName ((UINT16)Phase) != NULL) ? Data.Backend->PhaseName ((UINT16)Phase) : PhaseNumber
        );
      BdtOutCell (L"%d", PhaseLines[Phase]);
      BdtOutCell (L"%d", PhaseErrors[Phase]);
      BdtOutCell (L"%d", PhaseWarnings[Phase]);
      BdtOutRowEnd ();
    }

    BdtOutTableEnd ();
  }

  BdtOutFieldNum (L"Asserts", Counts[IssueAssert]);
  BdtOutFieldNum (L"Errors", Counts[IssueError]);
  BdtOutFieldNum (L"Warnings", Counts[IssueWarning]);
  LostNote (&Data);
  if (BdtOutGetFormat () == BdtFormatText) {
    BdtOutNote (
      L"%s",
      (Data.Backend->PhaseName != NULL) ? L"Errors / warnings come from the DEBUG() level." :
      L"This log has no debug levels: lines with \"error\", \"fail\" or \"warning\" are counted."
      );
  }

  FreeData (&Data);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtLogExport (
  IN CONST CHAR16  *Path
  )
{
  EFI_STATUS         Status;
  LOG_DATA           Data;
  SHELL_FILE_HANDLE  Handle;
  UINTN              Index;

  Status = LoadLog (&Data, TRUE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = BdtFileCreate (Path, &Handle);
  if (!EFI_ERROR (Status)) {
    for (Index = 0; (Index < Data.Count) && !EFI_ERROR (Status); Index++) {
      Status = BdtFileWrite (Handle, Data.Text + Data.Lines[Index].Offset, Data.Lines[Index].Length);
      if (!EFI_ERROR (Status)) {
        Status = BdtFileWrite (Handle, "\r\n", 2);
      }
    }

    BdtFileClose (Handle);
  }

  if (EFI_ERROR (Status)) {
    BdtOutError (L"Cannot write %s (%s)", Path, BdtStatusName (Status));
  } else {
    BdtOutField (L"File", L"%s", Path);
    BdtOutField (L"Backend", L"%s", Data.Backend->Name);
    BdtOutFieldNum (L"Lines", Data.Count);
    LostNote (&Data);
  }

  FreeData (&Data);
  return Status;
}

EFI_STATUS
EFIAPI
BdtLogClear (
  IN BOOLEAN  Force
  )
{
  EFI_STATUS     Status;
  LOG_DATA       Data;
  EFI_INPUT_KEY  Key;
  UINTN          Index;

  Status = LoadLog (&Data, FALSE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!Force) {
    ShellPrintEx (
      -1,
      -1,
      L"Clear the %s log (0x%lx bytes at 0x%lx)? [y/N] ",
      Data.Backend->Name,
      Data.Source.Used,
      Data.Source.Address
      );
    while (TRUE) {
      gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
      if (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
        break;
      }
    }

    ShellPrintEx (-1, -1, L"%c\r\n", (Key.UnicodeChar >= 0x20) ? Key.UnicodeChar : L' ');
    if ((Key.UnicodeChar != L'y') && (Key.UnicodeChar != L'Y')) {
      return EFI_ABORTED;
    }
  }

  Status = Data.Backend->Clear (&Data.Source);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"Cannot clear the %s log (%s)", Data.Backend->Name, BdtStatusName (Status));
    return Status;
  }

  BdtOutField (L"Backend", L"%s", Data.Backend->Name);
  BdtOutFieldHex (L"Cleared Bytes", Data.Source.Used, 0);
  return EFI_SUCCESS;
}
