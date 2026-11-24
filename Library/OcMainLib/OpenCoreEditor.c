/** @file
  Options editor of the OpenCore picker: shared helpers, the main menu and the entry point.

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

VOID
EdSetStatus (
  OUT CHAR8        *Status,
  IN  UINTN        Size,
  OUT BOOLEAN      *IsError,
  IN  BOOLEAN      Error,
  IN  CONST CHAR8  *Text
  )
{
  AsciiSPrint (Status, Size, "%a", Text);
  *IsError = Error;
}

VOID
EdUnicodeToAscii (
  IN  CONST CHAR16  *Text,
  OUT CHAR8         *Buffer,
  IN  UINTN         Size
  )
{
  UINTN  Index;

  for (Index = 0; (Index + 1 < Size) && (Text[Index] != L'\0'); ++Index) {
    Buffer[Index] = ((Text[Index] >= 0x20) && (Text[Index] <= 0x7E)) ? (CHAR8)Text[Index] : '?';
  }

  Buffer[Index] = '\0';
}

/**
  Get the name of the configuration which is in use, for the screen.
**/
VOID
EdConfigNameAscii (
  OUT CHAR8  *Buffer,
  IN  UINTN  Size
  )
{
  EdUnicodeToAscii (OcMiscGetConfigName (), Buffer, Size);
}

BOOLEAN
EdSameConfigName (
  IN  CONST CHAR16  *First,
  IN  CONST CHAR16  *Second
  )
{
  while (CharToUpper (*First) == CharToUpper (*Second)) {
    if (*First == L'\0') {
      return TRUE;
    }

    ++First;
    ++Second;
  }

  return FALSE;
}

BOOLEAN
EdIsConfigEditable (
  IN  CONST ED_STATE  *State,
  OUT CONST CHAR8     **Reason  OPTIONAL
  )
{
  if (Reason != NULL) {
    *Reason = NULL;
  }

  if ((State->Storage == NULL) || (State->Storage->FileSystem == NULL) || (State->Storage->StorageRoot == NULL)) {
    if (Reason != NULL) {
      *Reason = "The configuration file system is not available";
    }

    return FALSE;
  }

  if (State->Storage->HasVault) {
    //
    // Any change would not match the vault, and OpenCore refuses to start then.
    //
    if (Reason != NULL) {
      *Reason = "The vault is used, config.plist is protected by it";
    }

    return FALSE;
  }

  return TRUE;
}

VOID
EdReadBootArgs (
  OUT ED_BOOT_ARGS  *Args
  )
{
  EFI_STATUS  Status;
  UINT8       *Data;
  UINTN       Size;
  UINT32      Attributes;
  UINTN       Index;

  ZeroMem (Args, sizeof (*Args));
  Args->Editable = TRUE;

  Data   = NULL;
  Status = GetVariable3 (L"boot-args", &gAppleBootVariableGuid, (VOID **)&Data, &Size, &Attributes);
  if (EFI_ERROR (Status)) {
    return;
  }

  Args->Exists  = TRUE;
  Args->InFlash = (Attributes & EFI_VARIABLE_NON_VOLATILE) != 0;

  //
  // OpenCore writes the arguments with a terminator, macOS usually does not.
  //
  while ((Size > 0) && (Data[Size - 1] == '\0')) {
    --Size;
  }

  if (Size > ED_BOOT_ARGS_LENGTH) {
    Args->Editable = FALSE;
    Size           = ED_BOOT_ARGS_LENGTH;
  }

  for (Index = 0; Index < Size; ++Index) {
    if ((Data[Index] >= 0x20) && (Data[Index] <= 0x7E)) {
      Args->Text[Index] = (CHAR8)Data[Index];
    } else {
      Args->Text[Index] = '?';
      Args->Editable    = FALSE;
    }
  }

  FreePool (Data);
}

/**
  Check that a file has the expected contents.
**/
STATIC
BOOLEAN
EdVerifyFile (
  IN  EFI_FILE_PROTOCOL  *Root,
  IN  CONST CHAR16       *Path,
  IN  CONST VOID         *Data,
  IN  UINT32             Size
  )
{
  VOID     *Read;
  UINT32   ReadSize;
  BOOLEAN  Same;

  Read = OcReadFileFromDirectory (Root, Path, &ReadSize, 0);
  if (Read == NULL) {
    return FALSE;
  }

  Same = (ReadSize == Size) && (CompareMem (Read, Data, Size) == 0);
  FreePool (Read);

  return Same;
}

/**
  Replace config.plist, with the previous file saved next to it.

  @param[in]  Storage  OpenCore storage.
  @param[in]  Old      Contents of the file being replaced.
  @param[in]  OldSize  Size of Old.
  @param[in]  New      New contents.
  @param[in]  NewSize  Size of New.

  @retval EFI_SUCCESS  The file has been replaced and read back.
  @retval other        The file may still be as it was, or has been put back.
**/
EFI_STATUS
EdWriteConfig (
  IN  OC_STORAGE_CONTEXT  *Storage,
  IN  CONST VOID          *Old,
  IN  UINT32              OldSize,
  IN  CONST VOID          *New,
  IN  UINT32              NewSize
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  CHAR16             ConfigPath[OC_STORAGE_SAFE_PATH_MAX + OC_CONFIG_MAX_NAME + 16];
  CHAR16             BackupPath[OC_STORAGE_SAFE_PATH_MAX + OC_CONFIG_MAX_NAME + 16];

  if (StrLen (Storage->StorageRoot) > OC_STORAGE_SAFE_PATH_MAX) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // The configuration in use is the one that is changed.
  //
  UnicodeSPrint (ConfigPath, sizeof (ConfigPath), L"%s\\%s", Storage->StorageRoot, OcMiscGetConfigName ());
  UnicodeSPrint (BackupPath, sizeof (BackupPath), L"%s\\%s.bak", Storage->StorageRoot, OcMiscGetConfigName ());

  //
  // The storage itself is opened for reading only.
  //
  Status = Storage->FileSystem->OpenVolume (Storage->FileSystem, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // First the backup, the configuration is not touched if it cannot be made.
  //
  OcDeleteFile (Root, BackupPath);
  Status = OcSetFileData (Root, BackupPath, Old, OldSize);
  if (!EFI_ERROR (Status) && !EdVerifyFile (Root, BackupPath, Old, OldSize)) {
    Status = EFI_VOLUME_CORRUPTED;
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "OCE: Failed to write backup - %r\n", Status));
    OcDeleteFile (Root, BackupPath);
    Root->Close (Root);
    return Status;
  }

  OcDeleteFile (Root, ConfigPath);
  Status = OcSetFileData (Root, ConfigPath, New, NewSize);
  if (!EFI_ERROR (Status) && !EdVerifyFile (Root, ConfigPath, New, NewSize)) {
    Status = EFI_VOLUME_CORRUPTED;
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "OCE: Failed to write configuration - %r, putting the previous one back\n", Status));
    OcDeleteFile (Root, ConfigPath);
    OcSetFileData (Root, ConfigPath, Old, OldSize);
  }

  Root->Close (Root);
  return Status;
}

/**
  Check changes to the configuration, and write them.

  @param[in]  State     Editor state.
  @param[in]  Plist     Edited configuration.
  @param[in]  Baseline  Number of problems which the configuration had before editing.
  @param[out] Reason    Text to show on failure.
**/
EFI_STATUS
EdCommitConfig (
  IN  ED_STATE     *State,
  IN  EDIT_PLIST   *Plist,
  IN  UINT32       Baseline,
  OUT CONST CHAR8  **Reason
  )
{
  EFI_STATUS  Status;
  CHAR8       *Text;
  UINT32      Size;
  UINT32      Errors;

  *Reason = NULL;

  Status = EditPlistRender (Plist, &Text, &Size);
  if (EFI_ERROR (Status)) {
    *Reason = "Not enough memory to prepare config.plist";
    return Status;
  }

  //
  // The result has to load, and not to be any worse than what it was.
  // OpenCore does not start with a configuration it cannot load.
  //
  Status = EditPlistCountErrors (Text, Size, &Errors);
  if (EFI_ERROR (Status) || (Errors > Baseline)) {
    DEBUG ((DEBUG_WARN, "OCE: Rejecting configuration with %u problems vs %u - %r\n", Errors, Baseline, Status));
    FreePool (Text);
    *Reason = "The changed config.plist would not load properly, nothing was written";
    return EFI_COMPROMISED_DATA;
  }

  Status = EdWriteConfig (State->Storage, Plist->Source, Plist->SourceSize, Text, Size);
  FreePool (Text);

  if (EFI_ERROR (Status)) {
    *Reason = "Cannot write config.plist (the file system may be read-only)";
  }

  return Status;
}

/**
  Load config.plist for editing.

  @param[in]   State     Editor state.
  @param[out]  Plist     Loaded document.
  @param[out]  Baseline  Number of problems which the configuration has.
  @param[out]  Reason    Text to show on failure.
**/
EFI_STATUS
EdLoadConfig (
  IN  ED_STATE     *State,
  OUT EDIT_PLIST   *Plist,
  OUT UINT32       *Baseline,
  OUT CONST CHAR8  **Reason
  )
{
  EFI_STATUS  Status;
  VOID        *Data;
  UINT32      Size;

  if (!EdIsConfigEditable (State, Reason)) {
    return EFI_ACCESS_DENIED;
  }

  *Reason = NULL;

  Data = OcStorageReadFileUnicode (State->Storage, OcMiscGetConfigName (), &Size);
  if (Data == NULL) {
    *Reason = "Cannot read config.plist";
    return EFI_NOT_FOUND;
  }

  Status = EditPlistLoad (Plist, Data, Size);
  if (EFI_ERROR (Status)) {
    FreePool (Data);
    *Reason = "Cannot parse config.plist";
    return Status;
  }

  Status = EditPlistCountErrors (Data, Size, Baseline);
  FreePool (Data);
  if (EFI_ERROR (Status)) {
    EditPlistFree (Plist);
    *Reason = "OpenCore cannot load this config.plist";
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  Find a dictionary entry ignoring the case of the key.
**/
XML_NODE *
EdFindChildNoCase (
  IN  XML_NODE     *Dict,
  IN  CONST CHAR8  *Key
  )
{
  UINT32  Index;

  for (Index = 0; Index < EditPlistChildCount (Dict); ++Index) {
    if (AsciiStriCmp (EditPlistChildKey (Dict, Index), Key) == 0) {
      return EditPlistChildValue (Dict, Index);
    }
  }

  return NULL;
}

VOID
EdRestart (
  IN OUT ED_STATE  *State
  )
{
  EdUiPut (&State->Ui, 0, State->Ui.Rows - 3, State->Ui.AttrNormal, "Restarting...", 0);
  ResetWarm ();
}

/**
  Ask what to do with changes which have not been saved.

  @retval 0   Save and restart.
  @retval 1   Save.
  @retval 2   Discard the changes.
  @retval -1  Keep editing.
**/
INTN
EdAskUnsaved (
  IN OUT ED_UI  *Ui
  )
{
  CONST CHAR8  *Items[3];
  UINTN        Selected;

  EdUiPut (Ui, 0, Ui->Rows - 3, Ui->AttrChanged, "There are unsaved changes:", 0);
  EdUiHint (Ui, "Enter: choose   Esc: keep editing");
  Items[0] = "Save and restart";
  Items[1] = "Save";
  Items[2] = "Discard the changes";
  Selected = 0;

  return EdUiMenu (Ui, 4, Items, NULL, ARRAY_SIZE (Items), &Selected);
}

//
// Main menu.
//

STATIC
VOID
EdMainMenu (
  IN OUT ED_STATE  *State
  )
{
  ED_UI         *Ui;
  ED_BOOT_ARGS  Args;
  CHAR8         Subtitle[160];
  CHAR8         Name[128];
  CHAR8         Texts[14][200];
  CONST CHAR8   *Items[14];
  BOOLEAN       Enabled[14];
  UINTN         Count;
  UINTN         Selected;
  INTN          Choice;
  CONST CHAR8   *Reason;
  BOOLEAN       Replaced;

  Ui       = &State->Ui;
  Selected = 0;

  while (TRUE) {
    EdReadBootArgs (&Args);
    Replaced = OcIsBootArgsReplaced ();

    EdUiClear (Ui);

    Subtitle[0] = '\0';
    if (State->Entry != NULL) {
      EdUnicodeToAscii (State->Entry->Name, Name, sizeof (Name));
      AsciiSPrint (Subtitle, sizeof (Subtitle), "Highlighted entry: %a", Name);
    }

    EdUiTitle (Ui, "DrogaBoxBoot Options", Subtitle);

    Count = 0;

    AsciiSPrint (Texts[Count], sizeof (Texts[Count]), "Boot arguments:  %a", Args.Exists ? Args.Text : "(not set)");
    Items[Count]   = Texts[Count];
    Enabled[Count] = TRUE;
    ++Count;

    Items[Count]   = "Boot flags       (-v, -s, -x, ...)";
    Enabled[Count] = TRUE;
    ++Count;

    EdConfigNameAscii (Name, sizeof (Name));
    if (EdIsConfigEditable (State, &Reason)) {
      AsciiSPrint (Texts[Count], sizeof (Texts[Count]), "Edit %a", Name);
    } else {
      AsciiSPrint (Texts[Count], sizeof (Texts[Count]), "Edit %a  (%a)", Name, Reason);
    }

    Items[Count]   = Texts[Count];
    Enabled[Count] = TRUE;
    ++Count;

    if (EdIsConfigEditable (State, &Reason)) {
      AsciiSPrint (Texts[Count], sizeof (Texts[Count]), "Kexts            (on/off and load order)");
    } else {
      AsciiSPrint (Texts[Count], sizeof (Texts[Count]), "Kexts  (%a)", Reason);
    }

    Items[Count]   = Texts[Count];
    Enabled[Count] = TRUE;
    ++Count;

    Items[Count]   = "Drivers, ACPI, Tools  (on/off and order)";
    Enabled[Count] = TRUE;
    ++Count;

    Items[Count]   = "Quirks            (usual switches)";
    Enabled[Count] = TRUE;
    ++Count;

    Items[Count]   = "Check the config  (problems found)";
    Enabled[Count] = TRUE;
    ++Count;

    Items[Count]   = "Log               (what OpenCore did)";
    Enabled[Count] = TRUE;
    ++Count;

    Items[Count]   = "Kernel panic log  (left by macOS in NVRAM)";
    Enabled[Count] = TRUE;
    ++Count;

    Items[Count]   = "Compare configs   (what differs)";
    Enabled[Count] = TRUE;
    ++Count;

    Items[Count]   = "Undo the last save (put the .bak back)";
    Enabled[Count] = TRUE;
    ++Count;

    AsciiSPrint (
      Texts[Count],
      sizeof (Texts[Count]),
      "Config file:  %a%a",
      Name,
      EdConfigSourceNote (OcMiscGetConfigSource (NULL))
      );
    Items[Count]   = Texts[Count];
    Enabled[Count] = TRUE;
    ++Count;

    Items[Count]   = "Restart";
    Enabled[Count] = TRUE;
    ++Count;

    if (Replaced) {
      Items[Count]   = "Put the original boot arguments back now";
      Enabled[Count] = TRUE;
      ++Count;
    }

    if (State->Status[0] != '\0') {
      EdUiStatus (Ui, State->StatusIsError ? Ui->AttrError : Ui->AttrChanged, State->Status);
      State->Status[0] = '\0';
    }

    EdUiHint (Ui, "Up/Down, Enter: choose   Esc: back to the boot menu");

    Choice = EdUiMenu (Ui, 4, Items, Enabled, Count, &Selected);

    switch (Choice) {
      case 0:
        EdBootArgsScreen (State);
        break;
      case 1:
        EdBootFlagsScreen (State);
        break;
      case 2:
        if (EdIsConfigEditable (State, &Reason)) {
          EdConfigEditor (State);
        } else {
          EdUiNotify (Ui, Reason, TRUE);
        }

        break;
      case 3:
        if (EdIsConfigEditable (State, &Reason)) {
          EdKextsEditor (State, &mKindKexts);
        } else {
          EdUiNotify (Ui, Reason, TRUE);
        }

        break;
      case 4:
      case 5:
        if (EdIsConfigEditable (State, &Reason)) {
          if (Choice == 4) {
            EdListsMenu (State);
          } else {
            EdSwitchesEditor (State);
          }
        } else {
          EdUiNotify (Ui, Reason, TRUE);
        }

        break;
      case 6:
        if (EdIsConfigEditable (State, &Reason)) {
          EdCheckConfigScreen (State);
        } else {
          EdUiNotify (Ui, Reason, TRUE);
        }

        break;
      case 7:
        EdLogViewer (State);
        break;
      case 8:
        EdPanicViewer (State);
        break;
      case 9:
      case 10:
        if (EdIsConfigEditable (State, &Reason)) {
          if (Choice == 9) {
            EdCompareConfigs (State);
          } else {
            EdRestoreBackup (State);
          }
        } else {
          EdUiNotify (Ui, Reason, TRUE);
        }

        break;
      case 11:
        EdConfigChooser (State);
        break;
      case 12:
        if (EdUiConfirm (Ui, "Restart the computer?")) {
          EdRestart (State);
        }

        break;
      case 13:
        OcRestoreBootArgs ();
        EdSetStatus (State->Status, sizeof (State->Status), &State->StatusIsError, FALSE, "The original boot arguments are back");
        break;
      default:
        return;
    }
  }
}

EFI_STATUS
EFIAPI
OcMiscShowEditor (
  IN OUT OC_PICKER_CONTEXT  *Context,
  IN     OC_BOOT_ENTRY      *BootEntry OPTIONAL
  )
{
  EFI_STATUS  Status;
  ED_STATE    *State;

  //
  // Changing boot arguments and configuration is as sensitive as doing it with
  // hotkeys, so it needs the password if one is set.
  //
  Status = OcRequestPrivilege (Context, OcPrivilegeAuthorized);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OCE: Privilege request - %r\n", Status));
    return Status;
  }

  State = AllocateZeroPool (sizeof (*State));
  if (State == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  State->Picker  = Context;
  State->Storage = Context->StorageContext;
  State->Entry   = BootEntry;

  Status = EdUiInit (&State->Ui, Context);
  if (EFI_ERROR (Status)) {
    FreePool (State);
    return Status;
  }

  EdMainMenu (State);

  EdUiDone (&State->Ui);
  FreePool (State);

  return EFI_SUCCESS;
}
