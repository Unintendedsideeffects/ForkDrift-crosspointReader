# Contributing to CrossPoint Reader

Welcome! This guide helps you get started with contributing to CrossPoint Reader. Whether you are fixing a bug, adding a new feature, or improving documentation, we appreciate your help.

## Onboarding Guide

If you are new to the project, we recommend following this reading order to build a solid understanding of the system:

1.  **[Getting Started](./getting-started.md)**: Set up your local environment, clone the repository, and build the firmware.
2.  **[Architecture Overview](./architecture.md)**: Understand the high-level design, the activity model, and how different components interact.
3.  **[Development Workflow](./development-workflow.md)**: Learn about our branching strategy, commit conventions, and the process for submitting Pull Requests.
4.  **[Testing and Debugging](./testing-debugging.md)**: Discover tools and techniques for verifying your changes and troubleshooting issues.

## Important Note on Branches

All Pull Requests should be targeted at the **`fork-drift`** branch. This is our active development branch where new features and fixes are integrated before being merged into `master`.

## Local Git hooks

To use the repository-managed hooks, point Git at `scripts/hooks`:

```sh
git config core.hooksPath scripts/hooks
```

The current hooks do the following:

- `pre-commit`: regenerates staged generated assets, auto-formats staged C/C++ files, and runs a firmware build.
- `pre-push`: runs the heavier local CI checks (`cppcheck` + firmware build) before push.

## Additional Resources

- **[Project Scope](../../SCOPE.md)**: What is and isn't in scope for CrossPoint Reader.
- **[Governance](../../GOVERNANCE.md)**: How the project is managed and how decisions are made.
- **[Architecture (Detailed)](../../ARCHITECTURE.md)**: More technical details on specific subsystems.
- **[User Guide](../../USER_GUIDE.md)**: Documentation for end-users, useful for understanding intended behavior.

We look forward to your contributions!
