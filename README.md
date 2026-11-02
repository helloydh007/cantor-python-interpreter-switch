<p align="right">
  <img src="assets/zcode-badge.svg" alt="Modified with ZCode" height="30">
</p>

# Cantor — runtime Python environment switching

This repository is a snapshot of [KDE Cantor](https://apps.kde.org/cantor/) branch
`release/24.05` (upstream commit `a4fb818b`) plus patches that let the Python
backend discover Python environments and switch the interpreter at runtime.

## What this fork adds

KDE Cantor's Python backend normally launches `cantor_pythonserver`, a helper binary
that links `libpython` at build time. The interpreter is therefore fixed when Cantor is
built, and there is no way to point it at a virtualenv, a Conda environment or another
Python installation.

This fork adds:

- **`Interpreter` setting** — `Settings → Configure Cantor → Python backend → General →
  Python interpreter` (type `Url`, defaults to the `python3` found on `PATH`).
- **External interpreter mode** — when the configured path is executable, the session
  starts that interpreter and runs the embedded `src/backends/python/server.py`
  (`python -u -c <script>`).
- **Embedded fallback** — an empty or invalid path keeps using the original
  `cantor_pythonserver`, so nothing breaks on systems without a usable `python3`.
- **Protocol parity** — the external server implements the same wire protocol as the
  embedded one: `code`, `model`, `setFilePath`, `exit`, stdout/stderr capture, variable
  introspection and `SIGINT` handling.

The setting applies to sessions started after the change. When the configured
interpreter changes, open sessions are logged out and reconnect on the next
evaluation using the new interpreter.

## Environment detection (hybrid of MR !119)

The Python settings page can discover the Python environments available on the
machine instead of relying on a hand-typed path:

- The settings page scans system interpreters, virtualenvs and Conda environments.
  Default roots include `~/.virtualenvs`, `~/.local/share/virtualenvs`, `~/.conda/envs`,
  `~/miniconda3/envs` and `~/anaconda3/envs`; additional directories can be added
  from the dialog and are stored in `SearchFolders`.
- Selecting an entry writes the interpreter path to the `Interpreter` setting. The
  session then launches that external interpreter and runs `server.py` with it.
- The execution mechanism is still an external process, so the backend is not tied
  to the `libpython` version Cantor was built against; any usable Python 3
  installation can be selected.
- Switching the interpreter logs out the open sessions; they are reconnected with
  the new interpreter on the next evaluation.
- This environment-detection part is a hybrid of KDE merge request
  [!119](https://invent.kde.org/education/cantor/-/merge_requests/119); thanks to
  its author and reviewers.

## Verifying the selected interpreter

In a Cantor Python worksheet, run:

```python
import sys
print(sys.executable)
print(sys.version)
```

`sys.executable` shows the interpreter actually in use. If it shows
`cantor_pythonserver`, the embedded fallback is active.

## AppImage notes

The AppImage's `AppRun` sets `PATH` (including `$APPDIR/usr/bin`),
`LD_LIBRARY_PATH`, `QT_PLUGIN_PATH` and `XDG_DATA_DIRS` explicitly. This matters
when the host has no `python3` on `PATH`: the Python backend then falls back to
the bundled `cantor_pythonserver` instead of reporting "No Backend Found". The
other backends (Sage, Octave, Maxima, Scilab) are built but their interpreters
are not bundled; pick Python in the backend chooser.

## Files changed

- `src/backends/python/pythonbackend.kcfg` — new `Interpreter` entry and
  `SearchFolders` (`StringList`) entry
- `src/backends/python/settings.ui` — interpreter path widget (`KUrlRequester`),
  `envComboBox` environment selector and detect/browse buttons
- `src/backends/python/pythonsettingswidget.cpp` / `pythonsettingswidget.h` —
  environment scanning (system interpreters, virtualenvs, Conda environments) and
  selection
- `src/backends/python/pythonbackend.cpp` / `pythonbackend.h` — interpreter
  requirements check, session registry and restart-on-config-change
- `src/backends/python/pythonsession.cpp` — launch the external interpreter, fall back,
  and handle the `ready` handshake robustly
- `src/backends/python/server.py` — pure-Python backend server (new)
- `src/backends/python/python.qrc` — embed `server.py`

A standalone patch is included:
[`cantor-python-interpreter-switch.patch`](cantor-python-interpreter-switch.patch).

## Build

Same as upstream (KF5/Qt5; Cantor 24.05 requires KF5 ≥ 5.91 and Qt5 ≥ 5.8):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
```

On Ubuntu 24.04 the dependencies are the `qtbase5-dev`, `qttools5-dev`,
`qtwebengine5-dev`, `libkf5*-dev`, `libpoppler-qt5-dev` and `python3-dev` packages.

---

## Upstream README

## Cantor

Cantor is a KDE Application aimed to provide a nice Interface
for doing Mathematics and Scientific Computing. It doesn't implement
its own Computation Logic, but instead is built around different
Backends.

## Available Backends

- Julia Programming Language: http://julialang.org/
- KAlgebra for Calculation and Plotting: http://edu.kde.org/kalgebra/
- Lua Programming Language: http://lua.org/
- Maxima Computer Algebra System: https://maxima.sourceforge.io/
- Octave for Numerical Computation: https://gnu.org/software/octave/
- Python 2 Programming Language: http://python.org/
- Python 3 Programming Language: http://python.org/
- Qalculate Desktop Calculator: http://qalculate.sourceforge.net/
- R Project for Statistical Computing: http://r-project.org/
- Sage Mathematics Software: http://sagemath.org/
- Scilab for Numerical Computation: http://scilab.org/

## How To Build and Install Cantor

To build and install Cantor, follow the steps below:

```
cd cantor
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/path/to/install -DCMAKE_BUILD_TYPE=RELEASE
make
make install or su -c 'make install'
```

If `-DCMAKE_INSTALL_PREFIX` is not used, Cantor will be installed in
default cmake install directory (`/usr/local/` usually).
Also, setting `CMAKE_INSTALL_PREFIX` to some unstandart location may happens different problems
with searching Cantor files. So there is a need for passing proper paths for KDE install cmake variables.
For example, if no one Cantor's backends found after installation, KDE_INSTALL_PLUGINDIR path should
be specified on cmake configuration step to some location.

To uninstall the project:

`make uninstall` or `su -c 'make uninstall'`
