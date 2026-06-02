# Token Budget Policy

## Two-Tier Workflow (Default)

### Tier 1 (default)
- concise responses
- scoped file reads
- minimal context loading
- no broad repo scans unless explicitly needed

### Tier 2 (explicit escalation)
Use only for:
- high-risk refactors
- architecture migration planning
- incident/debugging with unknown blast radius

## Practical Patterns
- "Review only these files"
- "Findings first, short summary"
- "Propose patch plan before deeper scan"
- Ask for deeper mode only when Tier 1 evidence is insufficient
