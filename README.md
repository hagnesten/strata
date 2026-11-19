# Strata

Strata is a single-file SQL database engine and a studio app to work with it. It's written from scratch for Windows with no CRT and no outside code.

It's at the very start: the project builds and runs, and the storage layer comes next.

## Building

Open `strata.sln` in Visual Studio 2022 with the Clang toolset, or run MSBuild:

```
msbuild strata.sln /p:Configuration=Release /p:Platform=x64
```

Output goes to `build\release\strata.exe`.
