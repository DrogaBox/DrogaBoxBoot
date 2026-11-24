/** @file
  Types and functions shared by the screens of the options editor.

  Copyright (C) 2026, OpenCore contributors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-3-Clause
**/

#ifndef OPEN_CORE_EDITOR_PRIVATE_H
#define OPEN_CORE_EDITOR_PRIVATE_H

#include <Library/OcMainLib.h>

#include "OpenCoreEditorInternal.h"
#include "OpenCoreEditorUi.h"

/**
  Longest boot-args which can be edited.
**/
#define ED_BOOT_ARGS_LENGTH  512U

/**
  GUID under which macOS keeps boot-args, as it is written in the configuration.
**/
#define ED_APPLE_BOOT_GUID_TEXT  "7C436110-AB2A-4BBB-A880-FE41995C9F82"

#define ED_BOOT_ARGS_KEY  "boot-args"

/**
  Deepest nesting of the configuration which can be browsed.
**/
#define ED_MAX_DEPTH  ((UINTN)XML_PARSER_NEST_LEVEL + 2U)

/**
  Widest data which can be edited as hexadecimal text.
**/
#define ED_MAX_DATA_SIZE  256U

/**
  State of the editor.
**/
typedef struct {
  ED_UI                 Ui;
  OC_PICKER_CONTEXT     *Picker;
  OC_STORAGE_CONTEXT    *Storage;
  OC_BOOT_ENTRY         *Entry;
  //
  // Message to show the next time the main menu is drawn.
  //
  CHAR8                 Status[128];
  BOOLEAN               StatusIsError;
} ED_STATE;

/**
  Current boot-args variable.
**/
typedef struct {
  BOOLEAN    Exists;
  //
  // Kept in flash and not going away with the next reset.
  //
  BOOLEAN    InFlash;
  //
  // Nothing but printable ASCII, so it is safe to edit.
  //
  BOOLEAN    Editable;
  CHAR8      Text[ED_BOOT_ARGS_LENGTH + 1];
} ED_BOOT_ARGS;

/**
  A level of the configuration browser.
**/
typedef struct {
  XML_NODE    *Node;
  CHAR8       Name[64];
  ED_LIST     List;
} ED_LEVEL;

typedef struct {
  ED_STATE      *State;
  EDIT_PLIST    Plist;
  UINT32        Baseline;
  ED_LEVEL      Levels[ED_MAX_DEPTH];
  UINTN         Depth;
  CHAR8         Status[128];
  BOOLEAN       StatusIsError;
} ED_BROWSER;
/**
  Largest Info.plist of a kext that is looked into.
**/
#define ED_KEXT_MAX_INFO_SIZE  (1024U * 1024U)
/**
  Most kexts the screen works with. It looks at how the kexts fit together on every change.
**/
#define ED_KEXT_MAX_KEXTS  1024U
/**
  An array of dictionaries with Enabled, which the list screen works with.
**/
typedef struct {
  CONST CHAR8     *Title;
  CONST CHAR8     *Section;
  CONST CHAR8     *Key;
  //
  // Entries which name an item, the first that is not empty is used.
  //
  CONST CHAR8     *Names[4];
  //
  // Directory of the OpenCore directory where the files are, NULL if there are none to look for.
  //
  CONST CHAR16    *Directory;
  //
  // Kexts: they need each other and have to be in order.
  //
  BOOLEAN         Dependencies;
  CONST CHAR8     *Explanation;
} ED_LIST_KIND;
/**
  A kext of Kernel > Add as the kexts screen has it.
**/
typedef struct {
  XML_NODE       *Dict;
  //
  // Enabled as it is in the file, NULL if there is no boolean.
  //
  XML_NODE       *Enabled;
  BOOLEAN        WasEnabled;
  //
  // Enabled was not in the file and has been added.
  //
  BOOLEAN        EnabledAdded;
  //
  // Enabled is in the file, but it is not a boolean.
  //
  BOOLEAN        EnabledBroken;
  //
  // The kext has been moved by the user and is not where it was.
  //
  BOOLEAN        Moved;
  CHAR8          *Name;
  CHAR8          *Comment;
  //
  // Why OpenCore does not add the kext, if its files are not in order.
  //
  CONST CHAR8    *FileProblem;
} ED_KEXT_ROW;
typedef struct {
  ED_STATE              *State;
  CONST ED_LIST_KIND    *Kind;
  EDIT_PLIST            Plist;
  UINT32                Baseline;
  XML_NODE              *Array;
  UINT32                Count;
  //
  // By the index of the kext in the file.
  //
  ED_KEXT_ROW           *Rows;
  ED_KEXT_INFO          *Infos;
  BOOLEAN               *Enabled;
  ED_KEXT_PROBLEM       *Problems;
  //
  // For every position in the load order, the index of the kext in the file.
  //
  UINT32                *Order;
  UINT32                ProblemKexts;
  //
  // Identifiers of the kexts which Kernel > Force provides.
  //
  CONST CHAR8           **Provided;
  UINT32                ProvidedCount;
  ED_LIST               List;
  CHAR8                 Status[200];
  BOOLEAN               StatusIsError;
} ED_KEXTS;

extern CONST ED_LIST_KIND  mKindKexts;
extern CONST ED_LIST_KIND  mKindDrivers;
extern CONST ED_LIST_KIND  mKindAcpi;
extern CONST ED_LIST_KIND  mKindAcpiPatch;
extern CONST ED_LIST_KIND  mKindTools;
extern CONST ED_LIST_KIND  mKindEntries;

VOID
EdSetStatus (
  OUT CHAR8        *Status,
  IN  UINTN        Size,
  OUT BOOLEAN      *IsError,
  IN  BOOLEAN      Error,
  IN  CONST CHAR8  *Text
  );

VOID
EdUnicodeToAscii (
  IN  CONST CHAR16  *Text,
  OUT CHAR8         *Buffer,
  IN  UINTN         Size
  );

VOID
EdConfigNameAscii (
  OUT CHAR8  *Buffer,
  IN  UINTN  Size
  );

BOOLEAN
EdSameConfigName (
  IN  CONST CHAR16  *First,
  IN  CONST CHAR16  *Second
  );

BOOLEAN
EdIsConfigEditable (
  IN  CONST ED_STATE  *State,
  OUT CONST CHAR8     **Reason  OPTIONAL
  );

VOID
EdReadBootArgs (
  OUT ED_BOOT_ARGS  *Args
  );

EFI_STATUS
EdWriteConfig (
  IN  OC_STORAGE_CONTEXT  *Storage,
  IN  CONST VOID          *Old,
  IN  UINT32              OldSize,
  IN  CONST VOID          *New,
  IN  UINT32              NewSize
  );

EFI_STATUS
EdCommitConfig (
  IN  ED_STATE     *State,
  IN  EDIT_PLIST   *Plist,
  IN  UINT32       Baseline,
  OUT CONST CHAR8  **Reason
  );

EFI_STATUS
EdLoadConfig (
  IN  ED_STATE     *State,
  OUT EDIT_PLIST   *Plist,
  OUT UINT32       *Baseline,
  OUT CONST CHAR8  **Reason
  );

XML_NODE *
EdFindChildNoCase (
  IN  XML_NODE     *Dict,
  IN  CONST CHAR8  *Key
  );

VOID
EdRestart (
  IN OUT ED_STATE  *State
  );

INTN
EdAskUnsaved (
  IN OUT ED_UI  *Ui
  );

VOID
EdBootArgsScreen (
  IN OUT ED_STATE  *State
  );

VOID
EdBootFlagsScreen (
  IN OUT ED_STATE  *State
  );

VOID
EdDescribeValue (
  IN  CONST EDIT_PLIST  *Plist,
  IN  XML_NODE          *Node,
  OUT CHAR8             *Buffer,
  IN  UINTN             Size
  );

VOID
EdConfigEditor (
  IN OUT ED_STATE  *State
  );

CONST CHAR8 *
EdDictString (
  IN  CONST EDIT_PLIST  *Plist,
  IN  XML_NODE          *Dict,
  IN  CONST CHAR8       *Key
  );

VOID
EdKextsFree (
  IN OUT ED_KEXTS  *Kexts
  );

EFI_STATUS
EdKextsLoad (
  IN  ED_STATE            *State,
  IN  CONST ED_LIST_KIND  *Kind,
  OUT ED_KEXTS            **Result,
  OUT CONST CHAR8         **Reason
  );

VOID
EdKextDescribe (
  IN  CONST ED_KEXTS  *Kexts,
  IN  UINT32          Kext,
  OUT CHAR8           *Text,
  IN  UINTN           Size,
  OUT BOOLEAN         *IsProblem
  );

VOID
EdKextsEditor (
  IN OUT ED_STATE            *State,
  IN     CONST ED_LIST_KIND  *Kind
  );

VOID
EdListsMenu (
  IN OUT ED_STATE  *State
  );

VOID
EdSwitchesEditor (
  IN OUT ED_STATE  *State
  );

VOID
EdLogViewer (
  IN OUT ED_STATE  *State
  );

VOID
EdPanicViewer (
  IN OUT ED_STATE  *State
  );

VOID
EdCheckConfigScreen (
  IN OUT ED_STATE  *State
  );

VOID
EdCompareConfigs (
  IN OUT ED_STATE  *State
  );

VOID
EdRestoreBackup (
  IN OUT ED_STATE  *State
  );

CONST CHAR8 *
EdConfigSourceNote (
  IN  OC_CONFIG_SOURCE  Source
  );

VOID
EdConfigChooser (
  IN OUT ED_STATE  *State
  );

#endif // OPEN_CORE_EDITOR_PRIVATE_H
