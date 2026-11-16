# Security policy

Security fixes target the current `main` branch and the latest published version.
Older versions may require upgrading. This is an experimental recovery engine;
validate behavior on synthetic data before processing sensitive inputs.

## Report a vulnerability privately

Use [GitHub private vulnerability reporting](https://github.com/securecodelabs/wifi-gpu-public/security/advisories/new).
Include the affected version/commit, platform/backend, impact, and a minimal
synthetic reproduction. Do not post exploit details, captures, credentials, or
recovered passwords in public issues. If the private report form is unavailable,
open a public issue requesting a private contact without including sensitive details.

## Handling sensitive data

Results, sessions, wordlists, captures, and activity logs may contain secrets.
Keep them outside the repository and restrict file access. Session checksums
are not authentication. Do not trust inputs or binaries solely because a checksum
is present; verify provenance and run the available tests.

Public CI uses ephemeral GitHub-hosted runners with minimal token permissions.
Do not attach personal self-hosted runners to this public repository. Test GPU
changes separately on trusted hardware after review.
