---
status: review 2026-11-06 branch todo/chess-opponent-button-name: the HUD button is the fixed word "Menu" (element Menu, 110 wide), the side variable and SideLabel are removed, the choice is shown only in the panel; build and 697 tests green (ChessUiTest, ChessHudWebTest, control smoke), 3 WPs reviewed, integration APPROVE
---

# Chess HUD: the button that opens the opponent panel is named after a state ("Two players"); it is to be called "Menu"

**Where:** `config/SceneConfig.xml` (the `Opponent` button of the `Hud`, text `{side}`), `src/Chess/Engine/OpponentText.{h,cpp}` (`SideLabel`: "Two players", "You: White", "You: Black"), `src/Chess/Components/ChessBoardComponent.cpp` (publishes `side`), `tests/HudWords.h`, `tests/ChessUiTest.cpp` and `tests/ChessHudWebTest.cpp` (the layout through the size matrix with every word).

**What is missing:** the owner said (2026-11-05, while trying the opponent panel) that the menu button must not be called "2 players". Today the one HUD button that opens the panel shows the current state as its text: "Two players", "You: White" or "You: Black". A person reads that as a mode switch, not as a button that opens a menu, and it hides what it does. **Decided by the owner (2026-11-05): the button is called "Menu".** Where the current choice ("Two players", "You: White", "You: Black") is shown now, if it is still shown, was not said: inside the panel (its Opponent and Play as groups already show it) or, if the person needs it at a glance, in the `Turn` or `Status` label.

**A fix has to cover:**

- the button text "Menu" (a fixed word, no longer the variable `side`), and where the current choice is shown instead, if it is still shown;
- the words in `HudWords.h` and `OpponentText`, the size matrix and the narrowest window with the new words (`CheckContent().fits`, 8 units of air in a button), the HTML + CSS twin in `ChessHudWebTest`;
- the docs: `CLAUDE.md` ("The HUD", "The words of the HUD"), `README.md`.

A small decision left for the owner: whether the state stays visible on the HUD outside the panel (the default if nothing is said: it does not; the panel shows it).
