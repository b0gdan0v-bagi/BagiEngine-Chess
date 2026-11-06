# TODO

The list of known problems and ideas is the **todo store**: a service on Railway that holds every entry of the game (area `chess`) and of the engine (area `engine`), its status, the plans and the reviews (`engine/tools/todo/CLAUDE.md`). Nothing of it is in this repository. Vikunja (projects `BagiEngine-Chess` and `BagiEngine`) shows it to a person and takes back what a person changes there; `python3 engine/tools/todo/todo.py dashboard` gives the address of the dashboard.

To record an idea, run `/todo-add <the idea in a sentence or two>`. `/todo-db` lists and shows entries; `todo-loop` works the entries through in their own branches, and `/todo-merge` merges a finished branch into `main` after a report.
