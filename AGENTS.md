# Public source boundary

- This repository is public. Never copy, sync, commit, or publish the private
  `wifi-gpu/research/` directory or its contents here, including under another name.
- Bring changes from the private repository through reviewed, explicitly selected
  file snapshots. Exclude private research, operational logs, and experimental
  archives. Do not merge or rebase private Git history, mirror the private
  repository, or push its branches or tags to this remote.
- Before pushing, check the staged files and every commit being pushed for private
  research. Removing files from the latest commit does not remove older copies.
- Keep `/research/` in `.gitignore`. Ignore rules do not protect against already
  tracked files, forced additions, merges, or cherry-picks, so review remains
  necessary. Do not bypass the ignore rule to import private material.
