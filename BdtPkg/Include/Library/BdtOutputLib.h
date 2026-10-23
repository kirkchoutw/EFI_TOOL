/** @file
  BdtOutputLib - structured output in TEXT / CSV / JSON.

  All BDT commands emit their result through this library so that the same
  code produces human readable text, CSV for spreadsheets, or JSON for the
  PC-side analyzer. Output goes to the console or to a file.

  Structure model:
    Document
      Section  (named object)      -> may nest sections, lists, tables, fields
      List     (named array)       -> contains Items
      Item     (unnamed object)    -> may contain anything a section can
      Table    (named array)       -> contains Rows of Cells (one per column)
      Field    (key/value)

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_OUTPUT_LIB_H_
#define BDT_OUTPUT_LIB_H_

#include <Uefi.h>
#include <Library/ShellLib.h>

typedef enum {
  BdtFormatText = 0,
  BdtFormatCsv,
  BdtFormatJson
} BDT_OUTPUT_FORMAT;

typedef struct {
  CONST CHAR16    *Key;      ///< JSON / CSV key.
  CONST CHAR16    *Title;    ///< Text mode header.
  UINTN           Width;     ///< Text mode column width.
} BDT_COLUMN;

/**
  Parse a format name ("text", "csv", "json").

  @retval EFI_SUCCESS            Parsed.
  @retval EFI_INVALID_PARAMETER  Unknown name.
**/
EFI_STATUS
EFIAPI
BdtOutParseFormat (
  IN  CONST CHAR16       *Name,
  OUT BDT_OUTPUT_FORMAT  *Format
  );

VOID
EFIAPI
BdtOutSetFormat (
  IN BDT_OUTPUT_FORMAT  Format
  );

BDT_OUTPUT_FORMAT
EFIAPI
BdtOutGetFormat (
  VOID
  );

/**
  Return the file extension (without dot) of the current format.
**/
CONST CHAR16 *
EFIAPI
BdtOutGetExtension (
  VOID
  );

/**
  Redirect output to an open file, or back to the console with NULL.
  Data written to a file is converted to ASCII.
**/
VOID
EFIAPI
BdtOutSetFile (
  IN SHELL_FILE_HANDLE  FileHandle OPTIONAL
  );

/**
  Write a raw, already formatted string to the current sink.
**/
VOID
EFIAPI
BdtOutRaw (
  IN CONST CHAR16  *String
  );

/**
  Begin / end a document. Every command output must be wrapped by one pair.
  The title is shown in text mode and stored as "tool"/"command" in JSON.
**/
VOID
EFIAPI
BdtOutBegin (
  IN CONST CHAR16  *Command
  );

VOID
EFIAPI
BdtOutEnd (
  VOID
  );

VOID
EFIAPI
BdtOutSectionBegin (
  IN CONST CHAR16  *Name
  );

VOID
EFIAPI
BdtOutSectionEnd (
  VOID
  );

VOID
EFIAPI
BdtOutListBegin (
  IN CONST CHAR16  *Name
  );

VOID
EFIAPI
BdtOutListEnd (
  VOID
  );

/**
  Begin an item of a list. Title is printed in text mode only.
**/
VOID
EFIAPI
BdtOutItemBegin (
  IN CONST CHAR16  *TitleFormat OPTIONAL,
  ...
  );

VOID
EFIAPI
BdtOutItemEnd (
  VOID
  );

VOID
EFIAPI
BdtOutTableBegin (
  IN CONST CHAR16      *Name,
  IN CONST BDT_COLUMN  *Columns,
  IN UINTN             ColumnCount
  );

VOID
EFIAPI
BdtOutTableEnd (
  VOID
  );

VOID
EFIAPI
BdtOutRowBegin (
  VOID
  );

/**
  Add one cell to the current row. Cells are matched to columns in order.
**/
VOID
EFIAPI
BdtOutCell (
  IN CONST CHAR16  *Format,
  ...
  );

VOID
EFIAPI
BdtOutRowEnd (
  VOID
  );

/**
  Emit a string field.
**/
VOID
EFIAPI
BdtOutField (
  IN CONST CHAR16  *Key,
  IN CONST CHAR16  *Format,
  ...
  );

/**
  Emit a decimal number field (JSON number).
**/
VOID
EFIAPI
BdtOutFieldNum (
  IN CONST CHAR16  *Key,
  IN UINT64        Value
  );

/**
  Emit a hex field ("0x...", JSON string). Digits = 0 means no padding.
**/
VOID
EFIAPI
BdtOutFieldHex (
  IN CONST CHAR16  *Key,
  IN UINT64        Value,
  IN UINTN         Digits
  );

/**
  Emit a boolean field (JSON true/false).
**/
VOID
EFIAPI
BdtOutFieldBool (
  IN CONST CHAR16  *Key,
  IN BOOLEAN       Value
  );

/**
  Emit a hex dump of a buffer as a field.

  @param[in] Key      Field name.
  @param[in] Address  Address shown for the first byte.
  @param[in] Data     Buffer.
  @param[in] Size     Size of the buffer in bytes.
**/
VOID
EFIAPI
BdtOutHexDump (
  IN CONST CHAR16  *Key,
  IN UINT64        Address,
  IN CONST VOID    *Data,
  IN UINTN         Size
  );

/**
  Emit an informational note. Text mode only; ignored in CSV / JSON.
**/
VOID
EFIAPI
BdtOutNote (
  IN CONST CHAR16  *Format,
  ...
  );

/**
  Print an error message to the console (never into an output file).
**/
VOID
EFIAPI
BdtOutError (
  IN CONST CHAR16  *Format,
  ...
  );

#endif
