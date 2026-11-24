/** @file
  The options editor (F2) drawn by OpenCanopy: a sheet over the darkened
  background, with the font of the theme, instead of a jump to text mode.

  The editor (OcMainLib) keeps laying out its screens as rows of text with a
  role for each piece (normal, selected, title, changed, error, hint); this
  canvas keeps those rows and draws them when the editor waits for a key.

  Copyright (C) 2026, DrogaBox. All rights reserved.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/OcBootManagementLib.h>

#include "../OpenCanopy.h"
#include "../BmfLib.h"
#include "../GuiApp.h"
#include "../GuiIo.h"

extern BOOT_PICKER_GUI_CONTEXT  mGuiContext;

//
// Layout, in units of the 1x picker (multiplied by the scale).
//
#define CANVAS_COLUMNS        80U
#define CANVAS_MAX_ROWS       26U
#define CANVAS_PAD_X          28U
#define CANVAS_PAD_Y          22U
#define CANVAS_RADIUS         12U
#define CANVAS_SELECT_RADIUS  6U
#define CANVAS_SELECT_PAD     8U
#define CANVAS_RUN_GAP        4U
#define CANVAS_KEY_PAD_X      5U
#define CANVAS_KEY_RADIUS     4U

//
// Colours, as in the macOS dark appearance: sheet, selection and text roles.
//
#define CANVAS_DIM          0x70U  ///< How much of the background stays (of 0xFF).
#define CANVAS_SHEET_ALPHA  0xF2U

typedef struct {
  UINT8    Red;
  UINT8    Green;
  UINT8    Blue;
  UINT8    Alpha;
} CANVAS_COLOUR;

STATIC CONST CANVAS_COLOUR  mSheetColour   = { 0x1C, 0x1E, 0x23, CANVAS_SHEET_ALPHA };
STATIC CONST CANVAS_COLOUR  mBorderColour  = { 0xFF, 0xFF, 0xFF, 0x1F };
STATIC CONST CANVAS_COLOUR  mDividerColour = { 0xFF, 0xFF, 0xFF, 0x1A };
STATIC CONST CANVAS_COLOUR  mSelectColour  = { 0x0A, 0x6B, 0xF2, 0xFF };
STATIC CONST CANVAS_COLOUR  mKeyColour     = { 0xFF, 0xFF, 0xFF, 0x2A };

STATIC CONST CANVAS_COLOUR  mRoleColours[OcEditorRoleMax] = {
  { 0xF2, 0xF2, 0xF5, 0xFF }, ///< Normal
  { 0xFF, 0xFF, 0xFF, 0xFF }, ///< Selected
  { 0xFF, 0xFF, 0xFF, 0xFF }, ///< Title
  { 0xFF, 0xC8, 0x4B, 0xFF }, ///< Changed
  { 0xFF, 0x6B, 0x61, 0xFF }, ///< Error
  { 0x9A, 0x9C, 0xA4, 0xFF }  ///< Hint
};

STATIC GUI_OUTPUT_CONTEXT             *mCanvasOutput;
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *mCanvasFrame;
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *mCanvasBack;
STATIC UINT32                         mCanvasWidth;
STATIC UINT32                         mCanvasHeight;

STATIC CHAR16  *mCanvasText;
STATIC UINT8   *mCanvasRoles;
STATIC UINTN   mCanvasColumns;
STATIC UINTN   mCanvasRows;

STATIC UINT32  mCellWidth;
STATIC UINT32  mLineHeight;
STATIC UINT32  mTextOffsetY;
STATIC UINT32  mSheetX;
STATIC UINT32  mSheetY;
STATIC UINT32  mSheetWidth;
STATIC UINT32  mSheetHeight;
STATIC UINT32  mPadX;
STATIC UINT32  mPadY;

/**
  Blend Colour with Coverage (0-255) over a pixel.
**/
STATIC
VOID
CanvasBlendPixel (
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Pixel,
  IN     CONST CANVAS_COLOUR            *Colour,
  IN     UINT32                         Coverage
  )
{
  UINT32  Alpha;
  UINT32  Inverse;

  Alpha = (Coverage * Colour->Alpha + 127) / 255;
  if (Alpha == 0) {
    return;
  }

  Inverse      = 255 - Alpha;
  Pixel->Red   = (UINT8)((Colour->Red * Alpha + Pixel->Red * Inverse + 127) / 255);
  Pixel->Green = (UINT8)((Colour->Green * Alpha + Pixel->Green * Inverse + 127) / 255);
  Pixel->Blue  = (UINT8)((Colour->Blue * Alpha + Pixel->Blue * Inverse + 127) / 255);
}

/**
  Fill a rectangle with rounded corners (anti-aliased, 4 samples per pixel on the corners).
**/
STATIC
VOID
CanvasFillRoundRect (
  IN INT64                X,
  IN INT64                Y,
  IN INT64                Width,
  IN INT64                Height,
  IN INT64                Radius,
  IN CONST CANVAS_COLOUR  *Colour
  )
{
  INT64   PixelX;
  INT64   PixelY;
  INT64   CornerX;
  INT64   CornerY;
  INT64   DistX;
  INT64   DistY;
  UINT32  Inside;
  UINT32  Sample;
  INT64   SampleX;
  INT64   SampleY;

  if ((Width <= 0) || (Height <= 0)) {
    return;
  }

  Radius = MIN (Radius, MIN (Width, Height) / 2);

  for (PixelY = MAX (Y, 0); PixelY < MIN (Y + Height, (INT64)mCanvasHeight); ++PixelY) {
    for (PixelX = MAX (X, 0); PixelX < MIN (X + Width, (INT64)mCanvasWidth); ++PixelX) {
      //
      // Distance into the corner square, if in one.
      //
      CornerX = PixelX < X + Radius ? X + Radius : (PixelX >= X + Width - Radius ? X + Width - Radius : PixelX);
      CornerY = PixelY < Y + Radius ? Y + Radius : (PixelY >= Y + Height - Radius ? Y + Height - Radius : PixelY);
      if ((CornerX == PixelX) || (CornerY == PixelY)) {
        CanvasBlendPixel (&mCanvasFrame[PixelY * mCanvasWidth + PixelX], Colour, 255);
        continue;
      }

      Inside = 0;
      for (Sample = 0; Sample < 4; ++Sample) {
        //
        // Samples at (1/4, 3/4) of the pixel, in units of 1/4 pixel.
        //
        SampleX = PixelX * 4 + ((Sample & 1) != 0 ? 3 : 1);
        SampleY = PixelY * 4 + ((Sample & 2) != 0 ? 3 : 1);
        DistX   = SampleX - CornerX * 4;
        DistY   = SampleY - CornerY * 4;
        if (DistX * DistX + DistY * DistY <= Radius * Radius * 16) {
          ++Inside;
        }
      }

      if (Inside > 0) {
        CanvasBlendPixel (&mCanvasFrame[PixelY * mCanvasWidth + PixelX], Colour, Inside * 255 / 4);
      }
    }
  }
}

/**
  Draw a label image (white text with coverage in Reserved) in Colour, cut at MaxRight.
**/
STATIC
VOID
CanvasDrawLabel (
  IN CONST GUI_IMAGE      *Label,
  IN INT64                X,
  IN INT64                Y,
  IN INT64                MaxRight,
  IN CONST CANVAS_COLOUR  *Colour
  )
{
  UINT32  LabelX;
  UINT32  LabelY;
  INT64   PixelX;
  INT64   PixelY;

  for (LabelY = 0; LabelY < Label->Height; ++LabelY) {
    PixelY = Y + LabelY;
    if ((PixelY < 0) || (PixelY >= (INT64)mCanvasHeight)) {
      continue;
    }

    for (LabelX = 0; LabelX < Label->Width; ++LabelX) {
      PixelX = X + LabelX;
      if ((PixelX < 0) || (PixelX >= MaxRight) || (PixelX >= (INT64)mCanvasWidth)) {
        continue;
      }

      CanvasBlendPixel (
        &mCanvasFrame[PixelY * mCanvasWidth + PixelX],
        Colour,
        Label->Buffer[LabelY * Label->Width + LabelX].Reserved
        );
    }
  }
}

STATIC
INT64
CanvasColumnX (
  IN UINTN  Column
  )
{
  return (INT64)mSheetX + mPadX + (INT64)Column * mCellWidth;
}

STATIC
INT64
CanvasRowY (
  IN UINTN  Row
  )
{
  return (INT64)mSheetY + mPadY + (INT64)Row * mLineHeight;
}

/**
  Draw text in a colour at a place; Bold draws it twice, one pixel apart.
**/
STATIC
INT64
CanvasDrawText (
  IN CONST CHAR16         *Text,
  IN UINTN                Length,
  IN INT64                X,
  IN INT64                Y,
  IN INT64                MaxRight,
  IN CONST CANVAS_COLOUR  *Colour,
  IN BOOLEAN              Bold
  )
{
  GUI_IMAGE  Label;
  INT64      Width;

  if (Length == 0) {
    return 0;
  }

  ZeroMem (&Label, sizeof (Label));
  if (!GuiGetLabel (&Label, &mGuiContext.FontContext, Text, Length, FALSE)) {
    return 0;
  }

  CanvasDrawLabel (&Label, X, Y, MaxRight, Colour);
  if (Bold) {
    CanvasDrawLabel (&Label, X + 1, Y, MaxRight, Colour);
  }

  Width = Label.Width;
  FreePool (Label.Buffer);
  return Width;
}

/**
  Whether nothing but spaces comes before a column of a row.
**/
STATIC
BOOLEAN
CanvasIsFirstText (
  IN CONST CHAR16  *Text,
  IN UINTN         Column
  )
{
  UINTN  Index;

  for (Index = 0; Index < Column; ++Index) {
    if (Text[Index] != L' ') {
      return FALSE;
    }
  }

  return TRUE;
}

/**
  Draw one piece of a row. The marker of the selected line ("> ") is left out, as the
  selection shows it; in hints, the keys before ": " are drawn as key caps.
**/
STATIC
VOID
CanvasDrawPiece (
  IN UINTN  Row,
  IN UINTN  Start,
  IN UINTN  End,
  IN UINT8  Role,
  IN INT64  MaxRight
  )
{
  CONST CHAR16         *Text;
  CONST CANVAS_COLOUR  *Colour;
  INT64                X;
  INT64                Y;
  INT64                Width;
  UINTN                Colon;
  UINTN                Length;
  GUI_IMAGE            Label;

  Text   = &mCanvasText[Row * mCanvasColumns];
  Colour = &mRoleColours[Role < OcEditorRoleMax ? Role : OcEditorRoleNormal];
  X      = CanvasColumnX (Start);
  Y      = CanvasRowY (Row) + mTextOffsetY;

  if (  (Role == OcEditorRoleSelected)
     && (Text[Start] == L'>')
     && ((End - Start == 1) || (Text[Start + 1] == L' '))
     && CanvasIsFirstText (Text, Start))
  {
    Start += End - Start == 1 ? 1 : 2;
  }

  if (Role == OcEditorRoleHint) {
    Colon = Start;
    while (Colon + 1 < End && !(Text[Colon] == L':' && Text[Colon + 1] == L' ')) {
      ++Colon;
    }

    if ((Colon + 1 < End) && (Colon > Start)) {
      //
      // "Keys: action" - the keys in a cap, the action dimmed after it.
      //
      Length = Colon - Start;
      ZeroMem (&Label, sizeof (Label));
      if (GuiGetLabel (&Label, &mGuiContext.FontContext, &Text[Start], Length, FALSE)) {
        Width = Label.Width;
        FreePool (Label.Buffer);
        CanvasFillRoundRect (
          X,
          CanvasRowY (Row) + mTextOffsetY / 2,
          Width + 2 * CANVAS_KEY_PAD_X * mGuiContext.Scale,
          mLineHeight - mTextOffsetY,
          CANVAS_KEY_RADIUS * mGuiContext.Scale,
          &mKeyColour
          );
        CanvasDrawText (&Text[Start], Length, X + CANVAS_KEY_PAD_X * mGuiContext.Scale, Y, MaxRight, &mRoleColours[OcEditorRoleNormal], FALSE);
        X    += Width + 2 * CANVAS_KEY_PAD_X * mGuiContext.Scale + CANVAS_KEY_PAD_X * mGuiContext.Scale;
        Start = Colon + 2;
      }
    }
  }

  if (End > Start) {
    CanvasDrawText (&Text[Start], End - Start, X, Y, MaxRight, Colour, Role == OcEditorRoleTitle);
  }
}

/**
  Draw the text of one row: pieces of the same role, split where two or more spaces
  separate them, each at the place of its first column (the editor aligns with spaces).
**/
STATIC
VOID
CanvasDrawRowText (
  IN UINTN  Row
  )
{
  CONST CHAR16  *Text;
  CONST UINT8   *Roles;
  UINTN         Start;
  UINTN         End;
  UINTN         Next;
  INT64         MaxRight;

  Text  = &mCanvasText[Row * mCanvasColumns];
  Roles = &mCanvasRoles[Row * mCanvasColumns];

  Start = 0;
  while (Start < mCanvasColumns) {
    if (Text[Start] == L' ') {
      ++Start;
      continue;
    }

    //
    // The piece ends at a change of role or at two spaces.
    //
    End = Start;
    while (End < mCanvasColumns && Roles[End] == Roles[Start]) {
      if ((Text[End] == L' ') && ((End + 1 >= mCanvasColumns) || (Text[End + 1] == L' '))) {
        break;
      }

      ++End;
    }

    //
    // The next piece starts after the spaces; this one may not run into it.
    //
    Next = End;
    while (Next < mCanvasColumns && Text[Next] == L' ') {
      ++Next;
    }

    MaxRight = Next < mCanvasColumns ? CanvasColumnX (Next) - CANVAS_RUN_GAP * mGuiContext.Scale : (INT64)mSheetX + mSheetWidth - mPadX;

    if (End > Start) {
      CanvasDrawPiece (Row, Start, End, Roles[Start], MaxRight);
    }

    Start = End > Start ? End : Start + 1;
  }
}

/**
  Draw the selection behind the cells of a row which have the selected role.
  A selection reaching the last columns is a whole line and spans the sheet.
**/
STATIC
VOID
CanvasDrawRowSelection (
  IN UINTN  Row
  )
{
  CONST UINT8  *Roles;
  UINTN        Start;
  UINTN        End;
  INT64        Left;
  INT64        Right;

  Roles = &mCanvasRoles[Row * mCanvasColumns];
  Start = 0;
  while (Start < mCanvasColumns) {
    if (Roles[Start] != OcEditorRoleSelected) {
      ++Start;
      continue;
    }

    End = Start;
    while (End < mCanvasColumns && Roles[End] == OcEditorRoleSelected) {
      ++End;
    }

    Left  = CanvasColumnX (Start) - CANVAS_SELECT_PAD * mGuiContext.Scale;
    Right = End + 2 >= mCanvasColumns
            ? (INT64)mSheetX + mSheetWidth - mPadX + CANVAS_SELECT_PAD * mGuiContext.Scale
            : CanvasColumnX (End) + CANVAS_SELECT_PAD * mGuiContext.Scale / 2;
    CanvasFillRoundRect (
      Left,
      CanvasRowY (Row),
      Right - Left,
      mLineHeight,
      CANVAS_SELECT_RADIUS * mGuiContext.Scale,
      &mSelectColour
      );
    Start = End;
  }
}

STATIC
VOID
CanvasDrawDivider (
  IN INT64  Y
  )
{
  CanvasFillRoundRect (
    (INT64)mSheetX + mPadX,
    Y,
    (INT64)mSheetWidth - 2 * mPadX,
    mGuiContext.Scale,
    0,
    &mDividerColour
    );
}

STATIC
VOID
EFIAPI
CanvasFlush (
  IN OC_EDITOR_CANVAS  *This
  )
{
  UINTN  Row;

  if (mCanvasOutput == NULL) {
    return;
  }

  CopyMem (mCanvasFrame, mCanvasBack, (UINTN)mCanvasWidth * mCanvasHeight * sizeof (*mCanvasFrame));

  //
  // The sheet: a thin border, then the panel.
  //
  CanvasFillRoundRect (
    (INT64)mSheetX - mGuiContext.Scale,
    (INT64)mSheetY - mGuiContext.Scale,
    (INT64)mSheetWidth + 2 * mGuiContext.Scale,
    (INT64)mSheetHeight + 2 * mGuiContext.Scale,
    (CANVAS_RADIUS + 1) * mGuiContext.Scale,
    &mBorderColour
    );
  CanvasFillRoundRect (mSheetX, mSheetY, mSheetWidth, mSheetHeight, CANVAS_RADIUS * mGuiContext.Scale, &mSheetColour);

  //
  // Dividers under the title (rows 0 and 1) and above the status and hint (the last three rows).
  //
  CanvasDrawDivider (CanvasRowY (2) + mLineHeight / 2);
  if (mCanvasRows > 6) {
    CanvasDrawDivider (CanvasRowY (mCanvasRows - 3) - mLineHeight / 4);
  }

  for (Row = 0; Row < mCanvasRows; ++Row) {
    CanvasDrawRowSelection (Row);
    CanvasDrawRowText (Row);
  }

  //
  // Only the sheet changes, but its rectangle must be whole 4x4 blocks with zoom.
  //
  GuiOutputBlt (
    mCanvasOutput,
    mCanvasFrame,
    EfiBltBufferToVideo,
    mSheetX - 4 * mGuiContext.Scale,
    mSheetY - 4 * mGuiContext.Scale,
    mSheetX - 4 * mGuiContext.Scale,
    mSheetY - 4 * mGuiContext.Scale,
    mSheetWidth + 8 * mGuiContext.Scale,
    mSheetHeight + 8 * mGuiContext.Scale,
    (UINTN)mCanvasWidth * sizeof (*mCanvasFrame)
    );
}

STATIC
VOID
EFIAPI
CanvasClear (
  IN OC_EDITOR_CANVAS  *This
  )
{
  UINTN  Index;

  for (Index = 0; Index < mCanvasColumns * mCanvasRows; ++Index) {
    mCanvasText[Index]  = L' ';
    mCanvasRoles[Index] = OcEditorRoleNormal;
  }
}

STATIC
VOID
EFIAPI
CanvasPut (
  IN OC_EDITOR_CANVAS  *This,
  IN UINTN             Column,
  IN UINTN             Row,
  IN UINTN             Role,
  IN CONST CHAR16      *Text
  )
{
  UINTN  Index;

  if ((mCanvasText == NULL) || (Row >= mCanvasRows)) {
    return;
  }

  for (Index = 0; Text[Index] != CHAR_NULL && Column + Index < mCanvasColumns; ++Index) {
    mCanvasText[Row * mCanvasColumns + Column + Index]  = (Text[Index] >= 0x20 && Text[Index] < 0x7F) ? Text[Index] : L'?';
    mCanvasRoles[Row * mCanvasColumns + Column + Index] = (UINT8)(Role < OcEditorRoleMax ? Role : OcEditorRoleNormal);
  }
}

STATIC
VOID
CanvasFreeAll (
  VOID
  )
{
  if (mCanvasOutput != NULL) {
    GuiOutputDestruct (mCanvasOutput);
    mCanvasOutput = NULL;
  }

  if (mCanvasFrame != NULL) {
    FreePool (mCanvasFrame);
    mCanvasFrame = NULL;
  }

  if (mCanvasBack != NULL) {
    FreePool (mCanvasBack);
    mCanvasBack = NULL;
  }

  if (mCanvasText != NULL) {
    FreePool (mCanvasText);
    mCanvasText = NULL;
  }

  if (mCanvasRoles != NULL) {
    FreePool (mCanvasRoles);
    mCanvasRoles = NULL;
  }
}

/**
  Prepare the darkened background once: the theme background, centred as in the picker.
**/
STATIC
VOID
CanvasMakeBackground (
  VOID
  )
{
  UINTN            Index;
  INT64            OffsetX;
  INT64            OffsetY;
  UINT32           X;
  UINT32           Y;
  INT64            SourceX;
  INT64            SourceY;
  CONST GUI_IMAGE  *Background;
  CANVAS_COLOUR    Colour;

  for (Index = 0; Index < (UINTN)mCanvasWidth * mCanvasHeight; ++Index) {
    mCanvasBack[Index] = mGuiContext.BackgroundColor.Pixel;
  }

  Background = &mGuiContext.Background;
  if (Background->Buffer != NULL) {
    OffsetX = ((INT64)mCanvasWidth - Background->Width) / 2;
    OffsetY = ((INT64)mCanvasHeight - Background->Height) / 2;
    for (Y = 0; Y < mCanvasHeight; ++Y) {
      SourceY = (INT64)Y - OffsetY;
      if ((SourceY < 0) || (SourceY >= Background->Height)) {
        continue;
      }

      for (X = 0; X < mCanvasWidth; ++X) {
        SourceX = (INT64)X - OffsetX;
        if ((SourceX < 0) || (SourceX >= Background->Width)) {
          continue;
        }

        //
        // The background is opaque where it is drawn; blend it over the colour anyway.
        //
        Colour.Red   = Background->Buffer[SourceY * Background->Width + SourceX].Red;
        Colour.Green = Background->Buffer[SourceY * Background->Width + SourceX].Green;
        Colour.Blue  = Background->Buffer[SourceY * Background->Width + SourceX].Blue;
        Colour.Alpha = 0xFF;
        CanvasBlendPixel (
          &mCanvasBack[Y * mCanvasWidth + X],
          &Colour,
          Background->Buffer[SourceY * Background->Width + SourceX].Reserved
          );
      }
    }
  }

  for (Index = 0; Index < (UINTN)mCanvasWidth * mCanvasHeight; ++Index) {
    mCanvasBack[Index].Red   = (UINT8)(mCanvasBack[Index].Red * CANVAS_DIM / 0xFF);
    mCanvasBack[Index].Green = (UINT8)(mCanvasBack[Index].Green * CANVAS_DIM / 0xFF);
    mCanvasBack[Index].Blue  = (UINT8)(mCanvasBack[Index].Blue * CANVAS_DIM / 0xFF);
  }
}

STATIC
EFI_STATUS
EFIAPI
CanvasBegin (
  IN  OC_EDITOR_CANVAS  *This,
  OUT UINTN             *Columns,
  OUT UINTN             *Rows
  )
{
  CONST EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  GUI_IMAGE                                   Digits;
  UINT32                                      Scale;
  UINT32                                      MaxRows;

  if ((mCanvasOutput != NULL) || (mGuiContext.FontContext.FontImage.Buffer == NULL)) {
    return EFI_NOT_READY;
  }

  Scale         = mGuiContext.Scale;
  mCanvasOutput = GuiOutputConstruct (Scale, mGuiContext.Zoom);
  if (mCanvasOutput == NULL) {
    return EFI_UNSUPPORTED;
  }

  Info          = GuiOutputGetInfo (mCanvasOutput);
  mCanvasWidth  = Info->HorizontalResolution;
  mCanvasHeight = Info->VerticalResolution;

  //
  // A cell is as wide as a digit; a line is one and a half times the font.
  //
  ZeroMem (&Digits, sizeof (Digits));
  if (!GuiGetLabel (&Digits, &mGuiContext.FontContext, L"0123456789", 10, FALSE)) {
    CanvasFreeAll ();
    return EFI_UNSUPPORTED;
  }

  mCellWidth   = MAX (Digits.Width / 10, 1);
  mLineHeight  = Digits.Height * 3 / 2;
  mTextOffsetY = (mLineHeight - Digits.Height) / 2;
  FreePool (Digits.Buffer);

  mPadX          = CANVAS_PAD_X * Scale;
  mPadY          = CANVAS_PAD_Y * Scale;
  mCanvasColumns = CANVAS_COLUMNS;
  mSheetWidth    = (UINT32)mCanvasColumns * mCellWidth + 2 * mPadX;
  MaxRows        = (mCanvasHeight * 85 / 100 - 2 * mPadY) / mLineHeight;
  mCanvasRows    = MIN (MaxRows, CANVAS_MAX_ROWS);
  mSheetHeight   = (UINT32)mCanvasRows * mLineHeight + 2 * mPadY;

  if ((mSheetWidth + 16 * Scale > mCanvasWidth) || (mCanvasRows < 12)) {
    DEBUG ((DEBUG_INFO, "OCUI: Screen too small for the editor sheet\n"));
    CanvasFreeAll ();
    return EFI_UNSUPPORTED;
  }

  //
  // Centre the sheet on a 4 pixel grid, so that its blits stay whole blocks with zoom.
  //
  mSheetX      = ((mCanvasWidth - mSheetWidth) / 2) & ~3U;
  mSheetY      = ((mCanvasHeight - mSheetHeight) / 2) & ~3U;
  mSheetWidth  = ALIGN_VALUE (mSheetWidth, 4);
  mSheetHeight = ALIGN_VALUE (mSheetHeight, 4);

  mCanvasFrame = AllocatePool ((UINTN)mCanvasWidth * mCanvasHeight * sizeof (*mCanvasFrame));
  mCanvasBack  = AllocatePool ((UINTN)mCanvasWidth * mCanvasHeight * sizeof (*mCanvasBack));
  mCanvasText  = AllocatePool (mCanvasColumns * mCanvasRows * sizeof (*mCanvasText));
  mCanvasRoles = AllocatePool (mCanvasColumns * mCanvasRows * sizeof (*mCanvasRoles));
  if ((mCanvasFrame == NULL) || (mCanvasBack == NULL) || (mCanvasText == NULL) || (mCanvasRoles == NULL)) {
    CanvasFreeAll ();
    return EFI_OUT_OF_RESOURCES;
  }

  CanvasMakeBackground ();
  CanvasClear (This);

  //
  // The whole screen once: the darkened background around the sheet.
  //
  GuiOutputBlt (
    mCanvasOutput,
    mCanvasBack,
    EfiBltBufferToVideo,
    0,
    0,
    0,
    0,
    mCanvasWidth,
    mCanvasHeight,
    (UINTN)mCanvasWidth * sizeof (*mCanvasBack)
    );

  *Columns = mCanvasColumns;
  *Rows    = mCanvasRows;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
CanvasEnd (
  IN OC_EDITOR_CANVAS  *This
  )
{
  if (mCanvasOutput != NULL) {
    GuiOutputBlt (
      mCanvasOutput,
      &mGuiContext.BackgroundColor.Pixel,
      EfiBltVideoFill,
      0,
      0,
      0,
      0,
      mCanvasWidth,
      mCanvasHeight,
      0
      );
  }

  CanvasFreeAll ();
}

GLOBAL_REMOVE_IF_UNREFERENCED OC_EDITOR_CANVAS  mEditorCanvas = {
  OC_EDITOR_CANVAS_REVISION,
  CanvasBegin,
  CanvasEnd,
  CanvasClear,
  CanvasPut,
  CanvasFlush
};
