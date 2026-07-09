# CSM Remote Repository Setup

작성일: 2026-07-09  
상태: 적용 완료  

이 문서는 현재 `csm_remote` repository가 어떻게 구성되었는지와, 앞으로 CSM subtree를 어떻게 다뤄야 하는지 정의한다.

---

## 1. 현재 구조

```text
C:\WORKS\VS\csm_remote
  README.md
  CSM_REMOTE_*.md
  firmware/
    csm/
      platformio.ini
      include/
      src/
      tools/
      docs/
      ...
```

역할:

```text
루트: 제품 정의, 개발 하네스, decision/open decision/history/trace
firmware/csm: CSM firmware subtree, 기준 bfef287
```

---

## 2. Git 구성

현재 repo:

```text
main branch: csm_remote product integration repository
remote csm-upstream: C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM
subtree prefix: firmware/csm
CSM imported commit: bfef287aa424edcef6026dcd86aa6e4077a82386
```

Subtree import commit:

```text
7f0863f Add 'firmware/csm/' from commit 'bfef287aa424edcef6026dcd86aa6e4077a82386'
```

---

## 3. 왜 subtree인가

선택:

```text
copy-paste: reject
submodule: reject for current phase
subtree: accepted
```

이유:

```text
CSM은 외부 dependency가 아니라 직접 수정될 제품 firmware다.
제품 정의/decision/trace와 firmware 변경을 한 commit 흐름에서 관리해야 한다.
나중에 firmware/csm prefix 기준으로 다시 분리할 수 있어야 한다.
```

---

## 4. 금지 사항

```text
firmware/csm을 수동 복붙으로 갱신하지 않는다.
원본 CSM repo와 subtree 안 CSM을 동시에 임의 수정하지 않는다.
subtree prefix 밖에서 CSM firmware 파일을 흩뿌리지 않는다.
제품 정의 문서 없이 firmware/csm에 remote-control architecture를 바로 구현하지 않는다.
main.cpp에 새 기능을 임시로 몰아넣지 않는다.
```

---

## 5. 앞으로 CSM upstream을 가져오는 방법

원본 CSM에서 변경을 가져올 때:

```powershell
cd C:\WORKS\VS\csm_remote
git fetch csm-upstream codex/csm-cdc-uplink-architecture
git subtree pull --prefix=firmware/csm csm-upstream codex/csm-cdc-uplink-architecture
```

주의:

```text
pull 전에 working tree가 clean이어야 한다.
pull 후 Product Definition, Requirement Trace, Change History 영향 여부를 검토한다.
```

---

## 6. firmware/csm 변경을 다시 분리해야 할 때

나중에 CSM을 별도 repo로 다시 분리하거나 upstream에 반영하려면:

```powershell
cd C:\WORKS\VS\csm_remote
git subtree split --prefix=firmware/csm -b split/csm-remote
```

그 다음 `split/csm-remote` branch를 별도 remote로 push할 수 있다.

---

## 7. 작업 시작 순서

모든 작업은 다음 순서로 시작한다.

```text
1. README.md
2. CSM_REMOTE_AGENT_ROUTING_MATRIX_KO.md
3. 필요한 Product Definition 섹션
4. 관련 firmware/csm source
5. Decision/Open Decision/Requirement Trace 갱신 여부 확인
```

---

## 8. 현재 완료 상태

완료:

```text
git repo initialized
documentation baseline committed
text normalization policy added
CSM bfef287 imported as subtree under firmware/csm
csm-upstream remote connected
```

다음 단계:

```text
Phase 0 handoff 재판정
CSM source layout proposal
Phase 1 M7 scaffolding plan
```

