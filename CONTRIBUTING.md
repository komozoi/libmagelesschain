# Contributing to LibMagelessChain

Thank you for your interest in contributing to LibMagelessChain! As a project focused on performance and reliability, we
value contributions that maintain or improve these qualities.

## How to Contribute

1. **Report Bugs**: Use GitHub or Gitea Issues to report bugs. Please include steps to reproduce, expected vs. actual behavior,
   and environment details.  **Do not report security vulnerabilities this way, see SECURITY.md.**
2. **Suggest Features**: Open an issue to discuss new features before implementing them.
3. **Pull Requests**:
    - Ensure code follows the existing style: tabs, camelCase for methods, PascalCase for classes.
    - Include tests for ALL new functionality.
    - Update documentation in `docs/wiki/` if applicable.

## Development Setup

You'll need to install `gtest`, have CMake 3.14 or higher, and a C++17 compiler.  On Debian-based systems, these can be
installed with:

```bash
sudo apt install libgtest-dev cmake
```

On MacOS, you can install `gtest` with Homebrew:

```bash
brew install googletest
```

## Code Style

- Indentation: tabs
- Naming: `PascalCase` for classes, `camelCase` for methods, `snake_case` for some internal structures (mirroring
  OS/low-level patterns where appropriate).
- Keep opening curly braces (`{`) on the same line as the statement.
- Never EVER use `auto` as it often obscures the type.  I would rather the line be 200 characters long than have code
  that is easy to read but hard to understand.
- Prefer libexcessive types over builtin types.

## Licensing

By contributing, you agree that your contributions will be licensed under the Apache License 2.0.  By opening a PR,
you are confirming that you have the right to contribute this code and that it does not infringe on any third-party
patents, copyrights, trademarks, or other intellectual property rights.  Additionally, by opening a PR, the author
agrees that the code will be under the Apache License 2.0 when it is merged.
