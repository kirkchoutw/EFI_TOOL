/** @file
  BdtOutputLib - structured output in TEXT / CSV / JSON.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Library/ShellLib.h>

#define BDT_OUT_MAX_DEPTH   16
#define BDT_OUT_KEY_WIDTH   22
#define BDT_OUT_LINE_SIZE   (BDT_MAX_VALUE_LENGTH * 2)
#define BDT_OUT_ASCII_CHUNK 256

typedef enum {
  NodeDocument,
  NodeSection,
  NodeList,
  NodeItem,
  NodeTable,
  NodeRow
} BDT_NODE_KIND;

typedef struct {
  BDT_NODE_KIND    Kind;
  UINTN            Count;      ///< Number of children already emitted.
} BDT_NODE;

STATIC BDT_OUTPUT_FORMAT  mFormat = BdtFormatText;
STATIC SHELL_FILE_HANDLE  mFile   = NULL;
STATIC BDT_NODE           mStack[BDT_OUT_MAX_DEPTH];
STATIC UINTN              mDepth       = 0;
STATIC UINTN              mTextIndent  = 0;
STATIC BOOLEAN            mTextStarted = FALSE;
STATIC CONST BDT_COLUMN   *mColumns    = NULL;
STATIC UINTN              mColumnCount = 0;
STATIC UINTN              mCellIndex   = 0;
STATIC CHAR16             mValue[BDT_MAX_VALUE_LENGTH];
STATIC CHAR16             mLine[BDT_OUT_LINE_SIZE];
STATIC CHAR16             mRow[BDT_OUT_LINE_SIZE];
STATIC UINTN              mTextLines    = 0;      ///< Lines emitted in text mode.
STATIC UINTN              mItemLineMark = 0;      ///< mTextLines after the last top level item title.
STATIC BOOLEAN            mItemHadBody  = FALSE;  ///< Last top level item printed more than its title.

//
// ---------------------------------------------------------------------------
// Sink
// ---------------------------------------------------------------------------
//

VOID
EFIAPI
BdtOutRaw (
  IN CONST CHAR16  *String
  )
{
  CHAR8  Ascii[BDT_OUT_ASCII_CHUNK];
  UINTN  Length;
  UINTN  Size;

  if (String == NULL) {
    return;
  }

  if ((mFormat == BdtFormatText) && (String[0] != L'\0') && (String[StrLen (String) - 1] == L'\n')) {
    mTextLines++;
  }

  if (mFile == NULL) {
    gST->ConOut->OutputString (gST->ConOut, (CHAR16 *)String);
    return;
  }

  Length = 0;
  while (*String != L'\0') {
    Ascii[Length++] = (*String < 0x80) ? (CHAR8)*String : '?';
    String++;
    if (Length == BDT_OUT_ASCII_CHUNK) {
      Size = Length;
      ShellWriteFile (mFile, &Size, Ascii);
      Length = 0;
    }
  }

  if (Length != 0) {
    Size = Length;
    ShellWriteFile (mFile, &Size, Ascii);
  }
}

STATIC
VOID
EFIAPI
OutPrint (
  IN CONST CHAR16  *Format,
  ...
  )
{
  VA_LIST  Marker;

  VA_START (Marker, Format);
  UnicodeVSPrint (mLine, sizeof (mLine), Format, Marker);
  VA_END (Marker);
  BdtOutRaw (mLine);
}

STATIC
VOID
OutSpaces (
  IN UINTN  Count
  )
{
  STATIC CONST CHAR16  Spaces[] = L"                                        ";
  UINTN                Chunk;

  while (Count > 0) {
    Chunk = MIN (Count, ARRAY_SIZE (Spaces) - 1);
    BdtOutRaw (&Spaces[ARRAY_SIZE (Spaces) - 1 - Chunk]);
    Count -= Chunk;
  }
}

//
// ---------------------------------------------------------------------------
// Format specific helpers
// ---------------------------------------------------------------------------
//

STATIC
VOID
JsonString (
  IN CONST CHAR16  *String
  )
{
  CHAR16  Buffer[64];
  UINTN   Index;

  Index         = 0;
  Buffer[Index++] = L'"';
  for ( ; *String != L'\0'; String++) {
    if (Index > ARRAY_SIZE (Buffer) - 8) {
      Buffer[Index] = L'\0';
      BdtOutRaw (Buffer);
      Index = 0;
    }

    if ((*String == L'"') || (*String == L'\\')) {
      Buffer[Index++] = L'\\';
      Buffer[Index++] = *String;
    } else if (*String == L'\n') {
      Buffer[Index++] = L'\\';
      Buffer[Index++] = L'n';
    } else if (*String == L'\r') {
      Buffer[Index++] = L'\\';
      Buffer[Index++] = L'r';
    } else if (*String == L'\t') {
      Buffer[Index++] = L'\\';
      Buffer[Index++] = L't';
    } else if ((*String < 0x20) || (*String > 0x7E)) {
      UnicodeSPrint (&Buffer[Index], 7 * sizeof (CHAR16), L"\\u%04x", *String);
      Index += 6;
    } else {
      Buffer[Index++] = *String;
    }
  }

  Buffer[Index++] = L'"';
  Buffer[Index]   = L'\0';
  BdtOutRaw (Buffer);
}

STATIC
VOID
CsvString (
  IN CONST CHAR16  *String
  )
{
  CONST CHAR16  *Walk;
  BOOLEAN       Quote;
  CHAR16        Char[2];

  Quote = FALSE;
  for (Walk = String; *Walk != L'\0'; Walk++) {
    if ((*Walk == L',') || (*Walk == L'"') || (*Walk == L'\r') || (*Walk == L'\n')) {
      Quote = TRUE;
      break;
    }
  }

  if (!Quote) {
    BdtOutRaw (String);
    return;
  }

  Char[1] = L'\0';
  BdtOutRaw (L"\"");
  for (Walk = String; *Walk != L'\0'; Walk++) {
    if (*Walk == L'"') {
      BdtOutRaw (L"\"\"");
    } else {
      Char[0] = *Walk;
      BdtOutRaw (Char);
    }
  }

  BdtOutRaw (L"\"");
}

STATIC
BDT_NODE *
TopNode (
  VOID
  )
{
  return (mDepth == 0) ? NULL : &mStack[mDepth - 1];
}

/**
  Start a new JSON element in the current container: comma, newline, indent,
  and "key": when the container is an object.
**/
STATIC
VOID
JsonElement (
  IN CONST CHAR16  *Key OPTIONAL
  )
{
  BDT_NODE  *Node;

  Node = TopNode ();
  if (Node == NULL) {
    return;
  }

  BdtOutRaw ((Node->Count > 0) ? L",\r\n" : L"\r\n");
  Node->Count++;
  OutSpaces (mDepth * 2);
  if ((Key != NULL) && (Node->Kind != NodeList) && (Node->Kind != NodeTable)) {
    JsonString (Key);
    BdtOutRaw (L": ");
  }
}

STATIC
VOID
Push (
  IN BDT_NODE_KIND  Kind
  )
{
  if (mDepth < BDT_OUT_MAX_DEPTH) {
    mStack[mDepth].Kind  = Kind;
    mStack[mDepth].Count = 0;
  }

  mDepth++;
}

STATIC
VOID
JsonClose (
  IN CHAR16  Closer
  )
{
  CHAR16    Text[2];
  BDT_NODE  *Node;

  Node = TopNode ();
  if (Node == NULL) {
    return;
  }

  if (Node->Count > 0) {
    BdtOutRaw (L"\r\n");
    OutSpaces ((mDepth - 1) * 2);
  }

  Text[0] = Closer;
  Text[1] = L'\0';
  BdtOutRaw (Text);
}

STATIC
VOID
Pop (
  VOID
  )
{
  if (mDepth > 0) {
    mDepth--;
  }
}

STATIC
VOID
TextHeading (
  IN CONST CHAR16  *Name
  )
{
  UINTN  Length;

  //
  // Names starting with a lower case letter are machine keys (JSON / CSV
  // only); they get no heading in text mode.
  //
  if ((Name == NULL) || ((Name[0] >= L'a') && (Name[0] <= L'z'))) {
    return;
  }

  if (mTextStarted) {
    BdtOutRaw (L"\r\n");
  }

  mTextStarted = TRUE;
  OutSpaces (mTextIndent);
  BdtOutRaw (Name);
  BdtOutRaw (L"\r\n");
  OutSpaces (mTextIndent);
  for (Length = StrLen (Name); Length > 0; Length--) {
    BdtOutRaw (L"-");
  }

  BdtOutRaw (L"\r\n");
}

/**
  Emit a key/value pair. IsString selects JSON quoting.
**/
STATIC
VOID
EmitField (
  IN CONST CHAR16  *Key,
  IN CONST CHAR16  *Value,
  IN BOOLEAN       IsString
  )
{
  UINTN  Length;

  switch (mFormat) {
    case BdtFormatJson:
      JsonElement (Key);
      if (IsString) {
        JsonString (Value);
      } else {
        BdtOutRaw (Value);
      }

      break;

    case BdtFormatCsv:
      CsvString (Key);
      BdtOutRaw (L",");
      CsvString (Value);
      BdtOutRaw (L"\r\n");
      break;

    default:
      mTextStarted = TRUE;
      OutSpaces (mTextIndent);
      BdtOutRaw (Key);
      Length = StrLen (Key);
      OutSpaces ((Length < BDT_OUT_KEY_WIDTH) ? BDT_OUT_KEY_WIDTH - Length : 1);
      BdtOutRaw (L": ");
      BdtOutRaw (Value);
      BdtOutRaw (L"\r\n");
      break;
  }
}

//
// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
//

EFI_STATUS
EFIAPI
BdtOutParseFormat (
  IN  CONST CHAR16       *Name,
  OUT BDT_OUTPUT_FORMAT  *Format
  )
{
  if (BdtStriCmp (Name, L"text") == 0 || BdtStriCmp (Name, L"txt") == 0) {
    *Format = BdtFormatText;
  } else if (BdtStriCmp (Name, L"csv") == 0) {
    *Format = BdtFormatCsv;
  } else if (BdtStriCmp (Name, L"json") == 0) {
    *Format = BdtFormatJson;
  } else {
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

VOID
EFIAPI
BdtOutSetFormat (
  IN BDT_OUTPUT_FORMAT  Format
  )
{
  mFormat = Format;
}

BDT_OUTPUT_FORMAT
EFIAPI
BdtOutGetFormat (
  VOID
  )
{
  return mFormat;
}

CONST CHAR16 *
EFIAPI
BdtOutGetExtension (
  VOID
  )
{
  switch (mFormat) {
    case BdtFormatJson:
      return L"json";
    case BdtFormatCsv:
      return L"csv";
    default:
      return L"txt";
  }
}

VOID
EFIAPI
BdtOutSetFile (
  IN SHELL_FILE_HANDLE  FileHandle OPTIONAL
  )
{
  mFile = FileHandle;
}

VOID
EFIAPI
BdtOutBegin (
  IN CONST CHAR16  *Command
  )
{
  mDepth       = 0;
  mTextIndent  = 0;
  mTextStarted = FALSE;
  mItemHadBody = FALSE;
  mColumns     = NULL;
  mColumnCount = 0;

  if (mFormat == BdtFormatJson) {
    BdtOutRaw (L"{");
    Push (NodeDocument);
    EmitField (L"tool", L"BDT", TRUE);
    EmitField (L"version", BDT_VERSION_STRING, TRUE);
    EmitField (L"command", Command, TRUE);
  }
}

VOID
EFIAPI
BdtOutEnd (
  VOID
  )
{
  if (mFormat == BdtFormatJson) {
    //
    // Close anything left open by a failing command so the JSON stays valid.
    //
    while (mDepth > 0) {
      switch (TopNode ()->Kind) {
        case NodeList:
        case NodeTable:
          JsonClose (L']');
          break;
        default:
          JsonClose (L'}');
          break;
      }

      Pop ();
    }

    BdtOutRaw (L"\r\n");
  }

  mDepth      = 0;
  mTextIndent = 0;
}

VOID
EFIAPI
BdtOutSectionBegin (
  IN CONST CHAR16  *Name
  )
{
  switch (mFormat) {
    case BdtFormatJson:
      JsonElement (Name);
      BdtOutRaw (L"{");
      Push (NodeSection);
      break;
    case BdtFormatCsv:
      Push (NodeSection);
      break;
    default:
      TextHeading (Name);
      Push (NodeSection);
      mTextIndent += 2;
      break;
  }
}

VOID
EFIAPI
BdtOutSectionEnd (
  VOID
  )
{
  if (mFormat == BdtFormatJson) {
    JsonClose (L'}');
  } else if ((mFormat == BdtFormatText) && (mTextIndent >= 2)) {
    mTextIndent -= 2;
  }

  Pop ();
}

VOID
EFIAPI
BdtOutListBegin (
  IN CONST CHAR16  *Name
  )
{
  switch (mFormat) {
    case BdtFormatJson:
      JsonElement (Name);
      BdtOutRaw (L"[");
      break;
    case BdtFormatCsv:
      break;
    default:
      TextHeading (Name);
      break;
  }

  Push (NodeList);
}

VOID
EFIAPI
BdtOutListEnd (
  VOID
  )
{
  if (mFormat == BdtFormatJson) {
    JsonClose (L']');
  }

  Pop ();
}

VOID
EFIAPI
BdtOutItemBegin (
  IN CONST CHAR16  *TitleFormat OPTIONAL,
  ...
  )
{
  VA_LIST  Marker;

  switch (mFormat) {
    case BdtFormatJson:
      JsonElement (NULL);
      BdtOutRaw (L"{");
      break;
    case BdtFormatCsv:
      BdtOutRaw (L"\r\n");
      break;
    default:
      //
      // Separate top level items with a blank line, but only when the
      // previous one had a body; lists of one-line items stay compact.
      //
      if (mTextStarted && (mTextIndent == 0) && mItemHadBody) {
        BdtOutRaw (L"\r\n");
      }

      mTextStarted = TRUE;
      if (TitleFormat != NULL) {
        VA_START (Marker, TitleFormat);
        UnicodeVSPrint (mValue, sizeof (mValue), TitleFormat, Marker);
        VA_END (Marker);
        OutSpaces (mTextIndent);
        BdtOutRaw (mValue);
        BdtOutRaw (L"\r\n");
      }

      if (mTextIndent == 0) {
        mItemLineMark = mTextLines;
      }

      mTextIndent += 2;
      break;
  }

  Push (NodeItem);
}

VOID
EFIAPI
BdtOutItemEnd (
  VOID
  )
{
  if (mFormat == BdtFormatJson) {
    JsonClose (L'}');
  } else if ((mFormat == BdtFormatText) && (mTextIndent >= 2)) {
    mTextIndent -= 2;
    if (mTextIndent == 0) {
      mItemHadBody = mTextLines > mItemLineMark;
    }
  }

  Pop ();
}

VOID
EFIAPI
BdtOutTableBegin (
  IN CONST CHAR16      *Name,
  IN CONST BDT_COLUMN  *Columns,
  IN UINTN             ColumnCount
  )
{
  UINTN  Index;
  UINTN  Total;
  UINTN  Length;

  mColumns     = Columns;
  mColumnCount = ColumnCount;

  switch (mFormat) {
    case BdtFormatJson:
      JsonElement (Name);
      BdtOutRaw (L"[");
      break;

    case BdtFormatCsv:
      for (Index = 0; Index < ColumnCount; Index++) {
        if (Index > 0) {
          BdtOutRaw (L",");
        }

        CsvString (Columns[Index].Key);
      }

      BdtOutRaw (L"\r\n");
      break;

    default:
      if (mTextStarted) {
        BdtOutRaw (L"\r\n");
      }

      mTextStarted = TRUE;
      Total        = 0;
      OutSpaces (mTextIndent);
      for (Index = 0; Index < ColumnCount; Index++) {
        BdtOutRaw (Columns[Index].Title);
        Length = StrLen (Columns[Index].Title);
        if (Index + 1 < ColumnCount) {
          Length = (Length < Columns[Index].Width) ? Columns[Index].Width - Length : 1;
          OutSpaces (Length);
          Total += MAX (Columns[Index].Width, StrLen (Columns[Index].Title) + 1);
        } else {
          Total += MAX (Columns[Index].Width, Length);
        }
      }

      BdtOutRaw (L"\r\n");
      OutSpaces (mTextIndent);
      for ( ; Total > 0; Total--) {
        BdtOutRaw (L"-");
      }

      BdtOutRaw (L"\r\n");
      break;
  }

  Push (NodeTable);
}

VOID
EFIAPI
BdtOutTableEnd (
  VOID
  )
{
  if (mFormat == BdtFormatJson) {
    JsonClose (L']');
  }

  Pop ();
  mColumns     = NULL;
  mColumnCount = 0;
}

VOID
EFIAPI
BdtOutRowBegin (
  VOID
  )
{
  mCellIndex = 0;
  mRow[0]    = L'\0';
  if (mFormat == BdtFormatJson) {
    JsonElement (NULL);
    BdtOutRaw (L"{");
    Push (NodeRow);
  } else if (mFormat == BdtFormatText) {
    UnicodeSPrint (mRow, sizeof (mRow), L"%*s", mTextIndent, L"");
  }
}

VOID
EFIAPI
BdtOutCell (
  IN CONST CHAR16  *Format,
  ...
  )
{
  VA_LIST       Marker;
  CONST CHAR16  *Key;
  UINTN         Width;
  UINTN         Length;
  UINTN         RowLength;

  VA_START (Marker, Format);
  UnicodeVSPrint (mValue, sizeof (mValue), Format, Marker);
  VA_END (Marker);

  Key   = (mCellIndex < mColumnCount) ? mColumns[mCellIndex].Key : L"extra";
  Width = (mCellIndex < mColumnCount) ? mColumns[mCellIndex].Width : 0;

  switch (mFormat) {
    case BdtFormatJson:
      EmitField (Key, mValue, TRUE);
      break;

    case BdtFormatCsv:
      if (mCellIndex > 0) {
        BdtOutRaw (L",");
      }

      CsvString (mValue);
      break;

    default:
      StrCatS (mRow, ARRAY_SIZE (mRow) - 1, mValue);
      if (mCellIndex + 1 < mColumnCount) {
        Length    = StrLen (mValue);
        RowLength = StrLen (mRow);
        for (Length = (Length < Width) ? Width - Length : 1;
             Length > 0 && RowLength < ARRAY_SIZE (mRow) - 1;
             Length--)
        {
          mRow[RowLength++] = L' ';
        }

        mRow[RowLength] = L'\0';
      }

      break;
  }

  mCellIndex++;
}

VOID
EFIAPI
BdtOutRowEnd (
  VOID
  )
{
  UINTN  Length;

  switch (mFormat) {
    case BdtFormatJson:
      JsonClose (L'}');
      Pop ();
      break;
    case BdtFormatCsv:
      BdtOutRaw (L"\r\n");
      break;
    default:
      //
      // Trim trailing spaces left by empty last cells.
      //
      Length = StrLen (mRow);
      while (Length > 0 && mRow[Length - 1] == L' ') {
        mRow[--Length] = L'\0';
      }

      BdtOutRaw (mRow);
      BdtOutRaw (L"\r\n");
      break;
  }
}

VOID
EFIAPI
BdtOutField (
  IN CONST CHAR16  *Key,
  IN CONST CHAR16  *Format,
  ...
  )
{
  VA_LIST  Marker;

  VA_START (Marker, Format);
  UnicodeVSPrint (mValue, sizeof (mValue), Format, Marker);
  VA_END (Marker);
  EmitField (Key, mValue, TRUE);
}

VOID
EFIAPI
BdtOutFieldNum (
  IN CONST CHAR16  *Key,
  IN UINT64        Value
  )
{
  UnicodeSPrint (mValue, sizeof (mValue), L"%lu", Value);
  EmitField (Key, mValue, FALSE);
}

VOID
EFIAPI
BdtOutFieldHex (
  IN CONST CHAR16  *Key,
  IN UINT64        Value,
  IN UINTN         Digits
  )
{
  if (Digits == 0) {
    UnicodeSPrint (mValue, sizeof (mValue), L"0x%lX", Value);
  } else {
    UnicodeSPrint (mValue, sizeof (mValue), L"0x%0*lX", Digits, Value);
  }

  EmitField (Key, mValue, TRUE);
}

VOID
EFIAPI
BdtOutFieldBool (
  IN CONST CHAR16  *Key,
  IN BOOLEAN       Value
  )
{
  if (mFormat == BdtFormatJson) {
    EmitField (Key, Value ? L"true" : L"false", FALSE);
  } else {
    EmitField (Key, Value ? L"Yes" : L"No", TRUE);
  }
}

VOID
EFIAPI
BdtOutHexDump (
  IN CONST CHAR16  *Key,
  IN UINT64        Address,
  IN CONST VOID    *Data,
  IN UINTN         Size
  )
{
  CONST UINT8  *Bytes;
  UINTN        Offset;
  UINTN        Index;
  CHAR16       Ascii[17];
  CHAR16       Hex[4];

  Bytes = (CONST UINT8 *)Data;

  switch (mFormat) {
    case BdtFormatJson:
      JsonElement (Key);
      BdtOutRaw (L"{");
      Push (NodeSection);
      BdtOutFieldHex (L"address", Address, 0);
      BdtOutFieldNum (L"size", Size);
      JsonElement (L"data");
      BdtOutRaw (L"\"");
      for (Offset = 0; Offset < Size; Offset++) {
        UnicodeSPrint (Hex, sizeof (Hex), L"%02x", Bytes[Offset]);
        BdtOutRaw (Hex);
      }

      BdtOutRaw (L"\"");
      JsonClose (L'}');
      Pop ();
      break;

    case BdtFormatCsv:
      BdtOutRaw (L"address,data\r\n");
      for (Offset = 0; Offset < Size; Offset += 16) {
        OutPrint (L"0x%lX,", Address + Offset);
        for (Index = 0; Index < 16 && Offset + Index < Size; Index++) {
          OutPrint (L"%02X", Bytes[Offset + Index]);
        }

        BdtOutRaw (L"\r\n");
      }

      break;

    default:
      TextHeading (Key);

      mTextStarted = TRUE;
      for (Offset = 0; Offset < Size; Offset += 16) {
        OutSpaces (mTextIndent);
        OutPrint (L"%016lX: ", Address + Offset);
        for (Index = 0; Index < 16; Index++) {
          if (Offset + Index < Size) {
            OutPrint (L"%02X%c", Bytes[Offset + Index], (Index == 7) ? L'-' : L' ');
            Ascii[Index] = (Bytes[Offset + Index] >= 0x20 && Bytes[Offset + Index] < 0x7F) ?
                           (CHAR16)Bytes[Offset + Index] : L'.';
          } else {
            BdtOutRaw (L"   ");
            Ascii[Index] = L' ';
          }
        }

        Ascii[16] = L'\0';
        OutPrint (L" *%s*\r\n", Ascii);
      }

      break;
  }
}

VOID
EFIAPI
BdtOutNote (
  IN CONST CHAR16  *Format,
  ...
  )
{
  VA_LIST  Marker;

  if (mFormat != BdtFormatText) {
    return;
  }

  VA_START (Marker, Format);
  UnicodeVSPrint (mValue, sizeof (mValue), Format, Marker);
  VA_END (Marker);
  mTextStarted = TRUE;
  OutSpaces (mTextIndent);
  BdtOutRaw (mValue);
  BdtOutRaw (L"\r\n");
}

VOID
EFIAPI
BdtOutError (
  IN CONST CHAR16  *Format,
  ...
  )
{
  VA_LIST  Marker;
  CHAR16   Buffer[BDT_MAX_VALUE_LENGTH];

  VA_START (Marker, Format);
  UnicodeVSPrint (Buffer, sizeof (Buffer), Format, Marker);
  VA_END (Marker);
  ErrorPrint (L"BDT: %s\n", Buffer);
}
