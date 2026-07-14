# AGENTS.md - Imported Board Skill Router

## Scope
This file applies to imported board skill files under `firmware/csm/board/.agents/**`.

These skill files are CSM firmware helper references. They are not automatically
enabled product authority for CSM Remote.

## Rules
- Use them only when the task matches the skill description.
- CSM Remote product authority remains `../../../../docs/remote/product/PRODUCT_DEFINITION_KO.md`.
- If a skill suggests control or TX behavior, reconcile it with the remote
  authority and evidence rules before implementation.
