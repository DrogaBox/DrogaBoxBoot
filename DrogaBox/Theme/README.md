# AETHER, the default theme of DrogaBoxBoot

The zip installs it as `EFI/OC/Resources/Image/DrogaBox/AETHER`. With `Misc > Boot > PickerVariant` set to
`Auto` (the default), OpenCanopy uses it whenever it is there. To pick it by name, set
`PickerVariant` to `DrogaBox\AETHER`.

- The icons are those of the GoldenGate theme of
  [acidanthera/OcBinaryData](https://github.com/acidanthera/OcBinaryData), in the style of the startup
  screen of a Mac: disks, Recovery, Time Machine, Windows, tools, buttons, cursor. macOS entries show the
  disk icon, or the volume's own icon with `PickerAttributes` bit `0x0001`, as on a Mac.
- `Background.icns` is DrogaBox's: a dark field with soft red and blue glows, the same in 1x and 2x
  (1920x1080). It has no text in it, so nothing shows through behind the entries.

Every icon has a 1x and a 2x image (`ic07` and `ic13`). On screens of about 1080p, OpenCanopy draws the 2x
images and shrinks them to 1.5x (see `OC_ATTR_NO_ZOOM` in `PickerAttributes`).

OpenCanopy also needs the font in `Resources/Font` (`Font_1x` and `Font_2x`) and, for VoiceOver, the
files in `Resources/Audio`. Both come from
[OcBinaryData](https://github.com/acidanthera/OcBinaryData), as with stock OpenCore.
