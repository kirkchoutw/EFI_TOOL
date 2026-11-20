/** @file
  BdtAcpiLib - AML namespace parser for DSDT / SSDT.

  Walks the AML term lists the way the ACPI table loader does and records
  every named object (Device, Method, Name, OperationRegion, Field unit,
  Processor, PowerResource, ThermalZone, Mutex, Event, Alias, External,
  buffer fields). Method bodies are skipped; their argument counts are
  collected in a first pass so that method calls in load time code
  (If predicates, OperationRegion offsets...) can be parsed.

  Nothing is evaluated: values are shown only when they are constants.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Library/BdtFileLib.h>
#include <Library/BdtAcpiLib.h>

#define AML_MAX_SOURCES   64
#define AML_MAX_DEPTH     48
#define AML_MAX_SEGS      32
#define AML_PATH_MAX      200
#define AML_VALUE_MAX     96
#define AML_MAX_ERRORS    64
#define AML_MAX_LENGTH    SIZE_16MB
#define AML_HEADER_SIZE   36

#define AML_IS_LEAD(c)        ((((c) >= 'A') && ((c) <= 'Z')) || ((c) == '_'))
#define AML_IS_NAMECHAR(c)    (AML_IS_LEAD (c) || (((c) >= '0') && ((c) <= '9')))
#define AML_IS_NAME_START(c)  (AML_IS_LEAD (c) || ((c) == '\\') || ((c) == '^') || ((c) == 0x2E) || ((c) == 0x2F))

typedef enum {
  AmlDevice,
  AmlMethod,
  AmlName,
  AmlProcessor,
  AmlPowerResource,
  AmlThermalZone,
  AmlOpRegion,
  AmlField,
  AmlMutex,
  AmlEvent,
  AmlAlias,
  AmlExternal,
  AmlBufferField,
  AmlDataRegion,
  AmlTypeMax
} AML_TYPE;

STATIC CONST CHAR16  *mAmlTypeNames[AmlTypeMax] = {
  L"Device",
  L"Method",
  L"Name",
  L"Processor",
  L"PowerResource",
  L"ThermalZone",
  L"OperationRegion",
  L"Field",
  L"Mutex",
  L"Event",
  L"Alias",
  L"External",
  L"BufferField",
  L"DataRegion",
};

typedef struct {
  BOOLEAN    Root;
  UINTN      Parents;
  UINTN      Count;
  CHAR8      Seg[AML_MAX_SEGS][4];
} AML_NAME;

typedef struct {
  UINT8      Type;
  UINT8      Source;
  BOOLEAN    Conditional;
  UINT32     Offset;
  CHAR8      Path[AML_PATH_MAX];
  CHAR16     Value[AML_VALUE_MAX];
} AML_OBJECT;

typedef struct {
  CHAR8      Path[AML_PATH_MAX];
  UINT8      Args;
  BOOLEAN    Defined;   ///< Method() seen, not only External()
} AML_METHOD;

typedef struct {
  UINT8     Source;
  UINT32    Offset;
  CHAR16    Message[64];
} AML_ERROR;

typedef struct {
  CONST UINT8    *Table;
  UINT32         Length;
  BOOLEAN        Selected;
  CHAR16         Name[16];
} AML_SOURCE;

typedef struct {
  AML_SOURCE     Sources[AML_MAX_SOURCES];
  UINTN          SourceCount;
  UINTN          Current;
  BOOLEAN        Record;
  AML_OBJECT     *Objects;
  UINTN          ObjectCount;
  UINTN          ObjectCapacity;
  AML_METHOD     *Methods;
  UINTN          MethodCount;
  UINTN          MethodCapacity;
  AML_ERROR      Errors[AML_MAX_ERRORS];
  UINTN          ErrorCount;
} AML_STATE;

//
// Argument patterns of the opcodes that may appear in an expression or as
// a load time statement:
//   T TermArg   S SuperName / Target / data reference (no method call)
//   N NameString   b/w/d/q byte/word/dword/qword   A ASCII string
//
typedef struct {
  UINT8          Op;
  CONST CHAR8    *Args;
} AML_OP;

STATIC CONST AML_OP  mAmlOps[] = {
  { 0x00, ""       }, { 0x01, ""       }, { 0xFF, ""       },
  { 0x0A, "b"      }, { 0x0B, "w"      }, { 0x0C, "d"      }, { 0x0E, "q" }, { 0x0D, "A" },
  { 0x06, "NN"     }, { 0x08, "NS"     }, { 0x15, "Nbb"    },
  { 0x70, "TS"     }, { 0x71, "S"      }, { 0x72, "TTS"    }, { 0x73, "TTS"  },
  { 0x74, "TTS"    }, { 0x75, "S"      }, { 0x76, "S"      }, { 0x77, "TTS"  },
  { 0x78, "TTSS"   }, { 0x79, "TTS"    }, { 0x7A, "TTS"    }, { 0x7B, "TTS"  },
  { 0x7C, "TTS"    }, { 0x7D, "TTS"    }, { 0x7E, "TTS"    }, { 0x7F, "TTS"  },
  { 0x80, "TS"     }, { 0x81, "TS"     }, { 0x82, "TS"     }, { 0x83, "T"    },
  { 0x84, "TTS"    }, { 0x85, "TTS"    }, { 0x86, "ST"     }, { 0x87, "S"    },
  { 0x88, "TTS"    }, { 0x89, "TbTbTT" }, { 0x8A, "TTN"    }, { 0x8B, "TTN"  },
  { 0x8C, "TTN"    }, { 0x8D, "TTN"    }, { 0x8E, "S"      }, { 0x8F, "TTN"  },
  { 0x90, "TT"     }, { 0x91, "TT"     }, { 0x92, "T"      }, { 0x93, "TT"   },
  { 0x94, "TT"     }, { 0x95, "TT"     }, { 0x96, "TS"     }, { 0x97, "TS"   },
  { 0x98, "TS"     }, { 0x99, "TS"     }, { 0x9C, "TTS"    }, { 0x9D, "TS"   },
  { 0x9E, "TTTS"   }, { 0x9F, ""       }, { 0xA3, ""       }, { 0xA4, "T"    },
  { 0xA5, ""       }, { 0xCC, ""       },
};

STATIC CONST AML_OP  mAmlExtOps[] = {
  { 0x01, "Nb"     }, { 0x02, "N"      }, { 0x12, "SS"     }, { 0x13, "TTTN" },
  { 0x1F, "TTTTTT" }, { 0x20, "NS"     }, { 0x21, "T"      }, { 0x22, "T"    },
  { 0x23, "Sw"     }, { 0x24, "S"      }, { 0x25, "ST"     }, { 0x26, "S"    },
  { 0x27, "S"      }, { 0x28, "TS"     }, { 0x29, "TS"     }, { 0x2A, "S"    },
  { 0x30, ""       }, { 0x31, ""       }, { 0x32, "bdT"    }, { 0x33, ""     },
  { 0x80, "NbTT"   }, { 0x88, "NTTT"   },
};

STATIC CONST CHAR16  *mRegionSpaces[] = {
  L"SystemMemory", L"SystemIO",        L"PCI_Config", L"EmbeddedControl",
  L"SMBus",        L"SystemCMOS",      L"PciBarTarget", L"IPMI",
  L"GeneralPurposeIO", L"GenericSerialBus", L"PCC", L"PRM",
};

STATIC CONST CHAR16  *mExternalTypes[] = {
  L"Unknown",  L"Integer", L"String",       L"Buffer",    L"Package",
  L"FieldUnit", L"Device", L"Event",        L"Method",    L"Mutex",
  L"OperationRegion", L"PowerResource", L"Processor", L"ThermalZone",
  L"BufferField", L"DDBHandle",
};

//
// ---------------------------------------------------------------------------
// Bookkeeping
// ---------------------------------------------------------------------------
//

STATIC
VOID
AmlError (
  IN OUT AML_STATE    *State,
  IN     CONST UINT8  *At,
  IN     CONST CHAR16 *Message
  )
{
  AML_ERROR  *Error;

  if (!State->Record) {
    return;
  }

  if (State->ErrorCount < AML_MAX_ERRORS) {
    Error         = &State->Errors[State->ErrorCount];
    Error->Source = (UINT8)State->Current;
    Error->Offset = (UINT32)(At - State->Sources[State->Current].Table);
    StrCpyS (Error->Message, ARRAY_SIZE (Error->Message), Message);
  }

  State->ErrorCount++;
}

STATIC
AML_OBJECT *
AmlRecord (
  IN OUT AML_STATE    *State,
  IN     AML_TYPE     Type,
  IN     CONST CHAR8  *Path,
  IN     CONST UINT8  *At,
  IN     BOOLEAN      Conditional
  )
{
  AML_OBJECT  *Grown;
  AML_OBJECT  *Object;
  UINTN       NewCapacity;

  if (!State->Record) {
    return NULL;
  }

  if (State->ObjectCount == State->ObjectCapacity) {
    NewCapacity = (State->ObjectCapacity == 0) ? 256 : State->ObjectCapacity * 2;
    Grown       = ReallocatePool (
                    State->ObjectCapacity * sizeof (AML_OBJECT),
                    NewCapacity * sizeof (AML_OBJECT),
                    State->Objects
                    );
    if (Grown == NULL) {
      return NULL;
    }

    State->Objects        = Grown;
    State->ObjectCapacity = NewCapacity;
  }

  Object = &State->Objects[State->ObjectCount++];
  ZeroMem (Object, sizeof (*Object));
  Object->Type        = (UINT8)Type;
  Object->Source      = (UINT8)State->Current;
  Object->Conditional = Conditional;
  Object->Offset      = (UINT32)(At - State->Sources[State->Current].Table);
  AsciiStrCpyS (Object->Path, AML_PATH_MAX, Path);
  return Object;
}

STATIC
INTN
AmlFindMethod (
  IN AML_STATE    *State,
  IN CONST CHAR8  *Path
  )
{
  UINTN  Index;

  for (Index = 0; Index < State->MethodCount; Index++) {
    if (AsciiStrCmp (State->Methods[Index].Path, Path) == 0) {
      return State->Methods[Index].Args;
    }
  }

  return -1;
}

/**
  Remember a method. A Method() definition overrides the argument count of
  an earlier External(), which is often declared without one.
**/
STATIC
VOID
AmlAddMethod (
  IN OUT AML_STATE    *State,
  IN     CONST CHAR8  *Path,
  IN     UINT8        Args,
  IN     BOOLEAN      Defined
  )
{
  AML_METHOD  *Grown;
  UINTN       NewCapacity;
  UINTN       Index;

  for (Index = 0; Index < State->MethodCount; Index++) {
    if (AsciiStrCmp (State->Methods[Index].Path, Path) == 0) {
      if (Defined && !State->Methods[Index].Defined) {
        State->Methods[Index].Args    = Args;
        State->Methods[Index].Defined = TRUE;
      }

      return;
    }
  }

  if (State->MethodCount == State->MethodCapacity) {
    NewCapacity = (State->MethodCapacity == 0) ? 128 : State->MethodCapacity * 2;
    Grown       = ReallocatePool (
                    State->MethodCapacity * sizeof (AML_METHOD),
                    NewCapacity * sizeof (AML_METHOD),
                    State->Methods
                    );
    if (Grown == NULL) {
      return;
    }

    State->Methods        = Grown;
    State->MethodCapacity = NewCapacity;
  }

  AsciiStrCpyS (State->Methods[State->MethodCount].Path, AML_PATH_MAX, Path);
  State->Methods[State->MethodCount].Args    = Args;
  State->Methods[State->MethodCount].Defined = Defined;
  State->MethodCount++;
}

//
// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
//

/**
  Decode a PkgLength. PkgEnd receives the end of the package (the length
  counts its own encoding bytes).
**/
STATIC
BOOLEAN
AmlPkgLength (
  IN OUT CONST UINT8  **Pos,
  IN     CONST UINT8  *End,
  OUT    CONST UINT8  **PkgEnd
  )
{
  CONST UINT8  *Start;
  UINTN        Count;
  UINTN        Index;
  UINT32       Length;

  Start = *Pos;
  if (Start >= End) {
    return FALSE;
  }

  Count = Start[0] >> 6;
  if ((UINTN)(End - Start) < 1 + Count) {
    return FALSE;
  }

  if (Count == 0) {
    Length = Start[0] & 0x3F;
  } else {
    Length = Start[0] & 0x0F;
    for (Index = 1; Index <= Count; Index++) {
      Length |= (UINT32)Start[Index] << (4 + 8 * (Index - 1));
    }
  }

  if ((Length < 1 + Count) || (Length > (UINTN)(End - Start))) {
    return FALSE;
  }

  *Pos    = Start + 1 + Count;
  *PkgEnd = Start + Length;
  return TRUE;
}

/**
  Decode a PkgLength used as a plain number (field widths).
**/
STATIC
BOOLEAN
AmlPkgValue (
  IN OUT CONST UINT8  **Pos,
  IN     CONST UINT8  *End,
  OUT    UINT32       *Value
  )
{
  CONST UINT8  *Start;
  UINTN        Count;
  UINTN        Index;

  Start = *Pos;
  if (Start >= End) {
    return FALSE;
  }

  Count = Start[0] >> 6;
  if ((UINTN)(End - Start) < 1 + Count) {
    return FALSE;
  }

  if (Count == 0) {
    *Value = Start[0] & 0x3F;
  } else {
    *Value = Start[0] & 0x0F;
    for (Index = 1; Index <= Count; Index++) {
      *Value |= (UINT32)Start[Index] << (4 + 8 * (Index - 1));
    }
  }

  *Pos = Start + 1 + Count;
  return TRUE;
}

STATIC
BOOLEAN
AmlParseName (
  IN OUT CONST UINT8  **Pos,
  IN     CONST UINT8  *End,
  OUT    AML_NAME     *Name
  )
{
  CONST UINT8  *Walk;
  UINTN        Index;

  ZeroMem (Name, sizeof (*Name));
  Walk = *Pos;
  if ((Walk < End) && (*Walk == '\\')) {
    Name->Root = TRUE;
    Walk++;
  } else {
    while ((Walk < End) && (*Walk == '^')) {
      Name->Parents++;
      Walk++;
    }
  }

  if (Walk >= End) {
    return FALSE;
  }

  if (*Walk == 0x00) {
    Walk++;
  } else {
    if (*Walk == 0x2E) {
      Name->Count = 2;
      Walk++;
    } else if (*Walk == 0x2F) {
      if (Walk + 1 >= End) {
        return FALSE;
      }

      Name->Count = Walk[1];
      Walk       += 2;
    } else {
      Name->Count = 1;
    }

    if ((Name->Count == 0) || (Name->Count > AML_MAX_SEGS) || ((UINTN)(End - Walk) < Name->Count * 4)) {
      return FALSE;
    }

    for (Index = 0; Index < Name->Count; Index++) {
      if (!AML_IS_LEAD (Walk[0]) || !AML_IS_NAMECHAR (Walk[1]) ||
          !AML_IS_NAMECHAR (Walk[2]) || !AML_IS_NAMECHAR (Walk[3]))
      {
        return FALSE;
      }

      CopyMem (Name->Seg[Index], Walk, 4);
      Walk += 4;
    }
  }

  *Pos = Walk;
  return TRUE;
}

/**
  Remove the last segment of an absolute path ("\" stays "\").
**/
STATIC
VOID
AmlParent (
  IN OUT CHAR8  *Path
  )
{
  UINTN  Length;

  Length = AsciiStrLen (Path);
  while ((Length > 1) && (Path[Length - 1] != '.')) {
    Length--;
  }

  if (Length > 1) {
    Path[Length - 1] = '\0';
  } else {
    Path[1] = '\0';
  }
}

STATIC
BOOLEAN
AmlAppend (
  IN OUT CHAR8        *Path,
  IN     CONST CHAR8  Seg[4]
  )
{
  UINTN  Length;

  Length = AsciiStrLen (Path);
  if (Length + 6 > AML_PATH_MAX) {
    return FALSE;
  }

  if (Length > 1) {
    Path[Length++] = '.';
  }

  CopyMem (&Path[Length], Seg, 4);
  Path[Length + 4] = '\0';
  return TRUE;
}

/**
  Resolve a name declared in Scope into an absolute path "\XXXX.YYYY".
**/
STATIC
BOOLEAN
AmlResolve (
  IN  CONST CHAR8     *Scope,
  IN  CONST AML_NAME  *Name,
  OUT CHAR8           *Path
  )
{
  UINTN  Index;

  if (Name->Root) {
    AsciiStrCpyS (Path, AML_PATH_MAX, "\\");
  } else {
    AsciiStrCpyS (Path, AML_PATH_MAX, Scope);
    for (Index = 0; Index < Name->Parents; Index++) {
      AmlParent (Path);
    }
  }

  for (Index = 0; Index < Name->Count; Index++) {
    if (!AmlAppend (Path, Name->Seg[Index])) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
  Argument count of the method a name refers to, or -1. Single segment names
  are searched toward the root as the ACPI namespace rules say.
**/
STATIC
INTN
AmlMethodArgs (
  IN AML_STATE       *State,
  IN CONST CHAR8     *Scope,
  IN CONST AML_NAME  *Name
  )
{
  CHAR8  Path[AML_PATH_MAX];
  CHAR8  Search[AML_PATH_MAX];
  INTN   Args;

  if (Name->Count == 0) {
    return -1;
  }

  if (Name->Root || (Name->Parents > 0) || (Name->Count > 1)) {
    return AmlResolve (Scope, Name, Path) ? AmlFindMethod (State, Path) : -1;
  }

  AsciiStrCpyS (Search, AML_PATH_MAX, Scope);
  while (TRUE) {
    AsciiStrCpyS (Path, AML_PATH_MAX, Search);
    if (AmlAppend (Path, Name->Seg[0])) {
      Args = AmlFindMethod (State, Path);
      if (Args >= 0) {
        return Args;
      }
    }

    if (Search[1] == '\0') {
      return -1;
    }

    AmlParent (Search);
  }
}

/**
  Name as written in the AML ("^PCI0.LPCB"), trailing '_' of each segment
  removed.
**/
STATIC
VOID
AmlNameText (
  IN  CONST AML_NAME  *Name,
  OUT CHAR16          *Text,
  IN  UINTN           TextCount
  )
{
  UINTN  Used;
  UINTN  Index;
  UINTN  Length;
  UINTN  Char;

  Used = 0;
  if (Name->Root && (Used + 1 < TextCount)) {
    Text[Used++] = L'\\';
  }

  for (Index = 0; (Index < Name->Parents) && (Used + 1 < TextCount); Index++) {
    Text[Used++] = L'^';
  }

  for (Index = 0; Index < Name->Count; Index++) {
    if ((Index > 0) && (Used + 1 < TextCount)) {
      Text[Used++] = L'.';
    }

    Length = 4;
    while ((Length > 1) && (Name->Seg[Index][Length - 1] == '_')) {
      Length--;
    }

    for (Char = 0; (Char < Length) && (Used + 1 < TextCount); Char++) {
      Text[Used++] = (CHAR16)Name->Seg[Index][Char];
    }
  }

  Text[Used] = L'\0';
}

/**
  Absolute path for display: "\_SB_.PCI0.LPC_" -> "\_SB.PCI0.LPC".
**/
STATIC
VOID
AmlPathText (
  IN  CONST CHAR8  *Path,
  OUT CHAR16       *Text,
  IN  UINTN        TextCount
  )
{
  UINTN  In;
  UINTN  Out;
  UINTN  SegStart;
  UINTN  Length;

  Out = 0;
  In  = 0;
  if ((Path[0] == '\\') && (TextCount > 1)) {
    Text[Out++] = L'\\';
    In++;
  }

  while ((Path[In] != '\0') && (Out + 1 < TextCount)) {
    if (Path[In] == '.') {
      Text[Out++] = L'.';
      In++;
      continue;
    }

    SegStart = In;
    while ((Path[In] != '\0') && (Path[In] != '.')) {
      In++;
    }

    Length = In - SegStart;
    while ((Length > 1) && (Path[SegStart + Length - 1] == '_')) {
      Length--;
    }

    while ((Length > 0) && (Out + 1 < TextCount)) {
      Text[Out++] = (CHAR16)Path[SegStart++];
      Length--;
    }
  }

  Text[Out] = L'\0';
}

//
// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
//

STATIC
BOOLEAN
AmlTermArg (
  IN OUT AML_STATE    *State,
  IN OUT CONST UINT8  **Pos,
  IN     CONST UINT8  *End,
  IN     CONST CHAR8  *Scope,
  IN     BOOLEAN      AllowCall,
  IN     UINTN        Depth
  );

STATIC
CONST AML_OP *
AmlFindOp (
  IN CONST AML_OP  *Table,
  IN UINTN         Count,
  IN UINT8         Op
  )
{
  UINTN  Index;

  for (Index = 0; Index < Count; Index++) {
    if (Table[Index].Op == Op) {
      return &Table[Index];
    }
  }

  return NULL;
}

STATIC
BOOLEAN
AmlArgs (
  IN OUT AML_STATE    *State,
  IN OUT CONST UINT8  **Pos,
  IN     CONST UINT8  *End,
  IN     CONST CHAR8  *Scope,
  IN     CONST CHAR8  *Args,
  IN     UINTN        Depth
  )
{
  AML_NAME  Name;
  UINTN     Size;

  for ( ; *Args != '\0'; Args++) {
    switch (*Args) {
      case 'T':
      case 'S':
        if (!AmlTermArg (State, Pos, End, Scope, (BOOLEAN)(*Args == 'T'), Depth + 1)) {
          return FALSE;
        }

        break;

      case 'N':
        if (!AmlParseName (Pos, End, &Name)) {
          return FALSE;
        }

        break;

      case 'A':
        while ((*Pos < End) && (**Pos != 0)) {
          (*Pos)++;
        }

        if (*Pos >= End) {
          return FALSE;
        }

        (*Pos)++;
        break;

      default:
        Size = (*Args == 'b') ? 1 : (*Args == 'w') ? 2 : (*Args == 'd') ? 4 : 8;
        if ((UINTN)(End - *Pos) < Size) {
          return FALSE;
        }

        *Pos += Size;
        break;
    }
  }

  return TRUE;
}

/**
  Skip one TermArg (or SuperName when AllowCall is FALSE: a name is then a
  reference, not a method call).
**/
STATIC
BOOLEAN
AmlTermArg (
  IN OUT AML_STATE    *State,
  IN OUT CONST UINT8  **Pos,
  IN     CONST UINT8  *End,
  IN     CONST CHAR8  *Scope,
  IN     BOOLEAN      AllowCall,
  IN     UINTN        Depth
  )
{
  UINT8         Op;
  AML_NAME      Name;
  INTN          Args;
  CONST UINT8   *PkgEnd;
  CONST AML_OP  *Info;

  if ((Depth > AML_MAX_DEPTH) || (*Pos >= End)) {
    return FALSE;
  }

  Op = **Pos;
  if (AML_IS_NAME_START (Op)) {
    if (!AmlParseName (Pos, End, &Name)) {
      return FALSE;
    }

    Args = AllowCall ? AmlMethodArgs (State, Scope, &Name) : -1;
    for ( ; Args > 0; Args--) {
      if (!AmlTermArg (State, Pos, End, Scope, TRUE, Depth + 1)) {
        return FALSE;
      }
    }

    return TRUE;
  }

  if ((Op >= 0x60) && (Op <= 0x6E)) {
    (*Pos)++;
    return TRUE;
  }

  switch (Op) {
    case 0x10:    // Scope
    case 0x11:    // Buffer
    case 0x12:    // Package
    case 0x13:    // VarPackage
    case 0x14:    // Method
    case 0xA0:    // If
    case 0xA1:    // Else
    case 0xA2:    // While
      (*Pos)++;
      if (!AmlPkgLength (Pos, End, &PkgEnd)) {
        return FALSE;
      }

      *Pos = PkgEnd;
      return TRUE;

    case 0x5B:
      if (*Pos + 1 >= End) {
        return FALSE;
      }

      Op = (*Pos)[1];
      if ((Op >= 0x81) && (Op <= 0x87)) {
        *Pos += 2;
        if (!AmlPkgLength (Pos, End, &PkgEnd)) {
          return FALSE;
        }

        *Pos = PkgEnd;
        return TRUE;
      }

      Info = AmlFindOp (mAmlExtOps, ARRAY_SIZE (mAmlExtOps), Op);
      if (Info == NULL) {
        return FALSE;
      }

      *Pos += 2;
      return AmlArgs (State, Pos, End, Scope, Info->Args, Depth);

    default:
      Info = AmlFindOp (mAmlOps, ARRAY_SIZE (mAmlOps), Op);
      if (Info == NULL) {
        return FALSE;
      }

      (*Pos)++;
      return AmlArgs (State, Pos, End, Scope, Info->Args, Depth);
  }
}

/**
  Read an integer constant (Zero / One / Ones / Byte..QWord). Pos is only
  moved when the value is a constant.
**/
STATIC
BOOLEAN
AmlConstInt (
  IN OUT CONST UINT8  **Pos,
  IN     CONST UINT8  *End,
  OUT    UINT64       *Value
  )
{
  CONST UINT8  *Walk;
  UINTN        Size;
  UINTN        Index;

  Walk = *Pos;
  if (Walk >= End) {
    return FALSE;
  }

  switch (*Walk) {
    case 0x00: *Value = 0;
      *Pos = Walk + 1;
      return TRUE;
    case 0x01: *Value = 1;
      *Pos = Walk + 1;
      return TRUE;
    case 0xFF: *Value = MAX_UINT64;
      *Pos = Walk + 1;
      return TRUE;
    case 0x0A: Size = 1;
      break;
    case 0x0B: Size = 2;
      break;
    case 0x0C: Size = 4;
      break;
    case 0x0E: Size = 8;
      break;
    default:
      return FALSE;
  }

  if ((UINTN)(End - Walk) < 1 + Size) {
    return FALSE;
  }

  *Value = 0;
  for (Index = 0; Index < Size; Index++) {
    *Value |= LShiftU64 (Walk[1 + Index], Index * 8);
  }

  *Pos = Walk + 1 + Size;
  return TRUE;
}

/**
  Integer constant or any expression; Known tells which.
**/
STATIC
BOOLEAN
AmlIntArg (
  IN OUT AML_STATE    *State,
  IN OUT CONST UINT8  **Pos,
  IN     CONST UINT8  *End,
  IN     CONST CHAR8  *Scope,
  OUT    UINT64       *Value,
  OUT    BOOLEAN      *Known
  )
{
  *Known = AmlConstInt (Pos, End, Value);
  return *Known || AmlTermArg (State, Pos, End, Scope, TRUE, 0);
}

STATIC
BOOLEAN
AmlEisaId (
  IN  UINT64  Value,
  OUT CHAR16  *Text,
  IN  UINTN   TextSize
  )
{
  UINT8  B0;
  UINT8  B1;
  UINT8  C1;
  UINT8  C2;
  UINT8  C3;

  if (Value > MAX_UINT32) {
    return FALSE;
  }

  B0 = (UINT8)Value;
  B1 = (UINT8)(Value >> 8);
  C1 = (B0 >> 2) & 0x1F;
  C2 = (UINT8)(((B0 & 0x03) << 3) | (B1 >> 5));
  C3 = B1 & 0x1F;
  if (((B0 & 0x80) != 0) || (C1 < 1) || (C1 > 26) || (C2 < 1) || (C2 > 26) || (C3 < 1) || (C3 > 26)) {
    return FALSE;
  }

  UnicodeSPrint (
    Text,
    TextSize,
    L"%c%c%c%02X%02X",
    (CHAR16)('@' + C1),
    (CHAR16)('@' + C2),
    (CHAR16)('@' + C3),
    (UINTN)(UINT8)(Value >> 16),
    (UINTN)(UINT8)(Value >> 24)
    );
  return TRUE;
}

/**
  Decode a Name() value (DataRefObject) into text. IsId: _HID / _CID, where
  a DWORD is shown as an EISA ID.
**/
STATIC
BOOLEAN
AmlDataValue (
  IN OUT AML_STATE    *State,
  IN OUT CONST UINT8  **Pos,
  IN     CONST UINT8  *End,
  IN     CONST CHAR8  *Scope,
  IN     BOOLEAN      IsId,
  OUT    CHAR16       *Text,
  IN     UINTN        TextSize,
  IN     UINTN        Depth
  )
{
  UINT64       Value;
  CONST UINT8  *Start;
  CONST UINT8  *PkgEnd;
  UINTN        Used;
  UINTN        Count;
  UINTN        Elements;
  AML_NAME     Name;
  CHAR16       Item[48];
  BOOLEAN      Known;

  Text[0] = L'\0';
  Start   = *Pos;
  if (Start >= End) {
    return FALSE;
  }

  if (AmlConstInt (Pos, End, &Value)) {
    if (!(IsId && (*Start == 0x0C) && AmlEisaId (Value, Text, TextSize))) {
      UnicodeSPrint (Text, TextSize, L"0x%lx", Value);
    }

    return TRUE;
  }

  switch (*Start) {
    case 0x0D:
      (*Pos)++;
      Used = 0;
      if (TextSize >= 3 * sizeof (CHAR16)) {
        Text[Used++] = L'"';
      }

      while ((*Pos < End) && (**Pos != 0)) {
        if ((Used + 4) * sizeof (CHAR16) < TextSize) {
          Text[Used++] = ((**Pos >= 0x20) && (**Pos < 0x7F)) ? (CHAR16)**Pos : L'.';
        }

        (*Pos)++;
      }

      if (*Pos >= End) {
        return FALSE;
      }

      (*Pos)++;
      Text[Used++] = L'"';
      Text[Used]   = L'\0';
      return TRUE;

    case 0x11:
      (*Pos)++;
      if (!AmlPkgLength (Pos, End, &PkgEnd) || !AmlIntArg (State, Pos, PkgEnd, Scope, &Value, &Known)) {
        return FALSE;
      }

      if (Known) {
        UnicodeSPrint (Text, TextSize, L"Buffer (0x%lx bytes)", Value);
      } else {
        UnicodeSPrint (Text, TextSize, L"Buffer");
      }

      *Pos = PkgEnd;
      return TRUE;

    case 0x12:
    case 0x13:
      (*Pos)++;
      if (!AmlPkgLength (Pos, End, &PkgEnd)) {
        return FALSE;
      }

      if (*Start == 0x12) {
        if (*Pos >= PkgEnd) {
          return FALSE;
        }

        Elements = **Pos;
        (*Pos)++;
      } else if (!AmlIntArg (State, Pos, PkgEnd, Scope, &Value, &Known)) {
        return FALSE;
      } else {
        Elements = Known ? (UINTN)Value : 0;
      }

      if (IsId && (Depth == 0)) {
        //
        // _CID package: list the IDs.
        //
        Used  = 0;
        Count = 0;
        while (*Pos < PkgEnd) {
          if (!AmlDataValue (State, Pos, PkgEnd, Scope, TRUE, Item, sizeof (Item), Depth + 1)) {
            break;
          }

          if ((Used + StrLen (Item) + 3) * sizeof (CHAR16) >= TextSize) {
            break;
          }

          if (Count++ > 0) {
            StrCatS (Text, TextSize / sizeof (CHAR16), L", ");
          }

          StrCatS (Text, TextSize / sizeof (CHAR16), Item);
          Used = StrLen (Text);
        }

        if (Count > 0) {
          *Pos = PkgEnd;
          return TRUE;
        }
      }

      UnicodeSPrint (Text, TextSize, L"Package (%d elements)", Elements);
      *Pos = PkgEnd;
      return TRUE;

    default:
      break;
  }

  if (AML_IS_NAME_START (*Start)) {
    if (!AmlParseName (Pos, End, &Name)) {
      return FALSE;
    }

    AmlNameText (&Name, Item, ARRAY_SIZE (Item));
    UnicodeSPrint (Text, TextSize, L"Reference %s", Item);
    return TRUE;
  }

  if (!AmlTermArg (State, Pos, End, Scope, FALSE, Depth + 1)) {
    return FALSE;
  }

  UnicodeSPrint (Text, TextSize, L"(expression)");
  return TRUE;
}

//
// ---------------------------------------------------------------------------
// Term lists
// ---------------------------------------------------------------------------
//

STATIC
BOOLEAN
AmlIsSeg (
  IN CONST AML_NAME  *Name,
  IN CONST CHAR8     *Seg
  )
{
  return (Name->Count > 0) && (CompareMem (Name->Seg[Name->Count - 1], Seg, 4) == 0);
}

/**
  Parse a FieldList and record the named field units in Scope.
**/
STATIC
BOOLEAN
AmlFieldList (
  IN OUT AML_STATE     *State,
  IN     CONST UINT8   *Pos,
  IN     CONST UINT8   *End,
  IN     CONST CHAR8   *Scope,
  IN     CONST CHAR16  *Region,
  IN     BOOLEAN       Conditional
  )
{
  CONST UINT8  *Start;
  CONST UINT8  *PkgEnd;
  UINT64       BitOffset;
  UINT32       Width;
  AML_NAME     Name;
  CHAR8        Path[AML_PATH_MAX];
  AML_OBJECT   *Object;

  BitOffset = 0;
  while (Pos < End) {
    Start = Pos;
    switch (*Pos) {
      case 0x00:  // ReservedField
        Pos++;
        if (!AmlPkgValue (&Pos, End, &Width)) {
          return FALSE;
        }

        BitOffset += Width;
        break;

      case 0x01:  // AccessField
        if ((UINTN)(End - Pos) < 3) {
          return FALSE;
        }

        Pos += 3;
        break;

      case 0x03:  // ExtendedAccessField
        if ((UINTN)(End - Pos) < 4) {
          return FALSE;
        }

        Pos += 4;
        break;

      case 0x02:  // ConnectField
        Pos++;
        if ((Pos < End) && (*Pos == 0x11)) {
          Pos++;
          if (!AmlPkgLength (&Pos, End, &PkgEnd)) {
            return FALSE;
          }

          Pos = PkgEnd;
        } else if (!AmlParseName (&Pos, End, &Name)) {
          return FALSE;
        }

        break;

      default:    // NamedField
        if (((UINTN)(End - Pos) < 5) || !AML_IS_LEAD (Pos[0]) || !AML_IS_NAMECHAR (Pos[1]) ||
            !AML_IS_NAMECHAR (Pos[2]) || !AML_IS_NAMECHAR (Pos[3]))
        {
          return FALSE;
        }

        AsciiStrCpyS (Path, AML_PATH_MAX, Scope);
        if (!AmlAppend (Path, (CONST CHAR8 *)Pos)) {
          return FALSE;
        }

        Pos += 4;
        if (!AmlPkgValue (&Pos, End, &Width)) {
          return FALSE;
        }

        Object = AmlRecord (State, AmlField, Path, Start, Conditional);
        if (Object != NULL) {
          UnicodeSPrint (
            Object->Value,
            sizeof (Object->Value),
            L"%s 0x%lx.%d, %d bit%s",
            Region,
            RShiftU64 (BitOffset, 3),
            (UINTN)(BitOffset & 7),
            (UINTN)Width,
            (Width == 1) ? L"" : L"s"
            );
        }

        BitOffset += Width;
        break;
    }
  }

  return TRUE;
}

STATIC
VOID
AmlTermList (
  IN OUT AML_STATE    *State,
  IN     CONST UINT8  *Pos,
  IN     CONST UINT8  *End,
  IN     CONST CHAR8  *Scope,
  IN     BOOLEAN      Conditional,
  IN     UINTN        Depth
  );

/**
  Parse one named object or load time statement at Pos. Returns the position
  after it, or NULL when the AML cannot be followed.
**/
STATIC
CONST UINT8 *
AmlTermObject (
  IN OUT AML_STATE    *State,
  IN     CONST UINT8  *Pos,
  IN     CONST UINT8  *End,
  IN     CONST CHAR8  *Scope,
  IN     BOOLEAN      Conditional,
  IN     UINTN        Depth
  )
{
  CONST UINT8  *Start;
  CONST UINT8  *PkgEnd;
  UINT8        Op;
  UINT8        Ext;
  UINT8        Flags;
  UINT8        Space;
  UINT8        Type;
  UINT8        Args;
  UINT64       Offset;
  UINT64       Length;
  BOOLEAN      OffsetKnown;
  BOOLEAN      LengthKnown;
  AML_NAME     Name;
  AML_NAME     Name2;
  CHAR8        Path[AML_PATH_MAX];
  CHAR16       Text[64];
  CHAR16       Text2[64];
  AML_OBJECT   *Object;

  Start = Pos;
  Op    = *Pos;
  Ext   = ((Op == 0x5B) && (Pos + 1 < End)) ? Pos[1] : 0;

  //
  // Scope / Device / Processor / PowerResource / ThermalZone: a new scope.
  //
  if ((Op == 0x10) || ((Op == 0x5B) && (Ext >= 0x82) && (Ext <= 0x85))) {
    Pos += (Op == 0x10) ? 1 : 2;
    if (!AmlPkgLength (&Pos, End, &PkgEnd) || !AmlParseName (&Pos, PkgEnd, &Name) ||
        !AmlResolve (Scope, &Name, Path))
    {
      return NULL;
    }

    Object = NULL;
    if (Ext == 0x82) {
      Object = AmlRecord (State, AmlDevice, Path, Start, Conditional);
    } else if (Ext == 0x83) {
      if ((UINTN)(PkgEnd - Pos) < 6) {
        return NULL;
      }

      Object = AmlRecord (State, AmlProcessor, Path, Start, Conditional);
      if (Object != NULL) {
        UnicodeSPrint (
          Object->Value,
          sizeof (Object->Value),
          L"ProcId 0x%x, Pblk 0x%x, PblkLen 0x%x",
          (UINTN)Pos[0],
          (UINTN)ReadUnaligned32 ((CONST UINT32 *)(Pos + 1)),
          (UINTN)Pos[5]
          );
      }

      Pos += 6;
    } else if (Ext == 0x84) {
      if ((UINTN)(PkgEnd - Pos) < 3) {
        return NULL;
      }

      Object = AmlRecord (State, AmlPowerResource, Path, Start, Conditional);
      if (Object != NULL) {
        UnicodeSPrint (
          Object->Value,
          sizeof (Object->Value),
          L"SystemLevel 0x%x, ResourceOrder 0x%x",
          (UINTN)Pos[0],
          (UINTN)ReadUnaligned16 ((CONST UINT16 *)(Pos + 1))
          );
      }

      Pos += 3;
    } else if (Ext == 0x85) {
      AmlRecord (State, AmlThermalZone, Path, Start, Conditional);
    }

    AmlTermList (State, Pos, PkgEnd, Path, Conditional, Depth + 1);
    return PkgEnd;
  }

  //
  // Method: record it, skip the body.
  //
  if (Op == 0x14) {
    Pos++;
    if (!AmlPkgLength (&Pos, End, &PkgEnd) || !AmlParseName (&Pos, PkgEnd, &Name) ||
        !AmlResolve (Scope, &Name, Path) || (Pos >= PkgEnd))
    {
      return NULL;
    }

    Flags = *Pos;
    AmlAddMethod (State, Path, Flags & 0x07, TRUE);
    Object = AmlRecord (State, AmlMethod, Path, Start, Conditional);
    if (Object != NULL) {
      UnicodeSPrint (
        Object->Value,
        sizeof (Object->Value),
        L"%d arg%s, %sSerialized",
        (UINTN)(Flags & 0x07),
        ((Flags & 0x07) == 1) ? L"" : L"s",
        ((Flags & 0x08) != 0) ? L"" : L"Not"
        );
      if ((Flags & 0xF0) != 0) {
        UnicodeSPrint (Text, sizeof (Text), L", SyncLevel %d", (UINTN)(Flags >> 4));
        StrCatS (Object->Value, AML_VALUE_MAX, Text);
      }
    }

    return PkgEnd;
  }

  //
  // Name
  //
  if (Op == 0x08) {
    Pos++;
    if (!AmlParseName (&Pos, End, &Name) || !AmlResolve (Scope, &Name, Path)) {
      return NULL;
    }

    if (!AmlDataValue (State, &Pos, End, Scope, AmlIsSeg (&Name, "_HID") || AmlIsSeg (&Name, "_CID"), Text, sizeof (Text), 0)) {
      return NULL;
    }

    Object = AmlRecord (State, AmlName, Path, Start, Conditional);
    if (Object != NULL) {
      StrCpyS (Object->Value, AML_VALUE_MAX, Text);
    }

    return Pos;
  }

  //
  // Alias / External
  //
  if (Op == 0x06) {
    Pos++;
    if (!AmlParseName (&Pos, End, &Name) || !AmlParseName (&Pos, End, &Name2) || !AmlResolve (Scope, &Name2, Path)) {
      return NULL;
    }

    Object = AmlRecord (State, AmlAlias, Path, Start, Conditional);
    if (Object != NULL) {
      AmlNameText (&Name, Text, ARRAY_SIZE (Text));
      UnicodeSPrint (Object->Value, sizeof (Object->Value), L"-> %s", Text);
    }

    return Pos;
  }

  if (Op == 0x15) {
    Pos++;
    if (!AmlParseName (&Pos, End, &Name) || !AmlResolve (Scope, &Name, Path) || ((UINTN)(End - Pos) < 2)) {
      return NULL;
    }

    Type = Pos[0];
    Args = Pos[1];
    Pos += 2;
    if (Type == 8) {
      AmlAddMethod (State, Path, Args & 0x07, FALSE);
    }

    //
    // External is a declaration, never conditional (iasl wraps them in
    // "If (Zero)").
    //
    Object = AmlRecord (State, AmlExternal, Path, Start, FALSE);
    if (Object != NULL) {
      if (Type == 8) {
        UnicodeSPrint (Object->Value, sizeof (Object->Value), L"Method, %d arg%s", (UINTN)(Args & 0x07), ((Args & 0x07) == 1) ? L"" : L"s");
      } else {
        UnicodeSPrint (Object->Value, sizeof (Object->Value), L"%s", (Type < ARRAY_SIZE (mExternalTypes)) ? mExternalTypes[Type] : L"Unknown");
      }
    }

    return Pos;
  }

  //
  // Mutex / Event / OperationRegion / DataRegion / Fields
  //
  if (Op == 0x5B) {
    switch (Ext) {
      case 0x01:
      case 0x02:
        Pos += 2;
        if (!AmlParseName (&Pos, End, &Name) || !AmlResolve (Scope, &Name, Path)) {
          return NULL;
        }

        if (Ext == 0x01) {
          if (Pos >= End) {
            return NULL;
          }

          Object = AmlRecord (State, AmlMutex, Path, Start, Conditional);
          if (Object != NULL) {
            UnicodeSPrint (Object->Value, sizeof (Object->Value), L"SyncLevel %d", (UINTN)(*Pos & 0x0F));
          }

          Pos++;
        } else {
          AmlRecord (State, AmlEvent, Path, Start, Conditional);
        }

        return Pos;

      case 0x80:
        Pos += 2;
        if (!AmlParseName (&Pos, End, &Name) || !AmlResolve (Scope, &Name, Path) || (Pos >= End)) {
          return NULL;
        }

        Space = *Pos++;
        if (!AmlIntArg (State, &Pos, End, Scope, &Offset, &OffsetKnown) ||
            !AmlIntArg (State, &Pos, End, Scope, &Length, &LengthKnown))
        {
          return NULL;
        }

        Object = AmlRecord (State, AmlOpRegion, Path, Start, Conditional);
        if (Object != NULL) {
          if (Space < ARRAY_SIZE (mRegionSpaces)) {
            StrCpyS (Text2, ARRAY_SIZE (Text2), mRegionSpaces[Space]);
          } else {
            UnicodeSPrint (Text2, sizeof (Text2), L"0x%02x", (UINTN)Space);
          }

          UnicodeSPrint (
            Object->Value,
            sizeof (Object->Value),
            OffsetKnown ? L"%s, 0x%lx" : L"%s, (expression)",
            Text2,
            Offset
            );
          if (LengthKnown) {
            UnicodeSPrint (Text, sizeof (Text), L", 0x%lx", Length);
          } else {
            StrCpyS (Text, ARRAY_SIZE (Text), L", (expression)");
          }

          StrCatS (Object->Value, AML_VALUE_MAX, Text);
        }

        return Pos;

      case 0x88:
        Pos += 2;
        if (!AmlParseName (&Pos, End, &Name) || !AmlResolve (Scope, &Name, Path) ||
            !AmlArgs (State, &Pos, End, Scope, "TTT", 0))
        {
          return NULL;
        }

        AmlRecord (State, AmlDataRegion, Path, Start, Conditional);
        return Pos;

      case 0x81:
      case 0x86:
      case 0x87:
        Pos += 2;
        if (!AmlPkgLength (&Pos, End, &PkgEnd) || !AmlParseName (&Pos, PkgEnd, &Name)) {
          return NULL;
        }

        AmlNameText (&Name, Text, ARRAY_SIZE (Text));
        if (Ext != 0x81) {
          if (!AmlParseName (&Pos, PkgEnd, &Name2)) {
            return NULL;
          }

          AmlNameText (&Name2, Text2, ARRAY_SIZE (Text2));
          StrCatS (Text, ARRAY_SIZE (Text), L"/");
          StrCatS (Text, ARRAY_SIZE (Text), Text2);
          if ((Ext == 0x87) && !AmlTermArg (State, &Pos, PkgEnd, Scope, TRUE, 0)) {
            return NULL;
          }
        }

        if (Pos >= PkgEnd) {
          return NULL;
        }

        Pos++;    // FieldFlags
        if (!AmlFieldList (State, Pos, PkgEnd, Scope, Text, Conditional)) {
          AmlError (State, Start, L"Bad field list");
        }

        return PkgEnd;

      case 0x13:
        Pos += 2;
        if (!AmlArgs (State, &Pos, End, Scope, "TTT", 0)) {
          return NULL;
        }

        break;

      default:
        Pos = NULL;
        break;
    }

    if (Pos == NULL) {
      //
      // Any other extended opcode is a statement.
      //
      Pos = Start;
      return AmlTermArg (State, &Pos, End, Scope, TRUE, 0) ? Pos : NULL;
    }
  } else if ((Op >= 0x8A) && (Op <= 0x8F) && (Op != 0x8E)) {
    Pos++;
    if (!AmlArgs (State, &Pos, End, Scope, "TT", 0)) {
      return NULL;
    }
  } else if ((Op == 0xA0) || (Op == 0xA1) || (Op == 0xA2)) {
    //
    // If / Else / While at load time: objects inside are conditional.
    //
    Pos++;
    if (!AmlPkgLength (&Pos, End, &PkgEnd)) {
      return NULL;
    }

    if ((Op != 0xA1) && !AmlTermArg (State, &Pos, PkgEnd, Scope, TRUE, 0)) {
      AmlError (State, Start, L"Cannot parse predicate, block skipped");
      return PkgEnd;
    }

    AmlTermList (State, Pos, PkgEnd, Scope, TRUE, Depth + 1);
    return PkgEnd;
  } else {
    //
    // Any other statement or expression (Store, method call...).
    //
    return AmlTermArg (State, &Pos, End, Scope, TRUE, 0) ? Pos : NULL;
  }

  //
  // CreateField / CreateXxxField: the NameString is the new object.
  //
  if (!AmlParseName (&Pos, End, &Name) || !AmlResolve (Scope, &Name, Path)) {
    return NULL;
  }

  AmlRecord (State, AmlBufferField, Path, Start, Conditional);
  return Pos;
}

STATIC
VOID
AmlTermList (
  IN OUT AML_STATE    *State,
  IN     CONST UINT8  *Pos,
  IN     CONST UINT8  *End,
  IN     CONST CHAR8  *Scope,
  IN     BOOLEAN      Conditional,
  IN     UINTN        Depth
  )
{
  CONST UINT8  *Next;

  if (Depth > AML_MAX_DEPTH) {
    AmlError (State, Pos, L"Nesting too deep, block skipped");
    return;
  }

  while (Pos < End) {
    Next = AmlTermObject (State, Pos, End, Scope, Conditional, Depth);
    if ((Next == NULL) || (Next <= Pos)) {
      //
      // Lost track of the AML stream; the rest of this block is skipped
      // and parsing continues after it.
      //
      AmlError (State, Pos, L"Cannot parse AML, rest of block skipped");
      return;
    }

    Pos = Next;
  }
}

//
// ---------------------------------------------------------------------------
// Sources and output
// ---------------------------------------------------------------------------
//

STATIC
BOOLEAN
AmlIsAmlTable (
  IN CONST EFI_ACPI_DESCRIPTION_HEADER  *Table
  )
{
  return (Table->Signature == EFI_ACPI_6_0_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) ||
         (Table->Signature == EFI_ACPI_6_0_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE);
}

/**
  Collect the live DSDT / SSDTs. Select: signature to mark (NULL = all).
**/
STATIC
EFI_STATUS
AmlLoadLive (
  IN OUT AML_STATE  *State,
  IN     UINT32     Select,
  IN     UINTN      Instance
  )
{
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  EFI_ACPI_DESCRIPTION_HEADER  *Other;
  UINTN                        Pass;
  UINTN                        Index;
  UINTN                        Walk;
  UINTN                        Number;
  UINTN                        Total;
  UINTN                        Selected;
  BOOLEAN                      IsDsdt;
  AML_SOURCE                   *Source;

  //
  // DSDT first, then the SSDTs: the order the OS loads them.
  //
  Selected = 0;
  for (Pass = 0; Pass < 2; Pass++) {
    for (Index = 0; !EFI_ERROR (BdtAcpiGetTable (Index, &Table)); Index++) {
      IsDsdt = (BOOLEAN)(Table->Signature == EFI_ACPI_6_0_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE);
      if (!AmlIsAmlTable (Table) || (IsDsdt != (Pass == 0)) || (Table->Length < AML_HEADER_SIZE) ||
          (Table->Length > AML_MAX_LENGTH) || (State->SourceCount >= AML_MAX_SOURCES))
      {
        continue;
      }

      //
      // Same naming as "acpi save": DSDT, SSDT1, SSDT2...
      //
      Number = 0;
      Total  = 0;
      for (Walk = 0; !EFI_ERROR (BdtAcpiGetTable (Walk, &Other)); Walk++) {
        if (Other->Signature == Table->Signature) {
          if (Walk < Index) {
            Number++;
          }

          Total++;
        }
      }

      Source         = &State->Sources[State->SourceCount++];
      Source->Table  = (CONST UINT8 *)Table;
      Source->Length = Table->Length;
      UnicodeSPrint (
        Source->Name,
        sizeof (Source->Name),
        L"%c%c%c%c",
        (CHAR16)(Table->Signature & 0xFF),
        (CHAR16)((Table->Signature >> 8) & 0xFF),
        (CHAR16)((Table->Signature >> 16) & 0xFF),
        (CHAR16)(Table->Signature >> 24)
        );
      if (Total > 1) {
        UnicodeSPrint (&Source->Name[4], sizeof (Source->Name) - 4 * sizeof (CHAR16), L"%d", Number + 1);
      }

      Source->Selected = (Select == 0) || ((Table->Signature == Select) && (Number == Instance));
      if (Source->Selected) {
        Selected++;
      }
    }
  }

  return (Selected == 0) ? EFI_NOT_FOUND : EFI_SUCCESS;
}

STATIC
VOID
AmlFree (
  IN OUT AML_STATE  *State
  )
{
  if (State->Objects != NULL) {
    FreePool (State->Objects);
  }

  if (State->Methods != NULL) {
    FreePool (State->Methods);
  }
}

STATIC
VOID
AmlParseAll (
  IN OUT AML_STATE  *State
  )
{
  UINTN       Pass;
  AML_SOURCE  *Source;

  //
  // Pass 0 learns the methods (argument counts), pass 1 records objects.
  //
  for (Pass = 0; Pass < 2; Pass++) {
    State->Record      = (BOOLEAN)(Pass == 1);
    State->ObjectCount = 0;
    State->ErrorCount  = 0;
    for (State->Current = 0; State->Current < State->SourceCount; State->Current++) {
      Source = &State->Sources[State->Current];
      if (State->Record && !Source->Selected) {
        continue;
      }

      AmlTermList (State, Source->Table + AML_HEADER_SIZE, Source->Table + Source->Length, "\\", FALSE, 0);
    }
  }
}

/**
  Value text of Device.Seg: the Name value, "(Method)", or "" when absent.
**/
STATIC
CONST CHAR16 *
AmlChildValue (
  IN AML_STATE    *State,
  IN CONST CHAR8  *Device,
  IN CONST CHAR8  *Seg
  )
{
  UINTN       Index;
  UINTN       Length;
  AML_OBJECT  *Object;

  Length = AsciiStrLen (Device);
  for (Index = 0; Index < State->ObjectCount; Index++) {
    Object = &State->Objects[Index];
    if (((Object->Type != AmlName) && (Object->Type != AmlMethod)) ||
        (AsciiStrLen (Object->Path) != Length + 5) ||
        (CompareMem (Object->Path, Device, Length) != 0) ||
        (Object->Path[Length] != '.') || (CompareMem (&Object->Path[Length + 1], Seg, 4) != 0))
    {
      continue;
    }

    return (Object->Type == AmlMethod) ? L"(Method)" : Object->Value;
  }

  return L"";
}

STATIC
VOID
AmlOutputObjects (
  IN AML_STATE  *State
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"table",  L"Table",  10 },
    { L"offset", L"Offset", 8  },
    { L"type",   L"Type",   16 },
    { L"path",   L"Path",   34 },
    { L"value",  L"Value",  0  },
  };
  UINTN       Index;
  AML_OBJECT  *Object;
  CHAR16      Path[AML_PATH_MAX];

  BdtOutTableBegin (L"objects", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < State->ObjectCount; Index++) {
    Object = &State->Objects[Index];
    AmlPathText (Object->Path, Path, ARRAY_SIZE (Path));
    BdtOutRowBegin ();
    BdtOutCell (L"%s", State->Sources[Object->Source].Name);
    BdtOutCell (L"0x%04x", Object->Offset);
    BdtOutCell (L"%s", mAmlTypeNames[Object->Type]);
    BdtOutCell (L"%s", Path);
    BdtOutCell (
      L"%s%s%s",
      Object->Value,
      (Object->Conditional && (Object->Value[0] != L'\0')) ? L" " : L"",
      Object->Conditional ? L"[conditional]" : L""
      );
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
}

STATIC
UINTN
AmlOutputDevices (
  IN AML_STATE  *State
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"path",  L"Device", 30 },
    { L"hid",   L"_HID",   14 },
    { L"cid",   L"_CID",   14 },
    { L"adr",   L"_ADR",   12 },
    { L"uid",   L"_UID",   8  },
    { L"sta",   L"_STA",   10 },
    { L"table", L"Table",  0  },
  };
  UINTN       Index;
  UINTN       Count;
  AML_OBJECT  *Object;
  CHAR16      Path[AML_PATH_MAX];

  Count = 0;
  BdtOutTableBegin (L"devices", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < State->ObjectCount; Index++) {
    Object = &State->Objects[Index];
    if (Object->Type != AmlDevice) {
      continue;
    }

    AmlPathText (Object->Path, Path, ARRAY_SIZE (Path));
    BdtOutRowBegin ();
    BdtOutCell (L"%s", Path);
    BdtOutCell (L"%s", AmlChildValue (State, Object->Path, "_HID"));
    BdtOutCell (L"%s", AmlChildValue (State, Object->Path, "_CID"));
    BdtOutCell (L"%s", AmlChildValue (State, Object->Path, "_ADR"));
    BdtOutCell (L"%s", AmlChildValue (State, Object->Path, "_UID"));
    BdtOutCell (L"%s", AmlChildValue (State, Object->Path, "_STA"));
    BdtOutCell (L"%s", State->Sources[Object->Source].Name);
    BdtOutRowEnd ();
    Count++;
  }

  BdtOutTableEnd ();
  return Count;
}

EFI_STATUS
EFIAPI
BdtAcpiAml (
  IN CONST CHAR16  *Source OPTIONAL,
  IN UINTN         Instance,
  IN BOOLEAN       Devices
  )
{
  STATIC CONST BDT_COLUMN  ErrorColumns[] = {
    { L"table",   L"Table",   10 },
    { L"offset",  L"Offset",  8 },
    { L"message", L"Message", 0 },
  };
  EFI_STATUS                   Status;
  AML_STATE                    *State;
  VOID                         *File;
  UINTN                        FileSize;
  EFI_ACPI_DESCRIPTION_HEADER  *Header;
  UINT32                       Signature;
  UINTN                        Index;
  UINTN                        Count;
  CONST CHAR16                 *Base;

  State = AllocateZeroPool (sizeof (AML_STATE));
  if (State == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  File = NULL;
  if ((Source != NULL) && (StrLen (Source) == 4) && (StrStr (Source, L".") == NULL)) {
    //
    // Live table by signature.
    //
    Signature = 0;
    for (Index = 0; Index < 4; Index++) {
      Signature |= (UINT32)(Source[Index] & 0xFF) << (Index * 8);
    }

    Status = AmlLoadLive (State, Signature, Instance);
    if (EFI_ERROR (Status)) {
      BdtOutError (L"No AML table %s (instance %d); DSDT and SSDT contain AML", Source, Instance);
    }
  } else if (Source != NULL) {
    //
    // A table file (acpi save, acpidump -b, iasl output).
    //
    Status = BdtFileReadAll (Source, &File, &FileSize);
    if (EFI_ERROR (Status)) {
      BdtOutError (L"Cannot read %s (%s)", Source, BdtStatusName (Status));
    } else {
      Header = (EFI_ACPI_DESCRIPTION_HEADER *)File;
      if ((FileSize < AML_HEADER_SIZE) || !AmlIsAmlTable (Header) ||
          (Header->Length < AML_HEADER_SIZE) || (Header->Length > FileSize))
      {
        BdtOutError (L"%s is not a DSDT / SSDT table", Source);
        Status = EFI_VOLUME_CORRUPTED;
      } else {
        Base = Source;
        for (Index = 0; Source[Index] != L'\0'; Index++) {
          if ((Source[Index] == L'\\') || (Source[Index] == L'/') || (Source[Index] == L':')) {
            Base = &Source[Index + 1];
          }
        }

        State->Sources[0].Table    = (CONST UINT8 *)File;
        State->Sources[0].Length   = Header->Length;
        State->Sources[0].Selected = TRUE;
        StrnCpyS (State->Sources[0].Name, ARRAY_SIZE (State->Sources[0].Name), Base, ARRAY_SIZE (State->Sources[0].Name) - 1);
        State->SourceCount = 1;
      }
    }
  } else {
    Status = AmlLoadLive (State, 0, 0);
    if (EFI_ERROR (Status)) {
      BdtOutError (L"No DSDT / SSDT found");
    }
  }

  if (EFI_ERROR (Status)) {
    if (File != NULL) {
      FreePool (File);
    }

    FreePool (State);
    return Status;
  }

  AmlParseAll (State);

  Count = 0;
  for (Index = 0; Index < State->SourceCount; Index++) {
    if (State->Sources[Index].Selected) {
      Count++;
    }
  }

  BdtOutFieldNum (L"Tables", Count);
  if (Devices) {
    Count = AmlOutputDevices (State);
    BdtOutFieldNum (L"Devices", Count);
  } else {
    AmlOutputObjects (State);
    BdtOutFieldNum (L"Objects", State->ObjectCount);
  }

  BdtOutFieldNum (L"Parse Errors", State->ErrorCount);
  if (State->ErrorCount > 0) {
    BdtOutTableBegin (L"errors", ErrorColumns, ARRAY_SIZE (ErrorColumns));
    for (Index = 0; Index < MIN (State->ErrorCount, AML_MAX_ERRORS); Index++) {
      BdtOutRowBegin ();
      BdtOutCell (L"%s", State->Sources[State->Errors[Index].Source].Name);
      BdtOutCell (L"0x%04x", State->Errors[Index].Offset);
      BdtOutCell (L"%s", State->Errors[Index].Message);
      BdtOutRowEnd ();
    }

    BdtOutTableEnd ();
  }

  AmlFree (State);
  if (File != NULL) {
    FreePool (File);
  }

  FreePool (State);
  return EFI_SUCCESS;
}
