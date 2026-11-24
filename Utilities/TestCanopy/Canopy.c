/** @file
  Runs the real OpenCanopy boot picker on the host and saves what it draws.

  The screen is a framebuffer in memory, keys come from a script, and the
  OpenCore storage is a directory of the host holding Resources/. Each
  snapshot is written as a raw BGRA file (<prefix>-<n>.bgra with a 16 byte
  header: width, height, 'BGRA', 0), which png.py turns into a PNG.

  Copyright (C) 2026, DrogaBox. All rights reserved.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <UserFile.h>
#include <UserGlobalVar.h>

#include <Base.h>
#include <Uefi.h>

#include <Guid/AppleVariable.h>
#include <Protocol/GraphicsOutput.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MtrrLib.h>
#include <Library/OcBootManagementLib.h>
#include <Library/OcStorageLib.h>
#include <Library/OcConsoleLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "OpenCanopy.h"
#include "GuiApp.h"
#include "GuiIo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern BOOT_PICKER_GUI_CONTEXT  mGuiContext;
extern GUI_OBJ_CHILD            mCommonActionButtonsContainer;

EFI_STATUS
InternalContextConstruct (
  OUT BOOT_PICKER_GUI_CONTEXT  *Context,
  IN  OC_STORAGE_CONTEXT       *Storage,
  IN  OC_PICKER_CONTEXT        *Picker
  );

EFI_STATUS
EFIAPI
OcShowMenuByOc (
  IN     OC_BOOT_CONTEXT  *BootContext,
  IN     OC_BOOT_ENTRY    **BootEntries,
  OUT    OC_BOOT_ENTRY    **ChosenBootEntry
  );

//
// Settings, from the command line.
//
STATIC CONST char  *mResources;     ///< Directory holding Resources/.
STATIC CONST char  *mOutPrefix;     ///< Snapshot file prefix.
STATIC UINT32      mWidth  = 1920;
STATIC UINT32      mHeight = 1080;
STATIC UINT8       mScale  = 1;

// -------------------------------------------------------------------------------------------------
// Output: the real Output/OutputStGop.c on a fake GOP, whose screen is a framebuffer in memory.
// -------------------------------------------------------------------------------------------------

STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL         *mFrame;
STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mGopInfo;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     mGopMode;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL          mGop;
STATIC UINT32                                mSnapshots;
STATIC UINT64                                mBlitPixels;

STATIC
EFI_STATUS
EFIAPI
FakeGopBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer OPTIONAL,
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN  UINTN                              SourceX,
  IN  UINTN                              SourceY,
  IN  UINTN                              DestinationX,
  IN  UINTN                              DestinationY,
  IN  UINTN                              Width,
  IN  UINTN                              Height,
  IN  UINTN                              Delta OPTIONAL
  )
{
  UINTN  Y;
  UINTN  X;

  if ((DestinationX + Width > mWidth) || (DestinationY + Height > mHeight)) {
    printf ("FAIL: blit %zux%zu at %zu,%zu outside the %ux%u screen\n", (size_t)Width, (size_t)Height, (size_t)DestinationX, (size_t)DestinationY, mWidth, mHeight);
    exit (1);
  }

  if (Delta == 0) {
    Delta = Width * sizeof (*BltBuffer);
  }

  mBlitPixels += (UINT64)Width * Height;
  for (Y = 0; Y < Height; ++Y) {
    for (X = 0; X < Width; ++X) {
      if (BltOperation == EfiBltVideoFill) {
        mFrame[(DestinationY + Y) * mWidth + DestinationX + X] = BltBuffer[0];
      } else if (BltOperation == EfiBltBufferToVideo) {
        mFrame[(DestinationY + Y) * mWidth + DestinationX + X] =
          *(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((UINT8 *)BltBuffer + (SourceY + Y) * Delta + (SourceX + X) * sizeof (*BltBuffer));
      } else {
        printf ("FAIL: unexpected blit operation %d\n", (int)BltOperation);
        exit (1);
      }
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FakeHandleProtocol (
  IN  EFI_HANDLE  Handle,
  IN  EFI_GUID    *Protocol,
  OUT VOID        **Interface
  )
{
  if (CompareGuid (Protocol, &gEfiGraphicsOutputProtocolGuid)) {
    *Interface = &mGop;
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC
VOID
InitGop (
  VOID
  )
{
  mFrame                         = calloc ((size_t)mWidth * mHeight, sizeof (*mFrame));
  mGopInfo.HorizontalResolution  = mWidth;
  mGopInfo.VerticalResolution    = mHeight;
  mGopInfo.PixelFormat           = PixelBlueGreenRedReserved8BitPerColor;
  mGopInfo.PixelsPerScanLine     = mWidth;
  mGopMode.MaxMode               = 1;
  mGopMode.Info                  = &mGopInfo;
  mGopMode.SizeOfInfo            = sizeof (mGopInfo);
  mGop.Mode                      = &mGopMode;
  mGop.Blt                       = FakeGopBlt;
  gST->ConsoleOutHandle          = (EFI_HANDLE)&mGop;
  gBS->HandleProtocol            = FakeHandleProtocol;
}

STATIC
VOID
Snapshot (
  VOID
  )
{
  char    Path[1024];
  FILE    *File;
  UINT32  Header[4];

  snprintf (Path, sizeof (Path), "%s-%u.bgra", mOutPrefix, mSnapshots++);
  File = fopen (Path, "wb");
  if (File == NULL) {
    printf ("FAIL: cannot write %s\n", Path);
    exit (1);
  }

  Header[0] = mWidth;
  Header[1] = mHeight;
  Header[2] = 0x41524742;  // "BGRA"
  Header[3] = 0;
  fwrite (Header, sizeof (Header), 1, File);
  fwrite (mFrame, sizeof (*mFrame), (size_t)mWidth * mHeight, File);
  fclose (File);
  printf ("snapshot %s\n", Path);
  if (getenv ("CANOPY_DEBUG") != NULL) {
    printf (
      "  ease-in %d, attributes 0x%x, scale %u, zoom %d; action buttons: opacity %u at %d,%d size %ux%u\n",
      mGuiContext.UseMenuEaseIn,
      (unsigned)mGuiContext.PickerContext->PickerAttributes,
      mGuiContext.Scale,
      mGuiContext.Zoom,
      mCommonActionButtonsContainer.Obj.Opacity,
      (int)mCommonActionButtonsContainer.Obj.OffsetX,
      (int)mCommonActionButtonsContainer.Obj.OffsetY,
      mCommonActionButtonsContainer.Obj.Width,
      mCommonActionButtonsContainer.Obj.Height
      );
  }
}

// -------------------------------------------------------------------------------------------------
// Pointer: none.
// -------------------------------------------------------------------------------------------------

BOOLEAN
GuiPointerGetEvent (
  IN OUT GUI_POINTER_CONTEXT  *Context,
  OUT    GUI_PTR_EVENT        *Event
  )
{
  return FALSE;
}

VOID
GuiPointerGetPosition (
  IN OUT GUI_POINTER_CONTEXT  *Context,
  OUT    GUI_PTR_POSITION     *Position
  )
{
  Position->Pos.X = 0;
  Position->Pos.Y = 0;
}

VOID
GuiPointerSetPosition (
  IN OUT GUI_POINTER_CONTEXT     *Context,
  IN     CONST GUI_PTR_POSITION  *Position
  )
{
}

VOID
GuiPointerReset (
  IN OUT GUI_POINTER_CONTEXT  *Context
  )
{
}

GUI_POINTER_CONTEXT *
GuiPointerConstruct (
  IN UINT32  DefaultX,
  IN UINT32  DefaultY,
  IN UINT32  Width,
  IN UINT32  Height,
  IN UINT8   UiScale
  )
{
  return NULL;
}

VOID
GuiPointerDestruct (
  IN GUI_POINTER_CONTEXT  *Context
  )
{
}

// -------------------------------------------------------------------------------------------------
// Keys: a script of (wait N polls, then do something).
// -------------------------------------------------------------------------------------------------

STATIC GUI_KEY_CONTEXT  mKeys;

typedef enum {
  StepKey,
  StepSnapshot,
  StepEnd
} STEP_KIND;

typedef struct {
  UINT32       Wait;   ///< Key polls to let pass first (each poll is one frame of the draw loop).
  STEP_KIND    Kind;
  OC_KEY_CODE  Key;
  CHAR16       Char;
} STEP;

STATIC STEP    mScript[64];
STATIC UINT32  mScriptCount;
STATIC UINT32  mScriptIndex;
STATIC UINT32  mPolls;

GUI_KEY_CONTEXT *
GuiKeyConstruct (
  IN OC_PICKER_CONTEXT  *PickerContext
  )
{
  return &mKeys;
}

VOID
EFIAPI
GuiKeyReset (
  IN OUT GUI_KEY_CONTEXT  *Context
  )
{
}

BOOLEAN
GuiKeyGetEvent (
  IN OUT GUI_KEY_CONTEXT  *Context,
  OUT    GUI_KEY_EVENT    *Event
  )
{
  STEP  *Step;

  while (mScriptIndex < mScriptCount) {
    Step = &mScript[mScriptIndex];
    if (mPolls < Step->Wait) {
      ++mPolls;
      return FALSE;
    }

    mPolls = 0;
    ++mScriptIndex;
    if (Step->Kind == StepSnapshot) {
      Snapshot ();
      continue;
    }

    if (Step->Kind == StepEnd) {
      printf ("script ended inside the picker\n");
      exit (0);
    }

    ZeroMem (Event, sizeof (*Event));
    Event->OcKeyCode   = Step->Key;
    Event->UnicodeChar = Step->Char;
    return TRUE;
  }

  printf ("FAIL: script ran out while the picker was still waiting\n");
  exit (1);
}

VOID
GuiKeyDestruct (
  IN GUI_KEY_CONTEXT  *Context
  )
{
}

// -------------------------------------------------------------------------------------------------
// Storage: files under the Resources directory of the host.
// -------------------------------------------------------------------------------------------------

STATIC
char *
HostPath (
  IN CONST CHAR16  *FilePath
  )
{
  static char  Path[2048];
  size_t       Length;
  UINTN        Index;

  Length = (size_t)snprintf (Path, sizeof (Path), "%s/", mResources);
  for (Index = 0; FilePath[Index] != L'\0' && Length + 1 < sizeof (Path); ++Index) {
    Path[Length++] = FilePath[Index] == L'\\' ? '/' : (char)FilePath[Index];
  }

  Path[Length] = '\0';
  return Path;
}

BOOLEAN
TestStorageExistsFileUnicode (
  IN  OC_STORAGE_CONTEXT  *Context,
  IN  CONST CHAR16        *FilePath
  )
{
  struct stat  St;

  return stat (HostPath (FilePath), &St) == 0;
}

VOID *
TestStorageReadFileUnicode (
  IN  OC_STORAGE_CONTEXT  *Context,
  IN  CONST CHAR16        *FilePath,
  OUT UINT32              *FileSize OPTIONAL
  )
{
  UINT32  Size;
  UINT8   *Data;
  VOID    *Copy;

  Data = UserReadFile (HostPath (FilePath), &Size);
  if (Data == NULL) {
    return NULL;
  }

  //
  // OpenCanopy frees with FreePool.
  //
  Copy = AllocateCopyPool (Size, Data);
  free (Data);
  if (FileSize != NULL) {
    *FileSize = Size;
  }

  return Copy;
}

// -------------------------------------------------------------------------------------------------
// Firmware pieces OpenCanopy asks for.
// -------------------------------------------------------------------------------------------------

STATIC
EFI_STATUS
EFIAPI
FakeGetVariable (
  IN     CHAR16    *VariableName,
  IN     EFI_GUID  *VendorGuid,
  OUT    UINT32    *Attributes OPTIONAL,
  IN OUT UINTN     *DataSize,
  OUT    VOID      *Data
  )
{
  if ((StrCmp (VariableName, APPLE_UI_SCALE_VARIABLE_NAME) == 0) && (*DataSize >= 1)) {
    *(UINT8 *)Data = mScale;
    *DataSize      = 1;
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
EFIAPI
FakeLocateProtocol (
  IN  EFI_GUID  *Protocol,
  IN  VOID      *Registration OPTIONAL,
  OUT VOID      **Interface
  )
{
  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
EFIAPI
NoAudioFile (
  IN  OC_PICKER_CONTEXT  *Context,
  IN  CONST CHAR8        *BasePath,
  IN  CONST CHAR8        *BaseType,
  IN  BOOLEAN            Fallback
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
NoEntryIcon (
  IN  OC_PICKER_CONTEXT  *Context,
  IN  OC_BOOT_ENTRY      *BootEntry,
  OUT VOID               **ImageData,
  OUT UINT32             *DataLength
  )
{
  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
EFIAPI
NoEntryLabel (
  IN  OC_PICKER_CONTEXT  *Context,
  IN  OC_BOOT_ENTRY      *BootEntry,
  IN  UINT8              Scale,
  OUT VOID               **ImageData,
  OUT UINT32             *DataLength
  )
{
  return EFI_NOT_FOUND;
}

STATIC BOOLEAN  mEditorShown;

STATIC
EFI_STATUS
EFIAPI
FakeShowEditor (
  IN OUT OC_PICKER_CONTEXT  *Context,
  IN     OC_BOOT_ENTRY      *Entry OPTIONAL
  )
{
  mEditorShown = TRUE;
  printf ("editor requested\n");
  return EFI_SUCCESS;
}


//
// Not reached in the harness, or nothing to do on the host.
//
EFI_GUID  gOcInterfaceProtocolGuid = {
  0x53027CDF, 0x3A89, 0x4255, { 0xAE, 0x29, 0xD6, 0x66, 0x6E, 0xFE, 0x99, 0xEF }
};

EFI_CONSOLE_CONTROL_SCREEN_MODE
OcConsoleControlSetMode (
  IN EFI_CONSOLE_CONTROL_SCREEN_MODE  Mode
  )
{
  return EfiConsoleControlScreenGraphics;
}

VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  printf ("restart requested\n");
  exit (0);
}

VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  printf ("shut down requested\n");
  exit (0);
}

UINT64
TestGetTSCFrequency (
  VOID
  )
{
  //
  // Below 60 ticks a second the frame time target is 0, so frames never wait
  // (the host AsmReadTsc does not advance).
  //
  return 1;
}

VOID *
EFIAPI
SetMem32 (
  OUT VOID    *Buffer,
  IN  UINTN   Length,
  IN  UINT32  Value
  )
{
  UINTN  Index;

  for (Index = 0; Index < Length / sizeof (UINT32); ++Index) {
    ((UINT32 *)Buffer)[Index] = Value;
  }

  return Buffer;
}

// -------------------------------------------------------------------------------------------------
// Entries, like the ones a typical machine shows.
// -------------------------------------------------------------------------------------------------

typedef struct {
  CONST CHAR16          *Name;
  CONST CHAR8           *Flavour;
  OC_BOOT_ENTRY_TYPE    Type;
} SAMPLE_ENTRY;

STATIC CONST SAMPLE_ENTRY  mSampleEntries[] = {
  { L"macOS",        "Apple12:Apple",                     OC_BOOT_APPLE_OS           },
  { L"Recovery",     "AppleRecv:Apple",                   OC_BOOT_APPLE_RECOVERY     },
  { L"Windows",      "Windows",                           OC_BOOT_WINDOWS            },
  { L"Clean NVRAM",  "ResetNVRAM:NVRAMTool",              OC_BOOT_EXTERNAL_TOOL      },
  { L"UEFI Shell",   "UEFIShell:Shell",                   OC_BOOT_EXTERNAL_TOOL      },
  { L"Reset NVRAM",  "ResetNVRAM:NVRAMTool",              OC_BOOT_SYSTEM             },
};

STATIC
VOID
Usage (
  VOID
  )
{
  printf (
    "usage: Canopy <dir with Resources/> <theme, e.g. DrogaBox\\AETHER> <out prefix> [options] [script]\n"
    "  -w W -h H   screen size (1920x1080)\n"
    "  -s 1|2      UIScale variable (1)\n"
    "  -n N        number of entries (4, up to %u)\n"
    "  -t S        timeout seconds (0)\n"
    "  -a A        PickerAttributes (17)\n"
    "script items: wait:N  snap  right  left  enter  esc  f2  end   (default: wait:90 snap end)\n",
    (unsigned)ARRAY_SIZE (mSampleEntries)
    );
}

STATIC
VOID
AddStep (
  IN UINT32       Wait,
  IN STEP_KIND    Kind,
  IN OC_KEY_CODE  Key
  )
{
  if (mScriptCount == ARRAY_SIZE (mScript)) {
    printf ("FAIL: script too long\n");
    exit (2);
  }

  mScript[mScriptCount].Wait = Wait;
  mScript[mScriptCount].Kind = Kind;
  mScript[mScriptCount].Key  = Key;
  mScript[mScriptCount].Char = L'\0';
  ++mScriptCount;
}

int
ENTRY_POINT (
  int   argc,
  char  *argv[]
  )
{
  STATIC OC_STORAGE_CONTEXT  Storage;
  STATIC OC_PICKER_CONTEXT   Picker;
  STATIC OC_BOOT_CONTEXT     Boot;
  OC_BOOT_ENTRY              *Entries[ARRAY_SIZE (mSampleEntries)];
  OC_BOOT_ENTRY              *Chosen;
  EFI_STATUS                 Status;
  UINT32                     Count;
  UINT32                     Timeout;
  UINT32                     Attributes;
  UINT32                     Wait;
  UINT32                     Index;
  int                        Arg;

  setvbuf (stdout, NULL, _IONBF, 0);

  if (argc < 4) {
    Usage ();
    return 2;
  }

  mResources = argv[1];
  mOutPrefix = argv[3];
  Count      = 4;
  Timeout    = 0;
  Attributes = 17;
  Wait       = 0;

  for (Arg = 4; Arg < argc; ++Arg) {
    if ((strcmp (argv[Arg], "-w") == 0) && (Arg + 1 < argc)) {
      mWidth = (UINT32)atoi (argv[++Arg]);
    } else if ((strcmp (argv[Arg], "-h") == 0) && (Arg + 1 < argc)) {
      mHeight = (UINT32)atoi (argv[++Arg]);
    } else if ((strcmp (argv[Arg], "-s") == 0) && (Arg + 1 < argc)) {
      mScale = (UINT8)atoi (argv[++Arg]);
    } else if ((strcmp (argv[Arg], "-n") == 0) && (Arg + 1 < argc)) {
      Count = (UINT32)atoi (argv[++Arg]);
    } else if ((strcmp (argv[Arg], "-t") == 0) && (Arg + 1 < argc)) {
      Timeout = (UINT32)atoi (argv[++Arg]);
    } else if ((strcmp (argv[Arg], "-a") == 0) && (Arg + 1 < argc)) {
      Attributes = (UINT32)strtoul (argv[++Arg], NULL, 0);
    } else if (strncmp (argv[Arg], "wait:", 5) == 0) {
      Wait += (UINT32)atoi (argv[Arg] + 5);
    } else if (strcmp (argv[Arg], "snap") == 0) {
      AddStep (Wait, StepSnapshot, 0);
      Wait = 0;
    } else if (strcmp (argv[Arg], "right") == 0) {
      AddStep (Wait, StepKey, OC_INPUT_RIGHT);
      Wait = 0;
    } else if (strcmp (argv[Arg], "left") == 0) {
      AddStep (Wait, StepKey, OC_INPUT_LEFT);
      Wait = 0;
    } else if (strcmp (argv[Arg], "enter") == 0) {
      AddStep (Wait, StepKey, OC_INPUT_CONTINUE);
      Wait = 0;
    } else if (strcmp (argv[Arg], "esc") == 0) {
      AddStep (Wait, StepKey, OC_INPUT_ABORTED);
      Wait = 0;
    } else if (strcmp (argv[Arg], "f2") == 0) {
      AddStep (Wait, StepKey, OC_INPUT_FUNCTIONAL (2));
      Wait = 0;
    } else if (strcmp (argv[Arg], "end") == 0) {
      AddStep (Wait, StepEnd, 0);
      Wait = 0;
    } else {
      Usage ();
      return 2;
    }
  }

  if (mScriptCount == 0) {
    AddStep (90, StepSnapshot, 0);
    AddStep (0, StepEnd, 0);
  }

  if ((Count == 0) || (Count > ARRAY_SIZE (mSampleEntries))) {
    Usage ();
    return 2;
  }

  gRT->GetVariable  = FakeGetVariable;
  InitGop ();
  gBS->LocateProtocol = FakeLocateProtocol;

  Picker.PickerVariant      = argv[2];
  Picker.PickerAttributes   = Attributes;
  Picker.TimeoutSeconds     = Timeout;
  Picker.PlayAudioFile      = NoAudioFile;
  Picker.GetEntryIcon       = NoEntryIcon;
  Picker.GetEntryLabelImage = NoEntryLabel;
  Picker.ShowEditor         = FakeShowEditor;
  Picker.TitleSuffix        = NULL;
  Picker.AllowSetDefault    = TRUE;

  Status = InternalContextConstruct (&mGuiContext, &Storage, &Picker);
  if (EFI_ERROR (Status)) {
    printf ("FAIL: OpenCanopy context - %s\n", Status == EFI_UNSUPPORTED ? "unsupported (theme or font)" : "error");
    return 1;
  }

  for (Index = 0; Index < Count; ++Index) {
    Entries[Index]             = AllocateZeroPool (sizeof (OC_BOOT_ENTRY));
    Entries[Index]->Name       = AllocateCopyPool (StrSize (mSampleEntries[Index].Name), mSampleEntries[Index].Name);
    Entries[Index]->Flavour    = AllocateCopyPool (AsciiStrSize (mSampleEntries[Index].Flavour), mSampleEntries[Index].Flavour);
    Entries[Index]->Type       = mSampleEntries[Index].Type;
    Entries[Index]->EntryIndex = Index + 1;
  }

  Boot.BootEntryCount = Count;
  Boot.DefaultEntry   = Entries[0];
  Boot.PickerContext  = &Picker;

  Status = OcShowMenuByOc (&Boot, Entries, &Chosen);
  printf (
    "picker returned %s, chosen %s%s\n",
    EFI_ERROR (Status) ? "aborted" : "success",
    Chosen != NULL ? "an entry" : "nothing",
    mEditorShown ? ", editor shown" : ""
    );
  return 0;
}
