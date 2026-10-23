/** @file
  BdtHandleLib - handle / protocol / device path inspection.

  Handles are referred to by a 1-based index into the handle database
  (LocateHandleBuffer (AllHandles)), the same way the Shell "dh" command does.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Library/DevicePathLib.h>
#include <Library/BdtHandleLib.h>
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>

#define BDT_HANDLE_TREE_MAX_DEPTH  16

typedef struct {
  EFI_GUID    Guid;
  UINTN       Count;
} BDT_PROTOCOL_COUNT;

STATIC
EFI_STATUS
GetAllHandles (
  OUT EFI_HANDLE  **Handles,
  OUT UINTN       *Count
  )
{
  return gBS->LocateHandleBuffer (AllHandles, NULL, NULL, Count, Handles);
}

STATIC
UINTN
HandleToIndex (
  IN EFI_HANDLE  *Handles,
  IN UINTN       Count,
  IN EFI_HANDLE  Handle
  )
{
  UINTN  Index;

  for (Index = 0; Index < Count; Index++) {
    if (Handles[Index] == Handle) {
      return Index + 1;
    }
  }

  return 0;
}

STATIC
CONST CHAR16 *
ShortGuidName (
  IN  EFI_GUID  *Guid,
  OUT CHAR16    *Buffer,
  IN  UINTN     BufferSize
  )
{
  return BdtGuidToString (Guid, Buffer, BufferSize);
}

/**
  Return an allocated text form of the device path on Handle, or NULL.
**/
STATIC
CHAR16 *
HandleDevicePathText (
  IN EFI_HANDLE  Handle
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;

  DevicePath = DevicePathFromHandle (Handle);
  if (DevicePath == NULL) {
    return NULL;
  }

  return ConvertDevicePathToText (DevicePath, FALSE, TRUE);
}

/**
  Return the driver name of a driver image / binding handle, or NULL.
  The returned string is owned by the driver; do not free.
**/
STATIC
CONST CHAR16 *
DriverName (
  IN EFI_HANDLE  Handle
  )
{
  EFI_COMPONENT_NAME2_PROTOCOL  *Name2;
  EFI_COMPONENT_NAME_PROTOCOL   *Name1;
  CHAR16                        *Name;

  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiComponentName2ProtocolGuid, (VOID **)&Name2))) {
    if (!EFI_ERROR (Name2->GetDriverName (Name2, "en", &Name)) ||
        !EFI_ERROR (Name2->GetDriverName (Name2, Name2->SupportedLanguages, &Name)))
    {
      return Name;
    }
  }

  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiComponentNameProtocolGuid, (VOID **)&Name1))) {
    if (!EFI_ERROR (Name1->GetDriverName (Name1, "eng", &Name))) {
      return Name;
    }
  }

  return NULL;
}

STATIC
VOID
JoinProtocolNames (
  IN  EFI_HANDLE  Handle,
  OUT CHAR16      *Buffer,
  IN  UINTN       BufferSize,
  OUT UINTN       *Count
  )
{
  EFI_GUID  **Guids;
  UINTN     GuidCount;
  UINTN     Index;
  CHAR16    GuidText[40];

  Buffer[0] = L'\0';
  *Count    = 0;
  if (EFI_ERROR (gBS->ProtocolsPerHandle (Handle, &Guids, &GuidCount))) {
    return;
  }

  for (Index = 0; Index < GuidCount; Index++) {
    if (Index > 0) {
      StrCatS (Buffer, BufferSize / sizeof (CHAR16), L" ");
    }

    if (StrLen (Buffer) + 45 >= BufferSize / sizeof (CHAR16)) {
      StrCatS (Buffer, BufferSize / sizeof (CHAR16), L"...");
      break;
    }

    StrCatS (Buffer, BufferSize / sizeof (CHAR16), ShortGuidName (Guids[Index], GuidText, sizeof (GuidText)));
  }

  *Count = GuidCount;
  FreePool (Guids);
}

EFI_STATUS
EFIAPI
BdtHandleList (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"index",     L"Index",     7  },
    { L"handle",    L"Handle",    20 },
    { L"count",     L"Count",     7  },
    { L"protocols", L"Protocols", 0  },
  };
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  UINTN       ProtocolCount;
  CHAR16      *Names;

  Status = GetAllHandles (&Handles, &Count);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Names = AllocatePool (BDT_MAX_VALUE_LENGTH * sizeof (CHAR16));
  if (Names == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  BdtOutFieldNum (L"Handle Count", Count);
  BdtOutTableBegin (L"handles", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < Count; Index++) {
    JoinProtocolNames (Handles[Index], Names, BDT_MAX_VALUE_LENGTH * sizeof (CHAR16), &ProtocolCount);
    BdtOutRowBegin ();
    BdtOutCell (L"%d", Index + 1);
    BdtOutCell (L"0x%lx", (UINT64)(UINTN)Handles[Index]);
    BdtOutCell (L"%d", ProtocolCount);
    BdtOutCell (L"%s", Names);
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  FreePool (Names);
  FreePool (Handles);
  return EFI_SUCCESS;
}

STATIC
CONST CHAR16 *
OpenAttributeName (
  IN UINT32  Attributes
  )
{
  if ((Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) != 0) {
    return ((Attributes & EFI_OPEN_PROTOCOL_EXCLUSIVE) != 0) ? L"BY_DRIVER|EXCLUSIVE" : L"BY_DRIVER";
  }

  if ((Attributes & EFI_OPEN_PROTOCOL_EXCLUSIVE) != 0) {
    return L"EXCLUSIVE";
  }

  if ((Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) != 0) {
    return L"BY_CHILD_CONTROLLER";
  }

  if ((Attributes & EFI_OPEN_PROTOCOL_GET_PROTOCOL) != 0) {
    return L"GET_PROTOCOL";
  }

  if ((Attributes & EFI_OPEN_PROTOCOL_TEST_PROTOCOL) != 0) {
    return L"TEST_PROTOCOL";
  }

  return L"BY_HANDLE_PROTOCOL";
}

EFI_STATUS
EFIAPI
BdtHandleInfo (
  IN UINTN  Index
  )
{
  STATIC CONST BDT_COLUMN  OpenColumns[] = {
    { L"protocol",   L"Protocol",   42 },
    { L"agent",      L"Agent",      7  },
    { L"controller", L"Controller", 12 },
    { L"attributes", L"Attributes", 22 },
    { L"agent_name", L"Agent Name", 0  },
  };
  EFI_STATUS                           Status;
  EFI_HANDLE                           *Handles;
  UINTN                                Count;
  EFI_HANDLE                           Handle;
  EFI_GUID                             **Guids;
  UINTN                                GuidCount;
  UINTN                                GuidIndex;
  VOID                                 *Interface;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY  *OpenInfo;
  UINTN                                OpenCount;
  UINTN                                OpenIndex;
  CHAR16                               *Text;
  CONST CHAR16                         *Name;
  EFI_LOADED_IMAGE_PROTOCOL            *Image;
  EFI_DRIVER_BINDING_PROTOCOL          *Binding;
  CHAR16                               GuidText[40];

  Status = GetAllHandles (&Handles, &Count);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Index == 0) || (Index > Count)) {
    BdtOutError (L"Handle index %d out of range (1 - %d)", Index, Count);
    FreePool (Handles);
    return EFI_INVALID_PARAMETER;
  }

  Handle = Handles[Index - 1];
  BdtOutFieldNum (L"Index", Index);
  BdtOutFieldHex (L"Handle", (UINTN)Handle, 0);

  Text = HandleDevicePathText (Handle);
  if (Text != NULL) {
    BdtOutField (L"Device Path", L"%s", Text);
    FreePool (Text);
  }

  Name = DriverName (Handle);
  if (Name != NULL) {
    BdtOutField (L"Driver Name", L"%s", Name);
  }

  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiDriverBindingProtocolGuid, (VOID **)&Binding))) {
    BdtOutFieldHex (L"Driver Version", Binding->Version, 8);
    BdtOutFieldNum (L"Driver Image Index", HandleToIndex (Handles, Count, Binding->ImageHandle));
  }

  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image))) {
    BdtOutSectionBegin (L"Loaded Image");
    BdtOutFieldHex (L"Image Base", (UINTN)Image->ImageBase, 0);
    BdtOutFieldHex (L"Image Size", Image->ImageSize, 0);
    BdtOutFieldNum (L"Parent Index", HandleToIndex (Handles, Count, Image->ParentHandle));
    BdtOutFieldNum (L"Device Index", HandleToIndex (Handles, Count, Image->DeviceHandle));
    if (Image->FilePath != NULL) {
      Text = ConvertDevicePathToText (Image->FilePath, FALSE, TRUE);
      if (Text != NULL) {
        BdtOutField (L"File Path", L"%s", Text);
        FreePool (Text);
      }
    }

    BdtOutSectionEnd ();
  }

  Status = gBS->ProtocolsPerHandle (Handle, &Guids, &GuidCount);
  if (EFI_ERROR (Status)) {
    FreePool (Handles);
    return Status;
  }

  BdtOutListBegin (L"Protocols");
  for (GuidIndex = 0; GuidIndex < GuidCount; GuidIndex++) {
    Interface = NULL;
    gBS->HandleProtocol (Handle, Guids[GuidIndex], &Interface);
    BdtOutItemBegin (L"%s", ShortGuidName (Guids[GuidIndex], GuidText, sizeof (GuidText)));
    BdtOutField (L"guid", L"%g", Guids[GuidIndex]);
    if ((BdtGuidName (Guids[GuidIndex]) != NULL) && (BdtOutGetFormat () != BdtFormatText)) {
      BdtOutField (L"name", L"%s", BdtGuidName (Guids[GuidIndex]));
    }

    BdtOutFieldHex (L"interface", (UINTN)Interface, 0);
    BdtOutItemEnd ();
  }

  BdtOutListEnd ();

  //
  // Who opened protocols on this handle: drivers managing it and child handles.
  //
  BdtOutTableBegin (L"open_info", OpenColumns, ARRAY_SIZE (OpenColumns));
  for (GuidIndex = 0; GuidIndex < GuidCount; GuidIndex++) {
    if (EFI_ERROR (gBS->OpenProtocolInformation (Handle, Guids[GuidIndex], &OpenInfo, &OpenCount))) {
      continue;
    }

    for (OpenIndex = 0; OpenIndex < OpenCount; OpenIndex++) {
      Name = DriverName (OpenInfo[OpenIndex].AgentHandle);
      BdtOutRowBegin ();
      BdtOutCell (L"%s", ShortGuidName (Guids[GuidIndex], GuidText, sizeof (GuidText)));
      BdtOutCell (L"%d", HandleToIndex (Handles, Count, OpenInfo[OpenIndex].AgentHandle));
      BdtOutCell (L"%d", HandleToIndex (Handles, Count, OpenInfo[OpenIndex].ControllerHandle));
      BdtOutCell (L"%s", OpenAttributeName (OpenInfo[OpenIndex].Attributes));
      BdtOutCell (L"%s", (Name != NULL) ? Name : L"");
      BdtOutRowEnd ();
    }

    FreePool (OpenInfo);
  }

  BdtOutTableEnd ();
  FreePool (Guids);
  FreePool (Handles);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtHandleByProtocol (
  IN CONST EFI_GUID  *Protocol
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"index",       L"Index",       7  },
    { L"handle",      L"Handle",      20 },
    { L"interface",   L"Interface",   20 },
    { L"device_path", L"Device Path", 0  },
  };
  EFI_STATUS  Status;
  EFI_HANDLE  *All;
  UINTN       AllCount;
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  VOID        *Interface;
  CHAR16      *Text;
  CHAR16      GuidText[40];

  BdtOutField (L"Protocol", L"%s", BdtGuidToString (Protocol, GuidText, sizeof (GuidText)));
  BdtOutField (L"GUID", L"%g", Protocol);

  Status = gBS->LocateHandleBuffer (ByProtocol, (EFI_GUID *)Protocol, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    //
    // Protocols installed without a handle (e.g. via LocateProtocol only) are rare
    // but report whether an instance exists at all.
    //
    BdtOutFieldNum (L"Handle Count", 0);
    BdtOutFieldBool (L"Locatable", !EFI_ERROR (gBS->LocateProtocol ((EFI_GUID *)Protocol, NULL, &Interface)));
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (GetAllHandles (&All, &AllCount))) {
    All      = NULL;
    AllCount = 0;
  }

  BdtOutFieldNum (L"Handle Count", Count);
  BdtOutTableBegin (L"handles", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < Count; Index++) {
    Interface = NULL;
    gBS->HandleProtocol (Handles[Index], (EFI_GUID *)Protocol, &Interface);
    Text = HandleDevicePathText (Handles[Index]);
    BdtOutRowBegin ();
    BdtOutCell (L"%d", HandleToIndex (All, AllCount, Handles[Index]));
    BdtOutCell (L"0x%lx", (UINT64)(UINTN)Handles[Index]);
    BdtOutCell (L"0x%lx", (UINT64)(UINTN)Interface);
    BdtOutCell (L"%s", (Text != NULL) ? Text : L"");
    BdtOutRowEnd ();
    if (Text != NULL) {
      FreePool (Text);
    }
  }

  BdtOutTableEnd ();
  if (All != NULL) {
    FreePool (All);
  }

  FreePool (Handles);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtProtocolList (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"guid",    L"GUID",    38 },
    { L"handles", L"Handles", 9  },
    { L"name",    L"Name",    0  },
  };
  EFI_STATUS          Status;
  EFI_HANDLE          *Handles;
  UINTN               Count;
  UINTN               Index;
  EFI_GUID            **Guids;
  UINTN               GuidCount;
  UINTN               GuidIndex;
  BDT_PROTOCOL_COUNT  *Table;
  BDT_PROTOCOL_COUNT  *NewTable;
  UINTN               TableCount;
  UINTN               TableSize;
  UINTN               Entry;
  CONST CHAR16        *Name;

  Status = GetAllHandles (&Handles, &Count);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  TableSize  = 64;
  TableCount = 0;
  Table      = AllocateZeroPool (TableSize * sizeof (BDT_PROTOCOL_COUNT));
  if (Table == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->ProtocolsPerHandle (Handles[Index], &Guids, &GuidCount))) {
      continue;
    }

    for (GuidIndex = 0; GuidIndex < GuidCount; GuidIndex++) {
      for (Entry = 0; Entry < TableCount; Entry++) {
        if (CompareGuid (&Table[Entry].Guid, Guids[GuidIndex])) {
          break;
        }
      }

      if (Entry == TableCount) {
        if (TableCount == TableSize) {
          NewTable = ReallocatePool (
                       TableSize * sizeof (BDT_PROTOCOL_COUNT),
                       TableSize * 2 * sizeof (BDT_PROTOCOL_COUNT),
                       Table
                       );
          if (NewTable == NULL) {
            continue;
          }

          Table      = NewTable;
          TableSize *= 2;
        }

        CopyGuid (&Table[Entry].Guid, Guids[GuidIndex]);
        Table[Entry].Count = 0;
        TableCount++;
      }

      Table[Entry].Count++;
    }

    FreePool (Guids);
  }

  BdtOutFieldNum (L"Protocol Count", TableCount);
  BdtOutTableBegin (L"protocols", Columns, ARRAY_SIZE (Columns));
  for (Entry = 0; Entry < TableCount; Entry++) {
    Name = BdtGuidName (&Table[Entry].Guid);
    BdtOutRowBegin ();
    BdtOutCell (L"%g", &Table[Entry].Guid);
    BdtOutCell (L"%d", Table[Entry].Count);
    BdtOutCell (L"%s", (Name != NULL) ? Name : L"");
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  FreePool (Table);
  FreePool (Handles);
  return EFI_SUCCESS;
}

/**
  Collect the child handles of Handle (opened BY_CHILD_CONTROLLER).
**/
STATIC
UINTN
GetChildren (
  IN  EFI_HANDLE  Handle,
  OUT EFI_HANDLE  *Children,
  IN  UINTN       MaxChildren
  )
{
  EFI_GUID                             **Guids;
  UINTN                                GuidCount;
  UINTN                                GuidIndex;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY  *OpenInfo;
  UINTN                                OpenCount;
  UINTN                                OpenIndex;
  UINTN                                Count;
  UINTN                                Check;

  Count = 0;
  if (EFI_ERROR (gBS->ProtocolsPerHandle (Handle, &Guids, &GuidCount))) {
    return 0;
  }

  for (GuidIndex = 0; GuidIndex < GuidCount; GuidIndex++) {
    if (EFI_ERROR (gBS->OpenProtocolInformation (Handle, Guids[GuidIndex], &OpenInfo, &OpenCount))) {
      continue;
    }

    for (OpenIndex = 0; OpenIndex < OpenCount; OpenIndex++) {
      if (((OpenInfo[OpenIndex].Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) == 0) ||
          (OpenInfo[OpenIndex].ControllerHandle == Handle))
      {
        continue;
      }

      for (Check = 0; Check < Count; Check++) {
        if (Children[Check] == OpenInfo[OpenIndex].ControllerHandle) {
          break;
        }
      }

      if ((Check == Count) && (Count < MaxChildren)) {
        Children[Count++] = OpenInfo[OpenIndex].ControllerHandle;
      }
    }

    FreePool (OpenInfo);
  }

  FreePool (Guids);
  return Count;
}

STATIC
VOID
TreeNode (
  IN EFI_HANDLE  *Handles,
  IN UINTN       Count,
  IN EFI_HANDLE  Handle,
  IN UINTN       Depth
  )
{
  EFI_HANDLE  *Children;
  UINTN       ChildCount;
  UINTN       Index;
  CHAR16      *Text;

  Text = HandleDevicePathText (Handle);
  BdtOutItemBegin (
    L"%s[%d] %s",
    (Depth == 0) ? L"" : L"+-- ",
    HandleToIndex (Handles, Count, Handle),
    (Text != NULL) ? Text : L"(no device path)"
    );
  if (BdtOutGetFormat () != BdtFormatText) {
    BdtOutFieldNum (L"index", HandleToIndex (Handles, Count, Handle));
    BdtOutField (L"device_path", L"%s", (Text != NULL) ? Text : L"");
  }

  if (Text != NULL) {
    FreePool (Text);
  }

  Children = AllocateZeroPool (Count * sizeof (EFI_HANDLE));
  if ((Children != NULL) && (Depth < BDT_HANDLE_TREE_MAX_DEPTH)) {
    ChildCount = GetChildren (Handle, Children, Count);
    if (ChildCount > 0) {
      BdtOutListBegin (L"children");
      for (Index = 0; Index < ChildCount; Index++) {
        TreeNode (Handles, Count, Children[Index], Depth + 1);
      }

      BdtOutListEnd ();
    }
  }

  if (Children != NULL) {
    FreePool (Children);
  }

  BdtOutItemEnd ();
}

EFI_STATUS
EFIAPI
BdtHandleTree (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  UINTN       Parent;
  UINTN       ChildCount;
  UINTN       Child;
  BOOLEAN     *IsChild;
  EFI_HANDLE  *Children;

  Status = GetAllHandles (&Handles, &Count);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  IsChild  = AllocateZeroPool (Count * sizeof (BOOLEAN));
  Children = AllocateZeroPool (Count * sizeof (EFI_HANDLE));
  if ((IsChild == NULL) || (Children == NULL)) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  for (Parent = 0; Parent < Count; Parent++) {
    ChildCount = GetChildren (Handles[Parent], Children, Count);
    for (Child = 0; Child < ChildCount; Child++) {
      Index = HandleToIndex (Handles, Count, Children[Child]);
      if (Index != 0) {
        IsChild[Index - 1] = TRUE;
      }
    }
  }

  //
  // Roots: controllers with a device path that are nobody's child.
  //
  BdtOutListBegin (L"Controller Tree");
  for (Index = 0; Index < Count; Index++) {
    if (!IsChild[Index] && (DevicePathFromHandle (Handles[Index]) != NULL)) {
      TreeNode (Handles, Count, Handles[Index], 0);
    }
  }

  BdtOutListEnd ();

Done:
  if (IsChild != NULL) {
    FreePool (IsChild);
  }

  if (Children != NULL) {
    FreePool (Children);
  }

  FreePool (Handles);
  return Status;
}

EFI_STATUS
EFIAPI
BdtDevicePathList (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"index",       L"Index",       7 },
    { L"device_path", L"Device Path", 0 },
  };
  EFI_STATUS  Status;
  EFI_HANDLE  *All;
  UINTN       AllCount;
  UINTN       Index;
  CHAR16      *Text;

  Status = GetAllHandles (&All, &AllCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  BdtOutTableBegin (L"device_paths", Columns, ARRAY_SIZE (Columns));
  for (Index = 0; Index < AllCount; Index++) {
    Text = HandleDevicePathText (All[Index]);
    if (Text == NULL) {
      continue;
    }

    BdtOutRowBegin ();
    BdtOutCell (L"%d", Index + 1);
    BdtOutCell (L"%s", Text);
    BdtOutRowEnd ();
    FreePool (Text);
  }

  BdtOutTableEnd ();
  FreePool (All);
  return EFI_SUCCESS;
}

STATIC
CONST CHAR16 *
DevicePathTypeName (
  IN UINT8  Type
  )
{
  switch (Type) {
    case HARDWARE_DEVICE_PATH:
      return L"Hardware";
    case ACPI_DEVICE_PATH:
      return L"ACPI";
    case MESSAGING_DEVICE_PATH:
      return L"Messaging";
    case MEDIA_DEVICE_PATH:
      return L"Media";
    case BBS_DEVICE_PATH:
      return L"BBS";
    case END_DEVICE_PATH_TYPE:
      return L"End";
    default:
      return L"Unknown";
  }
}

EFI_STATUS
EFIAPI
BdtDevicePathDecode (
  IN UINTN  Index
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"node",    L"Node",    6  },
    { L"type",    L"Type",    12 },
    { L"subtype", L"SubType", 9  },
    { L"length",  L"Length",  8  },
    { L"text",    L"Text",    0  },
  };
  EFI_STATUS                Status;
  EFI_HANDLE                *All;
  UINTN                     AllCount;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  CHAR16                    *Text;
  UINTN                     NodeIndex;

  Status = GetAllHandles (&All, &AllCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Index == 0) || (Index > AllCount)) {
    BdtOutError (L"Handle index %d out of range (1 - %d)", Index, AllCount);
    FreePool (All);
    return EFI_INVALID_PARAMETER;
  }

  DevicePath = DevicePathFromHandle (All[Index - 1]);
  if (DevicePath == NULL) {
    BdtOutError (L"Handle %d has no device path", Index);
    FreePool (All);
    return EFI_NOT_FOUND;
  }

  BdtOutFieldNum (L"Index", Index);
  Text = ConvertDevicePathToText (DevicePath, FALSE, TRUE);
  if (Text != NULL) {
    BdtOutField (L"Device Path", L"%s", Text);
    FreePool (Text);
  }

  BdtOutFieldNum (L"Size", GetDevicePathSize (DevicePath));
  BdtOutTableBegin (L"nodes", Columns, ARRAY_SIZE (Columns));
  NodeIndex = 0;
  for (Node = DevicePath; !IsDevicePathEnd (Node) && NodeIndex < 64; Node = NextDevicePathNode (Node), NodeIndex++) {
    if (DevicePathNodeLength (Node) < sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
      BdtOutError (L"Malformed device path node %d", NodeIndex);
      break;
    }

    Text = ConvertDeviceNodeToText (Node, FALSE, TRUE);
    BdtOutRowBegin ();
    BdtOutCell (L"%d", NodeIndex);
    BdtOutCell (L"%s", DevicePathTypeName (DevicePathType (Node)));
    BdtOutCell (L"0x%02x", DevicePathSubType (Node));
    BdtOutCell (L"%d", DevicePathNodeLength (Node));
    BdtOutCell (L"%s", (Text != NULL) ? Text : L"?");
    BdtOutRowEnd ();
    if (Text != NULL) {
      FreePool (Text);
    }
  }

  BdtOutTableEnd ();
  FreePool (All);
  return EFI_SUCCESS;
}
