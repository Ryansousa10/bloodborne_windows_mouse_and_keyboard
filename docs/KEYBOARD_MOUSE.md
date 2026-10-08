# Keyboard and mouse

**English** · [Português](KEYBOARD_MOUSE.pt-BR.md)

bbport plays with keyboard and mouse as a PC game does: the Dark Souls III key layout, and a
mouse camera that turns by exact angles inside the game's own camera code. Keyboard, mouse and
a controller work at the same time.

## Default keys

| Action | Key |
|---|---|
| Move | W A S D |
| Walk (hold) | Left Alt |
| Camera | Mouse (I J K L on the keyboard as well) |
| Dodge / dash (hold) | Space |
| Jump | Space again while dashing (as in Dark Souls III), or C |
| Lock on / reset camera | Q or wheel click |
| Attack / strong attack (R1 / R2) | Left click / Shift + left click |
| Transform weapon (L1) | Right click |
| Firearm (L2) | Shift + right click or Left Ctrl |
| Interact | E |
| Use quick item | R |
| Blood vial | F |
| Switch quick item / d-pad up | ↓ / ↑ or wheel down / up |
| Switch left / right hand weapon | ← / → or Shift + wheel |
| Game menu (Options) | Tab |
| Gestures (touchpad) | G |
| Confirm / back in menus | Enter / Esc |
| Port settings menu | Insert |

Bloodborne uses one button (Circle) for both *back* and *dodge*, and the port cannot tell when
a menu is open, so Esc also backsteps outside the menus. The game menu is therefore on Tab.

## Controls page

The launcher's **Controls** page rebinds every action: click a field, then press a key or a mouse
button; hold Shift, Ctrl or Alt for a combination (as Shift + left click). Each action takes up to
three inputs, and ⚠ marks an input that more than one action uses. **Restore defaults** brings the
layout above back. Settings are saved in `keybinds.ini` next to `bbport.ini` when you press PLAY;
the game reads them at start. The file can also be edited by hand; deleting it restores the
defaults.

| Setting | `keybinds.ini` | Default |
|---|---|---|
| The mouse turns the camera | `mouse_camera` | on |
| Mouse sensitivity | `mouse_sensitivity` | 1.0 |
| Vertical sensitivity (× horizontal) | `mouse_sensitivity_y` | 1.0 |
| Invert horizontal / vertical | `mouse_invert_x` / `mouse_invert_y` | off |
| No camera auto-rotation while moving | `mouse_no_auto_rotation` | on |
| Walking speed | `walk_tilt` | 0.4 |
| Jump with the dodge key while dashing | `ds3_jump` | on |

**Sensitivity** uses the scale of Source games such as Deadlock and Counter-Strike: one mouse
count turns the camera by 0.022° × sensitivity. Copy the `sensitivity` value of those games and
the camera turns as far per centimetre of mouse movement.

## The mouse camera

A mouse fed to the game as a right stick never feels like a PC game: Bloodborne accelerates the
camera while the stick is held, levels its height while it turns, turns it after the character
while walking, and draws it from a position that follows the ideal one with a spring. So the
port turns the camera inside the game's own camera code instead (`src/runtime_camhook.c`):

- The camera object's update (image `0x143a000..0x1440200` of version 1.09) was found with
  hardware write watchpoints on the camera's angles and read with a disassembler. All its paths
  for a free camera meet at `image+0x143ce67`, after the frame's angles are written (pitch
  `+0x140`, yaw `+0x144`, radians) and before the camera is built from them.
- A jump there runs a small stub that calls the port on the game's camera thread when the mouse
  moved. It adds the mouse turn to the yaw and to the current and target pitch (`+0x150`), inside
  the game's own pitch limits, and turns the drawn position (`+0x100`) around the pivot (`+0xd0`)
  by the same angles, so the camera's spring has nothing to catch up with.
- The camera's yaw writes that turn it after the moving character are nopped (the community patch
  *Disable Camera Auto Rotation via Movement* by Imedved and Kyo), unless
  `mouse_no_auto_rotation = 0`.
- Mouse motion goes from the window thread straight to the stub, without waiting for the game's
  next pad read.

Measured in the game: a 0.30 rad turn is drawn complete, to four decimals, in the next frame
(before, as a stick: 38% after one frame and 92% after 100 ms). The stick, lock-on, collisions
and the rest of the camera stay the game's.

The hook checks the game code before it changes anything. On another game version, or where the
hook is not available yet (Linux), the mouse falls back to acting as the right stick, and a notice
says so in the game.

## Files

| File | What |
|---|---|
| `src/runtime_pad.c` | Keyboard and mouse bindings, `keybinds.ini`, the stick fallback |
| `src/runtime_camhook.c` | The camera hook (Windows) |
| `gpu/shim/window.cpp`, `gpu/shim/bbgpu.cpp` | Relative mouse mode, mouse motion to the hook |
| `gpu/shim/bbport_overlay.cpp` | On-screen notices |
| `launcher/bbport_controls.py` | The launcher's Controls page |
| `launcher/bbport_lang.py` | Its texts in the launcher's languages |
| `tests/test_pad.c` | Pad, keyboard and mouse test |
