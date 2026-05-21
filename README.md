# Phantom Gomoku Skeleton

This project is a VSCode-friendly SFML skeleton for a Gomoku course project.

## Included

- SFML window loop
- 15x15 board rendering
- mouse click placement
- turn switching
- restart with `R`
- obstacle mode toggle with `O`
- basic five-in-a-row winner detection

## Recommended VSCode setup

1. Install the `C/C++` extension.
2. Install the `CMake Tools` extension.
3. Make sure SFML 2.6 is installed and discoverable by CMake.
4. Open this folder in VSCode.
5. Run `CMake: Configure`.
6. Run `CMake: Build`.
7. Launch the generated executable from VSCode or the build folder.

## Notes

- The game status is shown in the window title to avoid extra font assets.
- Press `O` to switch between classic mode and obstacle mode.
- This is intentionally a small foundation so you can keep adding menu, AI, hints, replay, and polish later.
