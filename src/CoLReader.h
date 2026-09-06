#pragma once

// Reads Children of Lilith's player state straight from the game — no iWant
// Widgets, no mod events, no Papyrus calls:
//   energy / energyMax  -> script variables on the energy handler script bound
//                          to CoL_PlayerSuccubusQuest (CoL_Mechanic_EnergyHandler_Script)
//   drainCode           -> script variable on our iWant-free CoL_UI_Widget_Script stub
//                          (0 not draining, 1 draining, 2 drain to death); 0 if the
//                          original script is still installed
//   level / levelRatio / isSuccubus -> GlobalVariables in ChildrenOfLilith.esp
//
// Refresh() touches the Papyrus VM (attached-scripts map under its spin lock) and
// is only ever called from the HUD render callback, i.e. the game's main thread.
namespace CoLReader {

    struct State {
        bool  available  = false;   // ChildrenOfLilith.esp present + forms resolved
        bool  succubus   = false;   // CoL_IsPlayerSuccubus == 1
        bool  energyOk   = false;   // energy handler script found this read
        float energy     = 0.0f;
        float energyMax  = 100.0f;
        int   level      = 0;
        float levelRatio = 0.0f;    // 0..1 progress to next succubus level
        int   drainCode  = 0;
    };

    void  Detect();                 // kDataLoaded: resolve forms
    void  Invalidate();             // kPostLoadGame / kNewGame: force a fresh read
    void  Refresh(int cadenceMs);   // main thread; throttled
    State Get();
}
