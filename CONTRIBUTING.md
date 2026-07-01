# Contributing

Thanks for contributing to OpenTree.

## Development Environment

Recommended environment:

- Windows 10 or later
- Visual Studio 2022 Build Tools or Visual Studio 2022
- Qt 6.8.x MSVC 2022 64-bit
- CMake 3.24+

## Build

Fast path:

```bat
build_msvc.bat
```

## Coding Notes

- Keep changes small and focused.
- Prefer extending existing UI/service flows over adding parallel systems.
- Avoid committing build output, logs, databases, or machine-local config.

## Before Opening a PR

1. Build the app successfully.
2. Smoke test the changed workflow.
3. Update `README.md` or docs if behavior changed.
4. Keep the repo free of build artifacts.

## Bug Reports

Please include:

- what you expected
- what actually happened
- reproduction steps
- screenshots if UI-related
- whether you used the WebEngine build path
- any relevant console/log output

## Feature Requests

Please include:

- the workflow problem you are solving
- how you expect it to behave
- whether the request is Windows-only or cross-platform in intent
