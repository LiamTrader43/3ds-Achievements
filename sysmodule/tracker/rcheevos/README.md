# rcheevos (subset)

The trigger parser and evaluator from [rcheevos](https://github.com/RetroAchievements/rcheevos)
v12.5.0 (commit 1433173), the RetroAchievements runtime library, MIT licensed (see LICENSE).
Only the files needed for `rc_trigger_size` / `rc_parse_trigger` / `rc_evaluate_trigger` are
copied, unchanged. The tracker uses them for definition lines whose condition is a
RetroAchievements condition string (`rc:` prefix, see achievements.c).
