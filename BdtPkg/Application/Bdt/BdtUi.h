/** @file
  BDT.efi - full screen text user interface ("BDT.efi ui").

  The UI is a set of pages (tabs). A page shows a list of items; opening an
  item shows a hex view of its data source (PCI config space, memory, I/O
  ports, ...). The UI starts read only; F2 enables write mode.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_UI_H_
#define BDT_UI_H_

#include "BdtInternal.h"

#define BDT_UI_MAX_COLS   200
#define BDT_UI_PAGE_SIZE  256      ///< Bytes shown by the hex view (16 x 16).

typedef enum {
  BdtUiSourceMemory,               ///< Physical memory / MMIO (fault protected).
  BdtUiSourceIo,                   ///< I/O port space.
  BdtUiSourcePci                   ///< PCI configuration space.
} BDT_UI_SOURCE_KIND;

typedef struct _BDT_UI_HEX  BDT_UI_HEX;

/**
  Describe the data at Offset of a hex source (shown on the status line).
**/
typedef
VOID
(*BDT_UI_DESCRIBE)(
  IN  BDT_UI_HEX  *Hex,
  IN  UINT64      Offset,
  OUT CHAR16      *Buffer,
  IN  UINTN       BufferSize
  );

struct _BDT_UI_HEX {
  //
  // Filled by the page.
  //
  BDT_UI_SOURCE_KIND    Kind;
  UINT64                Base;          ///< Memory address / I/O port of offset 0.
  UINT64                Last;          ///< Last valid offset (inclusive).
  UINT64                DisplayBase;   ///< Address shown for offset 0.
  BDT_PCI_INFO          *Pci;          ///< BdtUiSourcePci only.
  UINTN                 MaxWidth;      ///< Largest access width (4 or 8).
  CHAR16                Title[128];
  BDT_UI_DESCRIBE       Describe;      ///< Optional.
  UINT64                Cursor;        ///< Initial cursor offset.
  BOOLEAN               ReadOnly;      ///< Never writable (e.g. a private copy).

  //
  // View state, owned by the hex view.
  //
  UINT64                Top;
  UINTN                 Width;
  UINT64                LoadedTop;
  BOOLEAN               Loaded;
  BOOLEAN               Protected;
  UINT8                 Data[BDT_UI_PAGE_SIZE];
  BOOLEAN               Valid[BDT_UI_PAGE_SIZE];
};

typedef struct _BDT_UI_PAGE  BDT_UI_PAGE;

struct _BDT_UI_PAGE {
  CONST CHAR16    *Name;               ///< Tab name.
  CONST CHAR16    *Header;             ///< List column header.
  BOOLEAN         DirectHex;           ///< No list: the page is a hex view of item 0.

  /**
    (Re)build the item list and return the item count.
  **/
  UINTN           (*Load)(VOID);

  /**
    Format list row Index.
  **/
  VOID            (*Row)(IN UINTN Index, OUT CHAR16 *Buffer, IN UINTN BufferSize);

  /**
    Prepare a hex view of item Index. FALSE when the item has no hex view.
  **/
  BOOLEAN         (*Open)(IN UINTN Index, OUT BDT_UI_HEX *Hex);

  /**
    Optional status line text for item Index.
  **/
  VOID            (*Info)(IN UINTN Index, OUT CHAR16 *Buffer, IN UINTN BufferSize);

  /**
    Optional "G" action with a hex value typed by the user. Returns TRUE and
    fills Hex to open a hex view; FALSE to stay in the list (*Select may
    name the item to select).
  **/
  BOOLEAN         (*Goto)(IN UINT64 Value, OUT BDT_UI_HEX *Hex, OUT UINTN *Select);
  CONST CHAR16    *GotoPrompt;

  /**
    Release page resources when the UI exits.
  **/
  VOID            (*Unload)(VOID);

  /**
    Optional write of item Index (list pages without hex view, e.g. MSR).
    Message receives the result shown on the status line.
  **/
  EFI_STATUS      (*Edit)(IN UINTN Index, IN UINT64 Value, OUT CHAR16 *Message, IN UINTN MessageSize);
  UINTN           EditDigits;

  //
  // List state, owned by the UI.
  //
  UINTN           Count;
  UINTN           Selected;
  UINTN           Top;
  BOOLEAN         Loaded;
};

extern BDT_UI_PAGE  *gBdtUiPages[];
extern CONST UINTN  gBdtUiPageCount;

#endif
