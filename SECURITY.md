# Security and privacy

The plugin queries the locally installed Codex app-server over child-process stdin/stdout.
It does not read `auth.json`, collect account identifiers, open a listening socket, or log raw responses.
Codex itself remains responsible for authentication, network access and its own state/logs.

Local session fallback is disabled by default. Enabling it reads recent session-file tails into memory;
these can contain conversation text, even though only structured quota events are extracted.
No session files, real configuration, credentials, or test logs are part of the repository or release archive.

The configured native `codex.exe` is trusted code, executed with TrafficMonitor's privileges.
Use official software and trusted Codex configuration. Running the host as administrator increases that trust boundary.
No plugin can protect against a compromised host, account, operating system or Codex installation.

The initial publishing review addressed:

- Cross-object and conversation-text matches in the former string-based JSON parser.
- Incorrect assumption that both windows always exist or have fixed positions.
- Working-directory executable lookup and broad child-process handle inheritance.
- Background worker execution during DLL unload and blocking inside DllMain.
- Misleading snapshot update times, unbounded intervals and raw diagnostic output in tests.
- Local user attribution and accidental inclusion of personal configuration or old build outputs.

Synthetic tests cover parsing, configuration, handle isolation, module lifetime and reset-time display.
The optional live test displays only success and quota percentages.
`scripts/check-release.ps1` checks the intended file list and compiled DLL for common credential patterns
and current-machine identifiers. Pattern scans reduce accidental disclosure; they are not proof that
all possible secrets or vulnerabilities have been found.

Public GitHub publication exposes the repository owner's public GitHub identity and repository activity.
Release commits use a project name and a reserved example email instead of the machine's Git identity.
Third-party authors' public copyright attributions are retained.
