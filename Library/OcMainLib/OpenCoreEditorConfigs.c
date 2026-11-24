/** @file
  Options editor: config files and the backup of the last save.

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

/**
  Put the config from before the last save back. The present one becomes the backup then.
**/
VOID
EdRestoreBackup (
  IN OUT ED_STATE  *State
  )
{
  CHAR16      BackupName[OC_CONFIG_MAX_NAME + 8];
  VOID        *Backup;
  VOID        *Present;
  UINT32      BackupSize;
  UINT32      PresentSize;
  UINT32      Errors;
  EFI_STATUS  Status;
  CHAR8       Line[160];
  CHAR8       Name[OC_CONFIG_MAX_NAME + 1];

  UnicodeSPrint (BackupName, sizeof (BackupName), L"%s.bak", OcMiscGetConfigName ());
  Backup  = OcStorageReadFileUnicode (State->Storage, BackupName, &BackupSize);
  Present = OcStorageReadFileUnicode (State->Storage, OcMiscGetConfigName (), &PresentSize);
  EdConfigNameAscii (Name, sizeof (Name));

  if ((Backup == NULL) || (Present == NULL)) {
    AsciiSPrint (Line, sizeof (Line), "There is no %a.bak: nothing was saved from the editor yet", Name);
    EdUiNotify (&State->Ui, Line, TRUE);
  } else if (EFI_ERROR (EditPlistCountErrors (Backup, BackupSize, &Errors))) {
    EdUiNotify (&State->Ui, "The backup is not a config OpenCore can load", TRUE);
  } else {
    AsciiSPrint (Line, sizeof (Line), "Put %a.bak back as %a? The present one will be kept as the backup", Name, Name);
    if (EdUiConfirm (&State->Ui, Line)) {
      Status = EdWriteConfig (State->Storage, Present, PresentSize, Backup, BackupSize);
      if (EFI_ERROR (Status)) {
        EdUiNotify (&State->Ui, "Cannot write the config (the file system may be read-only)", TRUE);
      } else {
        EdSetStatus (State->Status, sizeof (State->Status), &State->StatusIsError, FALSE, "The previous config is back, restart to apply it");
      }
    }
  }

  if (Backup != NULL) {
    FreePool (Backup);
  }

  if (Present != NULL) {
    FreePool (Present);
  }
}

//
// Choosing the configuration file.
//

/**
  Short note on where the configuration in use comes from, for the main menu.
**/
CONST CHAR8 *
EdConfigSourceNote (
  IN  OC_CONFIG_SOURCE  Source
  )
{
  switch (Source) {
    case OcConfigSourceOnce:
      return "  (this start only)";
    case OcConfigSourceKept:
      return "  (kept)";
    case OcConfigSourceFallback:
      return "  (the chosen one failed)";
    default:
      return "";
  }
}

/**
  Where the configuration in use comes from, for the list of configurations.
**/
STATIC
CONST CHAR8 *
EdConfigSourceDescription (
  IN  OC_CONFIG_SOURCE  Source
  )
{
  switch (Source) {
    case OcConfigSourceOnce:
      return "chosen for this start only";
    case OcConfigSourceKept:
      return "chosen to stay until another one is chosen";
    default:
      return "the default";
  }
}

STATIC
VOID
EdConfigItem (
  IN  VOID     *Context,
  IN  UINTN    Index,
  OUT CHAR8    *Text,
  IN  UINTN    Size,
  OUT BOOLEAN  *Changed,
  OUT BOOLEAN  *Dim,
  OUT BOOLEAN  *Error
  )
{
  OC_CONFIG_FILE  *Files;
  CHAR8           Name[OC_CONFIG_MAX_NAME + 1];
  BOOLEAN         InUse;
  BOOLEAN         IsDefault;

  Files = Context;
  EdUnicodeToAscii (Files[Index].Name, Name, sizeof (Name));

  InUse     = EdSameConfigName (Files[Index].Name, OcMiscGetConfigName ());
  IsDefault = EdSameConfigName (Files[Index].Name, OPEN_CORE_CONFIG_PATH);

  *Changed = FALSE;
  *Dim     = FALSE;
  *Error   = FALSE;
  AsciiSPrint (
    Text,
    Size,
    "%a%a",
    Name,
    InUse ? (IsDefault ? "   (in use, default)" : "   (in use)") : (IsDefault ? "   (default)" : "")
    );
}

/**
  Look into a configuration before starting OpenCore with it.

  @param[in]   State         Editor state.
  @param[in]   Name          Name of the configuration file.
  @param[out]  AllowsEditor  Whether the configuration enables this editor.
  @param[out]  Reason        Text to show on failure.
**/
STATIC
EFI_STATUS
EdCheckConfig (
  IN  ED_STATE      *State,
  IN  CONST CHAR16  *Name,
  OUT BOOLEAN       *AllowsEditor,
  OUT CONST CHAR8   **Reason
  )
{
  EFI_STATUS  Status;
  VOID        *Data;
  UINT32      Size;
  UINT32      Errors;
  EDIT_PLIST  Plist;
  XML_NODE    *Node;

  *AllowsEditor = FALSE;
  *Reason       = NULL;

  Data = OcStorageReadFileUnicode (State->Storage, Name, &Size);
  if (Data == NULL) {
    *Reason = "cannot read the file";
    return EFI_NOT_FOUND;
  }

  Status = EditPlistLoad (&Plist, Data, Size);
  if (EFI_ERROR (Status)) {
    FreePool (Data);
    *Reason = "this is not a valid plist with a dictionary";
    return Status;
  }

  Status = EditPlistCountErrors (Data, Size, &Errors);
  FreePool (Data);
  if (EFI_ERROR (Status)) {
    EditPlistFree (&Plist);
    *Reason = "OpenCore cannot load it";
    return Status;
  }

  Node = EditPlistFindChild (EditPlistFindChild (EditPlistFindChild (Plist.Root, "Misc"), "Security"), "AllowPickerEdit");

  *AllowsEditor = (Node != NULL) && (EditPlistNodeKind (Node) == EditPlistKindBoolean) && EditPlistGetBoolean (&Plist, Node);

  EditPlistFree (&Plist);
  return EFI_SUCCESS;
}

/**
  Ask how a configuration is to be used, then restart OpenCore with it.
**/
STATIC
VOID
EdConfigSwitch (
  IN OUT ED_STATE      *State,
  IN     CONST CHAR16  *Name
  )
{
  ED_UI        *Ui;
  CHAR8        AsciiName[OC_CONFIG_MAX_NAME + 1];
  CHAR8        Line[256];
  CHAR8        Once[128];
  CONST CHAR8  *Items[2];
  BOOLEAN      Enabled[2];
  UINTN        Count;
  UINTN        Selected;
  INTN         Choice;
  BOOLEAN      AllowsEditor;
  BOOLEAN      IsDefault;
  EFI_STATUS   Status;
  CONST CHAR8  *Reason;

  Ui = &State->Ui;

  if (EdSameConfigName (Name, OcMiscGetConfigName ())) {
    EdUiNotify (Ui, "This config is in use already", FALSE);
    return;
  }

  EdUnicodeToAscii (Name, AsciiName, sizeof (AsciiName));

  //
  // Do not restart into something which cannot work.
  //
  Status = EdCheckConfig (State, Name, &AllowsEditor, &Reason);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Line, sizeof (Line), "%a: %a", AsciiName, Reason);
    EdUiNotify (Ui, Line, TRUE);
    return;
  }

  IsDefault = EdSameConfigName (Name, OPEN_CORE_CONFIG_PATH);

  EdUiClear (Ui);
  AsciiSPrint (Line, sizeof (Line), "Start OpenCore with %a", AsciiName);
  EdUiTitle (Ui, Line, "OpenCore restarts to apply a different config");

  if (IsDefault) {
    Items[0]   = "Restart with the default config (and forget the choice made before)";
    Enabled[0] = TRUE;
    Count      = 1;
  } else {
    AsciiSPrint (Once, sizeof (Once), "This time only  (the next start uses %s again)", OPEN_CORE_CONFIG_PATH);
    Items[0]   = Once;
    Items[1]   = "Keep it        (until another config is chosen)";
    Enabled[0] = TRUE;
    Enabled[1] = AllowsEditor;
    Count      = 2;

    if (!AllowsEditor) {
      EdUiPut (Ui, 0, 3, Ui->AttrError, "AllowPickerEdit is off in this config, so this editor would not be there to", 0);
      EdUiPut (Ui, 0, 4, Ui->AttrError, "switch back. It can be used for one start only.", 0);
    }
  }

  Selected = 0;
  EdUiHint (Ui, "Enter: restart now   Esc: back");

  Choice = EdUiMenu (Ui, 6, Items, Enabled, Count, &Selected);
  if (Choice < 0) {
    return;
  }

  Status = OcMiscSetConfigChoice (State->Storage, Name, !IsDefault && (Choice == 1));
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Line, sizeof (Line), "Cannot save the choice (%r), the file system may be read-only", Status);
    EdUiNotify (Ui, Line, TRUE);
    return;
  }

  EdRestart (State);

  //
  // Still here means that the system cannot restart by itself.
  //
  EdSetStatus (
    State->Status,
    sizeof (State->Status),
    &State->StatusIsError,
    FALSE,
    "The choice is saved, restart the computer to apply it"
    );
}

VOID
EdConfigChooser (
  IN OUT ED_STATE  *State
  )
{
  ED_UI               *Ui;
  OC_CONFIG_FILE      *Files;
  ED_LIST             List;
  OC_PICKER_KEY_INFO  *Key;
  OC_CONFIG_SOURCE    Source;
  CONST CHAR16        *Failed;
  CHAR8               Name[OC_CONFIG_MAX_NAME + 1];
  CHAR8               Failure[OC_CONFIG_MAX_NAME + 80];
  CHAR8               Subtitle[160];
  UINTN               Count;
  UINTN               Index;
  UINTN               Visible;
  BOOLEAN             Redraw;

  Ui = &State->Ui;

  if ((State->Storage == NULL) || State->Storage->HasVault) {
    EdUiTitle (Ui, "Config files", NULL);
    EdUiNotify (Ui, "The vault is used, OpenCore can only start with the default config", TRUE);
    return;
  }

  Files = AllocateZeroPool (OC_CONFIG_MAX_FILES * sizeof (*Files));
  if (Files == NULL) {
    EdUiNotify (Ui, "Not enough memory", TRUE);
    return;
  }

  Count = OcMiscListConfigs (State->Storage, Files, OC_CONFIG_MAX_FILES);
  if (Count == 0) {
    FreePool (Files);
    EdUiTitle (Ui, "Config files", NULL);
    EdUiNotify (Ui, "No config files were found", TRUE);
    return;
  }

  List.Count    = Count;
  List.Selected = 0;
  List.Top      = 0;
  for (Index = 0; Index < Count; ++Index) {
    if (EdSameConfigName (Files[Index].Name, OcMiscGetConfigName ())) {
      List.Selected = Index;
    }
  }

  Visible = EdUiContentRows (Ui);
  Redraw  = TRUE;

  if (List.Selected >= Visible) {
    List.Top = List.Selected - Visible + 1;
  }

  while (TRUE) {
    if (Redraw) {
      EdUiClear (Ui);
      Redraw = FALSE;
    }

    Source = OcMiscGetConfigSource (&Failed);
    EdConfigNameAscii (Name, sizeof (Name));
    AsciiSPrint (
      Subtitle,
      sizeof (Subtitle),
      "In use: %a, %a",
      Name,
      EdConfigSourceDescription (Source)
      );
    EdUiTitle (Ui, "Config files", Subtitle);
    EdListDraw (Ui, 3, Visible, &List, EdConfigItem, Files);

    if (Source == OcConfigSourceFallback) {
      EdUnicodeToAscii (Failed, Name, sizeof (Name));
      AsciiSPrint (Failure, sizeof (Failure), "The chosen %a could not be used, the default config is in use", Name);
      EdUiStatus (Ui, Ui->AttrError, Failure);
    } else if (Count == 1) {
      EdUiStatus (Ui, Ui->AttrHint, "Other configs are files named config*.plist next to this one");
    } else {
      EdUiStatus (Ui, Ui->AttrHint, "");
    }

    EdUiHint (Ui, "Enter: start OpenCore with it   Esc: back");

    Key = EdUiWaitKey (Ui, FALSE);

    switch (Key->OcKeyCode) {
      case OC_INPUT_UP:
      case OC_INPUT_DOWN:
      case OC_INPUT_TOP:
      case OC_INPUT_BOTTOM:
        EdListMove (&List, Visible, Key->OcKeyCode);
        break;
      case OC_INPUT_CONTINUE:
      case OC_INPUT_RIGHT:
        EdConfigSwitch (State, Files[List.Selected].Name);
        Redraw = TRUE;
        break;
      case OC_INPUT_ABORTED:
      case OC_INPUT_LEFT:
        FreePool (Files);
        return;
      default:
        break;
    }
  }
}
