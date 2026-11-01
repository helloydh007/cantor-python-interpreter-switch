# Cantor — runtime Python interpreter switching

> **Modified with ZCode** · 2026-11-01
>
> This repository is a snapshot of [KDE Cantor](https://apps.kde.org/cantor/) branch
> `release/24.05` (upstream commit `a4fb818b`) plus one patch that lets the Python
> backend use a Python interpreter chosen at runtime.

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

The setting applies to sessions started after the change; restart the session (or open a
new worksheet) to pick up a different interpreter.

## Verifying the selected interpreter

In a Cantor Python worksheet, run:

```python
import sys
print(sys.executable)
print(sys.version)
```

`sys.executable` shows the interpreter actually in use. If it shows
`cantor_pythonserver`, the embedded fallback is active.

## Files changed

- `src/backends/python/pythonbackend.kcfg` — new `Interpreter` entry
- `src/backends/python/settings.ui` — interpreter path widget (`KUrlRequester`)
- `src/backends/python/server.py` — pure-Python backend server (new)
- `src/backends/python/python.qrc` — embed `server.py`
- `src/backends/python/pythonsession.cpp` — launch the external interpreter, fall back,
  and handle the `ready` handshake robustly
- `src/backends/python/pythonbackend.cpp` — requirements check respects the setting

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
