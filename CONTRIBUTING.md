# Contributing to Project Mobius

Thank you for your interest in contributing to Project Mobius! This document
covers how to get started and the policies contributors are expected to follow.

## Getting Started

1. Fork the repository and clone your fork
2. Follow the build instructions in [README.md](README.md#quick-start)
3. Create a feature branch from `main` for your work
4. Submit a pull request describing your changes

## Pull Requests

- Keep PRs focused — one logical change per PR
- Describe the scope of the change and include build/test commands used
- Link related issues where applicable
- Include screenshots for UI changes
- Follow the coding conventions in [CLAUDE.md](CLAUDE.md#coding-conventions)

## Coding Standards

- **Unreal C++ style**: 4-space indent, brace on new line for functions
- **Type prefixes**: `U` (UObject), `A` (Actor), `F` (struct), `I` (interface),
  `S` (Slate widget)
- **Functions/methods**: PascalCase. **Locals**: camelCase
- **Module layout**: public headers in `Public/`, implementations in `Private/`
- **Documentation**: Doxygen-style block comments for complex or new code

## AI Tool Usage Policy

AI coding assistants (e.g. GitHub Copilot, OpenAI Codex, Claude) are welcome as
productivity tools when contributing to this project. The following rules apply
to any contribution that involves AI-assisted code generation.

### Requirements

1. **Human ownership** — The contributor is the author of record. You must fully
   understand every line you submit. "The AI wrote it" is not an acceptable
   explanation for how code works or why a design choice was made.

2. **Review before commit** — AI-generated or AI-suggested code must be reviewed
   for correctness, security, and adherence to project conventions before it is
   committed. Do not blindly accept suggestions.

3. **Testing** — AI-assisted changes must pass the same build and test
   requirements as any other contribution. Contributors are responsible for
   verifying that changes work correctly in the Unreal Editor and do not
   introduce regressions.

4. **No copyrighted or proprietary code** — AI tools must not be used to
   reproduce code from copyrighted sources, proprietary codebases, or projects
   with incompatible licenses. If in doubt, write the code from scratch.

5. **Licensing responsibility** — Contributors must ensure that AI-assisted code
   does not introduce license conflicts. All contributions fall under the
   project's [MIT License](LICENSE).

6. **Disclosure** — If a substantial portion of a PR was generated with AI
   assistance (beyond autocomplete-level suggestions), note this in the PR
   description. This is for transparency, not gatekeeping — AI-assisted
   contributions are evaluated on the same quality bar as any other.

### What counts as "AI-assisted"

| Level | Example | Disclosure needed? |
|-------|---------|-------------------|
| Autocomplete | Single-line completions, variable names | No |
| Snippet generation | Boilerplate, repetitive patterns | No |
| Function/class generation | AI writes a full function or class | Yes — note in PR |
| Architecture or design | AI proposes system design or algorithms | Yes — note in PR |

### What is not acceptable

- Submitting AI output without reading and understanding it
- Using AI to bulk-generate code to inflate contribution volume
- Copying AI output from models trained on code with incompatible licenses
  without verifying license compatibility
- Relying on AI to make security-sensitive decisions (e.g. auth, crypto,
  input validation) without manual review

## Reporting Issues

Open an issue on the [GitHub repository](https://github.com/sir306/ProjectMobius/issues)
with steps to reproduce, expected behaviour, and actual behaviour.

## License

By contributing to Project Mobius you agree that your contributions will be
licensed under the [MIT License](LICENSE).
