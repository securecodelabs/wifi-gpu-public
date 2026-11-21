# Code quality before release

Run the recovery complexity check before pushing:

```sh
python -m pip install --only-binary=:all: --require-hashes -r .github/requirements-complexity.txt
python scripts/check-complexity.py
```

The pinned, checksum-verified clang-tidy checker applies a cognitive-complexity
limit of 25 to every function in `src/recovery.cpp`, including option parsing,
checkpointing and verification helpers. It needs a C++17 compiler's standard
headers but no GPU, kernel compilation or full project build. On Windows,
install the Visual Studio C++ build tools. This focused check covers recovery
complexity, not every static-analysis rule or every source file.

The `Recovery complexity` CI job runs the same local checker alongside the
native builds, tests, sanitizer and security checks. Public jobs run on
GitHub-hosted Linux and Windows runners. Release builds and publication depend
on those checks through reusable CI. No hosted code-analysis subscription or
SonarCloud result is required.

The recovery coordinator keeps candidate preparation speculative and advances
the durable offset only after all SSIDs in a batch have been verified. Producer
and checkpoint workers are joined before their storage or file locks are
destroyed. Regression tests cover multiple SSIDs with different hit positions,
partial recovery/resume, output ordering and tested-candidate counts.
