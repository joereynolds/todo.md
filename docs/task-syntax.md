All tasks should be based off of an extended form of markdown

A task typically looks like this:

```
- [ ] My task
```

The space after the ']' is mandatory.

## Task States

Tasks can be in a few different states

```
- [ ] A task that is still todo
- [-] A cancelled task
- [/] A task that is currently being worked on
- [x] A completed task
```

## Task dates

Tasks can also have date metadata attached to them

```
- [ ] A task with a due date !2026-12-25
- [ ] A task that was created on christmas +2026-12-25
- [ ] A task that was completed on christmas x2026-12-25
- [ ] A task that was cancelled on christmas -2026-12-25
```

## Subtasks

Tasks can have an infinite amount of tasks nested inside them

```
- [ ] Sort the kitchen
  - [ ] Fix the tap
    - [ ] Call a plumber
  - [ ] clean counter tops
  - [ ] take bins out
```

## ETAs

Tasks can have ETAs attached to them to indicate how long it might take.
If you're following the GTD philosophy, anything 2 minutes or under can and should be done immediately

```
- [ ] Wash hands ~2m
- [ ] Watch a movie ~2h
- [ ] Watch a short movie ~90m
- [ ] Cycle across Wales ~7d
- [ ] Cycle the uk ~5w
- [ ] Live and die ~85y
```

m - minutes
h - hours
d - days
w - weeks
y - years
