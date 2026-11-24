# Security Policy

## Supported versions

Only the latest release receives security fixes.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it privately
through GitHub's security advisory feature for this repository.

Include:

* a description of the issue and its impact,
* a minimal reproducing program or command,
* the compiler version from `coolc --version` and your Windows version.

We will acknowledge reports as quickly as possible and keep you informed about
the fix.

## Scope

CCPL compiles source into native programs. It is not a sandbox:

* A program can call `malloc`, write through pointers, and call foreign
  functions. Native memory errors are the programmer's responsibility.
* A foreign declaration can load any DLL and call any symbol it names. This is
  intentional and is equivalent to running native code.
* Python declarations run code inside an embedded Python interpreter with the
  same permissions as the process.
* `clm install` downloads archives and places them in the compiler directory.
  Install packages from sources you trust.

The security boundary that matters for the compiler is code generation: a
`.ccpl` source file must not be able to inject arbitrary C into the generated
output, escape string literals, or write files by itself. If you find a way for
source text to break out of the generated C, that is a vulnerability and we want
to hear about it.

Reports about a malicious package in a public registry, or about a program that
intentionally calls dangerous native code, are outside this boundary.