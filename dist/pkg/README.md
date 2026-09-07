# Lilith Widget

SKSE energy / level bar for **Children of Lilith – Succubus of Skyrim** (5.2.0), with iWant Widgets removed from the game entirely.

- Energy bar (current / max) coloured by drain state: lilac idle, pink draining, red drain-to-death. Breathes when energy is under 20%.
- Succubus level and XP-to-next-level strip.
- Shows only while the player is a succubus. Hides in menus, loading screens, vanity camera, and when the compass is hidden (iHUD, Sandbox When Idle).
- Hide-toggle hotkey. Position, size, opacity, text size in the SKSE Menu Framework settings page ("Lilith Widget"), saved to `Data/SKSE/Plugins/LilithWidget/config.json`.
- Art in `Data/Interface/HUDWidgets/lilith/`, two styles, both optional:
  - **Crop**: `bar_bg.dds`, `bar_fill.dds`, `bar_frame.dds`. One fill image, cropped from the left by energy percent.
  - **Stages**: `energy0.dds` … `energy8.dds`, one complete bar image per level (like the arousal widget's aroused0-8). Shipped, so this is the default look; switch to crop in settings if you prefer it.
  - `icon.dds` sits left of the bar in either style. The fill is tinted by drain state, so keep it light. Works with no art at all (flat shapes). Editable PNG/DDS templates for both styles are in the repo's `art/` folder.
- No Papyrus, no mod events, no iWant Widgets. Reads CoL's energy straight off its script object and the level globals from the plugin.

## What the package contains

| File | Purpose |
|---|---|
| `SKSE/Plugins/LilithWidget.dll` | The widget. SE 1.5.97 + AE 1.6.x (Address Library). |
| `ChildrenOfLilith.esp` | CoL 5.2.0 with the `iWant Widgets.esl` master removed. Only change: the iWidgets script property on the widget quest and the old widget MCM page are gone. Same FormIDs. |
| `Scripts/CoL_UI_Widget_Script.pex` | iWant-free replacement for CoL's meter script. Keeps every function the rest of CoL calls as no-ops and stores the drain state for the DLL. |
| Patches (FOMOD optional) | CoL's four bundled patches (A Skyrim Kiss, FlowerGirls, Immersive Lap Sitting, Vancian/Ordinator) re-mastered without iWant Widgets. Pick the ones you already use. |

Install AFTER Children of Lilith so the ESP and script override. Then remove iWant Widgets and iWant Widgets.esl.

Existing saves: fine. The old "Widgets" MCM page stays in saves that already had it (Papyrus keeps bound scripts), it just does nothing.

## Requirements

- Children of Lilith 5.2.0 and its own requirements (minus iWant Widgets)
- SKSE, Address Library, SKSE Menu Framework

## Build

`build.bat` (VS 2022 BuildTools, Ninja, vcpkg with the Monitor221hz registry for commonlibsse-ng). Deploys to `%SKYRIM_MODS_FOLDER%\Lilith Widget--Claude`.

`tools/StripIWant` (dotnet 8 + Mutagen) rewrites the CoL plugins without the iWant master:

```
dotnet run -c Release -- <CoL unpacked dir> <out dir>
```

`papyrus/` holds the stub script and the minimal headers it compiles against (Caprica).

## Data path

| Value | Source |
|---|---|
| energy, energy max | `playerEnergyCurrent_var` / `playerEnergyMax_var` on `CoL_Mechanic_EnergyHandler_Script`, bound to quest `CoL_PlayerSuccubusQuest` (0x000D64) |
| drain state | `drainCode` on the replacement `CoL_UI_Widget_Script` (quest 0x341362) |
| is succubus | GlobalVariable `CoL_IsPlayerSuccubus` (0x000D63) |
| level, progress | GlobalVariables `CoL_playerSuccubusLevel` (0x13B614), `CoL_playerSuccubusLevelRatio` (0x282A20) |

Children of Lilith is MIT + Commons Clause (Phalanx); the redistributed plugins carry its license file.
