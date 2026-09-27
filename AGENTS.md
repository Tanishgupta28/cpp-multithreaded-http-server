# Project development rules

Read AGENTS.md and ROADMAP.md before making changes in every session.

- Build an interview-friendly C++17 Linux/POSIX server using CMake.
- Implement only the authorized roadmap stage. Build, validate behavior and errors, fix issues, update documentation and roadmap, review the diff, commit, push if available, report, and STOP. Wait for explicit permission for the next stage.
- Prefer simple readable code, clear names, small functions, standard containers, and straightforward RAII. Avoid clever templates, macros, unnecessary inheritance, frameworks, and premature abstractions. Explain non-obvious systems concepts in comments.
- Keep socket APIs visible. Introduce OOP, threads, synchronization, routing, and caching gradually in their designated stages.
- Routine inspection, edits, builds, tests, fixes, documentation, Git initialization, normal commits, repository creation, and normal pushes are authorized. Choose sensible minor details autonomously and report assumptions.
- Ask only for material ambiguity, required unavailable credentials, or actions that risk meaningful work. Never force push, rewrite published history, delete repositories, remove unrelated files, or discard user work without confirmation.
- Check Git state, remotes, GitHub tooling/authentication, and existing repositories. Prefer a public cpp-multithreaded-http-server repository containing only this project.
- GitHub unavailability must not block local development or commits. Record pending synchronization in ROADMAP.md, retry on future runs, and push the complete existing stage history when access returns. Never restart or duplicate the project.
- Keep professional README documentation accurate and synchronized. Never claim unimplemented features, production readiness, or unmeasured results. Future benchmarks must document environment, commands, and measured results.
- Never commit credentials, tokens, passwords, private keys, environment secrets, or generated build output. Review staged content before pushing.
- Validate each stage meaningfully: TCP connections before HTTP, HTTP clients when implemented, multiple clients when concurrency exists. Avoid pointless tests.
- Stage reports cover files, implementation, build/run commands, validation, Git status/commit, GitHub status, assumptions, limitations, and next stage.
