/** @file
  Options editor: kexts, drivers, ACPI, tools and the other on/off lists.

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
// Kexts: switching them on and off and putting them in the order they are loaded in.
//

STATIC CHAR8  mListReason[96];

CONST ED_LIST_KIND  mKindKexts = {
  "Kexts", "Kernel", "Add", { "BundlePath", NULL }, NULL, TRUE,
  "Kexts are loaded from the top to the bottom, each needs the ones above it"
};
CONST ED_LIST_KIND  mKindDrivers = {
  "Drivers", "UEFI", "Drivers", { "Path", "Comment", NULL }, L"Drivers\\", FALSE,
  "Drivers are loaded from the top to the bottom"
};
CONST ED_LIST_KIND  mKindAcpi = {
  "ACPI files", "ACPI", "Add", { "Path", "Comment", NULL }, L"ACPI\\", FALSE,
  "ACPI tables (SSDT, DSDT) which are added to the system"
};
CONST ED_LIST_KIND  mKindAcpiPatch = {
  "ACPI patches", "ACPI", "Patch", { "Comment", "TableSignature", NULL }, NULL, FALSE,
  "Patches of ACPI tables"
};
CONST ED_LIST_KIND  mKindTools = {
  "Tools", "Misc", "Tools", { "Name", "Path", "Comment", NULL }, L"Tools\\", FALSE,
  "Tools shown in the boot menu"
};
CONST ED_LIST_KIND  mKindEntries = {
  "Entries", "Misc", "Entries", { "Name", "Path", "Comment", NULL }, NULL, FALSE,
  "Custom entries shown in the boot menu"
};

CONST CHAR8 *
EdDictString (
  IN  CONST EDIT_PLIST  *Plist,
  IN  XML_NODE          *Dict,
  IN  CONST CHAR8       *Key
  )
{
  XML_NODE  *Node;

  Node = EditPlistFindChild (Dict, Key);
  if ((Node != NULL) && (EditPlistNodeKind (Node) == EditPlistKindString)) {
    return EditPlistGetContent (Plist, Node);
  }

  return "";
}

/**
  Get the name of an entry by the keys of the kind.
**/
STATIC
CONST CHAR8 *
EdEntryName (
  IN  CONST EDIT_PLIST    *Plist,
  IN  XML_NODE            *Dict,
  IN  CONST ED_LIST_KIND  *Kind
  )
{
  UINTN        Index;
  CONST CHAR8  *Name;

  for (Index = 0; (Index < ARRAY_SIZE (Kind->Names)) && (Kind->Names[Index] != NULL); ++Index) {
    Name = EdDictString (Plist, Dict, Kind->Names[Index]);
    if (Name[0] != '\0') {
      return Name;
    }
  }

  return "";
}

STATIC
CHAR8 *
EdCopyString (
  IN  CONST CHAR8  *Text
  )
{
  CHAR8  *Copy;
  UINTN  Size;

  Size = AsciiStrLen (Text) + 1;
  Copy = AllocatePool (Size);
  if (Copy != NULL) {
    CopyMem (Copy, Text, Size);
  }

  return Copy;
}

/**
  Get the path of a file of a kext, the way OpenCore forms it.
**/
STATIC
BOOLEAN
EdKextFilePath (
  OUT CHAR16       *Path,
  IN  UINTN        Size,
  IN  CONST CHAR8  *BundlePath,
  IN  CONST CHAR8  *File
  )
{
  if (EFI_ERROR (OcUnicodeSafeSPrint (Path, Size * sizeof (CHAR16), OPEN_CORE_KEXT_PATH L"%a\\%a", BundlePath, File))) {
    return FALSE;
  }

  UnicodeUefiSlashes (Path);
  return TRUE;
}

/**
  Look into the files of a kext: what OpenCore needs, and what the kext needs.
**/
STATIC
VOID
EdKextReadFiles (
  IN  ED_KEXTS      *Kexts,
  IN  ED_KEXT_ROW   *Row,
  OUT ED_KEXT_INFO  *Info
  )
{
  CONST CHAR8  *BundlePath;
  CONST CHAR8  *PlistPath;
  CONST CHAR8  *ExecutablePath;
  CHAR16       Path[OC_STORAGE_SAFE_PATH_MAX];
  VOID         *Data;
  UINT32       Size;

  ZeroMem (Info, sizeof (*Info));

  if (Row->Dict == NULL) {
    Row->FileProblem   = "this is not a dictionary";
    Info->FilesMissing = TRUE;
    return;
  }

  if (!Kexts->Kind->Dependencies) {
    //
    // Nothing is needed from the files but their being there.
    //
    Info->HasInfo = TRUE;
    if (Kexts->Kind->Directory != NULL) {
      CONST CHAR8  *File;

      File = EdDictString (&Kexts->Plist, Row->Dict, "Path");
      if (  (File[0] == '\0')
         || EFI_ERROR (OcUnicodeSafeSPrint (Path, sizeof (Path), L"%s%a", Kexts->Kind->Directory, File))
         || !(UnicodeUefiSlashes (Path), OcStorageExistsFileUnicode (Kexts->State->Storage, Path)))
      {
        Row->FileProblem   = "the file is missing";
        Info->FilesMissing = TRUE;
      }
    }

    return;
  }

  BundlePath     = EdDictString (&Kexts->Plist, Row->Dict, "BundlePath");
  PlistPath      = EdDictString (&Kexts->Plist, Row->Dict, "PlistPath");
  ExecutablePath = EdDictString (&Kexts->Plist, Row->Dict, "ExecutablePath");

  //
  // Kexts for 32-bit systems are not looked at.
  //
  if (AsciiStrCmp (EdDictString (&Kexts->Plist, Row->Dict, "Arch"), "i386") == 0) {
    Info->HasInfo = TRUE;
    return;
  }

  if ((BundlePath[0] == '\0') || (PlistPath[0] == '\0')) {
    Row->FileProblem   = "BundlePath or PlistPath is empty";
    Info->FilesMissing = TRUE;
    return;
  }

  Data = NULL;
  Size = 0;
  if (EdKextFilePath (Path, ARRAY_SIZE (Path), BundlePath, PlistPath)) {
    Data = OcStorageReadFileUnicode (Kexts->State->Storage, Path, &Size);
  }

  if ((Data == NULL) || (Size > ED_KEXT_MAX_INFO_SIZE) || EFI_ERROR (EdKextParseInfo (Data, Size, Info))) {
    Row->FileProblem   = "Info.plist is missing or broken";
    Info->FilesMissing = TRUE;
  }

  if (Data != NULL) {
    FreePool (Data);
  }

  if (Info->FilesMissing) {
    return;
  }

  if (  (ExecutablePath[0] != '\0')
     && (  !EdKextFilePath (Path, ARRAY_SIZE (Path), BundlePath, ExecutablePath)
        || !OcStorageExistsFileUnicode (Kexts->State->Storage, Path)))
  {
    Row->FileProblem   = "the executable is missing";
    Info->FilesMissing = TRUE;
  }
}

STATIC
BOOLEAN
EdKextIsEnabled (
  IN  CONST ED_KEXTS  *Kexts,
  IN  UINT32          Kext
  )
{
  CONST ED_KEXT_ROW  *Row;

  Row = &Kexts->Rows[Kext];
  if (Row->Enabled != NULL) {
    return EditPlistGetBoolean (&Kexts->Plist, Row->Enabled);
  }

  return Row->EnabledAdded;
}

/**
  Find out the order and the state of the kexts, and what is wrong with them.
**/
STATIC
VOID
EdKextsRefresh (
  IN OUT ED_KEXTS  *Kexts
  )
{
  UINT32  Index;

  for (Index = 0; Index < Kexts->Count; ++Index) {
    Kexts->Order[Index]   = EditPlistArrayOriginalIndex (&Kexts->Plist, Kexts->Array, Index);
    Kexts->Enabled[Index] = EdKextIsEnabled (Kexts, Index);
  }

  //
  // A kext that is back in its place is not moved any more, and nothing is when all are back.
  //
  for (Index = 0; Index < Kexts->Count; ++Index) {
    if (Kexts->Order[Index] == Index) {
      Kexts->Rows[Index].Moved = FALSE;
    }
  }

  Kexts->ProblemKexts = EdKextFindProblems (
                          Kexts->Infos,
                          Kexts->Enabled,
                          Kexts->Count,
                          Kexts->Order,
                          Kexts->Provided,
                          Kexts->ProvidedCount,
                          Kexts->Problems
                          );
}

VOID
EdKextsFree (
  IN OUT ED_KEXTS  *Kexts
  )
{
  UINT32  Index;

  if (Kexts->Rows != NULL) {
    for (Index = 0; Index < Kexts->Count; ++Index) {
      if (Kexts->Rows[Index].Name != NULL) {
        FreePool (Kexts->Rows[Index].Name);
      }

      if (Kexts->Rows[Index].Comment != NULL) {
        FreePool (Kexts->Rows[Index].Comment);
      }
    }

    FreePool (Kexts->Rows);
  }

  if (Kexts->Infos != NULL) {
    for (Index = 0; Index < Kexts->Count; ++Index) {
      EdKextFreeInfo (&Kexts->Infos[Index]);
    }

    FreePool (Kexts->Infos);
  }

  if (Kexts->Enabled != NULL) {
    FreePool (Kexts->Enabled);
  }

  if (Kexts->Problems != NULL) {
    FreePool (Kexts->Problems);
  }

  if (Kexts->Order != NULL) {
    FreePool (Kexts->Order);
  }

  if (Kexts->Provided != NULL) {
    FreePool ((VOID *)Kexts->Provided);
  }

  EditPlistFree (&Kexts->Plist);
  FreePool (Kexts);
}

/**
  Read the kexts of the configuration in use.

  @param[in]   State   Editor state.
  @param[out]  Result  What was read.
  @param[out]  Reason  Text to show on failure.
**/
EFI_STATUS
EdKextsLoad (
  IN  ED_STATE            *State,
  IN  CONST ED_LIST_KIND  *Kind,
  OUT ED_KEXTS            **Result,
  OUT CONST CHAR8         **Reason
  )
{
  EFI_STATUS   Status;
  ED_KEXTS     *Kexts;
  XML_NODE     *Force;
  XML_NODE     *Entry;
  XML_NODE     *Enabled;
  CONST CHAR8  *Name;
  UINT32       Index;

  *Result = NULL;

  Kexts = AllocateZeroPool (sizeof (*Kexts));
  if (Kexts == NULL) {
    *Reason = "Not enough memory";
    return EFI_OUT_OF_RESOURCES;
  }

  Kexts->State = State;
  Kexts->Kind  = Kind;

  Status = EdLoadConfig (State, &Kexts->Plist, &Kexts->Baseline, Reason);
  if (EFI_ERROR (Status)) {
    FreePool (Kexts);
    return Status;
  }

  Kexts->Array = EditPlistFindChild (EditPlistFindChild (Kexts->Plist.Root, Kexts->Kind->Section), Kexts->Kind->Key);
  if (EditPlistNodeKind (Kexts->Array) != EditPlistKindArray) {
    AsciiSPrint (mListReason, sizeof (mListReason), "%a > %a is not found in the config", Kexts->Kind->Section, Kexts->Kind->Key);
    *Reason = mListReason;
    EdKextsFree (Kexts);
    return EFI_NOT_FOUND;
  }

  Kexts->Count = EditPlistChildCount (Kexts->Array);
  if (Kexts->Count > ED_KEXT_MAX_KEXTS) {
    *Reason = "Too many kexts to work with";
    EdKextsFree (Kexts);
    return EFI_UNSUPPORTED;
  }

  Kexts->Rows     = AllocateZeroPool (((UINTN)Kexts->Count + 1) * sizeof (*Kexts->Rows));
  Kexts->Infos    = AllocateZeroPool (((UINTN)Kexts->Count + 1) * sizeof (*Kexts->Infos));
  Kexts->Enabled  = AllocateZeroPool (((UINTN)Kexts->Count + 1) * sizeof (*Kexts->Enabled));
  Kexts->Problems = AllocateZeroPool (((UINTN)Kexts->Count + 1) * sizeof (*Kexts->Problems));
  Kexts->Order    = AllocateZeroPool (((UINTN)Kexts->Count + 1) * sizeof (*Kexts->Order));
  if (  (Kexts->Rows == NULL) || (Kexts->Infos == NULL) || (Kexts->Enabled == NULL)
     || (Kexts->Problems == NULL) || (Kexts->Order == NULL))
  {
    *Reason = "Not enough memory";
    EdKextsFree (Kexts);
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0; Index < Kexts->Count; ++Index) {
    Entry = EditPlistChildValue (Kexts->Array, Index);

    if (EditPlistNodeKind (Entry) == EditPlistKindDict) {
      Kexts->Rows[Index].Dict = Entry;
      Enabled                 = EditPlistFindChild (Entry, "Enabled");
      if (Enabled != NULL) {
        if (EditPlistNodeKind (Enabled) == EditPlistKindBoolean) {
          Kexts->Rows[Index].Enabled    = Enabled;
          Kexts->Rows[Index].WasEnabled = EditPlistGetBoolean (&Kexts->Plist, Enabled);
        } else {
          Kexts->Rows[Index].EnabledBroken = TRUE;
        }
      }

      Name                       = EdEntryName (&Kexts->Plist, Entry, Kexts->Kind);
      Kexts->Rows[Index].Name    = EdCopyString (Name[0] != '\0' ? Name : "(no name)");
      Kexts->Rows[Index].Comment = EdCopyString (EdDictString (&Kexts->Plist, Entry, "Comment"));
    } else {
      Kexts->Rows[Index].Name    = EdCopyString ("(not a dictionary)");
      Kexts->Rows[Index].Comment = EdCopyString ("");
    }

    if ((Kexts->Rows[Index].Name == NULL) || (Kexts->Rows[Index].Comment == NULL)) {
      *Reason = "Not enough memory";
      EdKextsFree (Kexts);
      return EFI_OUT_OF_RESOURCES;
    }

    EdKextReadFiles (Kexts, &Kexts->Rows[Index], &Kexts->Infos[Index]);
  }

  //
  // Kernel > Force: these kexts are read from the system, and are there before the others.
  //
  Force = EditPlistFindChild (EditPlistFindChild (Kexts->Plist.Root, "Kernel"), "Force");
  if (Kexts->Kind->Dependencies && (EditPlistNodeKind (Force) == EditPlistKindArray)) {
    Kexts->Provided = AllocateZeroPool (((UINTN)EditPlistChildCount (Force) + 1) * sizeof (*Kexts->Provided));
    for (Index = 0; (Kexts->Provided != NULL) && (Index < EditPlistChildCount (Force)); ++Index) {
      Entry   = EditPlistChildValue (Force, Index);
      Enabled = EditPlistFindChild (Entry, "Enabled");
      Name    = EdDictString (&Kexts->Plist, Entry, "Identifier");
      if (  (Enabled != NULL)
         && (EditPlistNodeKind (Enabled) == EditPlistKindBoolean)
         && EditPlistGetBoolean (&Kexts->Plist, Enabled)
         && (Name[0] != '\0'))
      {
        Kexts->Provided[Kexts->ProvidedCount++] = Name;
      }
    }
  }

  for (Index = 0; Index < Kexts->Count; ++Index) {
    Kexts->Order[Index] = Index;
  }

  EdKextsRefresh (Kexts);

  *Result = Kexts;
  return EFI_SUCCESS;
}

STATIC
VOID
EdKextsSetStatus (
  OUT ED_KEXTS     *Kexts,
  IN  BOOLEAN      IsError,
  IN  CONST CHAR8  *Text
  )
{
  EdSetStatus (Kexts->Status, sizeof (Kexts->Status), &Kexts->StatusIsError, IsError, Text);
}

/**
  Get how many kexts are not as they are in the file.
**/
STATIC
UINT32
EdKextsChanges (
  IN  CONST ED_KEXTS  *Kexts
  )
{
  UINT32  Index;
  UINT32  Changes;

  Changes = 0;
  for (Index = 0; Index < Kexts->Count; ++Index) {
    if (Kexts->Rows[Index].Moved || (Kexts->Enabled[Index] != Kexts->Rows[Index].WasEnabled)) {
      ++Changes;
    }
  }

  return Changes;
}

STATIC
VOID
EdKextItem (
  IN  VOID     *Context,
  IN  UINTN    Index,
  OUT CHAR8    *Text,
  IN  UINTN    Size,
  OUT BOOLEAN  *Changed,
  OUT BOOLEAN  *Dim,
  OUT BOOLEAN  *Error
  )
{
  ED_KEXTS     *Kexts;
  UINT32       Kext;
  ED_KEXT_ROW  *Row;
  BOOLEAN      On;

  Kexts = Context;
  Kext  = Kexts->Order[Index];
  Row   = &Kexts->Rows[Kext];
  On    = Kexts->Enabled[Kext];

  *Error   = Kexts->Problems[Kext].Kind != EdKextOk;
  *Changed = Row->Moved || (On != Row->WasEnabled);
  *Dim     = !On;

  AsciiSPrint (
    Text,
    Size,
    Kexts->Count >= 100 ? "%4u [%c]%c %a%a%a" : "%2u [%c]%c %a%a%a",
    (UINT32)(Index + 1),
    On ? 'x' : ' ',
    *Error ? '!' : (*Changed ? '*' : ' '),
    Row->Name,
    Row->Comment[0] != '\0' ? "   " : "",
    Row->Comment
    );
}

/**
  Say what is the matter with a kext, or what it is.
**/
VOID
EdKextDescribe (
  IN  CONST ED_KEXTS  *Kexts,
  IN  UINT32          Kext,
  OUT CHAR8           *Text,
  IN  UINTN           Size,
  OUT BOOLEAN         *IsProblem
  )
{
  CONST ED_KEXT_PROBLEM  *Problem;
  CONST ED_KEXT_INFO     *Info;
  CHAR8                  More[32];

  Problem    = &Kexts->Problems[Kext];
  Info       = &Kexts->Infos[Kext];
  *IsProblem = Problem->Kind != EdKextOk;
  More[0]    = '\0';

  if (Problem->LibraryProblems > 1) {
    AsciiSPrint (More, sizeof (More), "  (+%u more)", Problem->LibraryProblems - 1);
  }

  switch (Problem->Kind) {
    case EdKextNoFiles:
      AsciiSPrint (
        Text,
        Size,
        "OpenCore will not add this %a, %a",
        Kexts->Kind->Dependencies ? "kext" : "item",
        Kexts->Rows[Kext].FileProblem != NULL ? Kexts->Rows[Kext].FileProblem : "its files are missing"
        );
      break;
    //
    // The long part is last, as it is cut when it does not fit.
    //
    case EdKextOrder:
      AsciiSPrint (Text, Size, "Loaded too early, F5 fixes it%a: needs %a", More, Info->Libraries[Problem->Library]);
      break;
    case EdKextDisabled:
      AsciiSPrint (Text, Size, "Needs a kext which is off%a: %a (%a)", More, Info->Libraries[Problem->Library], Kexts->Rows[Problem->Provider].Name);
      break;
    case EdKextMissing:
      AsciiSPrint (Text, Size, "Needs a kext which is not in the list%a: %a", More, Info->Libraries[Problem->Library]);
      break;
    default:
      if (Info->Identifier != NULL) {
        AsciiSPrint (Text, Size, "%a", Info->Identifier);
      } else {
        Text[0] = '\0';
      }

      break;
  }
}

STATIC
VOID
EdKextsDraw (
  IN OUT ED_KEXTS  *Kexts
  )
{
  ED_UI    *Ui;
  UINT32   Index;
  UINT32   On;
  UINT32   Changes;
  CHAR8    Name[OC_CONFIG_MAX_NAME + 1];
  CHAR8    Subtitle[200];
  CHAR8    Line[256];
  BOOLEAN  IsProblem;

  Ui = &Kexts->State->Ui;

  On = 0;
  for (Index = 0; Index < Kexts->Count; ++Index) {
    if (Kexts->Enabled[Index]) {
      ++On;
    }
  }

  EdConfigNameAscii (Name, sizeof (Name));
  AsciiSPrint (Subtitle, sizeof (Subtitle), "%a: %u of %u on", Name, On, Kexts->Count);

  if (Kexts->ProblemKexts > 0) {
    AsciiSPrint (Line, sizeof (Line), ", %u with problems", Kexts->ProblemKexts);
    AsciiStrCatS (Subtitle, sizeof (Subtitle), Line);
  }

  Changes = EdKextsChanges (Kexts);
  if (Changes > 0) {
    AsciiSPrint (Line, sizeof (Line), ", %u unsaved changes", Changes);
    AsciiStrCatS (Subtitle, sizeof (Subtitle), Line);
  }

  EdUiTitle (Ui, Kexts->Kind->Title, Subtitle);
  EdUiPut (Ui, 0, 2, Ui->AttrHint, Kexts->Kind->Explanation, 0);

  EdListDraw (Ui, 3, EdUiContentRows (Ui), &Kexts->List, EdKextItem, Kexts);

  if (Kexts->Count == 0) {
    AsciiSPrint (Line, sizeof (Line), "  (there is nothing in %a > %a)", Kexts->Kind->Section, Kexts->Kind->Key);
    EdUiPut (Ui, 0, 3, Ui->AttrHint, Line, 0);
  }

  if (Kexts->Status[0] != '\0') {
    EdUiStatus (Ui, Kexts->StatusIsError ? Ui->AttrError : Ui->AttrChanged, Kexts->Status);
  } else if (Kexts->Count > 0) {
    EdKextDescribe (Kexts, Kexts->Order[Kexts->List.Selected], Line, sizeof (Line), &IsProblem);
    EdUiStatus (Ui, IsProblem ? Ui->AttrError : Ui->AttrHint, Line);
  } else {
    EdUiStatus (Ui, Ui->AttrHint, "");
  }

  EdUiHint (
    Ui,
    Kexts->Kind->Dependencies
    ? "Space: on/off   +/-: move down/up   F5: fix the order   F10: save   Esc: back"
    : "Space: on/off   +/-: move down/up   F10: save   Esc: back"
    );
}

/**
  Make sure that the selected item is in sight.
**/
STATIC
VOID
EdKextsReveal (
  IN OUT ED_KEXTS  *Kexts,
  IN     UINTN     Visible
  )
{
  if (Kexts->List.Selected < Kexts->List.Top) {
    Kexts->List.Top = Kexts->List.Selected;
  } else if (Kexts->List.Selected >= Kexts->List.Top + Visible) {
    Kexts->List.Top = Kexts->List.Selected - Visible + 1;
  }
}

STATIC
VOID
EdKextsToggle (
  IN OUT ED_KEXTS  *Kexts
  )
{
  EFI_STATUS   Status;
  UINT32       Kext;
  ED_KEXT_ROW  *Row;
  BOOLEAN      Now;
  BOOLEAN      Value;

  if (Kexts->Count == 0) {
    return;
  }

  Kext = Kexts->Order[Kexts->List.Selected];
  Row  = &Kexts->Rows[Kext];
  Now  = Kexts->Enabled[Kext];

  if (Row->Dict == NULL) {
    EdKextsSetStatus (Kexts, TRUE, "This entry is not a dictionary, it can be seen in Edit config.plist");
    return;
  }

  if (Row->EnabledBroken) {
    EdKextsSetStatus (Kexts, TRUE, "Enabled is not a boolean here, it can be fixed in Edit config.plist");
    return;
  }

  if (Row->Enabled != NULL) {
    Value  = !Now;
    Status = EditPlistSetBoolean (&Kexts->Plist, Row->Enabled, Value);
    if (!EFI_ERROR (Status) && (Value == Row->WasEnabled)) {
      //
      // Back to what the file says.
      //
      EditPlistRevert (&Kexts->Plist, Row->Enabled);
    }
  } else if (Row->EnabledAdded) {
    EditPlistRevertAdded (&Kexts->Plist, Row->Dict);
    Row->EnabledAdded = FALSE;
    Status            = EFI_SUCCESS;
  } else {
    //
    // The file says nothing, which means off.
    //
    Status = EditPlistAddBoolean (&Kexts->Plist, Row->Dict, "Enabled", TRUE);
    if (!EFI_ERROR (Status)) {
      Row->EnabledAdded = TRUE;
    }
  }

  if (EFI_ERROR (Status)) {
    EdKextsSetStatus (Kexts, TRUE, "This kext cannot be switched here, it can be done in Edit config.plist");
    return;
  }

  EdKextsRefresh (Kexts);
}

STATIC
VOID
EdKextsMove (
  IN OUT ED_KEXTS  *Kexts,
  IN     BOOLEAN   Down,
  IN     UINTN     Visible
  )
{
  EFI_STATUS  Status;
  UINTN       Target;

  if (Kexts->Count == 0) {
    return;
  }

  if (Down && (Kexts->List.Selected + 1 >= Kexts->Count)) {
    EdKextsSetStatus (Kexts, FALSE, "This one is the last already");
    return;
  }

  if (!Down && (Kexts->List.Selected == 0)) {
    EdKextsSetStatus (Kexts, FALSE, "This one is the first already");
    return;
  }

  Target = Down ? Kexts->List.Selected + 1 : Kexts->List.Selected - 1;
  Status = EditPlistArrayMove (&Kexts->Plist, Kexts->Array, (UINT32)Kexts->List.Selected, (UINT32)Target);
  if (EFI_ERROR (Status)) {
    EdKextsSetStatus (Kexts, TRUE, "The kexts cannot be moved in this config, it is not laid out for that");
    return;
  }

  Kexts->Rows[Kexts->Order[Kexts->List.Selected]].Moved = TRUE;
  Kexts->List.Selected                                  = Target;
  EdKextsReveal (Kexts, Visible);
  EdKextsRefresh (Kexts);
}

/**
  Tell what is still wrong with the kexts after their order is as good as it gets.

  @param[in]   Kexts  Kexts.
  @param[in]   Prefix  Start of the message.
  @param[out]  Text    Message.
  @param[in]   Size    Size of Text.
**/
STATIC
VOID
EdKextsLeftovers (
  IN  CONST ED_KEXTS  *Kexts,
  IN  CONST CHAR8     *Prefix,
  OUT CHAR8           *Text,
  IN  UINTN           Size
  )
{
  UINT32  Index;
  UINT32  Files;
  UINT32  Off;
  UINT32  Order;

  Files = 0;
  Off   = 0;
  Order = 0;

  for (Index = 0; Index < Kexts->Count; ++Index) {
    switch (Kexts->Problems[Index].Kind) {
      case EdKextNoFiles:
        ++Files;
        break;
      case EdKextOrder:
        ++Order;
        break;
      case EdKextDisabled:
      case EdKextMissing:
        ++Off;
        break;
      default:
        break;
    }
  }

  if (Order > 0) {
    //
    // Kexts which wait for each other.
    //
    AsciiSPrint (Text, Size, "Some kexts need each other, their order cannot be fixed");
  } else if ((Files > 0) && (Off > 0)) {
    AsciiSPrint (Text, Size, "%a Marked kexts lack files or need kexts that are off/missing", Prefix);
  } else if (Files > 0) {
    AsciiSPrint (Text, Size, "%a Marked kexts lack files which OpenCore needs", Prefix);
  } else {
    AsciiSPrint (Text, Size, "%a Marked kexts need kexts that are off or missing", Prefix);
  }
}

/**
  Put the kexts after the ones they need.
**/
STATIC
VOID
EdKextsFix (
  IN OUT ED_KEXTS  *Kexts,
  IN     UINTN     Visible
  )
{
  EFI_STATUS  Status;
  UINT32      *Order;
  UINT32      Selected;
  UINT32      Index;
  UINT32      Changed;
  CHAR8       Line[128];

  if (Kexts->Count == 0) {
    return;
  }

  Order = AllocatePool (((UINTN)Kexts->Count + 1) * sizeof (*Order));
  if (Order == NULL) {
    EdKextsSetStatus (Kexts, TRUE, "Not enough memory");
    return;
  }

  CopyMem (Order, Kexts->Order, (UINTN)Kexts->Count * sizeof (*Order));
  Changed = EdKextSort (Kexts->Infos, Kexts->Enabled, Kexts->Count, Order);

  if (Changed == 0) {
    if (Kexts->ProblemKexts == 0) {
      EdKextsSetStatus (Kexts, FALSE, "The order is right already");
    } else {
      EdKextsLeftovers (Kexts, "The order is right.", Line, sizeof (Line));
      EdKextsSetStatus (Kexts, TRUE, Line);
    }

    FreePool (Order);
    return;
  }

  Selected = Kexts->Order[Kexts->List.Selected];
  Status   = EditPlistArraySetOrder (&Kexts->Plist, Kexts->Array, Order, Kexts->Count);
  if (EFI_ERROR (Status)) {
    EdKextsSetStatus (Kexts, TRUE, "The kexts cannot be moved in this config, it is not laid out for that");
    FreePool (Order);
    return;
  }

  //
  // The ones which got another place are shown, and the selection stays with its kext.
  //
  for (Index = 0; Index < Kexts->Count; ++Index) {
    if (Kexts->Order[Index] != Order[Index]) {
      Kexts->Rows[Order[Index]].Moved = TRUE;
    }

    if (Order[Index] == Selected) {
      Kexts->List.Selected = Index;
    }
  }

  FreePool (Order);
  EdKextsReveal (Kexts, Visible);
  EdKextsRefresh (Kexts);

  if (Kexts->ProblemKexts == 0) {
    EdKextsSetStatus (Kexts, FALSE, "The order is fixed: every kext comes after the ones it needs");
  } else {
    EdKextsLeftovers (Kexts, "The order is fixed.", Line, sizeof (Line));
    EdKextsSetStatus (Kexts, TRUE, Line);
  }
}

/**
  Check that the text to be saved has the kexts in the order, and in the state, which are on the screen.
**/
STATIC
BOOLEAN
EdKextsVerify (
  IN  CONST ED_KEXTS  *Kexts
  )
{
  EFI_STATUS  Status;
  CHAR8       *Text;
  UINT32      Size;
  EDIT_PLIST  Again;
  XML_NODE    *Array;
  XML_NODE    *Entry;
  XML_NODE    *Enabled;
  UINT32      Position;
  UINT32      Kext;
  BOOLEAN     Right;
  BOOLEAN     On;

  Status = EditPlistRender (&Kexts->Plist, &Text, &Size);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Status = EditPlistLoad (&Again, Text, Size);
  FreePool (Text);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Right = FALSE;
  Array = EditPlistFindChild (EditPlistFindChild (Again.Root, Kexts->Kind->Section), Kexts->Kind->Key);
  if ((EditPlistNodeKind (Array) == EditPlistKindArray) && (EditPlistChildCount (Array) == Kexts->Count)) {
    Right = TRUE;
    for (Position = 0; Right && (Position < Kexts->Count); ++Position) {
      Kext  = Kexts->Order[Position];
      Entry = EditPlistChildValue (Array, Position);

      if (Kexts->Rows[Kext].Dict == NULL) {
        Right = EditPlistNodeKind (Entry) != EditPlistKindDict;
        continue;
      }

      Enabled = EditPlistFindChild (Entry, "Enabled");
      On      = (Enabled != NULL) && (EditPlistNodeKind (Enabled) == EditPlistKindBoolean) && EditPlistGetBoolean (&Again, Enabled);
      Right   = EditPlistNodeKind (Entry) == EditPlistKindDict
                && AsciiStrCmp (EdEntryName (&Again, Entry, Kexts->Kind), EdEntryName (&Kexts->Plist, Kexts->Rows[Kext].Dict, Kexts->Kind)) == 0
                && (Kexts->Rows[Kext].EnabledBroken || (On == Kexts->Enabled[Kext]));
    }
  }

  EditPlistFree (&Again);
  return Right;
}

STATIC
BOOLEAN
EdKextsSave (
  IN OUT ED_KEXTS  *Kexts
  )
{
  EFI_STATUS   Status;
  CONST CHAR8  *Reason;
  CHAR8        Line[200];

  if (EditPlistPatchCount (&Kexts->Plist) == 0) {
    EdKextsSetStatus (Kexts, FALSE, "No changes to save");
    return FALSE;
  }

  //
  // Nothing is written which is not what the user sees.
  //
  if (!EdKextsVerify (Kexts)) {
    EdUiNotify (&Kexts->State->Ui, "Internal error: the changed config does not match the list, nothing was written", TRUE);
    return FALSE;
  }

  Status = EdCommitConfig (Kexts->State, &Kexts->Plist, Kexts->Baseline, &Reason);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Line, sizeof (Line), "%a (%r)", Reason != NULL ? Reason : "Failed", Status);
    EdUiNotify (&Kexts->State->Ui, Line, TRUE);
    return FALSE;
  }

  return TRUE;
}

VOID
EdKextsEditor (
  IN OUT ED_STATE            *State,
  IN     CONST ED_LIST_KIND  *Kind
  )
{
  ED_UI               *Ui;
  ED_KEXTS            *Kexts;
  EFI_STATUS          Status;
  CONST CHAR8         *Reason;
  OC_PICKER_KEY_INFO  *Key;
  UINTN               Visible;
  INTN                Choice;
  BOOLEAN             Redraw;

  Ui = &State->Ui;

  EdUiClear (Ui);
  EdUiTitle (Ui, Kind->Title, NULL);
  EdUiStatus (Ui, Ui->AttrHint, "Reading the files...");

  Status = EdKextsLoad (State, Kind, &Kexts, &Reason);
  if (EFI_ERROR (Status)) {
    EdUiNotify (Ui, Reason != NULL ? Reason : "Cannot read the list", TRUE);
    return;
  }

  Visible           = EdUiContentRows (Ui);
  Redraw            = TRUE;
  Kexts->List.Count = Kexts->Count;

  while (TRUE) {
    if (Redraw) {
      EdUiClear (Ui);
      Redraw = FALSE;
    }

    EdKextsDraw (Kexts);

    Key = EdUiWaitKeyWithPlusMinus (Ui);

    //
    // A message is shown until the next key.
    //
    Kexts->Status[0] = '\0';

    switch (Key->OcKeyCode) {
      case OC_INPUT_UP:
      case OC_INPUT_DOWN:
      case OC_INPUT_TOP:
      case OC_INPUT_BOTTOM:
        EdListMove (&Kexts->List, Visible, Key->OcKeyCode);
        break;
      case OC_INPUT_MORE:
      case OC_INPUT_CONTINUE:
        EdKextsToggle (Kexts);
        break;
      case OC_INPUT_PLUS:
        EdKextsMove (Kexts, TRUE, Visible);
        break;
      case OC_INPUT_MINUS:
        EdKextsMove (Kexts, FALSE, Visible);
        break;
      case OC_INPUT_FUNCTIONAL (5):
        if (Kexts->Kind->Dependencies) {
          EdKextsFix (Kexts, Visible);
        }

        break;
      case OC_INPUT_FUNCTIONAL (10):
        Redraw = TRUE;
        if (EdKextsSave (Kexts)) {
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
        if (EditPlistPatchCount (&Kexts->Plist) == 0) {
          goto Done;
        }

        Choice = EdAskUnsaved (Ui);
        if (Choice == 2) {
          goto Done;
        }

        if ((Choice >= 0) && EdKextsSave (Kexts)) {
          EdSetStatus (State->Status, sizeof (State->Status), &State->StatusIsError, FALSE, "The config is saved, restart to apply the changes");
          if (Choice == 0) {
            EdRestart (State);
          }

          goto Done;
        }

        break;
      default:
        break;
    }
  }

Done:
  EdKextsFree (Kexts);
}

/**
  The other lists of the config.
**/
VOID
EdListsMenu (
  IN OUT ED_STATE  *State
  )
{
  STATIC CONST ED_LIST_KIND  *Kinds[] = { &mKindDrivers, &mKindAcpi, &mKindAcpiPatch, &mKindTools, &mKindEntries };

  CONST CHAR8  *Items[ARRAY_SIZE (Kinds)];
  UINTN        Selected;
  INTN         Choice;
  UINTN        Index;

  for (Index = 0; Index < ARRAY_SIZE (Kinds); ++Index) {
    Items[Index] = Kinds[Index]->Title;
  }

  Selected = 0;
  while (TRUE) {
    EdUiClear (&State->Ui);
    EdUiTitle (&State->Ui, "Lists", "Turn items on and off, and put them in order");
    EdUiHint (&State->Ui, "Up/Down, Enter: choose   Esc: back");
    Choice = EdUiMenu (&State->Ui, 4, Items, NULL, ARRAY_SIZE (Kinds), &Selected);
    if (Choice < 0) {
      return;
    }

    EdKextsEditor (State, Kinds[Choice]);
  }
}
