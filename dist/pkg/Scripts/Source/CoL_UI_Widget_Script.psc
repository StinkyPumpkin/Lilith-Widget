Scriptname CoL_UI_Widget_Script extends Quest
; --Claude: iWant-free replacement for Children of Lilith 5.2.0's energy meter script.
; Keeps every public function the rest of CoL calls (Initialize/Uninitialize from the
; player quest, UpdateColor from the drain handler, UpdateMeter from the MCM) as no-ops.
; The only state kept is drainCode, which LilithWidget.dll reads straight off this
; script object to colour its bar (0 = not draining, 1 = draining, 2 = drain to death).

CoL_PlayerSuccubusQuestScript Property CoL Auto
CoL_ConfigHandler_Script Property configHandler Auto
CoL_Mechanic_EnergyHandler_Script Property energyHandler Auto

int drainCode = 0

Function Initialize()
    drainCode = 0
EndFunction

Function CreateMeter()
EndFunction

Function Log(string msg)
EndFunction

Function Maintenance()
EndFunction

Function UpdateMeter()
EndFunction

Function MoveEnergyMeter()
EndFunction

Function UpdateFill(float newEnergy, float maxEnergy)
EndFunction

Function UpdateFillDirection()
EndFunction

Function UpdateColor(int newDrainCode = -1)
    if newDrainCode >= 0
        drainCode = newDrainCode
    endif
EndFunction

int[] Function GetColor(int newDrainCode = -1)
    int[] color = new int[6]
    if drainCode == 2
        color[0] = 255
    endif
    return color
EndFunction

Function ShowMeter()
EndFunction

Function Uninitialize()
    drainCode = 0
EndFunction
