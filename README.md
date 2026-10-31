# Todo.md

A GUI frontend to interface with markdown tasks.

<img width="1273" height="614" alt="image" src="https://github.com/user-attachments/assets/6c5c0fcd-daff-4826-a24d-059e737b5ad3" />

<img width="1271" height="427" alt="image" src="https://github.com/user-attachments/assets/c742629e-5e7e-49b6-8e8b-9e1c26548c8c" />

## Installing

Download the latest AppImage from [releases](https://github.com/joereynolds/todo.md/releases)

### Nix

Try before you buy

```
nix run github:joereynolds/todo.md
```

Or install

```
nix profile install github:joereynolds/todo.md
```

## How it works

Point it at a directory and it'll slurp up all `.md` recursively in that
directory.

It'll find any tasks that follow the Github flavoured markdown task format
(with a few extensions by yours truly).

If you're interested on the markdown internals read on...

### Markdown representation 

All tasks are represented with markdown. The task syntax is how Github treats
it and the rest is matched as closely as possible with how Obsidian treats
tasks so that I follow some sort of standard. I've deviated from the standards
where the standards use emojis because I'm not 12 years old.

A few examples

```
- [ ] An incomplete task
- [ ] An incomplete task that is due on christmas !2026-12-25
- [-] A cancelled task
- [x] A completed task
- [ ] A task that can be done in roughly 5 minutes ~5m
- [/] A task that's in progress
- [ ] A task created on halloween +2026-10-31
- [x] A task completed on bonfire night x2026-11-05
- [-] A task cancelled on boxing day -2026-12-26
- [ ] A bigger job ~3d
- [ ] An even bigger job ~2w
```

It also supports subtasks. Subtasks are just arbitrarily nested tasks.

```
- [ ] Sort the kitchen
  - [x] Fix the tap
    - [x] Call a plumber
  - [ ] Clean counter tops ~15m
  - [ ] Take bins out ~2m
```

You don't really need to know any of this if you're managing it through the UI
but if you want to edit them by hand, the above is useful.

Enjoy!
