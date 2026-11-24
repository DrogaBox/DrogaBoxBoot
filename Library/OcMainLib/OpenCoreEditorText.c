/** @file
  Options editor: log, kernel panic, config check and config comparison.

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
// Text to read: the log, and the result of checking the config.
//

typedef struct {
  CONST CHAR8    *Text;
  UINT32         *Starts;
  UINTN          Count;
} ED_TEXT;

STATIC
VOID
EdTextItem (
  IN  VOID     *Context,
  IN  UINTN    Index,
  OUT CHAR8    *Text,
  IN  UINTN    Size,
  OUT BOOLEAN  *Changed,
  OUT BOOLEAN  *Dim,
  OUT BOOLEAN  *Error
  )
{
  ED_TEXT      *Lines;
  CONST CHAR8  *Line;
  UINTN        Length;

  Lines = Context;
  Line  = &Lines->Text[Lines->Starts[Index]];
  for (Length = 0; (Length + 1 < Size) && (Line[Length] != '\0') && (Line[Length] != '\n') && (Line[Length] != '\r'); ++Length) {
    Text[Length] = Line[Length];
  }

  Text[Length] = '\0';
  *Changed     = FALSE;
  *Dim         = FALSE;
  *Error       = (Line[0] == '!') || (AsciiStrStr (Text, "Failed") != NULL) || (AsciiStrStr (Text, "ERROR") != NULL);
}

/**
  Let the user read text line by line, starting at its end if asked to.
**/
STATIC
VOID
EdShowText (
  IN OUT ED_STATE     *State,
  IN     CONST CHAR8  *Title,
  IN     CONST CHAR8  *Subtitle,
  IN     CONST CHAR8  *Text,
  IN     BOOLEAN      AtEnd
  )
{
  ED_UI               *Ui;
  ED_TEXT             Lines;
  ED_LIST             List;
  OC_PICKER_KEY_INFO  *Key;
  UINTN               Visible;
  UINTN               Index;
  UINTN               Max;
  CHAR8               Line[96];

  Ui = &State->Ui;

  //
  // Lines, where the last one may have no break.
  //
  Max = 1;
  for (Index = 0; Text[Index] != '\0'; ++Index) {
    if (Text[Index] == '\n') {
      ++Max;
    }
  }

  Lines.Text   = Text;
  Lines.Starts = AllocatePool (Max * sizeof (*Lines.Starts));
  if (Lines.Starts == NULL) {
    EdUiNotify (Ui, "Not enough memory", TRUE);
    return;
  }

  Lines.Count = 0;
  for (Index = 0; Text[Index] != '\0'; ++Index) {
    if ((Index == 0) || (Text[Index - 1] == '\n')) {
      Lines.Starts[Lines.Count++] = (UINT32)Index;
    }
  }

  Visible       = EdUiContentRows (Ui);
  List.Count    = Lines.Count;
  List.Selected = AtEnd && (Lines.Count > 0) ? Lines.Count - 1 : 0;
  List.Top      = List.Selected >= Visible ? List.Selected - Visible + 1 : 0;

  EdUiClear (Ui);
  while (TRUE) {
    AsciiSPrint (Line, sizeof (Line), "%a   line %u of %u", Subtitle, (UINT32)(Lines.Count == 0 ? 0 : List.Selected + 1), (UINT32)Lines.Count);
    EdUiTitle (Ui, Title, Line);
    EdListDraw (Ui, 3, Visible, &List, EdTextItem, &Lines);
    EdUiHint (Ui, "Up/Down, Home, End: scroll   Esc: back");

    Key = EdUiWaitKey (Ui, FALSE);
    switch (Key->OcKeyCode) {
      case OC_INPUT_UP:
      case OC_INPUT_DOWN:
      case OC_INPUT_TOP:
      case OC_INPUT_BOTTOM:
        EdListMove (&List, Visible, Key->OcKeyCode);
        break;
      case OC_INPUT_LEFT:
      case OC_INPUT_ABORTED:
        FreePool (Lines.Starts);
        return;
      default:
        break;
    }
  }
}

VOID
EdLogViewer (
  IN OUT ED_STATE  *State
  )
{
  CONST CHAR8  *Log;

  Log = OcMiscGetLogBuffer ();
  if ((Log == NULL) || (Log[0] == '\0')) {
    EdUiNotify (&State->Ui, "There is no log: it is kept when Misc > Debug > Target has logging on", TRUE);
    return;
  }

  EdShowText (State, "OpenCore log", "The latest is at the end", Log, TRUE);
}

/**
  Show the kernel panic which macOS left in NVRAM.
**/
VOID
EdPanicViewer (
  IN OUT ED_STATE  *State
  )
{
  VOID    *Panic;
  UINT32  Size;

  Panic = OcReadApplePanicLog (&Size);
  if ((Panic == NULL) || (Size == 0) || (((CHAR8 *)Panic)[0] == '\0')) {
    if (Panic != NULL) {
      FreePool (Panic);
    }

    EdUiNotify (&State->Ui, "No panic log in NVRAM: macOS keeps it there only after a kernel panic that it could save", TRUE);
    return;
  }

  EdShowText (State, "Kernel panic log", "Saved by macOS in NVRAM (aapl,panic-info)", Panic, FALSE);
  FreePool (Panic);
}

STATIC
VOID
EdAppendLine (
  IN OUT CHAR8        **Text,
  IN OUT UINTN        *Length,
  IN OUT UINTN        *Capacity,
  IN     CONST CHAR8  *Line
  )
{
  CHAR8  *Bigger;
  UINTN  Size;

  Size = AsciiStrLen (Line) + 1;
  if (*Length + Size + 1 > *Capacity) {
    *Capacity = (*Length + Size + 1) * 2;
    Bigger    = AllocatePool (*Capacity);
    if (Bigger == NULL) {
      return;
    }

    if (*Text != NULL) {
      CopyMem (Bigger, *Text, *Length);
      FreePool (*Text);
    }

    *Text = Bigger;
  }

  if (*Text == NULL) {
    return;
  }

  CopyMem (&(*Text)[*Length], Line, Size - 1);
  *Length             += Size - 1;
  (*Text)[(*Length)++] = '\n';
  (*Text)[*Length]     = '\0';
}

/**
  Look at the config for what can be told without starting anything: the structure, and what the
  lists point to which is not there.
**/
VOID
EdCheckConfigScreen (
  IN OUT ED_STATE  *State
  )
{
  STATIC CONST ED_LIST_KIND  *Kinds[] = { &mKindKexts, &mKindDrivers, &mKindAcpi, &mKindAcpiPatch, &mKindTools, &mKindEntries };

  EFI_STATUS   Status;
  EDIT_PLIST   Plist;
  UINT32       Baseline;
  CONST CHAR8  *Reason;
  CHAR8        *Text;
  UINTN        Length;
  UINTN        Capacity;
  CHAR8        *Body;
  UINTN        BodyLength;
  UINTN        BodyCapacity;
  CHAR8        Line[200];
  ED_KEXTS     *Kexts;
  UINTN        Kind;
  UINT32       Index;
  UINT32       Total;
  BOOLEAN      Dummy;
  CHAR8        Name[OC_CONFIG_MAX_NAME + 1];
  CHAR8        Describe[160];

  EdUiClear (&State->Ui);
  EdUiTitle (&State->Ui, "Check the config", NULL);
  EdUiStatus (&State->Ui, State->Ui.AttrHint, "Checking...");

  Status = EdLoadConfig (State, &Plist, &Baseline, &Reason);
  if (EFI_ERROR (Status)) {
    EdUiNotify (&State->Ui, Reason != NULL ? Reason : "Cannot load the config", TRUE);
    return;
  }

  EditPlistFree (&Plist);

  Text         = NULL;
  Length       = 0;
  Capacity     = 0;
  Body         = NULL;
  BodyLength   = 0;
  BodyCapacity = 0;
  Total        = 0;

  if (Baseline > 0) {
    AsciiSPrint (Line, sizeof (Line), "! Structure: %u problems (unknown or wrongly typed keys), the log has details", Baseline);
    EdAppendLine (&Body, &BodyLength, &BodyCapacity, Line);
    Total += Baseline;
  } else {
    EdAppendLine (&Body, &BodyLength, &BodyCapacity, "Structure: no problems found");
  }

  for (Kind = 0; Kind < ARRAY_SIZE (Kinds); ++Kind) {
    if (EFI_ERROR (EdKextsLoad (State, Kinds[Kind], &Kexts, &Reason))) {
      continue;
    }

    for (Index = 0; Index < Kexts->Count; ++Index) {
      if (Kexts->Problems[Kexts->Order[Index]].Kind == EdKextOk) {
        continue;
      }

      EdKextDescribe (Kexts, Kexts->Order[Index], Describe, sizeof (Describe), &Dummy);
      AsciiSPrint (Line, sizeof (Line), "! %a: %a", Kinds[Kind]->Title, Kexts->Rows[Kexts->Order[Index]].Name);
      EdAppendLine (&Body, &BodyLength, &BodyCapacity, Line);
      AsciiSPrint (Line, sizeof (Line), "    %a", Describe);
      EdAppendLine (&Body, &BodyLength, &BodyCapacity, Line);
      ++Total;
    }

    EdKextsFree (Kexts);
  }

  EdConfigNameAscii (Name, sizeof (Name));
  if (Total == 0) {
    AsciiSPrint (Line, sizeof (Line), "No problems found in %a", Name);
  } else {
    AsciiSPrint (Line, sizeof (Line), "%u problems found in %a", Total, Name);
  }

  EdAppendLine (&Text, &Length, &Capacity, Line);
  EdAppendLine (&Text, &Length, &Capacity, "");
  if (Body != NULL) {
    EdAppendLine (&Text, &Length, &Capacity, Body);
    FreePool (Body);
  }

  EdAppendLine (&Text, &Length, &Capacity, "");
  EdAppendLine (&Text, &Length, &Capacity, "Checked: structure, kexts and their order, files of drivers, ACPI and tools.");
  EdAppendLine (&Text, &Length, &Capacity, "Not checked: the values of settings (use ocvalidate on a computer for that).");

  if (Text == NULL) {
    EdUiNotify (&State->Ui, "Not enough memory", TRUE);
    return;
  }

  EdShowText (State, "Check the config", Name, Text, FALSE);
  FreePool (Text);
}

//
// Comparing two configs, and going back to the config before the last save.
//

typedef struct {
  EDIT_PLIST    *A;
  EDIT_PLIST    *B;
  CHAR8         *Text;
  UINTN         Length;
  UINTN         Capacity;
  UINT32        Lines;
} ED_DIFF;

#define ED_DIFF_MAX_LINES  300U

STATIC
VOID
EdDiffLine (
  IN OUT ED_DIFF      *Diff,
  IN     CONST CHAR8  *Mark,
  IN     CONST CHAR8  *Path,
  IN     CONST CHAR8  *Detail
  )
{
  CHAR8  Line[400];

  if (Diff->Lines >= ED_DIFF_MAX_LINES) {
    return;
  }

  AsciiSPrint (Line, sizeof (Line), "%a %a%a%a", Mark, Path, Detail[0] != '\0' ? ": " : "", Detail);
  EdAppendLine (&Diff->Text, &Diff->Length, &Diff->Capacity, Line);
  ++Diff->Lines;
}

STATIC
CONST CHAR8 *
EdAnyName (
  IN  CONST EDIT_PLIST  *Plist,
  IN  XML_NODE          *Entry
  )
{
  STATIC CONST CHAR8  *Keys[] = { "BundlePath", "Path", "Name", "Identifier", "Comment" };

  UINTN        Index;
  CONST CHAR8  *Name;

  if (EditPlistNodeKind (Entry) != EditPlistKindDict) {
    return "";
  }

  for (Index = 0; Index < ARRAY_SIZE (Keys); ++Index) {
    Name = EdDictString (Plist, Entry, Keys[Index]);
    if (Name[0] != '\0') {
      return Name;
    }
  }

  return "";
}

STATIC
VOID
EdDiffNodes (
  IN OUT ED_DIFF   *Diff,
  IN     XML_NODE  *A,
  IN     XML_NODE  *B,
  IN OUT CHAR8     *Path,
  IN     UINTN     PathSize,
  IN     UINTN     Depth
  )
{
  UINTN            Length;
  UINT32           Index;
  UINT32           Other;
  UINT32           Previous;
  UINT32           Scan;
  CONST CHAR8      *Key;
  XML_NODE         *Match;
  CONST CHAR8      *Name;
  BOOLEAN          *Used;
  BOOLEAN          Ordered;
  CHAR8            First[120];
  CHAR8            Second[120];
  CHAR8            Detail[300];
  EDIT_PLIST_KIND  Kind;

  Kind = EditPlistNodeKind (A);
  if ((Kind != EditPlistNodeKind (B)) || (Depth > 12)) {
    if (Kind != EditPlistNodeKind (B)) {
      EdDiffLine (Diff, "~", Path, "different kinds of values");
    }

    return;
  }

  Length = AsciiStrLen (Path);

  switch (Kind) {
    case EditPlistKindDict:
      for (Index = 0; Index < EditPlistChildCount (A); ++Index) {
        Key = EditPlistChildKey (A, Index);
        AsciiSPrint (&Path[Length], PathSize - Length, "/%a", Key);
        Match = EditPlistFindChild (B, Key);
        if (Match == NULL) {
          EdDiffLine (Diff, "-", Path, "only in the first");
        } else {
          EdDiffNodes (Diff, EditPlistChildValue (A, Index), Match, Path, PathSize, Depth + 1);
        }

        Path[Length] = '\0';
      }

      for (Index = 0; Index < EditPlistChildCount (B); ++Index) {
        Key = EditPlistChildKey (B, Index);
        if (EditPlistFindChild (A, Key) == NULL) {
          AsciiSPrint (&Path[Length], PathSize - Length, "/%a", Key);
          EdDiffLine (Diff, "+", Path, "only in the second");
          Path[Length] = '\0';
        }
      }

      break;
    case EditPlistKindArray:
      Used = AllocateZeroPool (((UINTN)EditPlistChildCount (B) + 1) * sizeof (*Used));
      if (Used == NULL) {
        return;
      }

      Previous = 0;
      Ordered  = TRUE;
      for (Index = 0; Index < EditPlistChildCount (A); ++Index) {
        Name  = EdAnyName (Diff->A, EditPlistChildValue (A, Index));
        Other = MAX_UINT32;
        if (Name[0] != '\0') {
          for (Scan = 0; Scan < EditPlistChildCount (B); ++Scan) {
            if (!Used[Scan] && (AsciiStrCmp (EdAnyName (Diff->B, EditPlistChildValue (B, Scan)), Name) == 0)) {
              Other = Scan;
              break;
            }
          }
        } else if ((Index < EditPlistChildCount (B)) && !Used[Index] && (EdAnyName (Diff->B, EditPlistChildValue (B, Index))[0] == '\0')) {
          Other = Index;
        }

        if (Name[0] != '\0') {
          AsciiSPrint (&Path[Length], PathSize - Length, "/%a", Name);
        } else {
          AsciiSPrint (&Path[Length], PathSize - Length, "/[%u]", Index);
        }

        if (Other == MAX_UINT32) {
          EdDiffLine (Diff, "-", Path, "only in the first");
        } else {
          Used[Other] = TRUE;
          if (Other < Previous) {
            Ordered = FALSE;
          } else {
            Previous = Other;
          }

          EdDiffNodes (Diff, EditPlistChildValue (A, Index), EditPlistChildValue (B, Other), Path, PathSize, Depth + 1);
        }

        Path[Length] = '\0';
      }

      for (Index = 0; Index < EditPlistChildCount (B); ++Index) {
        if (!Used[Index]) {
          Name = EdAnyName (Diff->B, EditPlistChildValue (B, Index));
          if (Name[0] != '\0') {
            AsciiSPrint (&Path[Length], PathSize - Length, "/%a", Name);
          } else {
            AsciiSPrint (&Path[Length], PathSize - Length, "/[%u]", Index);
          }

          EdDiffLine (Diff, "+", Path, "only in the second");
          Path[Length] = '\0';
        }
      }

      if (!Ordered) {
        EdDiffLine (Diff, "~", Path, "the order is different");
      }

      FreePool (Used);
      break;
    default:
      EdDescribeValue (Diff->A, A, First, sizeof (First));
      EdDescribeValue (Diff->B, B, Second, sizeof (Second));
      if (  (AsciiStrCmp (First, Second) != 0)
         || (((Kind == EditPlistKindString) || (Kind == EditPlistKindData)) && (AsciiStrCmp (EditPlistGetContent (Diff->A, A), EditPlistGetContent (Diff->B, B)) != 0)))
      {
        AsciiSPrint (Detail, sizeof (Detail), "%a  ->  %a", First, Second);
        EdDiffLine (Diff, "~", Path, Detail);
      }

      break;
  }
}

STATIC
EFI_STATUS
EdLoadNamedConfig (
  IN  ED_STATE      *State,
  IN  CONST CHAR16  *Name,
  OUT EDIT_PLIST    *Plist
  )
{
  VOID    *Data;
  UINT32  Size;

  Data = OcStorageReadFileUnicode (State->Storage, Name, &Size);
  if (Data == NULL) {
    return EFI_NOT_FOUND;
  }

  if (EFI_ERROR (EditPlistLoad (Plist, Data, Size))) {
    FreePool (Data);
    return EFI_COMPROMISED_DATA;
  }

  FreePool (Data);
  return EFI_SUCCESS;
}

VOID
EdCompareConfigs (
  IN OUT ED_STATE  *State
  )
{
  OC_CONFIG_FILE  *Files;
  CHAR8           Names[16][OC_CONFIG_MAX_NAME + 1];
  CONST CHAR8     *Items[16];
  UINTN           Count;
  UINTN           Shown;
  UINTN           Index;
  UINTN           Selected;
  INTN            Choice;
  EDIT_PLIST      A;
  EDIT_PLIST      B;
  ED_DIFF         Diff;
  CHAR8           Path[400];
  CHAR8           Title[160];
  CHAR8           Mine[OC_CONFIG_MAX_NAME + 1];

  Files = AllocateZeroPool (OC_CONFIG_MAX_FILES * sizeof (*Files));
  if (Files == NULL) {
    EdUiNotify (&State->Ui, "Not enough memory", TRUE);
    return;
  }

  Count = OcMiscListConfigs (State->Storage, Files, OC_CONFIG_MAX_FILES);
  Shown = 0;
  for (Index = 0; (Index < Count) && (Shown < ARRAY_SIZE (Items)); ++Index) {
    if (!EdSameConfigName (Files[Index].Name, OcMiscGetConfigName ())) {
      EdUnicodeToAscii (Files[Index].Name, Names[Shown], sizeof (Names[Shown]));
      Items[Shown] = Names[Shown];
      Files[Shown] = Files[Index];
      ++Shown;
    }
  }

  EdConfigNameAscii (Mine, sizeof (Mine));
  EdUiClear (&State->Ui);
  AsciiSPrint (Title, sizeof (Title), "Compare %a with:", Mine);
  EdUiTitle (&State->Ui, "Compare configs", Title);
  if (Shown == 0) {
    FreePool (Files);
    EdUiNotify (&State->Ui, "There is no other config to compare with", TRUE);
    return;
  }

  EdUiHint (&State->Ui, "Up/Down, Enter: choose   Esc: back");
  Selected = 0;
  Choice   = EdUiMenu (&State->Ui, 4, Items, NULL, Shown, &Selected);
  if (Choice < 0) {
    FreePool (Files);
    return;
  }

  if (EFI_ERROR (EdLoadNamedConfig (State, OcMiscGetConfigName (), &A))) {
    FreePool (Files);
    EdUiNotify (&State->Ui, "Cannot read the config in use", TRUE);
    return;
  }

  if (EFI_ERROR (EdLoadNamedConfig (State, Files[Choice].Name, &B))) {
    EditPlistFree (&A);
    FreePool (Files);
    EdUiNotify (&State->Ui, "Cannot read that config", TRUE);
    return;
  }

  ZeroMem (&Diff, sizeof (Diff));
  Diff.A  = &A;
  Diff.B  = &B;
  Path[0] = '\0';
  EdAppendLine (&Diff.Text, &Diff.Length, &Diff.Capacity, "- only in the first   + only in the second   ~ differs");
  EdAppendLine (&Diff.Text, &Diff.Length, &Diff.Capacity, "");
  Diff.Lines = 0;
  EdDiffNodes (&Diff, A.Root, B.Root, Path, sizeof (Path), 0);

  if (Diff.Lines == 0) {
    EdAppendLine (&Diff.Text, &Diff.Length, &Diff.Capacity, "No differences");
  } else if (Diff.Lines >= ED_DIFF_MAX_LINES) {
    EdAppendLine (&Diff.Text, &Diff.Length, &Diff.Capacity, "... and more, only the first lines are shown");
  }

  AsciiSPrint (Title, sizeof (Title), "%a  ->  %a", Mine, Items[Choice]);
  if (Diff.Text != NULL) {
    EdShowText (State, "Compare configs", Title, Diff.Text, FALSE);
    FreePool (Diff.Text);
  }

  EditPlistFree (&A);
  EditPlistFree (&B);
  FreePool (Files);
}
