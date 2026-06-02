# Agent Playbook

## Default Working Style
- Start narrow: only target requested paths.
- Return findings first, then short summary.
- Propose changes before scanning unrelated modules.

## Prompt Contract (required)
Every implementation request should specify:
1. target paths
2. definition of done
3. output size expectation
4. explicit "no unrelated exploration" rule

## Clean Code Rules
- Single-responsibility modules.
- Keep orchestration in services/use-cases, not render loops.
- Update architecture docs when module boundaries change.
