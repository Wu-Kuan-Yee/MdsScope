# MdsScope

MdsScope is a C++/Qt desktop application for browsing WebScope-style shot
configuration files (`*.webscp`) and plotting MDSplus signal data through MDSIP.

The project is developed as a native C++/Qt implementation of the EAST MDSplus
data viewing workflow described by Yang and Xiao (2012).

https://doi.org/10.1016/j.fusengdes.2012.09.015

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
- Follow the system light/dark preference on GNOME via the desktop portal.

## Requirements

MdsScope requires:

- CMake 3.16 or newer
- A C++23 compiler
- Qt 6.4 or newer with Core, Widgets, Network, Concurrent, and DBus

MdsScope is currently developed and tested for Linux desktop environments.
macOS and Windows are not currently supported.

The current development machine uses:

```text
GCC/G++ 15.2.0
CMake 4.2.3
Qt 6.10.2
```

Older compiler and Qt versions may work as long as they support C++23 and the
required Qt 6 modules.

### Dependency Installation

Debian / Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel
```

## Build and Install

```bash
cmake -S . -B build
cmake --build build -j
cmake --install build --prefix "$HOME/opt/mdsscope"
```

Run the installed application:

```bash
~/opt/mdsscope/bin/MdsScope
```

Run the converter:

```bash
~/opt/mdsscope/bin/transfer --help
```

For development without installing, run the build-tree binaries directly:

```bash
./build/MdsScope
./build/transfer --help
```

## Configuration

### Environment Files

MdsScope uses two environment directories with different roles:

```text
~/opt/mdsscope/share/mdsscope/environment/
```

This is the installed template/default configuration directory. It is copied
from the repository `environment/` directory during `cmake --install`. Treat it
as application data; upgrades may replace it.

```text
~/.config/mdsscope/environment/
```

This is the user configuration directory. The GUI loads from and saves to this
directory by default.

MdsScope keeps user-editable files under:

```text
~/.config/mdsscope/
  mdsscope_ui.ini
  environment/
```

Login credentials and tokens are cached under:

```text
~/.cache/mdsscope/auth.cache
```

The cache file is not plaintext; it stores the username, password, and token in
an encrypted local cache bound to the current machine/user.

Tree-bound signal indexes are installed under:

```text
~/opt/mdsscope/share/mdsscope/source_index/
  trees.txt
  signals/<tree>.txt
```

On startup, MdsScope seeds and updates the user cache at
`~/.cache/mdsscope/source_index/` from the installed index while preserving
locally discovered entries. If a user-entered tree/signal is not already in the
cache, MdsScope adds it only after that signal has been read successfully.
Failed reads are treated as invalid input and are not cached.

The Data Source Setup dialog uses this cache for case-insensitive tree and
signal completion. Signal candidates are scoped to the tree entered on the same
row.

On first run, MdsScope creates `~/.config/mdsscope/environment/` and copies
template `*.toml` and `*.webscp` files from the source tree or installed
`share/mdsscope/environment/` directory when the user environment directory is
empty.

MdsScope supports two configuration formats:

- `*.toml`: the recommended native MdsScope format
- `*.webscp`: legacy WebScope-compatible format

On startup, MdsScope loads `~/.config/mdsscope/environment/init.toml` first. If
that file is missing, it tries `init.webscp`. If neither default file exists,
the first `*.toml` or `*.webscp` file sorted by name is loaded.

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
extraction_points = 2000
grid = true
custom_x_range = false
custom_y_range = false

[[panels.signals]]
tree = "pcs_east"
server = "202.127.204.12"
y = "\\pcrl01"
x = ""
color = "#2364aa"
manual_color = false
hidden = false
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

When converting from legacy `.webscp`, MdsScope treats the most common
`shot_txt` value as the old default shot and omits it from TOML. If a panel or
signal clearly uses a different shot, that value is preserved as a signal-level
`shot` override.

### Converting Legacy `.webscp` Files

Convert one file:

```bash
./build/transfer ~/.config/mdsscope/environment/init.webscp
```

Convert all `.webscp` files in one directory:

```bash
./build/transfer ~/.config/mdsscope/environment
```

Write converted TOML files to another directory:

```bash
./build/transfer --out-dir converted ~/.config/mdsscope/environment
```

Convert recursively:

```bash
./build/transfer --recursive ~/.config/mdsscope/environment
```

### EAST HTTP Metadata API

The EAST HTTP metadata endpoint is stored in the repository `APIurl` file. The
file is installed to `share/mdsscope/APIurl` by `cmake --install` and contains a
comment citing the public reference for the EAST data-service workflow.

On startup, MdsScope reads `APIurl`, checks the local encrypted auth cache, and
opens the login dialog before the main window if a valid token is not available.
After successful login, MdsScope caches the returned token and the entered
credentials in `~/.cache/mdsscope/auth.cache`. Later starts reuse the cached
token, or refresh it automatically with cached credentials when it expires.

The HTTP API is used for latest shot lookup and the top-bar `Ip`, `Pulse`, `It`,
and `Time` summary. Plot data continues to come from EAST MDSIP.

### Data Export

Data export writes files under an `output/` subdirectory of the selected base
directory. The default base directory is:

```text
~/Downloads/mdsscope
```

So the default export location is:

```text
~/Downloads/mdsscope/output/
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

## References

The original EAST web-based MDSplus/WebScope workflow is described in:

Yang, F.; Xiao, B. J. A web based MDSplus data analysis and visualization
system for EAST. Fusion Engineering and Design 2012, 87(12), 2161-2165.

https://doi.org/10.1016/j.fusengdes.2012.09.015

## License

MdsScope is licensed under the GNU General Public License version 3. See
`COPYING` for the full license text.

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
