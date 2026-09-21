# Security Policy

## Supported versions

Only the latest release receives security fixes.

## Reporting a vulnerability

Please do not open a public issue for security problems. Instead, report them
privately through GitHub's [security advisory](https://docs.github.com/en/code-security/security-advisories)
feature for this repository.

Include:

- a description of the issue and its impact,
- a minimal reproducing `.ccpl` program or command,
- the compiler version (`coolc --version`) and your Windows version.

We will acknowledge reports as quickly as possible and keep you informed of the
fix.

## Scope

CCPL compiles untrusted source and generates C code that is handed to TinyCC. If
you find a way for a `.ccpl` program to escape the intended sandbox (for example,
by injecting arbitrary C through the code generator), that is a vulnerability and
we want to hear about it.
