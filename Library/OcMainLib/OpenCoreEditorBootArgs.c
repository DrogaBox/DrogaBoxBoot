/** @file
  Options editor: boot-args and boot flags.

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
  Store boot arguments in config.plist as the value of the NVRAM boot-args variable.
**/
STATIC
EFI_STATUS
EdSaveBootArgsToConfig (
  IN  ED_STATE     *State,
  IN  CONST CHAR8  *Args,
  OUT CONST CHAR8  **Reason
  )
{
  EFI_STATUS  Status;
  EDIT_PLIST  Plist;
  UINT32      Baseline;
  XML_NODE    *Add;
  XML_NODE    *Guid;
  XML_NODE    *Value;

  Status = EdLoadConfig (State, &Plist, &Baseline, Reason);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Add = EditPlistFindChild (EditPlistFindChild (Plist.Root, "NVRAM"), "Add");
  if (EditPlistNodeKind (Add) != EditPlistKindDict) {
    EditPlistFree (&Plist);
    *Reason = "NVRAM > Add is not found in config.plist";
    return EFI_NOT_FOUND;
  }

  Guid = EdFindChildNoCase (Add, ED_APPLE_BOOT_GUID_TEXT);
  if (Guid == NULL) {
    Status = EditPlistAddDictWithString (&Plist, Add, ED_APPLE_BOOT_GUID_TEXT, ED_BOOT_ARGS_KEY, Args);
  } else if (EditPlistNodeKind (Guid) != EditPlistKindDict) {
    Status = EFI_UNSUPPORTED;
  } else {
    Value = EditPlistFindChild (Guid, ED_BOOT_ARGS_KEY);
    if (Value != NULL) {
      Status = EditPlistSetString (&Plist, Value, Args);
    } else {
      Status = EditPlistAddString (&Plist, Guid, ED_BOOT_ARGS_KEY, Args);
    }
  }

  if (EFI_ERROR (Status)) {
    EditPlistFree (&Plist);
    *Reason = Status == EFI_INVALID_PARAMETER
              ? "The arguments contain characters which cannot be saved to config.plist"
              : "Cannot change NVRAM > Add > boot-args in config.plist";
    return Status;
  }

  if (EditPlistPatchCount (&Plist) == 0) {
    //
    // Already what is wanted.
    //
    EditPlistFree (&Plist);
    return EFI_SUCCESS;
  }

  Status = EdCommitConfig (State, &Plist, Baseline, Reason);
  EditPlistFree (&Plist);

  return Status;
}

STATIC
BOOLEAN
EdBootArgsFilter (
  IN CHAR8  Character
  )
{
  return Character != '<' && Character != '>';
}

/**
  Let the user choose how boot arguments are applied, and apply them.

  @param[in,out]  State  Editor state.
  @param[in]      Args   Boot arguments to apply.

  @retval TRUE   The arguments are applied, the editor of arguments is done.
  @retval FALSE  Esc was pressed, the arguments are still to be edited.
**/
STATIC
BOOLEAN
EdBootArgsApply (
  IN OUT ED_STATE     *State,
  IN     CONST CHAR8  *Args
  )
{
  ED_UI        *Ui;
  CHAR8        Line[ED_BOOT_ARGS_LENGTH + 80];
  CHAR8        Subtitle[160];
  CHAR8        Name[128];
  CONST CHAR8  *Items[3];
  BOOLEAN      Enabled[3];
  UINTN        Selected;
  UINTN        Row;
  UINTN        Offset;
  UINTN        Width;
  UINTN        Length;
  INTN         Choice;
  EFI_STATUS   Status;
  CONST CHAR8  *Reason;

  Ui = &State->Ui;

  Items[0]   = "Boot once      (NVRAM is put back the next time OpenCore starts)";
  Items[1]   = "Keep in NVRAM  (stays until changed, like the nvram tool)";
  Items[2]   = "Keep in NVRAM and save to config.plist (NVRAM > Add)";
  Enabled[0] = TRUE;
  Enabled[1] = TRUE;
  Enabled[2] = EdIsConfigEditable (State, NULL);
  Selected   = 0;

  //
  // This is a screen of its own, which the caller has to draw again when it gets back.
  //
  EdUiClear (Ui);

  Subtitle[0] = '\0';
  if (State->Entry != NULL) {
    EdUnicodeToAscii (State->Entry->Name, Name, sizeof (Name));
    AsciiSPrint (Subtitle, sizeof (Subtitle), "Highlighted entry: %a", Name);
  }

  EdUiTitle (Ui, "Apply boot arguments", Subtitle);
  EdUiPut (Ui, 0, 3, Ui->AttrNormal, "Boot arguments to apply:", 0);

  //
  // The arguments can be longer than a row.
  //
  Width  = Ui->Columns - 1;
  Length = AsciiStrLen (Args);
  if (Length == 0) {
    EdUiPut (Ui, 0, 4, Ui->AttrChanged, "(none, boot-args are removed)", 0);
  } else {
    for (Row = 0, Offset = 0; (Row < 4) && (Offset < Length); ++Row, Offset += Width) {
      AsciiStrnCpyS (Line, sizeof (Line), &Args[Offset], MIN (Width, Length - Offset));
      EdUiPut (Ui, 0, 4 + Row, Ui->AttrChanged, Line, 0);
    }
  }

  while (TRUE) {
    EdUiHint (Ui, "Enter: apply   Esc: back");

    Choice = EdUiMenu (Ui, 9, Items, Enabled, ARRAY_SIZE (Items), &Selected);
    if (Choice < 0) {
      return FALSE;
    }

    Reason = NULL;
    Status = EFI_SUCCESS;

    if (Choice == 2) {
      //
      // Configuration first as it is more likely to fail, and nothing has been changed yet.
      //
      Status = EdSaveBootArgsToConfig (State, Args, &Reason);
    }

    if (!EFI_ERROR (Status)) {
      Status = OcSetBootArgs (Args, Choice != 0);
      if (EFI_ERROR (Status)) {
        Reason = "Cannot change the NVRAM boot-args variable";
      }
    }

    if (EFI_ERROR (Status)) {
      AsciiSPrint (Line, sizeof (Line), "%a (%r)", Reason != NULL ? Reason : "Failed", Status);
      EdUiNotify (Ui, Line, TRUE);
      continue;
    }

    EdSetStatus (
      State->Status,
      sizeof (State->Status),
      &State->StatusIsError,
      FALSE,
      Choice == 0
      ? "Boot arguments are set for this boot only"
      : (Choice == 1 ? "Boot arguments are kept in NVRAM" : "Boot arguments are kept in NVRAM and saved to config.plist")
      );
    return TRUE;
  }
}

VOID
EdBootArgsScreen (
  IN OUT ED_STATE  *State
  )
{
  ED_UI         *Ui;
  ED_BOOT_ARGS  Current;
  CHAR8         Buffer[ED_BOOT_ARGS_LENGTH + 1];
  CHAR8         Line[ED_BOOT_ARGS_LENGTH + 80];
  UINTN         FieldRows;

  Ui = &State->Ui;

  EdReadBootArgs (&Current);
  if (!Current.Editable) {
    EdUiTitle (Ui, "Boot arguments", NULL);
    EdUiNotify (Ui, "The current boot-args hold bytes which cannot be edited here", TRUE);
    return;
  }

  AsciiStrCpyS (Buffer, sizeof (Buffer), Current.Text);

  while (TRUE) {
    EdUiClear (Ui);
    EdUiTitle (Ui, "Boot arguments", "Used by macOS entries");

    AsciiSPrint (
      Line,
      sizeof (Line),
      "NVRAM now: %a%a",
      Current.Exists ? Current.Text : "(not set)",
      Current.Exists ? (Current.InFlash ? "" : "   [this boot only]") : ""
      );
    EdUiPut (Ui, 0, 3, Ui->AttrNormal, Line, 0);

    if (State->Picker->AppleBootArgs[0] != '\0') {
      AsciiSPrint (Line, sizeof (Line), "Added by hotkeys (kept as they are): %a", State->Picker->AppleBootArgs);
      EdUiPut (Ui, 0, 4, Ui->AttrNormal, Line, 0);
    }

    if ((State->Entry != NULL) && ((State->Entry->Type & OC_BOOT_APPLE_ANY) == 0)) {
      EdUiPut (Ui, 0, 5, Ui->AttrChanged, "The highlighted entry is not macOS, boot-args matter only for macOS", 0);
    }

    EdUiPut (Ui, 0, 7, Ui->AttrNormal, "New boot arguments:", 0);
    EdUiHint (Ui, "Enter: continue   Esc: clear, then cancel   Arrows, Home, End, Del: edit");

    FieldRows = Ui->Rows - 8 - 4;
    if (FieldRows > 8) {
      FieldRows = 8;
    }

    if (EdUiEditLine (Ui, 8, FieldRows, Buffer, sizeof (Buffer), EdBootArgsFilter) == EdLineCancelled) {
      return;
    }

    if (EdBootArgsApply (State, Buffer)) {
      return;
    }
  }
}

//
// Boot flags: the usual boot arguments which can be switched on and off.
//

typedef struct {
  CONST CHAR8    *Flag;
  CONST CHAR8    *Description;
} ED_BOOT_FLAG;

STATIC CONST ED_BOOT_FLAG  mBootFlags[] = {
  { "-v",                       "Verbose boot"                          },
  { "-s",                       "Single user mode"                      },
  { "-x",                       "Safe mode"                             },
  { "-f",                       "Ignore the kernel cache"               },
  { "-no_compat_check",         "Skip the macOS compatibility check"    },
  { "keepsyms=1",               "Keep symbols for panic reports"        },
  { "debug=0x100",              "Do not restart after a kernel panic"   },
  { "cpus=1",                   "Use one processor core only"           },
  { "nv_disable=1",             "Disable NVIDIA graphics acceleration"  },
  { "amfi_get_out_of_my_way=1", "Disable AMFI (needed by root patches)" }
};

typedef enum {
  EdFlagOff,
  EdFlagOn,
  EdFlagOther
} ED_FLAG_STATE;

/**
  Check if a token of boot arguments is about the same setting as a flag:
  the very same token for switches like -v, or any value for flags like debug=0x100.
**/
STATIC
BOOLEAN
EdTokenIsSetting (
  IN  CONST CHAR8  *Token,
  IN  UINTN        Length,
  IN  CONST CHAR8  *Flag
  )
{
  CONST CHAR8  *Equals;
  UINTN        KeyLength;

  Equals = AsciiStrStr (Flag, "=");
  if (Equals == NULL) {
    return Length == AsciiStrLen (Flag) && AsciiStrnCmp (Token, Flag, Length) == 0;
  }

  KeyLength = (UINTN)(Equals - Flag) + 1;
  return Length >= KeyLength && AsciiStrnCmp (Token, Flag, KeyLength) == 0;
}

STATIC
ED_FLAG_STATE
EdGetFlagState (
  IN  CONST CHAR8  *Args,
  IN  CONST CHAR8  *Flag
  )
{
  ED_FLAG_STATE  State;
  UINTN          Index;
  UINTN          Length;

  State = EdFlagOff;
  Index = 0;
  while (Args[Index] != '\0') {
    if (Args[Index] == ' ') {
      ++Index;
      continue;
    }

    for (Length = 0; (Args[Index + Length] != '\0') && (Args[Index + Length] != ' '); ++Length) {
    }

    if (EdTokenIsSetting (&Args[Index], Length, Flag)) {
      if ((Length == AsciiStrLen (Flag)) && (AsciiStrnCmp (&Args[Index], Flag, Length) == 0)) {
        return EdFlagOn;
      }

      State = EdFlagOther;
    }

    Index += Length;
  }

  return State;
}

/**
  Remove all tokens which are about the setting of the flag.
**/
STATIC
VOID
EdRemoveFlag (
  IN OUT CHAR8        *Args,
  IN     CONST CHAR8  *Flag
  )
{
  UINTN  Index;
  UINTN  Length;
  UINTN  End;
  UINTN  Total;

  Index = 0;
  while (Args[Index] != '\0') {
    if (Args[Index] == ' ') {
      ++Index;
      continue;
    }

    for (Length = 0; (Args[Index + Length] != '\0') && (Args[Index + Length] != ' '); ++Length) {
    }

    if (!EdTokenIsSetting (&Args[Index], Length, Flag)) {
      Index += Length;
      continue;
    }

    //
    // Remove the token with the spaces after it.
    //
    End = Index + Length;
    while (Args[End] == ' ') {
      ++End;
    }

    Total = AsciiStrLen (Args);
    CopyMem (&Args[Index], &Args[End], Total - End + 1);
  }

  //
  // No dangling space is left if the token was the last one.
  //
  Total = AsciiStrLen (Args);
  while ((Total > 0) && (Args[Total - 1] == ' ')) {
    Args[--Total] = '\0';
  }
}

/**
  Switch a flag on or off.

  @retval FALSE  The flag does not fit and arguments are left as they were.
**/
STATIC
BOOLEAN
EdToggleFlag (
  IN OUT CHAR8        *Args,
  IN     UINTN        Size,
  IN     CONST CHAR8  *Flag
  )
{
  CHAR8  Saved[ED_BOOT_ARGS_LENGTH + 1];
  UINTN  Length;

  AsciiStrCpyS (Saved, sizeof (Saved), Args);

  if (EdGetFlagState (Args, Flag) == EdFlagOn) {
    EdRemoveFlag (Args, Flag);
    return TRUE;
  }

  EdRemoveFlag (Args, Flag);

  Length = AsciiStrLen (Args);
  if (Length + ((Length > 0) ? 1 : 0) + AsciiStrLen (Flag) >= Size) {
    AsciiStrCpyS (Args, Size, Saved);
    return FALSE;
  }

  if (Length > 0) {
    Args[Length++] = ' ';
  }

  AsciiStrCpyS (&Args[Length], Size - Length, Flag);
  return TRUE;
}

typedef struct {
  CHAR8    Original[ED_BOOT_ARGS_LENGTH + 1];
  CHAR8    Args[ED_BOOT_ARGS_LENGTH + 1];
} ED_FLAGS;

STATIC
VOID
EdFlagItem (
  IN  VOID     *Context,
  IN  UINTN    Index,
  OUT CHAR8    *Text,
  IN  UINTN    Size,
  OUT BOOLEAN  *Changed,
  OUT BOOLEAN  *Dim,
  OUT BOOLEAN  *Error
  )
{
  ED_FLAGS       *Flags;
  ED_FLAG_STATE  State;
  CHAR8          Flag[32];
  UINTN          Length;

  Flags = Context;
  State = EdGetFlagState (Flags->Args, mBootFlags[Index].Flag);

  *Changed = State != EdGetFlagState (Flags->Original, mBootFlags[Index].Flag);
  *Dim     = FALSE;
  *Error   = FALSE;

  //
  // [~] is for a flag which is set with another value than the one it is switched on with.
  //
  AsciiSPrint (Flag, sizeof (Flag), "%a", mBootFlags[Index].Flag);
  for (Length = AsciiStrLen (Flag); Length < 26; ++Length) {
    Flag[Length] = ' ';
  }

  Flag[26] = '\0';

  AsciiSPrint (
    Text,
    Size,
    "%a [%a] %a %a",
    *Changed ? "*" : " ",
    State == EdFlagOn ? "x" : (State == EdFlagOther ? "~" : " "),
    Flag,
    mBootFlags[Index].Description
    );
}

VOID
EdBootFlagsScreen (
  IN OUT ED_STATE  *State
  )
{
  ED_UI               *Ui;
  ED_BOOT_ARGS        Current;
  ED_FLAGS            *Flags;
  ED_LIST             List;
  OC_PICKER_KEY_INFO  *Key;
  UINTN               Visible;
  CHAR8               Line[ED_BOOT_ARGS_LENGTH + 80];
  BOOLEAN             Redraw;

  Ui = &State->Ui;

  EdReadBootArgs (&Current);
  if (!Current.Editable) {
    EdUiTitle (Ui, "Boot flags", NULL);
    EdUiNotify (Ui, "The current boot-args hold bytes which cannot be edited here", TRUE);
    return;
  }

  Flags = AllocateZeroPool (sizeof (*Flags));
  if (Flags == NULL) {
    EdUiNotify (Ui, "Not enough memory", TRUE);
    return;
  }

  AsciiStrCpyS (Flags->Original, sizeof (Flags->Original), Current.Text);
  AsciiStrCpyS (Flags->Args, sizeof (Flags->Args), Current.Text);

  List.Count    = ARRAY_SIZE (mBootFlags);
  List.Selected = 0;
  List.Top      = 0;
  Visible       = EdUiContentRows (Ui);
  Redraw        = TRUE;

  while (TRUE) {
    if (Redraw) {
      EdUiClear (Ui);
      Redraw = FALSE;
    }

    EdUiTitle (Ui, "Boot flags", "Used by macOS entries");
    EdListDraw (Ui, 3, Visible, &List, EdFlagItem, Flags);

    AsciiSPrint (Line, sizeof (Line), "Boot arguments: %a", Flags->Args[0] != '\0' ? Flags->Args : "(none)");
    EdUiStatus (Ui, Ui->AttrChanged, Line);
    EdUiHint (Ui, "Space: on/off   Enter: apply   Esc: back");

    Key = EdUiWaitKey (Ui, FALSE);

    switch (Key->OcKeyCode) {
      case OC_INPUT_UP:
      case OC_INPUT_DOWN:
      case OC_INPUT_TOP:
      case OC_INPUT_BOTTOM:
        EdListMove (&List, Visible, Key->OcKeyCode);
        break;
      case OC_INPUT_MORE:
      case OC_INPUT_RIGHT:
        if (!EdToggleFlag (Flags->Args, sizeof (Flags->Args), mBootFlags[List.Selected].Flag)) {
          EdUiNotify (Ui, "The boot arguments would become too long", TRUE);
        }

        break;
      case OC_INPUT_CONTINUE:
        Redraw = TRUE;
        if (EdBootArgsApply (State, Flags->Args)) {
          FreePool (Flags);
          return;
        }

        break;
      case OC_INPUT_ABORTED:
      case OC_INPUT_LEFT:
        FreePool (Flags);
        return;
      default:
        break;
    }
  }
}
