# Security policy

Report credential exposure, arbitrary code execution, or similar findings through GitHub private vulnerability reporting when enabled. For ordinary bugs, open a public issue with the Windows version, Codex version, reproduction steps, and observed behavior.

Never attach `.codex/auth.json`, access tokens, or other credentials.

Codex Quota Orb does not read `.codex/auth.json` and has no direct HTTP client. It gets rate-limit data from the locally installed Codex app-server in read-only mode over standard-input/output pipes.
