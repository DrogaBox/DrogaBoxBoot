/** @file
  Options editor: the config.plist tree.

  It is started from the picker with F2. OpenCoreEditorUi.c draws it on the text console or on the
  OpenCanopy sheet.

  Copyright (C) 2026, OpenCore contributors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Library/OcMainLib.h>
#include <Guid/AppleVariable.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/OcFileLib.h>
#include <Library/OcStringLib.h>
#include <Library/PrintLib.h>
#include <Library/ResetSystemLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "OpenCoreEditorPrivate.h"

//
// Description of values.
//

STATIC
XML_NODE *
EdFindEnabled (
  IN  XML_NODE  *Dict
  )
{
  XML_NODE  *Enabled;

  Enabled = EditPlistFindChild (Dict, "Enabled");
  if ((Enabled != NULL) && (EditPlistNodeKind (Enabled) == EditPlistKindBoolean)) {
    return Enabled;
  }

  return NULL;
}

/**
  Describe an array element: a dictionary is named after its most telling entry.
**/
STATIC
VOID
EdArrayLabel (
  IN  CONST EDIT_PLIST  *Plist,
  IN  XML_NODE          *Element,
  IN  UINT32            Index,
  OUT CHAR8             *Buffer,
  IN  UINTN             Size
  )
{
  STATIC CONST CHAR8  *Names[] = { "BundlePath", "Path", "Name", "Identifier", "Comment", "Arguments" };

  UINTN        Item;
  XML_NODE     *Entry;
  XML_NODE     *Enabled;
  CONST CHAR8  *Title;

  Title   = "";
  Enabled = NULL;

  if (EditPlistNodeKind (Element) == EditPlistKindDict) {
    Enabled = EdFindEnabled (Element);

    for (Item = 0; Item < ARRAY_SIZE (Names); ++Item) {
      Entry = EditPlistFindChild (Element, Names[Item]);
      if ((Entry != NULL) && (EditPlistNodeKind (Entry) == EditPlistKindString)) {
        Title = EditPlistGetContent (Plist, Entry);
        if (Title[0] != '\0') {
          break;
        }
      }
    }
  }

  AsciiSPrint (
    Buffer,
    Size,
    "[%u] %a%a",
    Index,
    Enabled != NULL ? (EditPlistGetBoolean (Plist, Enabled) ? "[x] " : "[ ] ") : "",
    Title
    );
}

VOID
EdDescribeValue (
  IN  CONST EDIT_PLIST  *Plist,
  IN  XML_NODE          *Node,
  OUT CHAR8             *Buffer,
  IN  UINTN             Size
  )
{
  CHAR8        Number[32];
  CONST CHAR8  *Content;
  UINT8        *Data;
  UINTN        DataSize;
  UINTN        Index;
  UINTN        Length;
  CHAR8        Hex[8 * 3 + 1];

  switch (EditPlistNodeKind (Node)) {
    case EditPlistKindDict:
      AsciiSPrint (Buffer, Size, "> {%u}", EditPlistChildCount (Node));
      break;
    case EditPlistKindArray:
      AsciiSPrint (Buffer, Size, "> [%u]", EditPlistChildCount (Node));
      break;
    case EditPlistKindBoolean:
      AsciiSPrint (Buffer, Size, "%a", EditPlistGetBoolean (Plist, Node) ? "true" : "false");
      break;
    case EditPlistKindInteger:
      EditPlistGetInteger (Plist, Node, Number, sizeof (Number));
      AsciiSPrint (Buffer, Size, "%a", Number);
      break;
    case EditPlistKindString:
      Content = EditPlistGetContent (Plist, Node);
      Length  = AsciiStrLen (Content);
      if (Length > 90) {
        AsciiSPrint (Buffer, Size, "\"%.90a...\"", Content);
      } else {
        AsciiSPrint (Buffer, Size, "\"%a\"", Content);
      }

      break;
    case EditPlistKindData:
      if (EFI_ERROR (EditPlistGetData (Plist, Node, &Data, &DataSize))) {
        AsciiSPrint (Buffer, Size, "<invalid data>");
        break;
      }

      Hex[0] = '\0';
      for (Index = 0; (Index < DataSize) && (Index < 8); ++Index) {
        AsciiSPrint (&Hex[Index * 3], sizeof (Hex) - Index * 3, "%02X ", Data[Index]);
      }

      AsciiSPrint (Buffer, Size, "<%u bytes> %a%a", (UINT32)DataSize, Hex, DataSize > 8 ? "..." : "");
      if (Data != NULL) {
        FreePool (Data);
      }

      break;
    default:
      AsciiSPrint (Buffer, Size, "<%a>", XmlNodeName (Node));
      break;
  }
}

/**
  Check if anything in the node has been changed.
**/
STATIC
BOOLEAN
EdIsChanged (
  IN  CONST EDIT_PLIST  *Plist,
  IN  XML_NODE          *Node
  )
{
  UINT32  Index;
  UINT32  Start;
  UINT32  End;

  if (EditPlistIsPatched (Plist, Node)) {
    return TRUE;
  }

  if (!XmlNodeSourceRange (Node, &Start, &End)) {
    return FALSE;
  }

  for (Index = 0; Index < Plist->PatchCount; ++Index) {
    if ((Plist->Patches[Index].Start >= Start) && (Plist->Patches[Index].Start < End)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Settings which change how OpenCore verifies things or starts are not to be edited here:
  mistakes in them stop OpenCore from starting. Switches can be changed.
**/
STATIC
BOOLEAN
EdIsProtected (
  IN  CONST ED_BROWSER  *Browser,
  IN  XML_NODE          *Node
  )
{
  return Browser->Depth == 2
         && AsciiStrCmp (Browser->Levels[1].Name, "Misc") == 0
         && AsciiStrCmp (Browser->Levels[2].Name, "Security") == 0
         && EditPlistNodeKind (Node) != EditPlistKindBoolean;
}

STATIC
VOID
EdBrowserItem (
  IN  VOID     *Context,
  IN  UINTN    Index,
  OUT CHAR8    *Text,
  IN  UINTN    Size,
  OUT BOOLEAN  *Changed,
  OUT BOOLEAN  *Dim,
  OUT BOOLEAN  *Error
  )
{
  ED_BROWSER   *Browser;
  XML_NODE     *Node;
  XML_NODE     *Child;
  CONST CHAR8  *Key;
  CHAR8        Label[160];
  CHAR8        Value[200];
  UINTN        Length;

  Browser = Context;
  Node    = Browser->Levels[Browser->Depth].Node;
  Child   = EditPlistChildValue (Node, (UINT32)Index);
  Key     = EditPlistChildKey (Node, (UINT32)Index);

  *Changed = EdIsChanged (&Browser->Plist, Child);
  *Dim     = EdIsProtected (Browser, Child);
  *Error   = FALSE;

  if (Key != NULL) {
    AsciiSPrint (Label, sizeof (Label), "%a", Key);
  } else {
    EdArrayLabel (&Browser->Plist, Child, (UINT32)Index, Label, sizeof (Label));
  }

  //
  // Values start at the same column unless the label is very long.
  //
  Length = AsciiStrLen (Label);
  if (Length > 34) {
    Label[34] = '\0';
    Length    = 34;
  }

  while (Length < 34) {
    Label[Length++] = ' ';
  }

  Label[Length] = '\0';

  if ((Key == NULL) && (EditPlistNodeKind (Child) == EditPlistKindDict)) {
    Value[0] = '\0';
    if (EditPlistNodeKind (Child) == EditPlistKindDict) {
      AsciiSPrint (Value, sizeof (Value), "> {%u}", EditPlistChildCount (Child));
    }
  } else {
    EdDescribeValue (&Browser->Plist, Child, Value, sizeof (Value));
  }

  AsciiSPrint (Text, Size, "%a %a %a", *Changed ? "*" : " ", Label, Value);
}

//
// Editing of values.
//

/**
  Check changes made to a value, and take them back if they spoil the configuration.
**/
STATIC
BOOLEAN
EdCheckChange (
  IN OUT ED_BROWSER  *Browser,
  IN     XML_NODE    *Node
  )
{
  EFI_STATUS  Status;
  CHAR8       *Text;
  UINT32      Size;
  UINT32      Errors;

  Status = EditPlistRender (&Browser->Plist, &Text, &Size);
  if (!EFI_ERROR (Status)) {
    Status = EditPlistCountErrors (Text, Size, &Errors);
    FreePool (Text);
  }

  if (EFI_ERROR (Status) || (Errors > Browser->Baseline)) {
    EditPlistRevert (&Browser->Plist, Node);
    EdUiNotify (&Browser->State->Ui, "OpenCore does not accept this value, it was not changed", TRUE);
    return FALSE;
  }

  return TRUE;
}

STATIC
BOOLEAN
EdIntegerFilter (
  IN CHAR8  Character
  )
{
  return (Character >= '0' && Character <= '9')
         || (Character >= 'a' && Character <= 'f')
         || (Character >= 'A' && Character <= 'F')
         || Character == 'x'
         || Character == 'X';
}

STATIC
BOOLEAN
EdHexFilter (
  IN CHAR8  Character
  )
{
  return (Character >= '0' && Character <= '9')
         || (Character >= 'a' && Character <= 'f')
         || (Character >= 'A' && Character <= 'F')
         || Character == ' ';
}

STATIC
BOOLEAN
EdValueFilter (
  IN CHAR8  Character
  )
{
  return Character != '<' && Character != '>';
}

/**
  Parse a decimal number or a hexadecimal number which starts with 0x.
**/
STATIC
BOOLEAN
EdParseInteger (
  IN  CONST CHAR8  *Text,
  OUT UINT64       *Value
  )
{
  UINT64  Result;
  UINT64  Digit;
  UINT64  Base;
  UINTN   Index;

  Base  = 10;
  Index = 0;
  if ((Text[0] == '0') && ((Text[1] == 'x') || (Text[1] == 'X'))) {
    Base  = 16;
    Index = 2;
  }

  if (Text[Index] == '\0') {
    return FALSE;
  }

  Result = 0;
  for ( ; Text[Index] != '\0'; ++Index) {
    if ((Text[Index] >= '0') && (Text[Index] <= '9')) {
      Digit = (UINT64)(Text[Index] - '0');
    } else if ((Base == 16) && (Text[Index] >= 'a') && (Text[Index] <= 'f')) {
      Digit = (UINT64)(Text[Index] - 'a') + 10;
    } else if ((Base == 16) && (Text[Index] >= 'A') && (Text[Index] <= 'F')) {
      Digit = (UINT64)(Text[Index] - 'A') + 10;
    } else {
      return FALSE;
    }

    if (Result > (MAX_UINT64 - Digit) / Base) {
      return FALSE;
    }

    Result = Result * Base + Digit;
  }

  *Value = Result;
  return TRUE;
}

/**
  Show the heading of an editing screen.
**/
STATIC
VOID
EdValueScreen (
  IN OUT ED_BROWSER   *Browser,
  IN     CONST CHAR8  *Kind,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Current,
  IN     CONST CHAR8  *Hint
  )
{
  ED_UI  *Ui;
  CHAR8  Line[256];

  Ui = &Browser->State->Ui;

  EdUiClear (Ui);
  AsciiSPrint (Line, sizeof (Line), "Edit %a", Kind);
  EdUiTitle (Ui, Line, Name);
  AsciiSPrint (Line, sizeof (Line), "Now: %a", Current);
  EdUiPut (Ui, 0, 3, Ui->AttrNormal, Line, 0);
  EdUiPut (Ui, 0, 5, Ui->AttrNormal, "New value:", 0);
  EdUiHint (Ui, Hint);
}

STATIC
VOID
EdEditInteger (
  IN OUT ED_BROWSER   *Browser,
  IN     XML_NODE     *Node,
  IN     CONST CHAR8  *Name
  )
{
  ED_UI   *Ui;
  CHAR8   Buffer[32];
  CHAR8   Current[32];
  UINT64  Value;

  Ui = &Browser->State->Ui;

  EditPlistGetInteger (&Browser->Plist, Node, Current, sizeof (Current));
  AsciiStrCpyS (Buffer, sizeof (Buffer), Current);

  while (TRUE) {
    EdValueScreen (Browser, "integer", Name, Current, "Decimal number or 0x hexadecimal.   Enter: accept   Esc: clear, then cancel");
    if (EdUiEditLine (Ui, 6, 1, Buffer, sizeof (Buffer), EdIntegerFilter) == EdLineCancelled) {
      return;
    }

    if (EdParseInteger (Buffer, &Value)) {
      break;
    }

    EdUiNotify (Ui, "Not a number", TRUE);
  }

  if (!EFI_ERROR (EditPlistSetInteger (&Browser->Plist, Node, Value))) {
    EdCheckChange (Browser, Node);
  }
}

STATIC
VOID
EdEditString (
  IN OUT ED_BROWSER   *Browser,
  IN     XML_NODE     *Node,
  IN     CONST CHAR8  *Name
  )
{
  ED_UI        *Ui;
  CHAR8        Buffer[EDIT_PLIST_MAX_VALUE_LENGTH + 1];
  CONST CHAR8  *Current;
  UINTN        Rows;

  Ui      = &Browser->State->Ui;
  Current = EditPlistGetContent (&Browser->Plist, Node);

  if (!EditPlistIsPlainString (Current)) {
    EdUiNotify (Ui, "This string has characters which cannot be edited here", TRUE);
    return;
  }

  AsciiStrCpyS (Buffer, sizeof (Buffer), Current);

  Rows = Ui->Rows - 6 - 4;
  while (TRUE) {
    EdValueScreen (Browser, "string", Name, Current, "Enter: accept   Esc: clear, then cancel   Arrows, Home, End, Del: edit");
    if (EdUiEditLine (Ui, 6, Rows, Buffer, sizeof (Buffer), EdValueFilter) == EdLineCancelled) {
      return;
    }

    if (EditPlistIsPlainString (Buffer)) {
      break;
    }

    EdUiNotify (Ui, "Only printable characters are accepted, & has to be part of &amp; &lt; &gt; &quot; or &apos;", TRUE);
  }

  if (!EFI_ERROR (EditPlistSetString (&Browser->Plist, Node, Buffer))) {
    EdCheckChange (Browser, Node);
  }
}

STATIC
VOID
EdEditData (
  IN OUT ED_BROWSER   *Browser,
  IN     XML_NODE     *Node,
  IN     CONST CHAR8  *Name
  )
{
  ED_UI       *Ui;
  EFI_STATUS  Status;
  UINT8       *Data;
  UINTN       Size;
  CHAR8       Buffer[ED_MAX_DATA_SIZE * 2 + 1];
  CHAR8       Current[ED_MAX_DATA_SIZE * 2 + 1];
  UINT8       Bytes[ED_MAX_DATA_SIZE];
  UINTN       Index;
  UINTN       Digits;
  UINTN       Count;
  UINT8       Nibble;

  Ui = &Browser->State->Ui;

  Status = EditPlistGetData (&Browser->Plist, Node, &Data, &Size);
  if (EFI_ERROR (Status)) {
    EdUiNotify (Ui, "This data is not valid base64", TRUE);
    return;
  }

  if (Size > ED_MAX_DATA_SIZE) {
    if (Data != NULL) {
      FreePool (Data);
    }

    EdUiNotify (Ui, "This data is too large to be edited here", TRUE);
    return;
  }

  for (Index = 0; Index < Size; ++Index) {
    AsciiSPrint (&Current[Index * 2], sizeof (Current) - Index * 2, "%02X", Data[Index]);
  }

  Current[Size * 2] = '\0';
  if (Data != NULL) {
    FreePool (Data);
  }

  AsciiStrCpyS (Buffer, sizeof (Buffer), Current);

  while (TRUE) {
    EdValueScreen (Browser, "data", Name, Current[0] != '\0' ? Current : "(empty)", "Hexadecimal bytes, e.g. E7030000.   Enter: accept   Esc: clear, then cancel");
    if (EdUiEditLine (Ui, 6, Ui->Rows - 6 - 4, Buffer, sizeof (Buffer), EdHexFilter) == EdLineCancelled) {
      return;
    }

    Digits = 0;
    Count  = 0;
    for (Index = 0; Buffer[Index] != '\0'; ++Index) {
      if (Buffer[Index] == ' ') {
        continue;
      }

      if ((Buffer[Index] >= '0') && (Buffer[Index] <= '9')) {
        Nibble = (UINT8)(Buffer[Index] - '0');
      } else if ((Buffer[Index] >= 'a') && (Buffer[Index] <= 'f')) {
        Nibble = (UINT8)(Buffer[Index] - 'a' + 10);
      } else {
        Nibble = (UINT8)(Buffer[Index] - 'A' + 10);
      }

      if ((Digits & 1) == 0) {
        Bytes[Count] = (UINT8)(Nibble << 4);
      } else {
        Bytes[Count++] |= Nibble;
      }

      ++Digits;
    }

    if ((Digits & 1) == 0) {
      break;
    }

    EdUiNotify (Ui, "An even number of hexadecimal digits is needed", TRUE);
  }

  if (!EFI_ERROR (EditPlistSetData (&Browser->Plist, Node, Bytes, Count))) {
    EdCheckChange (Browser, Node);
  }
}

STATIC
VOID
EdToggle (
  IN OUT ED_BROWSER  *Browser,
  IN     XML_NODE    *Node
  )
{
  EditPlistSetBoolean (&Browser->Plist, Node, !EditPlistGetBoolean (&Browser->Plist, Node));
}

//
// The browser.
//

STATIC
VOID
EdBrowserInitLevel (
  IN OUT ED_BROWSER   *Browser,
  IN     UINTN        Depth,
  IN     XML_NODE     *Node,
  IN     CONST CHAR8  *Name
  )
{
  ED_LEVEL  *Level;

  Level = &Browser->Levels[Depth];

  Level->Node = Node;
  AsciiSPrint (Level->Name, sizeof (Level->Name), "%a", Name);
  Level->List.Count    = EditPlistChildCount (Node);
  Level->List.Selected = 0;
  Level->List.Top      = 0;
}

STATIC
VOID
EdBrowserPath (
  IN  CONST ED_BROWSER  *Browser,
  OUT CHAR8             *Path,
  IN  UINTN             Size
  )
{
  UINTN  Level;
  UINTN  Length;

  EdConfigNameAscii (Path, Size);

  for (Level = 1; Level <= Browser->Depth; ++Level) {
    Length = AsciiStrLen (Path);
    AsciiSPrint (&Path[Length], Size - Length, " > %a", Browser->Levels[Level].Name);
  }
}

STATIC
VOID
EdBrowserDraw (
  IN OUT ED_BROWSER  *Browser
  )
{
  ED_UI     *Ui;
  ED_LEVEL  *Level;
  CHAR8     Path[256];
  CHAR8     Title[OC_CONFIG_MAX_NAME + 16];
  CHAR8     Line[288];
  UINTN     Visible;

  Ui      = &Browser->State->Ui;
  Level   = &Browser->Levels[Browser->Depth];
  Visible = EdUiContentRows (Ui);

  EdBrowserPath (Browser, Path, sizeof (Path));
  EdConfigNameAscii (Title, sizeof (Title) - L_STR_LEN ("Edit "));
  AsciiSPrint (Line, sizeof (Line), "Edit %a", Title);
  AsciiStrCpyS (Title, sizeof (Title), Line);

  if (EditPlistPatchCount (&Browser->Plist) > 0) {
    AsciiSPrint (Line, sizeof (Line), "%a   (%u unsaved changes)", Path, EditPlistPatchCount (&Browser->Plist));
    EdUiTitle (Ui, Title, Line);
  } else {
    EdUiTitle (Ui, Title, Path);
  }

  EdListDraw (Ui, 3, Visible, &Level->List, EdBrowserItem, Browser);

  if (Level->List.Count == 0) {
    EdUiPut (Ui, 0, 3, Ui->AttrHint, "  (empty)", 0);
  }

  if (Browser->Status[0] != '\0') {
    EdUiStatus (Ui, Browser->StatusIsError ? Ui->AttrError : Ui->AttrChanged, Browser->Status);
  } else {
    AsciiSPrint (Line, sizeof (Line), "%u of %u", (UINT32)(Level->List.Count == 0 ? 0 : Level->List.Selected + 1), (UINT32)Level->List.Count);
    EdUiStatus (Ui, Ui->AttrHint, Line);
  }

  EdUiHint (Ui, "Enter: open/edit  Space: on/off  Left/Esc: back  F10: save");
}

/**
  Get what is selected.
**/
STATIC
XML_NODE *
EdBrowserSelected (
  IN  CONST ED_BROWSER  *Browser,
  OUT CONST CHAR8       **Key  OPTIONAL
  )
{
  CONST ED_LEVEL  *Level;

  Level = &Browser->Levels[Browser->Depth];
  if (Level->List.Count == 0) {
    return NULL;
  }

  if (Key != NULL) {
    *Key = EditPlistChildKey (Level->Node, (UINT32)Level->List.Selected);
  }

  return EditPlistChildValue (Level->Node, (UINT32)Level->List.Selected);
}

STATIC
VOID
EdBrowserSetStatus (
  OUT ED_BROWSER   *Browser,
  IN  BOOLEAN      IsError,
  IN  CONST CHAR8  *Text
  )
{
  EdSetStatus (Browser->Status, sizeof (Browser->Status), &Browser->StatusIsError, IsError, Text);
}

STATIC
VOID
EdBrowserActivate (
  IN OUT ED_BROWSER  *Browser
  )
{
  XML_NODE     *Child;
  CONST CHAR8  *Key;
  CHAR8        Name[160];
  ED_LEVEL     *Level;

  Child = EdBrowserSelected (Browser, &Key);
  if (Child == NULL) {
    return;
  }

  Level = &Browser->Levels[Browser->Depth];

  if (Key != NULL) {
    AsciiSPrint (Name, sizeof (Name), "%a", Key);
  } else {
    EdArrayLabel (&Browser->Plist, Child, (UINT32)Level->List.Selected, Name, sizeof (Name));
  }

  switch (EditPlistNodeKind (Child)) {
    case EditPlistKindDict:
    case EditPlistKindArray:
      if (Browser->Depth + 1 >= ED_MAX_DEPTH) {
        EdBrowserSetStatus (Browser, TRUE, "Nested too deeply");
        return;
      }

      ++Browser->Depth;
      //
      // Elements of arrays are named by their position here.
      //
      if (Key == NULL) {
        AsciiSPrint (Name, sizeof (Name), "[%u]", (UINT32)Level->List.Selected);
      }

      EdBrowserInitLevel (Browser, Browser->Depth, Child, Name);
      break;
    case EditPlistKindBoolean:
      EdToggle (Browser, Child);
      break;
    case EditPlistKindInteger:
    case EditPlistKindString:
    case EditPlistKindData:
      if (EdIsProtected (Browser, Child)) {
        EdBrowserSetStatus (Browser, TRUE, "Protected: a mistake here can stop OpenCore from starting, use a file editor");
        return;
      }

      if (EditPlistNodeKind (Child) == EditPlistKindInteger) {
        EdEditInteger (Browser, Child, Name);
      } else if (EditPlistNodeKind (Child) == EditPlistKindString) {
        EdEditString (Browser, Child, Name);
      } else {
        EdEditData (Browser, Child, Name);
      }

      break;
    default:
      EdBrowserSetStatus (Browser, TRUE, "This kind of value cannot be edited here");
      break;
  }
}

/**
  Space: switch the value on or off, or the Enabled entry of the selected dictionary.
**/
STATIC
VOID
EdBrowserQuickToggle (
  IN OUT ED_BROWSER  *Browser
  )
{
  XML_NODE  *Child;

  Child = EdBrowserSelected (Browser, NULL);
  if (Child == NULL) {
    return;
  }

  if (EditPlistNodeKind (Child) == EditPlistKindDict) {
    Child = EdFindEnabled (Child);
  }

  if ((Child == NULL) || (EditPlistNodeKind (Child) != EditPlistKindBoolean)) {
    EdBrowserSetStatus (Browser, FALSE, "Nothing to switch here");
    return;
  }

  EdToggle (Browser, Child);
}

/**
  Save the changes.

  @retval TRUE  Changes have been written.
**/
STATIC
BOOLEAN
EdBrowserSave (
  IN OUT ED_BROWSER  *Browser
  )
{
  EFI_STATUS   Status;
  CONST CHAR8  *Reason;
  CHAR8        Line[200];

  if (EditPlistPatchCount (&Browser->Plist) == 0) {
    EdBrowserSetStatus (Browser, FALSE, "No changes to save");
    return FALSE;
  }

  Status = EdCommitConfig (Browser->State, &Browser->Plist, Browser->Baseline, &Reason);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Line, sizeof (Line), "%a (%r)", Reason != NULL ? Reason : "Failed", Status);
    EdUiNotify (&Browser->State->Ui, Line, TRUE);
    return FALSE;
  }

  return TRUE;
}

VOID
EdConfigEditor (
  IN OUT ED_STATE  *State
  )
{
  ED_UI               *Ui;
  ED_BROWSER          *Browser;
  EFI_STATUS          Status;
  CONST CHAR8         *Reason;
  OC_PICKER_KEY_INFO  *Key;
  UINTN               Visible;
  INTN                Choice;
  BOOLEAN             Saved;
  BOOLEAN             Redraw;

  Ui = &State->Ui;

  Browser = AllocateZeroPool (sizeof (*Browser));
  if (Browser == NULL) {
    EdUiNotify (Ui, "Not enough memory", TRUE);
    return;
  }

  Browser->State = State;

  Status = EdLoadConfig (State, &Browser->Plist, &Browser->Baseline, &Reason);
  if (EFI_ERROR (Status)) {
    EdUiTitle (Ui, "Edit config", NULL);
    EdUiNotify (Ui, Reason != NULL ? Reason : "Cannot load the config", TRUE);
    FreePool (Browser);
    return;
  }

  EdBrowserInitLevel (Browser, 0, Browser->Plist.Root, "");

  Visible = EdUiContentRows (Ui);
  Redraw  = TRUE;

  while (TRUE) {
    //
    // Clearing the screen is slow on some systems, thus it is only done when something else was shown.
    //
    if (Redraw) {
      EdUiClear (Ui);
      Redraw = FALSE;
    }

    EdBrowserDraw (Browser);

    Key = EdUiWaitKey (Ui, FALSE);

    //
    // A message is shown until the next key.
    //
    Browser->Status[0] = '\0';

    switch (Key->OcKeyCode) {
      case OC_INPUT_UP:
      case OC_INPUT_DOWN:
      case OC_INPUT_TOP:
      case OC_INPUT_BOTTOM:
        EdListMove (&Browser->Levels[Browser->Depth].List, Visible, Key->OcKeyCode);
        break;
      case OC_INPUT_RIGHT:
      case OC_INPUT_CONTINUE:
        EdBrowserActivate (Browser);
        Redraw = TRUE;
        break;
      case OC_INPUT_MORE:
        EdBrowserQuickToggle (Browser);
        break;
      case OC_INPUT_FUNCTIONAL (10):
        Redraw = TRUE;
        if (EdBrowserSave (Browser)) {
          EdSetStatus (State->Status, sizeof (State->Status), &State->StatusIsError, FALSE, "The config is saved, restart to apply the changes");
          if (EdUiConfirm (Ui, "Saved. Restart now to apply the changes?")) {
            EdRestart (State);
          }

          goto Done;
        }

        break;
      case OC_INPUT_LEFT:
      case OC_INPUT_ABORTED:
        Redraw = TRUE;
        if (Browser->Depth > 0) {
          --Browser->Depth;
          break;
        }

        if (EditPlistPatchCount (&Browser->Plist) == 0) {
          goto Done;
        }

        Choice = EdAskUnsaved (Ui);
        if (Choice == 2) {
          goto Done;
        }

        if (Choice >= 0) {
          Saved = EdBrowserSave (Browser);
          if (Saved) {
            EdSetStatus (State->Status, sizeof (State->Status), &State->StatusIsError, FALSE, "The config is saved, restart to apply the changes");
            if (Choice == 0) {
              EdRestart (State);
            }

            goto Done;
          }
        }

        break;
      default:
        break;
    }
  }

Done:
  EditPlistFree (&Browser->Plist);
  FreePool (Browser);
}
