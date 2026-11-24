/** @file
  This file is part of OpenCanopy, OpenCore GUI.

  Copyright (c) 2018-2019, Download-Fritz. All rights reserved.<BR>
  Copyright (c) 2026, DrogaBox. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Uefi.h>

#include <Protocol/GraphicsOutput.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "../GuiIo.h"

#define MIN_RESOLUTION_HORIZONTAL  640U
#define MIN_RESOLUTION_VERTICAL    480U

//
// With zoom, the GUI draws into a virtual screen 4/3 the size of the real one
// (2x artwork on a 1080p screen looks like 1.5x), and every block of 4x4
// virtual pixels becomes 3x3 real pixels on the way out.
//
#define ZOOM_VIRTUAL  4U
#define ZOOM_REAL     3U

struct GUI_OUTPUT_CONTEXT_ {
  EFI_GRAPHICS_OUTPUT_PROTOCOL            *Gop;
  BOOLEAN                                 Zoom;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION    VirtualInfo;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL           *Scratch;
  UINTN                                   ScratchPixels;
};

STATIC
EFI_GRAPHICS_OUTPUT_PROTOCOL *
InternalGuiOutputLocateGop (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  Status = gBS->HandleProtocol (
                  gST->ConsoleOutHandle,
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **)&Gop
                  );
  if (EFI_ERROR (Status)) {
    //
    // Do not fall back to other GOP instances to match AppleEvent.
    //
    return NULL;
  }

  return Gop;
}

BOOLEAN
GuiOutputCanZoom (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  UINT32                        Width;
  UINT32                        Height;

  Gop = InternalGuiOutputLocateGop ();
  if ((Gop == NULL) || (Gop->Mode == NULL) || (Gop->Mode->Info == NULL)) {
    return FALSE;
  }

  Width  = Gop->Mode->Info->HorizontalResolution;
  Height = Gop->Mode->Info->VerticalResolution;

  //
  // Worth it from about 1080p, and only while 2x would still be too big (the scale 2
  // threshold of OpenCore is 4 million pixels). The sizes must divide into 3 pixel blocks.
  //
  return Height >= 1000U
         && (UINT64)Width * Height < 4000000U
         && (Width % ZOOM_REAL) == 0
         && (Height % ZOOM_REAL) == 0
         && Width / ZOOM_REAL * ZOOM_VIRTUAL >= MIN_RESOLUTION_HORIZONTAL * 2
         && Height / ZOOM_REAL * ZOOM_VIRTUAL >= MIN_RESOLUTION_VERTICAL * 2;
}

GUI_OUTPUT_CONTEXT *
GuiOutputConstruct (
  IN UINT32   Scale,
  IN BOOLEAN  Zoom
  )
{
  // TODO: alloc on the fly?
  STATIC GUI_OUTPUT_CONTEXT  Context;

  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  UINT32                        Width;
  UINT32                        Height;

  Gop = InternalGuiOutputLocateGop ();
  if (Gop == NULL) {
    return NULL;
  }

  Width  = Gop->Mode->Info->HorizontalResolution;
  Height = Gop->Mode->Info->VerticalResolution;

  if (Zoom && !GuiOutputCanZoom ()) {
    DEBUG ((DEBUG_WARN, "OCUI: Zoom unsupported at %ux%u\n", Width, Height));
    return NULL;
  }

  if (Zoom) {
    Width  = Width / ZOOM_REAL * ZOOM_VIRTUAL;
    Height = Height / ZOOM_REAL * ZOOM_VIRTUAL;
  }

  if (  (Width < MIN_RESOLUTION_HORIZONTAL * Scale)
     || (Height < MIN_RESOLUTION_VERTICAL * Scale))
  {
    DEBUG ((
      DEBUG_WARN,
      "OCUI: Expected at least %dx%d for resolution, actual %dx%d\n",
      MIN_RESOLUTION_HORIZONTAL * Scale,
      MIN_RESOLUTION_VERTICAL * Scale,
      Width,
      Height
      ));
    return NULL;
  }

  ZeroMem (&Context, sizeof (Context));
  Context.Gop  = Gop;
  Context.Zoom = Zoom;
  if (Zoom) {
    CopyMem (&Context.VirtualInfo, Gop->Mode->Info, sizeof (Context.VirtualInfo));
    Context.VirtualInfo.HorizontalResolution = Width;
    Context.VirtualInfo.VerticalResolution   = Height;
    Context.VirtualInfo.PixelsPerScanLine    = Width;
  }

  return &Context;
}

/**
  Shrink 4 values into 3, keeping the area each one covers.
**/
STATIC
VOID
InternalZoomQuad (
  IN  CONST UINT32  *In,
  OUT UINT32        *Out
  )
{
  Out[0] = (3 * In[0] + In[1] + 2) / 4;
  Out[1] = (In[1] + In[2] + 1) / 2;
  Out[2] = (In[2] + 3 * In[3] + 2) / 4;
}

/**
  Draw a 4-aligned rectangle of the virtual screen buffer to the real screen.
**/
STATIC
EFI_STATUS
InternalZoomBlt (
  IN GUI_OUTPUT_CONTEXT                   *Context,
  IN CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Buffer,
  IN UINTN                                Delta,
  IN UINTN                                X,
  IN UINTN                                Y,
  IN UINTN                                Width,
  IN UINTN                                Height
  )
{
  UINTN                                RealWidth;
  UINTN                                RealHeight;
  UINTN                                Pixels;
  UINTN                                Row;
  UINTN                                Col;
  UINTN                                Line;
  UINTN                                Channel;
  UINT32                               In[4][4][4];
  UINT32                               Mid[4][3][4];
  UINT32                               Column[4];
  UINT32                               Result[3];
  CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Source;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL        *Target;

  ASSERT (X % ZOOM_VIRTUAL == 0 && Y % ZOOM_VIRTUAL == 0);
  ASSERT (Width % ZOOM_VIRTUAL == 0 && Height % ZOOM_VIRTUAL == 0);

  RealWidth  = Width / ZOOM_VIRTUAL * ZOOM_REAL;
  RealHeight = Height / ZOOM_VIRTUAL * ZOOM_REAL;
  Pixels     = RealWidth * RealHeight;
  if (Pixels == 0) {
    return EFI_SUCCESS;
  }

  if (Pixels > Context->ScratchPixels) {
    if (Context->Scratch != NULL) {
      FreePool (Context->Scratch);
    }

    //
    // Take the size of the whole screen at once, the largest a request can be.
    //
    Context->ScratchPixels = MAX (
                               Pixels,
                               (UINTN)Context->Gop->Mode->Info->HorizontalResolution * Context->Gop->Mode->Info->VerticalResolution
                               );
    Context->Scratch = AllocatePool (Context->ScratchPixels * sizeof (*Context->Scratch));
    if (Context->Scratch == NULL) {
      Context->ScratchPixels = 0;
      return EFI_OUT_OF_RESOURCES;
    }
  }

  for (Row = 0; Row < Height; Row += ZOOM_VIRTUAL) {
    for (Col = 0; Col < Width; Col += ZOOM_VIRTUAL) {
      //
      // Read a 4x4 block, shrink each of its rows to 3, then each column of 4 to 3.
      //
      for (Line = 0; Line < ZOOM_VIRTUAL; ++Line) {
        Source = (CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((CONST UINT8 *)Buffer + (Y + Row + Line) * Delta) + X + Col;
        for (Channel = 0; Channel < 4; ++Channel) {
          In[Line][0][Channel] = ((CONST UINT8 *)&Source[0])[Channel];
          In[Line][1][Channel] = ((CONST UINT8 *)&Source[1])[Channel];
          In[Line][2][Channel] = ((CONST UINT8 *)&Source[2])[Channel];
          In[Line][3][Channel] = ((CONST UINT8 *)&Source[3])[Channel];
        }

        for (Channel = 0; Channel < 4; ++Channel) {
          Column[0] = In[Line][0][Channel];
          Column[1] = In[Line][1][Channel];
          Column[2] = In[Line][2][Channel];
          Column[3] = In[Line][3][Channel];
          InternalZoomQuad (Column, Result);
          Mid[Line][0][Channel] = Result[0];
          Mid[Line][1][Channel] = Result[1];
          Mid[Line][2][Channel] = Result[2];
        }
      }

      for (Line = 0; Line < ZOOM_REAL; ++Line) {
        Target = Context->Scratch + (Row / ZOOM_VIRTUAL * ZOOM_REAL) * RealWidth + Col / ZOOM_VIRTUAL * ZOOM_REAL;
        for (Channel = 0; Channel < 4; ++Channel) {
          Column[0] = Mid[0][Line][Channel];
          Column[1] = Mid[1][Line][Channel];
          Column[2] = Mid[2][Line][Channel];
          Column[3] = Mid[3][Line][Channel];
          InternalZoomQuad (Column, Result);
          ((UINT8 *)&Target[Line])[Channel]                 = (UINT8)Result[0];
          ((UINT8 *)&Target[RealWidth + Line])[Channel]     = (UINT8)Result[1];
          ((UINT8 *)&Target[2 * RealWidth + Line])[Channel] = (UINT8)Result[2];
        }
      }
    }
  }

  return Context->Gop->Blt (
                         Context->Gop,
                         Context->Scratch,
                         EfiBltBufferToVideo,
                         0,
                         0,
                         X / ZOOM_VIRTUAL * ZOOM_REAL,
                         Y / ZOOM_VIRTUAL * ZOOM_REAL,
                         RealWidth,
                         RealHeight,
                         RealWidth * sizeof (*Context->Scratch)
                         );
}

EFI_STATUS
EFIAPI
GuiOutputBlt (
  IN GUI_OUTPUT_CONTEXT                 *Context,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta OPTIONAL
  )
{
  UINTN  X0;
  UINTN  Y0;
  UINTN  X1;
  UINTN  Y1;

  if (!Context->Zoom) {
    return Context->Gop->Blt (
                           Context->Gop,
                           BltBuffer,
                           BltOperation,
                           SourceX,
                           SourceY,
                           DestinationX,
                           DestinationY,
                           Width,
                           Height,
                           Delta
                           );
  }

  if ((Width == 0) || (Height == 0)) {
    return EFI_SUCCESS;
  }

  if (BltOperation == EfiBltVideoFill) {
    X0 = DestinationX * ZOOM_REAL / ZOOM_VIRTUAL;
    Y0 = DestinationY * ZOOM_REAL / ZOOM_VIRTUAL;
    X1 = ((DestinationX + Width) * ZOOM_REAL + ZOOM_VIRTUAL - 1) / ZOOM_VIRTUAL;
    Y1 = ((DestinationY + Height) * ZOOM_REAL + ZOOM_VIRTUAL - 1) / ZOOM_VIRTUAL;
    return Context->Gop->Blt (
                           Context->Gop,
                           BltBuffer,
                           EfiBltVideoFill,
                           0,
                           0,
                           X0,
                           Y0,
                           X1 - X0,
                           Y1 - Y0,
                           0
                           );
  }

  //
  // OpenCanopy only copies parts of its whole screen buffer, at the same place.
  // Widen the part to whole 4x4 blocks; the buffer holds the whole screen.
  //
  if (  (BltOperation != EfiBltBufferToVideo)
     || (SourceX != DestinationX)
     || (SourceY != DestinationY)
     || (Delta != Context->VirtualInfo.HorizontalResolution * sizeof (*BltBuffer)))
  {
    ASSERT (FALSE);
    return EFI_UNSUPPORTED;
  }

  X0 = DestinationX / ZOOM_VIRTUAL * ZOOM_VIRTUAL;
  Y0 = DestinationY / ZOOM_VIRTUAL * ZOOM_VIRTUAL;
  X1 = MIN (ALIGN_VALUE (DestinationX + Width, ZOOM_VIRTUAL), Context->VirtualInfo.HorizontalResolution);
  Y1 = MIN (ALIGN_VALUE (DestinationY + Height, ZOOM_VIRTUAL), Context->VirtualInfo.VerticalResolution);

  return InternalZoomBlt (Context, BltBuffer, Delta, X0, Y0, X1 - X0, Y1 - Y0);
}

CONST EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *
GuiOutputGetInfo (
  IN GUI_OUTPUT_CONTEXT  *Context
  )
{
  if (Context->Zoom) {
    return &Context->VirtualInfo;
  }

  return Context->Gop->Mode->Info;
}

VOID
GuiOutputDestruct (
  IN GUI_OUTPUT_CONTEXT  *Context
  )
{
  ASSERT (Context != NULL);
  if (Context->Scratch != NULL) {
    FreePool (Context->Scratch);
  }

  ZeroMem (Context, sizeof (*Context));
}
