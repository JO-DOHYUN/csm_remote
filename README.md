# CSM Remote

Portenta H7 기반 CSM firmware 원본 저장소다.

- 사람 시작점: `START_HERE_KO.md`
- Codex route: `AGENTS.md`
- authority map: `docs/index.md`
- active architecture: `docs/architecture/ACTIVE_ARCHITECTURE.yaml`
- PlatformIO project: `firmware/csm`
- canonical wire: `firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md`

```powershell
python tools/verify_harness.py
python firmware/csm/tools/product_invariant_guard.py
python firmware/csm/tools/architecture_conformance_guard.py
python firmware/csm/tools/experiment_guard.py
```

정확한 build/test 명령은 `docs/operations/DEVELOPMENT_SETUP_KO.md`가 소유한다.
build는 device/HIL proof가 아니며 upload와 CAN HIL은 별도 승인과 안전 gate가 필요하다.
