# MdsScope

MdsScope is a C++/Qt desktop application for browsing WebScope-style shot
configuration files (`*.webscp`) and plotting MDSplus signal data through MDSIP.

It is intended to be a transparent, open-source alternative to older Java 8 based
WebScope-style clients, which can be difficult to maintain on newer Java
runtimes. The application is currently EAST-specific: the bundled signal
configuration, MDSIP defaults, login flow, and HTTP metadata API target EAST.

## Features

- Load WebScope-style environment files from the user configuration directory.
- Display multiple synchronized plotting panels.
- Fetch signal data from MDSIP servers.
- Switch between thin and full data read modes.
- Apply shot numbers globally across panels.
- EAST HTTP metadata support for latest shot and top-bar shot summary.
- Follow the system light/dark preference on Linux, macOS, and Windows, with a
  manual Auto / Light / Dark theme switch.
- About dialog with application, Git, Qt, system, source, and update-check
  information.
- Run on Linux, macOS, and Windows desktop environments.

## Requirements

- CMake 3.16 or newer
- A C++23 compiler
- Qt 6.4 or newer with Core, Widgets, Network, and Concurrent
- Qt 6 DBus on Linux

MdsScope is currently developed and tested for Linux, macOS, and Windows
desktop environments. Windows support targets Windows 10/11 with MSVC 2022 and
Qt 6. Windows 7 is not supported.

### Dependency Installation

Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools
```

macOS:

```bash
xcode-select --install
brew install cmake qt
```

Windows: install Visual Studio 2022 with the C++ desktop workload, CMake, and a
matching Qt 6 MSVC build.

## Build and Install

Prebuilt release packages are published from Git tags:

- Windows portable package: download the Windows zip from the GitHub release
  page, extract it, and run `MdsScope.exe`.
- Ubuntu packages: download the matching `.deb` package for the target Ubuntu
  release and CPU architecture, then install it with your preferred package
  tool.
- macOS: no prebuilt disk image is currently published; build from source.

Linux source build:

```bash
cmake -S . -B build
cmake --build build -j
cmake --install build
MdsScope
```

For non-root Linux source installs, the default prefix is `~/.local`; use
`--prefix` only for a custom location.

Windows source build:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

macOS source build:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
open ./build/MdsScope.app
```

Install the macOS app:

```bash
cmake --install build --prefix "$HOME/Applications" --component app
```

Install the command-line tools to the default prefix (`$HOME/.local` for
non-root users):

```bash
cmake --install build --component tools
```

This gives:

```text
$HOME/.local/bin/MdsScope
$HOME/.local/bin/transfer
```

The `app` component installs `MdsScope.app`; the `tools` component installs the
`MdsScope` and `transfer` command-line executables.

Linux uses `mdsscope.desktop`; macOS uses `MdsScope.app`.

Uninstall a CMake install:

```bash
cmake --build build --target uninstall
```

Remove a user-installed macOS app bundle:

```bash
rm -rf "$HOME/Applications/MdsScope.app"
```

## Configuration

### Environment Files

Installed templates and signal indexes are read from the application resources:

```text
Linux source install: ~/.local/share/mdsscope/
Linux package install: /usr/share/mdsscope/
macOS app bundle:     MdsScope.app/Contents/Resources/
Windows portable:     extracted package directory
```

These locations contain:

```text
environment/
source_index/
```

User-editable configuration is stored separately:

```text
Linux: ~/.config/mdsscope/
macOS: ~/Library/Application Support/mdsscope/
Windows: %LOCALAPPDATA%\MdsScope\
```

Cache data is stored separately:

```text
Linux: ~/.cache/mdsscope/
macOS: ~/Library/Caches/mdsscope/
Windows: %LOCALAPPDATA%\MdsScope\cache\
```

The placeholders `<config>` and `<cache>` below refer to these platform-specific
directories.

`auth.cache` is not plaintext; it stores the username, password, and token in an
encrypted local cache bound to the current machine/user.

On startup, MdsScope seeds and updates the user cache at
`<cache>/source_index/` from the installed index while preserving locally
discovered entries. If a user-entered tree/signal is not already in the cache,
MdsScope adds it only after that signal has been read successfully. Failed reads
are treated as invalid input and are not cached.

The Data Source Setup dialog uses this cache for case-insensitive tree and
signal completion. Signal candidates are scoped to the tree entered on the same
row.

On first run, MdsScope creates `<config>/environment/` and copies template
`*.toml` and `*.webscp` files from the source tree or installed application
resources when the user environment directory is empty.

MdsScope supports two configuration formats:

- `*.toml`: the recommended native MdsScope format
- `*.webscp`: legacy WebScope-compatible format

On startup, MdsScope loads `<config>/environment/init.toml` first. If that file
is missing, it tries `init.webscp`. If neither default file exists, the first
`*.toml` or `*.webscp` file sorted by name is loaded.

When saving from the GUI, MdsScope writes both formats with the same base name:

- saving `example.toml` also writes `example.webscp`
- saving `example.webscp` also writes `example.toml`

This keeps the native format and the legacy compatibility format synchronized.
The Open dialog remembers the last selected file filter, so users can choose to
browse native TOML files, legacy WEBSCP files, or both.

Example TOML:

```toml
version = 1

[[panels]]
column = 1
row = 1
title = "Plasma current"
x_label = "s"
y_label = "kA"

[[panels.signals]]
tree = "pcs_east"
server = "202.127.204.12"
y = "\\pcrl01"
full = true
```

The native TOML format intentionally does not store a normal panel shot number.
Panels use the current shot shown in the MdsScope UI. The `shot` field under
`[[panels.signals]]` is only for an explicit per-signal shot override, for
example when comparing one signal against another shot.

Signals can be hidden with `hidden = true`. Hidden signals stay in the panel
configuration, but MdsScope skips data loading, plotting, point readouts,
axis-range calculation, export, and benchmark work for those curves. Legacy
`.webscp` files do not carry this setting; converted signals default to
`hidden = false`.

Signals default to thin data loading. Add `full = true` under an individual
`[[panels.signals]]` entry to load that curve in full mode while the rest of the
configuration stays thin. Legacy `.webscp` files do not carry this setting;
converting TOML to `.webscp` intentionally omits it.

When MdsScope saves TOML, it omits default values to keep files compact. Omitted
panel defaults are empty labels, `extraction_points = 2000`, `grid = true`, and
automatic axis ranges. Omitted signal defaults are an empty `x`, automatic
series color, `hidden = false`, and thin data loading.

When converting from legacy `.webscp`, MdsScope treats the most common
`shot_txt` value as the old default shot and omits it from TOML. If a panel or
signal clearly uses a different shot, that value is preserved as a signal-level
`shot` override.

### Converting Legacy `.webscp` Files

Convert one file:

```bash
./build/transfer "<config>/environment/init.webscp"
```

Convert all `.webscp` files in one directory:

```bash
./build/transfer "<config>/environment"
```

Write converted TOML files to another directory:

```bash
./build/transfer --out-dir converted "<config>/environment"
```

Convert recursively:

```bash
./build/transfer --recursive "<config>/environment"
```

### EAST HTTP Metadata API

The EAST HTTP metadata endpoint is stored in the repository `APIurl` file. The
file is installed with the application resources and contains a comment citing
the public reference for the EAST data-service workflow.

On startup, MdsScope reads `APIurl`, checks the local encrypted auth cache, and
opens the login dialog before the main window if a valid token is not available.
After successful login, MdsScope caches the returned token and the entered
credentials in `<cache>/auth.cache`. Later starts reuse the cached token, or
refresh it automatically with cached credentials when it expires.

The HTTP API is used for latest shot lookup and the top-bar `Ip`, `Pulse`, `It`,
and `Time` summary. Plot data continues to come from EAST MDSIP.

### Data Export

Data export writes files under an `output/` subdirectory of the selected base
directory. The default export locations are:

```text
Linux:   ~/Downloads/mdsscope/output/
macOS:   ~/Downloads/mdsscope/output/
Windows: %USERPROFILE%\Downloads\mdsscope\output\  (usual location)
```

## Benchmark Mode

MdsScope includes a simple benchmark mode for MDS data loading:

```bash
./build/MdsScope --benchmark environment/your_config.webscp --summary
```

Useful options:

```text
--shot SHOT_NUMBER
--full
--repeat N
--summary
--prewarm
```

For headless environments:

```bash
QT_QPA_PLATFORM=offscreen ./build/MdsScope --benchmark environment/your_config.webscp --summary
```

## Repository Hygiene

Do not commit:

- access tokens
- local auth caches
- build artifacts

The EAST API URL is intentionally stored in `APIurl`; do not commit personal
tokens or cache files.

## Disclaimer

This software is provided for research, engineering, and data visualization
workflows. It is provided "as is", without warranty of any kind, express or
implied, including but not limited to warranties of correctness, reliability,
fitness for a particular purpose, or non-infringement.

The authors and contributors are not responsible for incorrect analysis,
operational decisions, data loss, service disruption, unauthorized access, or
any other direct or indirect consequences arising from use of this software.

Users are responsible for ensuring they have permission to access any MDSIP
servers, HTTP APIs, shot data, configuration files, and tokens used with this
application. Do not publish private endpoints, tokens, or restricted
experimental data.

## References

The original EAST web-based MDSplus/WebScope workflow is described in:

Yang, F.; Xiao, B. J. A web based MDSplus data analysis and visualization
system for EAST. Fusion Engineering and Design 2012, 87(12), 2161-2165.

https://doi.org/10.1016/j.fusengdes.2012.09.015

## License

Copyright (C) 2026 Weikang Wang.

MdsScope is licensed under
[GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html). See `COPYING`
for the full GNU General Public License version 3 text.
