# clm, the CCPL package manager

`clm` (Cool Library Manager) installs packages for `get`. It reads a static
registry index, downloads package archives, and places them in the compiler's
`packages` directory.

The default registry is:

```text
https://cdn.jsdelivr.net/gh/ccpl-lang/ccpl-packages@main
```

Set `CLM_REGISTRY` to use another registry. A `file://` URL works for local
testing.

> Package archives are code and data that land in your compiler directory.
> Install only packages that you trust.

## Contents

* [Commands](#commands)
* [Version selection](#version-selection)
* [Install locations](#install-locations)
* [Downloads and archives](#downloads-and-archives)
* [Authentication](#authentication)
* [Publishing](#publishing)
* [Package manifest](#package-manifest)
* [Limits](#limits)

## Commands

```text
clm search <query>
clm install <name>[@<version>]
clm update [<name>]
clm remove <name>
clm list
clm login
clm whoami
clm logout
clm publish <dir> [--submit <url>]
clm --version
clm --help
```

`search` matches the query against package names and descriptions. `install`
also installs dependencies listed under `deps` in the package `clm.json`.
`update` with no name checks every installed package, and with a name checks
only that package. `remove` deletes the package directory and its entry in
`installed.json`; it does not remove dependencies that other packages may use.

## Version selection

A version may be exact (`1.2.3`), a caret range (`^1.2.3`), a tilde range
(`~1.2.3` or `~~1.2.3`), or a comparison (`>=1.2.0`, `<2.0.0`).

Comparison uses numeric major, minor, and patch parts. `1.10.0` is newer than
`1.9.0`, and a caret range keeps the same major version. `~~` keeps the same
major version, while a single `~` keeps the same minor version.

## Install locations

Packages go to `<compiler dir>\packages\<name>`. `get` looks there and in
`<compiler dir>\..\packages`. The compiler directory is the directory that
contains `coolc.exe` or `clm.exe`.

`installed.json` in the `packages` directory records the installed versions.

## Downloads and archives

`clm` uses the Windows `curl.exe` and `tar.exe` tools. A package URL has this
shape:

```text
<registry>/packages/<name>/<name>-<version>.zip
```

The archive must contain a top level directory with the package name, or a
single top level directory that can be renamed to the package name. Archive
entries with `..`, absolute paths, or drive letters are rejected before
extraction.

Package and version names are limited to ASCII letters, digits, dot, underscore,
and hyphen, and may not start with a dot or contain `..`. This prevents path
traversal and keeps network URLs well formed.

## Authentication

`clm login` uses the GitHub device flow. It prints a one time code, opens
`https://github.com/login/device`, and stores the token in
`%USERPROFILE%\.clm\auth.json`.

`clm whoami` prints the linked account and refreshes an expired access token
when a refresh token is available. `clm logout` clears the stored file.

When `CLM_CLIENT_SECRET` is set, `clm login` uses the browser callback flow with
PKCE instead. The device flow needs no secret and is the normal path.

## Publishing

`clm publish <dir>` reads `clm.json` from the directory, validates the name and
version, copies the tree into a staging directory, and creates
`<name>-<version>.zip` in the current directory.

With `--submit <url>` and a linked account, it sends the archive to that URL as
a multipart upload with a bearer token. If the upload fails, the local archive
is left in place.

Without `--submit`, submit the archive by opening a pull request against the
registry repository and updating its `index.json`.

## Package manifest

A package needs a `clm.json` at its root:

```json
{
  "name": "mymath",
  "version": "1.0.0",
  "description": "small math helpers",
  "deps": {
    "other": "^1.0.0"
  }
}
```

`name` and `version` are required. `deps` is optional.

## Limits

* There is no lockfile.
* Dependency conflicts are resolved by installing the newest version that
  satisfies each request, one at a time.
* Removing a package does not remove its dependencies.
* Registry JSON is read by a small parser, not a full JSON library.
* Only Windows is supported, because the tool relies on `LoadLibrary`,
  `curl.exe`, and `tar.exe`.