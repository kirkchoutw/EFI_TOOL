/** @file
  BDT.efi - full screen text user interface framework.

  Screen layout (rows):
    0          title bar
    1          page tabs
    2 .. R-3   content (list or hex view)
    R-2        status / description line
    R-1        key help

  Only EFI_SIMPLE_TEXT_OUTPUT / INPUT are used, so the UI works on a local
  screen and on a serial console alike.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include "BdtUi.h"
#include <Library/IoLib.h>

#define ATTR_TITLE     EFI_TEXT_ATTR (EFI_WHITE,     EFI_BLUE)
#define ATTR_TAB       EFI_TEXT_ATTR (EFI_BLACK,     EFI_LIGHTGRAY)
#define ATTR_TAB_SEL   EFI_TEXT_ATTR (EFI_WHITE,     EFI_RED)
#define ATTR_NORMAL    EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)
#define ATTR_HEADER    EFI_TEXT_ATTR (EFI_YELLOW,    EFI_BLACK)
#define ATTR_SELECT    EFI_TEXT_ATTR (EFI_BLACK,     EFI_CYAN)
#define ATTR_INVALID   EFI_TEXT_ATTR (EFI_DARKGRAY,  EFI_BLACK)
#define ATTR_STATUS    EFI_TEXT_ATTR (EFI_YELLOW,    EFI_BLUE)
#define ATTR_KEYS      EFI_TEXT_ATTR (EFI_BLACK,     EFI_LIGHTGRAY)

#define UI_MIN_COLS    80
#define UI_MIN_ROWS    24

#define UI_EXIT        ((INTN)-1)
#define UI_BACK        ((INTN)-2)

#define CHAR_ESC       0x1B

STATIC UINTN  mCols;
STATIC UINTN  mRows;
STATIC UINTN  mPage;
STATIC CHAR16 mLine[BDT_UI_MAX_COLS + 1];

STATIC CONST CHAR16  *mListKeys = L" Enter:Open  \x2191\x2193:Select  \x2190\x2192/1-7:Page  G:Goto  R:Refresh  Esc:Quit";
STATIC CONST CHAR16  *mHexKeys  = L" Arrows:Move  PgUp/PgDn  W:Width  G:Goto  Tab/-:Next/Prev  R:Refresh  Esc:Back";

//
// ---------------------------------------------------------------------------
// Screen primitives
// ---------------------------------------------------------------------------
//

/**
  Draw Text at (Col, Row) without padding.
**/
STATIC
VOID
DrawAt (
  IN UINTN         Col,
  IN UINTN         Row,
  IN UINTN         Attr,
  IN CONST CHAR16  *Text
  )
{
  gST->ConOut->SetAttribute (gST->ConOut, Attr);
  gST->ConOut->SetCursorPosition (gST->ConOut, Col, Row);
  gST->ConOut->OutputString (gST->ConOut, (CHAR16 *)Text);
}

/**
  Draw a full row, padded (or cut) to the screen width. The last cell of the
  last row is never written so the screen does not scroll.
**/
STATIC
VOID
DrawRow (
  IN UINTN         Row,
  IN UINTN         Attr,
  IN CONST CHAR16  *Text
  )
{
  UINTN  Width;
  UINTN  Index;

  Width = (Row + 1 == mRows) ? mCols - 1 : mCols;
  for (Index = 0; Index < Width && Text[Index] != L'\0'; Index++) {
    mLine[Index] = Text[Index];
  }

  for ( ; Index < Width; Index++) {
    mLine[Index] = L' ';
  }

  mLine[Width] = L'\0';
  DrawAt (0, Row, Attr, mLine);
}

STATIC
VOID
DrawFrame (
  IN CONST CHAR16  *Title
  )
{
  CHAR16  Text[BDT_UI_MAX_COLS];
  UINTN   Index;
  UINTN   Col;

  UnicodeSPrint (Text, sizeof (Text), L" %s %s  |  %s", BDT_TOOL_NAME, BDT_VERSION_STRING, Title);
  DrawRow (0, ATTR_TITLE, Text);

  DrawRow (1, ATTR_TAB, L"");
  Col = 1;
  for (Index = 0; Index < gBdtUiPageCount; Index++) {
    UnicodeSPrint (Text, sizeof (Text), L" %d:%s ", Index + 1, gBdtUiPages[Index]->Name);
    if (Col + StrLen (Text) >= mCols) {
      break;
    }

    DrawAt (Col, 1, (Index == mPage) ? ATTR_TAB_SEL : ATTR_TAB, Text);
    Col += StrLen (Text) + 1;
  }
}

STATIC
VOID
DrawStatus (
  IN CONST CHAR16  *Status,
  IN CONST CHAR16  *Keys
  )
{
  DrawRow (mRows - 2, ATTR_STATUS, Status);
  DrawRow (mRows - 1, ATTR_KEYS, Keys);
}

STATIC
VOID
ReadKey (
  OUT EFI_INPUT_KEY  *Key
  )
{
  UINTN  Index;

  while (TRUE) {
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    if (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, Key))) {
      return;
    }
  }
}

/**
  Ask for a hex value on the status line.

  @retval TRUE   Value entered.
  @retval FALSE  Cancelled or invalid.
**/
STATIC
BOOLEAN
PromptHex (
  IN  CONST CHAR16  *Label,
  OUT UINT64        *Value
  )
{
  EFI_INPUT_KEY  Key;
  CHAR16         Input[20];
  UINTN          Length;
  CHAR16         Text[BDT_UI_MAX_COLS];

  Length   = 0;
  Input[0] = L'\0';
  DrawRow (mRows - 1, ATTR_KEYS, L" Enter hex value, Enter:OK  Backspace:Delete  Esc:Cancel");
  while (TRUE) {
    UnicodeSPrint (Text, sizeof (Text), L" %s 0x%s_", Label, Input);
    DrawRow (mRows - 2, ATTR_STATUS, Text);
    ReadKey (&Key);
    if ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == CHAR_ESC)) {
      return FALSE;
    }

    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      return (Length > 0) && !EFI_ERROR (BdtParseHex (Input, Value));
    }

    if ((Key.UnicodeChar == CHAR_BACKSPACE) && (Length > 0)) {
      Input[--Length] = L'\0';
    } else if ((Length < 16) &&
               (((Key.UnicodeChar >= L'0') && (Key.UnicodeChar <= L'9')) ||
                ((Key.UnicodeChar >= L'a') && (Key.UnicodeChar <= L'f')) ||
                ((Key.UnicodeChar >= L'A') && (Key.UnicodeChar <= L'F'))))
    {
      Input[Length++] = Key.UnicodeChar;
      Input[Length]   = L'\0';
    }
  }
}

/**
  Map keys shared by all views. Returns the requested page, UI_EXIT, or -3
  when the key is not a global key.
**/
STATIC
INTN
GlobalKey (
  IN EFI_INPUT_KEY  *Key
  )
{
  if ((Key->UnicodeChar >= L'1') && (Key->UnicodeChar < L'1' + (CHAR16)gBdtUiPageCount)) {
    return Key->UnicodeChar - L'1';
  }

  if ((Key->UnicodeChar == L'q') || (Key->UnicodeChar == L'Q')) {
    return UI_EXIT;
  }

  return -3;
}

//
// ---------------------------------------------------------------------------
// Hex view
// ---------------------------------------------------------------------------
//

STATIC
VOID
HexLoad (
  IN OUT BDT_UI_HEX  *Hex
  )
{
  UINTN    Count;
  UINTN    Units;
  UINTN    Width;
  UINTN    Index;
  UINTN    Byte;
  UINT64   Port;
  BOOLEAN  UnitValid[BDT_UI_PAGE_SIZE];

  SetMem (Hex->Data, sizeof (Hex->Data), 0xFF);
  ZeroMem (Hex->Valid, sizeof (Hex->Valid));
  Hex->Loaded    = TRUE;
  Hex->LoadedTop = Hex->Top;

  Count = (Hex->Last - Hex->Top >= BDT_UI_PAGE_SIZE - 1) ? BDT_UI_PAGE_SIZE : (UINTN)(Hex->Last - Hex->Top + 1);
  Units = Count / Hex->Width;

  switch (Hex->Kind) {
    case BdtUiSourceMemory:
      //
      // Windows into tables (ACPI, SMBIOS) may be unaligned: read bytes then.
      //
      Width = (((Hex->Base + Hex->Top) & (Hex->Width - 1)) == 0) ? Hex->Width : 1;
      Units = Count / Width;
      if (Units > 0) {
        BdtMemorySafeRead (Hex->Base + Hex->Top, Width, Units, Hex->Data, UnitValid, &Hex->Protected);
        for (Index = 0; Index < Units; Index++) {
          for (Byte = 0; Byte < Width; Byte++) {
            Hex->Valid[Index * Width + Byte] = UnitValid[Index];
          }
        }
      }

      //
      // A tail shorter than the access width is read byte by byte.
      //
      for (Index = Units * Width; Index < Count; Index++) {
        BdtMemorySafeRead (Hex->Base + Hex->Top + Index, 1, 1, &Hex->Data[Index], &Hex->Valid[Index], NULL);
      }

      break;

    case BdtUiSourceIo:
      for (Index = 0; Index < Units; Index++) {
        Port = Hex->Base + Hex->Top + Index * Hex->Width;
        switch (Hex->Width) {
          case 1:
            Hex->Data[Index] = IoRead8 ((UINTN)Port);
            break;
          case 2:
            WriteUnaligned16 ((UINT16 *)&Hex->Data[Index * 2], IoRead16 ((UINTN)Port));
            break;
          default:
            WriteUnaligned32 ((UINT32 *)&Hex->Data[Index * 4], IoRead32 ((UINTN)Port));
            break;
        }
      }

      SetMem (Hex->Valid, Units * Hex->Width, TRUE);
      break;

    case BdtUiSourcePci:
      if ((Units > 0) && !EFI_ERROR (BdtPciConfigRead (Hex->Pci, (UINT32)Hex->Top, Hex->Width, Units, Hex->Data))) {
        SetMem (Hex->Valid, Units * Hex->Width, TRUE);
      }

      break;
  }
}

STATIC
UINT64
HexValueAt (
  IN BDT_UI_HEX  *Hex,
  IN UINT64      Offset
  )
{
  UINT64  Value;

  Value = 0;
  CopyMem (&Value, &Hex->Data[Offset - Hex->Top], Hex->Width);
  return Value;
}

/**
  Number of address digits needed for the addresses currently on screen, so
  low memory still fits an 80 column console.
**/
STATIC
UINTN
HexAddressDigits (
  IN BDT_UI_HEX  *Hex
  )
{
  UINT64  Highest;

  Highest = Hex->DisplayBase + MIN (Hex->Last, Hex->Top + BDT_UI_PAGE_SIZE - 1);
  if (Highest < Hex->DisplayBase) {
    Highest = MAX_UINT64;
  }

  if (Highest <= 0xFFFF) {
    return 4;
  }

  if (Highest <= 0xFFFFFFFF) {
    return 8;
  }

  return (Highest <= 0xFFFFFFFFFFFFULL) ? 12 : 16;
}

STATIC
VOID
HexDraw (
  IN BDT_UI_HEX  *Hex
  )
{
  STATIC CONST CHAR16  *WidthNames[] = { L"", L"Byte", L"Word", L"", L"Dword", L"", L"", L"", L"Qword" };
  CHAR16               Text[BDT_UI_MAX_COLS];
  CHAR16               Cell[24];
  CHAR16               Ascii[17];
  UINTN                Digits;
  UINTN                Row;
  UINTN                Unit;
  UINTN                Units;
  UINTN                Byte;
  UINTN                Col;
  UINT64               Offset;
  UINT64               Value;
  UINTN                Attr;
  CHAR16               Describe[96];

  if (!Hex->Loaded || (Hex->LoadedTop != Hex->Top)) {
    HexLoad (Hex);
  }

  Digits = HexAddressDigits (Hex);
  Units  = 16 / Hex->Width;

  UnicodeSPrint (
    Text,
    sizeof (Text),
    L" %s   [%s]%s",
    Hex->Title,
    WidthNames[Hex->Width],
    ((Hex->Kind == BdtUiSourceMemory) && !Hex->Protected) ? L"  (fault guard unavailable)" : L""
    );
  DrawRow (2, ATTR_HEADER, Text);

  //
  // Column header.
  //
  UnicodeSPrint (Text, sizeof (Text), L" %-*s  ", Digits, (Digits >= 7) ? L"Address" : L"Addr");
  for (Unit = 0; Unit < Units; Unit++) {
    UnicodeSPrint (Cell, sizeof (Cell), L"%-*x ", Hex->Width * 2, (UINT32)(Unit * Hex->Width));
    StrCatS (Text, ARRAY_SIZE (Text), Cell);
  }

  StrCatS (Text, ARRAY_SIZE (Text), L" 0123456789ABCDEF");
  DrawRow (3, ATTR_HEADER, Text);

  for (Row = 0; Row < 16; Row++) {
    Offset = Hex->Top + Row * 16;
    if ((Offset > Hex->Last) || (Offset < Hex->Top)) {
      DrawRow (4 + Row, ATTR_NORMAL, L"");
      continue;
    }

    UnicodeSPrint (Text, sizeof (Text), L" %0*lx  ", Digits, Hex->DisplayBase + Offset);
    for (Unit = 0; Unit < Units; Unit++) {
      Offset = Hex->Top + Row * 16 + Unit * Hex->Width;
      if (Offset > Hex->Last) {
        UnicodeSPrint (Cell, sizeof (Cell), L"%*s ", Hex->Width * 2, L"");
      } else if (!Hex->Valid[Offset - Hex->Top]) {
        SetMem16 (Cell, Hex->Width * 4, L'-');
        Cell[Hex->Width * 2]     = L' ';
        Cell[Hex->Width * 2 + 1] = L'\0';
      } else {
        UnicodeSPrint (Cell, sizeof (Cell), L"%0*lx ", Hex->Width * 2, HexValueAt (Hex, Offset));
      }

      StrCatS (Text, ARRAY_SIZE (Text), Cell);
    }

    for (Byte = 0; Byte < 16; Byte++) {
      Offset = Hex->Top + Row * 16 + Byte;
      if (Offset > Hex->Last) {
        Ascii[Byte] = L' ';
      } else if (!Hex->Valid[Offset - Hex->Top]) {
        Ascii[Byte] = L'.';
      } else {
        Ascii[Byte] = ((Hex->Data[Offset - Hex->Top] >= 0x20) && (Hex->Data[Offset - Hex->Top] < 0x7F)) ?
                      (CHAR16)Hex->Data[Offset - Hex->Top] : L'.';
      }
    }

    Ascii[16] = L'\0';
    StrCatS (Text, ARRAY_SIZE (Text), L" ");
    StrCatS (Text, ARRAY_SIZE (Text), Ascii);
    DrawRow (4 + Row, ATTR_NORMAL, Text);

    //
    // Overdraw unreadable units and the cursor.
    //
    for (Unit = 0; Unit < Units; Unit++) {
      Offset = Hex->Top + Row * 16 + Unit * Hex->Width;
      if (Offset > Hex->Last) {
        break;
      }

      Col  = 1 + Digits + 2 + Unit * (Hex->Width * 2 + 1);
      Attr = (Offset == Hex->Cursor) ? ATTR_SELECT : (Hex->Valid[Offset - Hex->Top] ? 0 : ATTR_INVALID);
      if (Attr == 0) {
        continue;
      }

      CopyMem (Cell, &Text[Col], Hex->Width * 2 * sizeof (CHAR16));
      Cell[Hex->Width * 2] = L'\0';
      DrawAt (Col, 4 + Row, Attr, Cell);
    }
  }

  for (Row = 20; Row < mRows - 2; Row++) {
    DrawRow (Row, ATTR_NORMAL, L"");
  }

  //
  // Status line: address, value, description.
  //
  Describe[0] = L'\0';
  if (Hex->Describe != NULL) {
    Hex->Describe (Hex, Hex->Cursor, Describe, sizeof (Describe));
  }

  if (Hex->Valid[Hex->Cursor - Hex->Top]) {
    Value = HexValueAt (Hex, Hex->Cursor);
    UnicodeSPrint (
      Text,
      sizeof (Text),
      L" Addr 0x%lx  Value 0x%0*lx (%lu)  %s",
      Hex->DisplayBase + Hex->Cursor,
      Hex->Width * 2,
      Value,
      Value,
      Describe
      );
  } else {
    UnicodeSPrint (Text, sizeof (Text), L" Addr 0x%lx  Value: not readable  %s", Hex->DisplayBase + Hex->Cursor, Describe);
  }

  DrawStatus (Text, mHexKeys);
}

/**
  Keep Cursor inside the source, aligned to Width, and visible.
**/
STATIC
VOID
HexClamp (
  IN OUT BDT_UI_HEX  *Hex
  )
{
  if (Hex->Cursor > Hex->Last) {
    Hex->Cursor = Hex->Last;
  }

  Hex->Cursor &= ~(UINT64)(Hex->Width - 1);
  if (Hex->Cursor < Hex->Top) {
    Hex->Top = Hex->Cursor & ~(UINT64)0xF;
  } else if (Hex->Cursor - Hex->Top >= BDT_UI_PAGE_SIZE) {
    Hex->Top = (Hex->Cursor & ~(UINT64)0xF) - (BDT_UI_PAGE_SIZE - 16);
  }

  Hex->Top &= ~(UINT64)0xF;
}

/**
  Run the hex view. Page is used for next / previous item; Index is the item.

  @return New page index, UI_EXIT or UI_BACK.
**/
STATIC
INTN
HexView (
  IN     BDT_UI_PAGE  *Page,
  IN OUT UINTN        *Index,
  IN OUT BDT_UI_HEX   *Hex
  )
{
  EFI_INPUT_KEY  Key;
  INTN           Global;
  UINT64         Value;
  UINTN          Next;
  BDT_UI_HEX     *Other;

  Hex->Width  = 1;
  Hex->Loaded = FALSE;
  Hex->Top    = Hex->Cursor & ~(UINT64)0xF;
  HexClamp (Hex);

  while (TRUE) {
    DrawFrame (Page->Name);
    HexDraw (Hex);
    ReadKey (&Key);

    Global = GlobalKey (&Key);
    if (Global != -3) {
      return Global;
    }

    switch (Key.ScanCode) {
      case SCAN_ESC:
        return UI_BACK;
      case SCAN_UP:
        Hex->Cursor = (Hex->Cursor >= 16) ? Hex->Cursor - 16 : Hex->Cursor;
        break;
      case SCAN_DOWN:
        Hex->Cursor = (Hex->Last - Hex->Cursor >= 16) ? Hex->Cursor + 16 : Hex->Cursor;
        break;
      case SCAN_LEFT:
        Hex->Cursor = (Hex->Cursor >= Hex->Width) ? Hex->Cursor - Hex->Width : Hex->Cursor;
        break;
      case SCAN_RIGHT:
        Hex->Cursor = (Hex->Last - Hex->Cursor >= Hex->Width) ? Hex->Cursor + Hex->Width : Hex->Cursor;
        break;
      case SCAN_PAGE_UP:
        if (Hex->Top >= BDT_UI_PAGE_SIZE) {
          Hex->Top    -= BDT_UI_PAGE_SIZE;
          Hex->Cursor -= BDT_UI_PAGE_SIZE;
        } else {
          Hex->Top    = 0;
          Hex->Cursor = 0;
        }

        break;
      case SCAN_PAGE_DOWN:
        if (Hex->Last - Hex->Top >= BDT_UI_PAGE_SIZE) {
          Hex->Top   += BDT_UI_PAGE_SIZE;
          Hex->Cursor = (Hex->Last - Hex->Cursor >= BDT_UI_PAGE_SIZE) ? Hex->Cursor + BDT_UI_PAGE_SIZE : Hex->Last;
        }

        break;
      case SCAN_HOME:
        Hex->Cursor = 0;
        break;
      case SCAN_END:
        Hex->Cursor = Hex->Last;
        break;
      case SCAN_F5:
        Hex->Loaded = FALSE;
        break;
      default:
        break;
    }

    switch (Key.UnicodeChar) {
      case CHAR_ESC:
        return UI_BACK;
      case L'w':
      case L'W':
        Hex->Width  = (Hex->Width >= Hex->MaxWidth) ? 1 : Hex->Width * 2;
        Hex->Loaded = FALSE;
        break;
      case L'r':
      case L'R':
        Hex->Loaded = FALSE;
        break;
      case L'g':
      case L'G':
        if (PromptHex (L"Go to address:", &Value)) {
          if ((Value >= Hex->DisplayBase) && (Value - Hex->DisplayBase <= Hex->Last)) {
            Hex->Cursor = Value - Hex->DisplayBase;
            Hex->Top    = Hex->Cursor & ~(UINT64)0xF;
          }
        }

        break;
      case CHAR_TAB:
      case L'+':
      case L'-':
        if (Page->DirectHex || (Page->Count < 2)) {
          break;
        }

        Next = (Key.UnicodeChar == L'-') ? ((*Index == 0) ? Page->Count - 1 : *Index - 1) :
               ((*Index + 1 >= Page->Count) ? 0 : *Index + 1);
        Other = AllocateZeroPool (sizeof (BDT_UI_HEX));
        if ((Other != NULL) && Page->Open (Next, Other)) {
          //
          // Keep cursor and width when moving between items of the same kind.
          //
          Other->Cursor = MIN (Hex->Cursor, Other->Last);
          Other->Width  = MIN (Hex->Width, Other->MaxWidth);
          CopyMem (Hex, Other, sizeof (BDT_UI_HEX));
          Hex->Loaded = FALSE;
          Hex->Top    = Hex->Cursor & ~(UINT64)0xF;
          *Index      = Next;
        }

        if (Other != NULL) {
          FreePool (Other);
        }

        break;
      default:
        break;
    }

    HexClamp (Hex);
  }
}

//
// ---------------------------------------------------------------------------
// List view
// ---------------------------------------------------------------------------
//

STATIC
VOID
ListDraw (
  IN BDT_UI_PAGE  *Page
  )
{
  UINTN   Visible;
  UINTN   Row;
  UINTN   Index;
  CHAR16  Text[BDT_UI_MAX_COLS];

  Visible = mRows - 5;
  if (Page->Selected < Page->Top) {
    Page->Top = Page->Selected;
  } else if (Page->Selected >= Page->Top + Visible) {
    Page->Top = Page->Selected - Visible + 1;
  }

  DrawFrame (Page->Name);
  DrawRow (2, ATTR_HEADER, Page->Header);
  for (Row = 0; Row < Visible; Row++) {
    Index = Page->Top + Row;
    if (Index >= Page->Count) {
      DrawRow (3 + Row, ATTR_NORMAL, (Page->Count == 0 && Row == 0) ? L" (no items)" : L"");
      continue;
    }

    Text[0] = L' ';
    Page->Row (Index, &Text[1], sizeof (Text) - sizeof (CHAR16));
    DrawRow (3 + Row, (Index == Page->Selected) ? ATTR_SELECT : ATTR_NORMAL, Text);
  }

  if ((Page->Info != NULL) && (Page->Count > 0)) {
    Text[0] = L' ';
    Page->Info (Page->Selected, &Text[1], sizeof (Text) - sizeof (CHAR16));
  } else {
    UnicodeSPrint (Text, sizeof (Text), L" %d item(s)", Page->Count);
  }

  DrawStatus (Text, mListKeys);
}

STATIC
VOID
LoadPage (
  IN BDT_UI_PAGE  *Page
  )
{
  Page->Count  = Page->Load ();
  Page->Loaded = TRUE;
  if (Page->Selected >= Page->Count) {
    Page->Selected = (Page->Count == 0) ? 0 : Page->Count - 1;
  }
}

/**
  Run one page until the user switches page or exits.
**/
STATIC
INTN
PageView (
  IN BDT_UI_PAGE  *Page
  )
{
  EFI_INPUT_KEY  Key;
  INTN           Global;
  INTN           Result;
  UINT64         Value;
  UINTN          Select;
  UINTN          Visible;
  BDT_UI_HEX     *Hex;

  Hex = AllocateZeroPool (sizeof (BDT_UI_HEX));
  if (Hex == NULL) {
    return UI_EXIT;
  }

  if (!Page->Loaded) {
    DrawFrame (Page->Name);
    DrawStatus (L" Loading...", L"");
    LoadPage (Page);
  }

  Result = UI_EXIT;
  while (TRUE) {
    if (Page->DirectHex) {
      ZeroMem (Hex, sizeof (BDT_UI_HEX));
      if (!Page->Open (0, Hex)) {
        break;
      }

      Result = HexView (Page, &Page->Selected, Hex);
      Result = (Result == UI_BACK) ? UI_EXIT : Result;
      break;
    }

    ListDraw (Page);
    ReadKey (&Key);
    Global = GlobalKey (&Key);
    if (Global != -3) {
      Result = Global;
      break;
    }

    Visible = mRows - 5;
    if ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == CHAR_ESC)) {
      Result = UI_EXIT;
      break;
    }

    if (Key.ScanCode == SCAN_LEFT) {
      Result = (mPage == 0) ? (INTN)gBdtUiPageCount - 1 : (INTN)mPage - 1;
      break;
    }

    if (Key.ScanCode == SCAN_RIGHT) {
      Result = (mPage + 1 >= gBdtUiPageCount) ? 0 : (INTN)mPage + 1;
      break;
    }

    switch (Key.ScanCode) {
      case SCAN_UP:
        Page->Selected = (Page->Selected > 0) ? Page->Selected - 1 : 0;
        break;
      case SCAN_DOWN:
        Page->Selected = (Page->Selected + 1 < Page->Count) ? Page->Selected + 1 : Page->Selected;
        break;
      case SCAN_PAGE_UP:
        Page->Selected = (Page->Selected > Visible) ? Page->Selected - Visible : 0;
        break;
      case SCAN_PAGE_DOWN:
        Page->Selected = MIN (Page->Selected + Visible, (Page->Count == 0) ? 0 : Page->Count - 1);
        break;
      case SCAN_HOME:
        Page->Selected = 0;
        break;
      case SCAN_END:
        Page->Selected = (Page->Count == 0) ? 0 : Page->Count - 1;
        break;
      case SCAN_F5:
        LoadPage (Page);
        break;
      default:
        break;
    }

    if ((Key.UnicodeChar == L'r') || (Key.UnicodeChar == L'R')) {
      LoadPage (Page);
    }

    if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) && (Page->Count > 0)) {
      ZeroMem (Hex, sizeof (BDT_UI_HEX));
      if (Page->Open (Page->Selected, Hex)) {
        Result = HexView (Page, &Page->Selected, Hex);
        if (Result != UI_BACK) {
          break;
        }
      }
    }

    if (((Key.UnicodeChar == L'g') || (Key.UnicodeChar == L'G')) && (Page->Goto != NULL)) {
      if (PromptHex (Page->GotoPrompt, &Value)) {
        ZeroMem (Hex, sizeof (BDT_UI_HEX));
        Select = Page->Selected;
        if (Page->Goto (Value, Hex, &Select)) {
          Result = HexView (Page, &Page->Selected, Hex);
          if (Result != UI_BACK) {
            break;
          }
        } else {
          LoadPage (Page);
          Page->Selected = MIN (Select, (Page->Count == 0) ? 0 : Page->Count - 1);
        }
      }
    }
  }

  FreePool (Hex);
  return Result;
}

//
// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------
//

EFI_STATUS
BdtCmdUi (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  EFI_STATUS  Status;
  INTN        Next;
  UINTN       Index;
  INT32       SavedAttribute;
  BOOLEAN     SavedCursor;

  if (Argc != 0) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &mCols, &mRows);
  if (EFI_ERROR (Status) || (mCols < UI_MIN_COLS) || (mRows < UI_MIN_ROWS)) {
    BdtOutError (L"The UI needs a console of at least %dx%d (current %dx%d)", UI_MIN_COLS, UI_MIN_ROWS, mCols, mRows);
    return EFI_UNSUPPORTED;
  }

  mCols          = MIN (mCols, BDT_UI_MAX_COLS);
  SavedAttribute = gST->ConOut->Mode->Attribute;
  SavedCursor    = gST->ConOut->Mode->CursorVisible;
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  gST->ConOut->SetAttribute (gST->ConOut, ATTR_NORMAL);
  gST->ConOut->ClearScreen (gST->ConOut);

  mPage = 0;
  while (TRUE) {
    Next = PageView (gBdtUiPages[mPage]);
    if ((Next < 0) || ((UINTN)Next >= gBdtUiPageCount)) {
      break;
    }

    mPage = (UINTN)Next;
  }

  for (Index = 0; Index < gBdtUiPageCount; Index++) {
    if ((gBdtUiPages[Index]->Unload != NULL) && gBdtUiPages[Index]->Loaded) {
      gBdtUiPages[Index]->Unload ();
    }

    gBdtUiPages[Index]->Loaded = FALSE;
  }

  gST->ConOut->SetAttribute (gST->ConOut, (UINTN)SavedAttribute);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, SavedCursor);
  return EFI_SUCCESS;
}
