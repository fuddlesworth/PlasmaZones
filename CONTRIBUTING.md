# Contributing to PlasmaZones

Thanks for considering contributing. This document covers the basics of how to get involved.

## Getting Started

Clone the repo and build it locally first. The README has build instructions, but the short version:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)
```

Debug builds are slower but give you better error messages and assert failures actually fire.

## Code Style

We follow KDE coding conventions. There's a `.clang-format` in the repo root - run it on your changes before submitting:

```bash
clang-format -i src/path/to/your/file.cpp
```

The highlights:
- 4 spaces, no tabs
- Braces on their own line for functions and classes, same line for control flow
- Pointer asterisk goes with the type: `int* foo` not `int *foo`
- 120 character line limit

If you're not sure about something, look at the existing code and match what's already there.

## License Headers

Every source file needs an SPDX header at the top. Use this format:

```cpp
// SPDX-FileCopyrightText: 2026 Your Name
// SPDX-License-Identifier: GPL-3.0-or-later
```

If you're modifying an existing file, add your name to the copyright line or add a new line below the existing ones.

## Submitting Changes

1. Fork the repo
2. Create a branch for your work
3. Make your changes
4. Test them (at minimum, make sure it builds and runs)
5. Open a pull request

Keep commits focused. One logical change per commit is easier to review than a massive dump of unrelated changes.

Write decent commit messages. First line should be a short summary, then a blank line, then more detail if needed. No need to write a novel, but "fix bug" tells reviewers nothing useful.

## Testing

The test suite lives in `tests/`. Run it with:

```bash
cd build
ctest --output-on-failure
```

If you're adding new functionality, add tests for it. If you're fixing a bug, a test that would have caught it is appreciated.

## Translations

PlasmaZones uses [KI18n](https://api.kde.org/frameworks/ki18n/html/) (Gettext) with three domains: `plasmazonesd` (daemon/overlay), `plasmazones-editor` (editor), and `kcm_plasmazones` (System Settings).

- **Extract strings** after changing user-visible text:  
  `cmake --build build --target extract-pot`  
  This updates `po/*.pot`. Commit them so translators can merge.

- **Add or update a translation**: see `po/README.md` for `msginit`, `msgmerge`, and the `po/<lang>/<domain>.po` layout.

- **Build/install**: `.po` in `po/<lang>/` are compiled to `.mo` and installed automatically.

## Questions

Open an issue if you're stuck or want to discuss something. There's no mailing list or chat channel at the moment.
