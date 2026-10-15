# Implementation Rules

## Architecture
- Follow SOLID, DRY, KISS and YAGNI.
- Prefer composition over inheritance.
- Separate domain logic, application orchestration and infrastructure.
- Keep dependencies directed inward and acyclic.
- Isolate frameworks, hardware and transports behind adapters.
- Inject dependencies; avoid globals and service locators.
- Introduce abstractions only for actual requirements.
- Keep each component recipe beside its `code/` and deployment `files/` directories.

## Functions
- Use one responsibility and one abstraction level per function.
- Prefer early returns over nested control flow.
- Use simple lambdas; extract complex logic into named functions.
- Use standard algorithms when they improve clarity.
- Use cohesive input/result types instead of long parameter lists.
- Make side effects explicit.

## Types and Classes
- Model domain concepts with meaningful types.
- Maintain invariants within the owning type.
- Keep classes cohesive and public interfaces small.
- Encapsulate state; expose operations instead of unrestricted setters.
- Prefer immutable data and Rule of Zero.

## Errors
- Represent expected failures explicitly.
- Preserve error context across boundaries.
- Catch exceptions only to recover, add context or contain them.
- Avoid silent failures and implicit fallback behavior.

## Readability
- Prefer straightforward code over clever abstractions.

## Comments
- Use `//`: describe non-obvious API behavior above declarations and explain implementation intent or tradeoffs beside the code.
- Document relevant ownership, lifetimes, units, nullability, threading, preconditions and failure behavior; skip obvious or repetitive descriptions.
- Keep comments accurate when behavior changes; review changed APIs for missing or stale documentation before finishing.

## Tests
- Do not add tests or test tooling until explicitly requested.
