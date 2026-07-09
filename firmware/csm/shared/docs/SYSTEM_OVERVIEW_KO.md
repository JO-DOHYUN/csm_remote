# SYSTEM_OVERVIEW_KO

## 시스템 목표
이 시스템의 목표는 단순 로깅이 아니라, 아래를 판별 가능한 증거 시스템이다.
- 센서 원신호 문제인지
- 하위제어기 CAN 보고 문제인지
- 제어명령 영향인지
- 관측계 손실/포화 문제인지

## 공통 원칙
- mono64 공통 시간축
- raw / derived 분리
- source 분리
- drop/overflow/event 가시화
- command audit chain 필수
