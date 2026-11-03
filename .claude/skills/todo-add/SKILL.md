---
name: todo-add
description: Quickly records a new idea or known problem as an entry file in todo/ (checks for a duplicate, picks an id, writes the entry in the project format, validates it). Use when the user asks to add a todo, record this idea, note a problem for later, or says "заведи запись в todo".
---

# todo-add (BagiEngine-Chess)

`/todo-add <the idea in one or two sentences>`. Goal: a correct entry file in `todo/` with as little research as possible, so ideas are not lost and `todo-loop` can pick them up. The format is `todo/README.md`.

## Steps

1. **No idea text:** ask for it in one line and do nothing else.
2. **Duplicate check.** A short search over `todo/*.md` (titles and the `**What is missing:**` paragraphs) for the key words of the idea. If an entry about the same thing exists, say which one and offer to add to it instead; do not create a new entry without the user's consent.
3. **File name.** A short English kebab-case id in the style of the existing ones (`no-ci`, `prefabs`, `render-output-size`), at most about 40 characters, no `.md` in the id. Never overwrite an existing file: add `-2`. The file name is the id in the tracker, so it is not renamed later.
4. **Content.** `# Title` (English, even if the idea was given in Russian), then:
   - `**Where:**` only if one or two quick `grep`s show the place in the code; do not invent it;
   - `**What is missing:**` (required: the dashboard and the tracker take the summary from it);
   - `**A fix has to cover:**` if the idea gives something to list.

   No front matter: the entry is `open` with no stage. Add `stage:` only when `todo/roadmap.md` makes the stage obvious; otherwise leave it out and say so in the reply.
5. **Validate.** `engine/.venv/bin/python engine/tools/todo_sync/todo_sync.py check` (on Windows `engine\.venv\Scripts\python`). On a format error, fix your own file.
6. **Reply** in a few lines: the file path, the id, the title. Do not commit. Do not run `push` or `pull`; if the user wants the task in the tracker right away, point them to the `todo-sync` skill. Do not delete entries or edit other people's entries.

A vague idea is still recorded as written; `todo-loop` skips what it cannot work on with the reason "needs detail", so do not make the entry up to look complete.
