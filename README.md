# night_voyage

## Project Structure

```
night_voyage/
├── addons/
│   └── night_voyageExt/    # GDExtension plugin
│       ├── bin/                # Compiled native binaries (output of CMake build)
│       ├── icons/              # Plugin icons
│       └── ...
├── native/
│   └── night_voyageExt/    # C++ source code for the GDExtension
│       ├── godot-cpp/          # Git submodule: Godot C++ bindings
│       ├── CMakeLists.txt
│       └── src/
│           ├── register_types.h/.cpp
│           └── example_class.h/.cpp
├── cmake/
│   └── GodotAddonsManager.cmake
├── CMakeLists.txt              # Root CMake configuration
├── project.godot               # Godot project file
└── main.tscn                   # Main scene
```

## Building the Native Extension

1. Configure: `cmake -B cmake-build-debug -S .`
2. Build: `cmake --build cmake-build-debug`

The compiled library is copied to `addons/night_voyageExt/bin/` by CMake after each build.

See [addons/night_voyageExt/README.md](addons/night_voyageExt/README.md) for the reusable UI effects, Godot scene setup, performance notes, and GPU-backed validation commands.
