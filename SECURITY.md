# Security Policy

## Supported versions

Security fixes are applied to the latest code on the `main` branch and to the
most recent release.

## Reporting a vulnerability

Please **do not open a public issue** for security problems.

Report vulnerabilities privately through GitHub's
[private vulnerability reporting](https://github.com/Olorunnisola01/Local-models/security/advisories/new)
("Security" tab → "Report a vulnerability"). Include:

- a description of the issue and its impact,
- steps to reproduce (project file, input text or server URL if relevant),
- the version or commit you tested.

You can expect an acknowledgement within 7 days. Once a fix is ready, we will
publish an advisory and credit you unless you prefer to stay anonymous.

## Scope notes

- The app connects to remote servers you configure (Fish Audio S2 / remote
  Kokoro over Kaggle or RunPod tunnels). Only point it at servers you control.
- Model files downloaded by the `scripts/fetch_*.ps1` helpers come from their
  upstream sources (Hugging Face, NuGet, GitHub). Issues in those upstream
  projects should be reported to them directly.
