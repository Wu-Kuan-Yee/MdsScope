# MdsScope

MdsScope is a C++/Qt desktop application for browsing WebScope-style shot
configuration files (`*.webscp`) and plotting MDSplus signal data through MDSIP.

It is intended to be a transparent, open-source alternative to older Java 8 based
WebScope-style clients, which can be difficult to maintain on newer Java
runtimes. The application is currently EAST-specific: the bundled signal
configuration, MDSIP defaults, login flow, and HTTP metadata API target EAST.

## Features

- Load TOML and WebScope-style `.webscp` environment files.
- Plot multiple synchronized MDSplus signal panels.
- Switch between thin and full data read modes.
- Apply single-shot or batch-shot expressions globally.
- Use EAST HTTP metadata for latest shot and top-bar shot summary.
- Run on Linux, macOS, and Windows.

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
- Ubuntu packages: download and install the matching `.deb` package.
- macOS: build from source.

Linux source build:

```bash
cmake -S . -B build
cmake --build build -j
cmake --install build
MdsScope
```

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

Optional install commands:

```bash
cmake --install build --prefix "$HOME/Applications" --component app
cmake --install build --component tools
cmake --build build --target uninstall
```

For non-root source installs, the default command-line prefix is `~/.local`; use
`--prefix` only for a custom location.

## Configuration

Installed templates and signal indexes are read from the application resources:

```text
Linux source install: ~/.local/share/mdsscope/
Linux package install: /usr/share/mdsscope/
macOS app bundle:     MdsScope.app/Contents/Resources/
Windows portable:     extracted package directory
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

`auth.cache` is not plaintext; it stores the username, password, and token in an
encrypted local cache bound to the current machine/user.

MdsScope supports two environment file formats:

- `*.toml`: the recommended native MdsScope format
- `*.webscp`: legacy WebScope-compatible format

On first run, MdsScope creates the user environment directory and copies bundled
templates if it is empty. The default file is `init.toml`, with `init.webscp` as
a legacy fallback. Saving from the GUI keeps TOML and WEBSCP files synchronized.
The Open dialog can browse native TOML files, legacy WEBSCP files, or both.

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

Panels use the shot shown in the MdsScope UI. A signal-level `shot` value is
only needed for explicit per-signal shot overrides, such as shot comparison.

Shot fields and the bottom-bar Shot input support single shots, semicolon lists,
ranges, and mixed expressions:

```text
143850
143850;143851;143853
143850-143858
143850-143858;143865
```

Shot expressions are expanded at runtime so saved configuration files stay
compact.

Signals can be hidden with `hidden = true`. Use `full = true` on an individual
signal to load that curve in full mode while the rest of the configuration
stays thin.

Convert legacy `.webscp` files with `transfer`:

```bash
./build/transfer "<config>/environment/init.webscp"
./build/transfer "<config>/environment"
./build/transfer --recursive "<config>/environment"
```

Windows portable package examples:

```powershell
.\transfer.exe ".\environment\init.webscp"
.\transfer.exe ".\environment"
.\transfer.exe --recursive ".\environment"
.\transfer.exe --recursive --out-dir ".\converted" ".\environment"
```

Windows source build examples:

```powershell
.\build\Release\transfer.exe ".\environment\init.webscp"
.\build\Release\transfer.exe --recursive --out-dir ".\converted" ".\environment"
```

### EAST HTTP Metadata API

The EAST HTTP metadata endpoint is stored in `APIurl`. It is used for latest
shot lookup and the top-bar `Ip`, `Pulse`, `It`, and `Time` summary. Plot data
continues to come from EAST MDSIP.

### Data Export

Data export writes files under an `output/` subdirectory of the selected base
directory. Export supports text, CSV, TSV, and JSON formats, plus optional
x-range selection. The default export locations are:

```text
Linux:   ~/Downloads/mdsscope/output/
macOS:   ~/Downloads/mdsscope/output/
Windows: %USERPROFILE%\Downloads\mdsscope\output\  (usual location)
```

## Benchmark Mode

MdsScope includes a simple benchmark mode for MDS data loading. Useful options
include `--shot`, `--full`, `--repeat`, `--summary`, and `--prewarm`.

```bash
./build/MdsScope --benchmark environment/your_config.webscp --summary
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
