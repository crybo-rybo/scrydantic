# Security policy

## Supported versions

Only the latest release receives security fixes. Before 1.0, a fix is
released as a new patch version of the latest minor series.

| Version | Supported |
|---|---|
| 0.1.x | Yes |

## Scope

The JSON core (`src/json/`, `scrydantic::json`) parses untrusted input. A
crash, a memory error, unbounded recursion or memory use, or an accepted input
that the parser must reject (`src/json/document.hpp` lists the rules) in that
code is a security issue. The codec and tool binding read their input through
the same core, so the same issue types apply to them.

## Report a vulnerability

Do not open a public issue. Use the repository's
[private vulnerability reporting](https://github.com/crybo-rybo/scrydantic/security/advisories/new)
on GitHub. Include:

- the Scrydantic version or commit,
- the GCC version and the build flags,
- the input or the program that shows the issue, and
- the result that you expected and the result that you got.

You get an acknowledgement in 7 days. A confirmed issue gets a fix, a GitHub
security advisory, and a CHANGELOG.md entry.
