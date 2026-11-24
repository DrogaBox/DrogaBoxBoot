/** @file
  Options editor: quirks.

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
// Switches that are often needed, so they are all in one place.
//

typedef struct {
  CONST CHAR8    *Section;
  CONST CHAR8    *Group;
  CONST CHAR8    *Key;
} ED_SWITCH;

STATIC CONST ED_SWITCH  mSwitches[] = {
  { "Booter", "Quirks",   "AvoidRuntimeDefrag"       },
  { "Booter", "Quirks",   "DevirtualiseMmio"         },
  { "Booter", "Quirks",   "EnableSafeModeSlide"      },
  { "Booter", "Quirks",   "EnableWriteUnprotector"   },
  { "Booter", "Quirks",   "ProtectUefiServices"      },
  { "Booter", "Quirks",   "ProvideCustomSlide"       },
  { "Booter", "Quirks",   "RebuildAppleMemoryMap"    },
  { "Booter", "Quirks",   "SetupVirtualMap"          },
  { "Booter", "Quirks",   "SyncRuntimePermissions"   },
  { "Kernel", "Quirks",   "AppleCpuPmCfgLock"        },
  { "Kernel", "Quirks",   "AppleXcpmCfgLock"         },
  { "Kernel", "Quirks",   "CustomSMBIOSGuid"         },
  { "Kernel", "Quirks",   "DisableIoMapper"          },
  { "Kernel", "Quirks",   "DisableLinkeditJettison"  },
  { "Kernel", "Quirks",   "DisableRtcChecksum"       },
  { "Kernel", "Quirks",   "ExtendBTFeatureFlags"     },
  { "Kernel", "Quirks",   "ForceAquantiaEthernet"    },
  { "Kernel", "Quirks",   "ForceSecureBootScheme"    },
  { "Kernel", "Quirks",   "IncreasePciBarSize"       },
  { "Kernel", "Quirks",   "LapicKernelPanic"         },
  { "Kernel", "Quirks",   "PanicNoKextDump"          },
  { "Kernel", "Quirks",   "PowerTimeoutKernelPanic"  },
  { "Kernel", "Quirks",   "ProvideCurrentCpuInfo"    },
  { "Kernel", "Quirks",   "ThirdPartyDrives"         },
  { "Kernel", "Quirks",   "XhciPortLimit"            },
  { "UEFI",   "Quirks",   "EnableVectorAcceleration" },
  { "UEFI",   "Quirks",   "ForgeUefiSupport"         },
  { "UEFI",   "Quirks",   "ReleaseUsbOwnership"      },
  { "UEFI",   "Quirks",   "RequestBootVarRouting"    },
  { "UEFI",   "Quirks",   "UnblockFsConnect"         },
  { "ACPI",   "Quirks",   "FadtEnableReset"          },
  { "ACPI",   "Quirks",   "NormalizeHeaders"         },
  { "ACPI",   "Quirks",   "RebaseRegions"            },
  { "ACPI",   "Quirks",   "ResetHwSig"               },
  { "ACPI",   "Quirks",   "ResetLogoStatus"          },
  { "ACPI",   "Quirks",   "SyncTableIds"             },
  { "Misc",   "Boot",     "HibernateSkipsPicker"     },
  { "Misc",   "Boot",     "HideAuxiliary"            },
  { "Misc",   "Boot",     "PickerAudioAssist"        },
  { "Misc",   "Boot",     "PollAppleHotKeys"         },
  { "Misc",   "Boot",     "ShowPicker"               },
  { "Misc",   "Debug",    "AppleDebug"               },
  { "Misc",   "Debug",    "ApplePanic"               },
  { "Misc",   "Debug",    "DisableWatchDog"          },
  { "Misc",   "Debug",    "SysReport"                },
  { "Misc",   "Security", "AllowNvramReset"          },
  { "Misc",   "Security", "AllowSetDefault"          },
  { "Misc",   "Security", "AuthRestart"              },
  { "Misc",   "Security", "BlacklistAppleUpdate"     }
};

typedef struct {
  XML_NODE    *Node;
  BOOLEAN     Was;
  UINTN       Switch;
} ED_SWITCH_ROW;

typedef struct {
  ED_STATE         *State;
  EDIT_PLIST       Plist;
  UINT32           Baseline;
  ED_SWITCH_ROW    Rows[ARRAY_SIZE (mSwitches)];
  UINTN            Count;
  ED_LIST          List;
  CHAR8            Status[128];
  BOOLEAN          StatusIsError;
} ED_SWITCHES;

STATIC
VOID
EdSwitchItem (
  IN  VOID     *Context,
  IN  UINTN    Index,
  OUT CHAR8    *Text,
  IN  UINTN    Size,
  OUT BOOLEAN  *Changed,
  OUT BOOLEAN  *Dim,
  OUT BOOLEAN  *Error
  )
{
  ED_SWITCHES          *Switches;
  CONST ED_SWITCH_ROW  *Row;
  BOOLEAN              On;

  Switches = Context;
  Row      = &Switches->Rows[Index];
  On       = EditPlistGetBoolean (&Switches->Plist, Row->Node);
  *Changed = On != Row->Was;
  *Dim     = !On;
  *Error   = FALSE;
  AsciiSPrint (
    Text,
    Size,
    "[%c]%c %a > %a > %a",
    On ? 'x' : ' ',
    *Changed ? '*' : ' ',
    mSwitches[Row->Switch].Section,
    mSwitches[Row->Switch].Group,
    mSwitches[Row->Switch].Key
    );
}

STATIC
BOOLEAN
EdSwitchesSave (
  IN OUT ED_SWITCHES  *Switches
  )
{
  EFI_STATUS   Status;
  CONST CHAR8  *Reason;
  CHAR8        Line[200];

  if (EditPlistPatchCount (&Switches->Plist) == 0) {
    return FALSE;
  }

  Status = EdCommitConfig (Switches->State, &Switches->Plist, Switches->Baseline, &Reason);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Line, sizeof (Line), "%a (%r)", Reason != NULL ? Reason : "Failed", Status);
    EdUiNotify (&Switches->State->Ui, Line, TRUE);
    return FALSE;
  }

  return TRUE;
}

VOID
EdSwitchesEditor (
  IN OUT ED_STATE  *State
  )
{
  ED_UI               *Ui;
  ED_SWITCHES         *Switches;
  CONST CHAR8         *Reason;
  OC_PICKER_KEY_INFO  *Key;
  ED_SWITCH_ROW       *Row;
  XML_NODE            *Node;
  UINTN               Index;
  UINTN               Visible;
  INTN                Choice;
  BOOLEAN             Redraw;
  BOOLEAN             Value;
  CHAR8               Line[160];

  Ui       = &State->Ui;
  Switches = AllocateZeroPool (sizeof (*Switches));
  if (Switches == NULL) {
    EdUiNotify (Ui, "Not enough memory", TRUE);
    return;
  }

  Switches->State = State;
  if (EFI_ERROR (EdLoadConfig (State, &Switches->Plist, &Switches->Baseline, &Reason))) {
    EdUiNotify (Ui, Reason != NULL ? Reason : "Cannot load the config", TRUE);
    FreePool (Switches);
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (mSwitches); ++Index) {
    Node = EditPlistFindChild (
             EditPlistFindChild (EditPlistFindChild (Switches->Plist.Root, mSwitches[Index].Section), mSwitches[Index].Group),
             mSwitches[Index].Key
             );
    if ((Node != NULL) && (EditPlistNodeKind (Node) == EditPlistKindBoolean)) {
      Switches->Rows[Switches->Count].Node   = Node;
      Switches->Rows[Switches->Count].Was    = EditPlistGetBoolean (&Switches->Plist, Node);
      Switches->Rows[Switches->Count].Switch = Index;
      ++Switches->Count;
    }
  }

  Switches->List.Count = Switches->Count;
  Visible              = EdUiContentRows (Ui);
  Redraw               = TRUE;

  while (TRUE) {
    if (Redraw) {
      EdUiClear (Ui);
      Redraw = FALSE;
    }

    {
      CHAR8  Name[OC_CONFIG_MAX_NAME + 1];
      CHAR8  Subtitle[160];

      EdConfigNameAscii (Name, sizeof (Name));
      AsciiSPrint (Subtitle, sizeof (Subtitle), "%a: %u switches", Name, (UINT32)Switches->Count);
      if (EditPlistPatchCount (&Switches->Plist) > 0) {
        AsciiSPrint (Line, sizeof (Line), ", %u unsaved changes", EditPlistPatchCount (&Switches->Plist));
        AsciiStrCatS (Subtitle, sizeof (Subtitle), Line);
      }

      EdUiTitle (Ui, "Quirks and switches", Subtitle);
    }

    EdListDraw (Ui, 3, Visible, &Switches->List, EdSwitchItem, Switches);
    if (Switches->Count == 0) {
      EdUiPut (Ui, 0, 3, Ui->AttrHint, "  (none of the usual switches is in this config)", 0);
    }

    EdUiStatus (Ui, Switches->StatusIsError ? Ui->AttrError : Ui->AttrHint, Switches->Status);
    EdUiHint (Ui, "Space: on/off   F10: save   Esc: back");

    Key                 = EdUiWaitKey (Ui, FALSE);
    Switches->Status[0] = '\0';

    switch (Key->OcKeyCode) {
      case OC_INPUT_UP:
      case OC_INPUT_DOWN:
      case OC_INPUT_TOP:
      case OC_INPUT_BOTTOM:
        EdListMove (&Switches->List, Visible, Key->OcKeyCode);
        break;
      case OC_INPUT_MORE:
      case OC_INPUT_CONTINUE:
        if (Switches->Count > 0) {
          Row   = &Switches->Rows[Switches->List.Selected];
          Value = !EditPlistGetBoolean (&Switches->Plist, Row->Node);
          if (EFI_ERROR (EditPlistSetBoolean (&Switches->Plist, Row->Node, Value))) {
            EdSetStatus (Switches->Status, sizeof (Switches->Status), &Switches->StatusIsError, TRUE, "Cannot change this switch");
          } else if (Value == Row->Was) {
            EditPlistRevert (&Switches->Plist, Row->Node);
          }
        }

        break;
      case OC_INPUT_FUNCTIONAL (10):
        Redraw = TRUE;
        if (EdSwitchesSave (Switches)) {
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
        if (EditPlistPatchCount (&Switches->Plist) == 0) {
          goto Done;
        }

        Choice = EdAskUnsaved (Ui);
        if (Choice == 2) {
          goto Done;
        }

        if ((Choice >= 0) && EdSwitchesSave (Switches)) {
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
  EditPlistFree (&Switches->Plist);
  FreePool (Switches);
}
