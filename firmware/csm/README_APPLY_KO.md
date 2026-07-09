# Codex 리서치 반영 최종 번들

이 번들은 **짧은 AGENTS + 짧은 BRIEF + 작업별 SKILL + 설명은 docs 분리** 원칙으로 다시 다듬은 최종본이다.

## 넣는 방법

### 보드 저장소
루트에 아래 3개를 넣는다.
- `board/`
- `shared/`
- `install/`

### QT 저장소
루트에 아래 3개를 넣는다.
- `qt/`
- `shared/`
- `install/`

## 왜 다시 줄였나
- `AGENTS.md`는 짧고 강하게 유지
- `BRIEF.md`는 이번 턴 기준만 유지
- 절차는 `SKILL.md`로 분리
- 배경 설명과 장기 기준은 `docs/`로 분리

## 이번 번들의 핵심 결정
- **보드**: raw CAN + encoder direct + optional analog debug + control safety + typed records
- **QT**: transport / storage / analysis / evidence / graph / control / operator 분리
- **공통**: mono64 시간축, 원본/유도값 분리, drop/overflow 숨김 금지, control audit chain 필수
