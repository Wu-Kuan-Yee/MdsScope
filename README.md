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
- Three sampling quality modes: **thin** (fast preview), **medium** (high-resolution), and **full** (complete data).
- Apply single-shot or batch-shot expressions globally.
- Use EAST HTTP metadata for latest shot and top-bar shot summary.
- Run on Linux, macOS, and Windows.

### Sampling Quality Modes

MdsScope offers three data sampling modes to balance speed and precision:

- **Thin** (default): Fast preview mode for quick trend visualization.

- **Medium**: Higher-resolution sampled mode using real measured values. Recommended when spike amplitude matters.

- **Full**: Reads all raw data with no sampling. Use for detailed analysis of specific time ranges.

The mode can be set globally via the top toolbar dropdown, or overridden per signal in the source setup dialog. When zoomed to small time ranges, the application automatically uses full precision regardless of the selected mode.

## Requirements

- CMake 3.16 or newer
- A C++23 compiler
- Qt 6.4 or newer with Core, Widgets, Network, and Concurrent
- Qt 6 DBus on Linux

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

Prebuilt Windows and Ubuntu packages are published from Git tags. To build from
source:

Linux:

```bash
cmake -S . -B build
cmake --build build -j
./build/MdsScope
```

macOS:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
open ./build/MdsScope.app
```

Windows:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\MdsScope.exe
```

Install from a source build when needed:

Linux:

```bash
cmake --install build
```

For non-root Linux installs, the default command-line prefix is `~/.local`.

macOS:

```bash
# GUI app bundle
cmake --install build --prefix "$HOME/Applications" --component app

# Command-line tools and shared resources
cmake --install build --prefix "$HOME/.local" --component tools
```

## Configuration

On first run, MdsScope creates a user configuration directory and copies bundled
templates if it is empty:

```text
Linux:   ~/.config/mdsscope/
macOS:   ~/Library/Application Support/mdsscope/
Windows: %LOCALAPPDATA%\MdsScope\
```

`auth.cache` is not plaintext; it stores the username, password, and token in an
encrypted local cache bound to the current machine/user.

MdsScope supports native `*.toml` files and legacy WebScope-compatible
`*.webscp` files. The default template is `init.toml`; saving from the GUI keeps
TOML and WEBSCP files synchronized.

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
read_mode = "full"
```

Panels use the shot shown in the MdsScope UI. A signal-level `shot` value is
only needed for explicit per-signal shot overrides, such as shot comparison.

Shot fields support single shots, semicolon lists, ranges, and mixed
expressions:

```text
143850
143850;143851;143853
143850-143858
143850-143858;143865
```

Useful signal options include `shot`, `hidden = true`, and
`read_mode = "medium"` or `read_mode = "full"` for per-signal data mode
overrides.

Convert legacy `.webscp` files with `transfer`:

```bash
./build/transfer resources/environment/init.webscp
./build/transfer --recursive resources/environment
```

On Windows source builds, use the executable under `build\Release`:

```powershell
.\build\Release\transfer.exe ".\resources\environment\init.webscp"
.\build\Release\transfer.exe --recursive --out-dir ".\converted" ".\resources\environment"
```

### EAST HTTP Metadata API

The EAST HTTP metadata endpoint is stored in `resources/APIurl`. It is used for
latest shot lookup and the top-bar summary. Plot data continues to come from
EAST MDSIP.

### SSH Remote Access

Use the SSH button next to Login to tunnel MDSIP data and EAST metadata when
they are only reachable from an internal network. Linux, macOS, and Windows are
supported when a system OpenSSH client is available. The SSH server may run on
any platform, but it must allow TCP forwarding and reach the required internal
services.

Authentication supports a password, a selected identity file, or the default
OpenSSH configuration and `ssh-agent` when both are left empty. Saved SSH
settings are stored in the machine-bound encrypted user cache.

SSH access is slower than a direct internal-network connection because of
unavoidable network, encryption, and compression overhead. Users are responsible
for using only trusted, authorized SSH hosts and for complying with applicable
network and security policies.

### Data Export

Data export supports text, CSV, TSV, and JSON formats. By default, exported
files are written under:

```text
Linux:   ~/Downloads/mdsscope/output/
macOS:   ~/Downloads/mdsscope/output/
Windows: %USERPROFILE%\Downloads\mdsscope\output\  (usual location)
```

## Benchmark Mode

MdsScope includes a simple benchmark mode for MDS data loading:

```bash
./build/MdsScope --benchmark resources/environment/your_config.webscp --summary
```

## Repository Hygiene

Do not commit:

- access tokens
- local auth caches
- build artifacts

The EAST API URL is intentionally stored in `resources/APIurl`; do not commit
personal tokens or cache files.

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
