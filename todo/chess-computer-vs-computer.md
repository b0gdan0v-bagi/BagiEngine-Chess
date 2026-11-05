# Chess: two computers can play each other in the window (computer vs computer)

**Where:** `src/Chess/Engine/OpponentController.{h,cpp}` (the seat of ONE program: `GetPlays()` is one color, one engine, one level), `src/Chess/Engine/EngineOpponent.{h,cpp}` (plays one side of a `ChessGameModel`), `src/Chess/Engine/OpponentChoice.{h,cpp}` and `config/SceneConfig.xml` (the `OpponentPanel`, group `ModeGroup`), `src/Chess/ChessGameControl.{h,cpp}` (`opponent.plays` is `white`, `black` or `null`), `tools/stockfish_bridge.py` (today the only way to see a program play a program, from outside the window).

**What is missing:** the window plays a person against one program, or two people. A game of two programs, each with its own engine and strength (for example Stockfish at `max` against the own engine at `easy`), cannot be started or watched in the window. Today `OpponentController` holds one side, one engine and one level, and the model has no notion of two programs.

**A fix has to cover:**

- a third mode in the panel next to "Two players" and "Computer" (the name and place are the owner's call, see `chess-hud-opponent-panel` for how the panel is built on `UISegmented`): both sides played by programs;
- an engine and a level per side (two choices of the existing engine and level groups), and what the person sees while it runs: whose turn, "Computer is thinking", the last move, a way to pause, stop and take over a side or start again;
- a pace between moves (an engine answers in milliseconds at a low level: a game would end before anyone looks), probably a minimum delay between moves as a setting, and a limit for a game that never ends (the 50-move and repetition draws already end it);
- the control API: `opponent` gets a way to say both sides (today `plays` is one color or `null`; keep the old shape working), and `get_state` reports both; the manual clock must stay deterministic with the own engine at a fixed depth (a game of two own engines is the end-to-end test, as `OwnEngineTest` already plays one against itself);
- the click guard: the board takes no clicks while both sides are programs (today `PlaysFor(color)` guards one color);
- tests through the size matrix for any new HUD word, and the controller/choice tests for two seats.
