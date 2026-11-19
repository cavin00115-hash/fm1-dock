# BADAPPLE

FM6(DX7 스타일 6오퍼레이터 FM, `modules/fm6` 그대로)에 숨겨진 Bad Apple!! 플레이어를 붙인 단독 앱.

## 시크릿

1. **HOME → OCT+ → OCT−** (하나씩, 동시에 누르지 않기)
2. 메인 멜로디 첫 다섯 음 **미♭ 파 솔♭ 라♭ 시♭** (옥타브 상관없음)

각 입력 사이 2초 이내. 마지막 음에서 영상(80×60, 30fps)과 음악이 처음부터 재생되고,
**HOME** 또는 **PLAY**로 멈춤. 끝나면 FM6으로 자동 복귀.

평소에는 FM6으로 동작함. 건반·옥타브는 모듈이 직접 처리하고(OCT−/OCT+ 동시: 옥타브 0),
엔코더는 FM6 페이지 그대로.

## 빌드 (GitHub Actions)

1. `modules/badapple/data/badapple.bin`(pack)을 저장소에 올림 (웹: Add file → Upload files)
2. Actions 탭 → **badapple** → Run workflow (또는 `ba-v0.1` 같은 태그)
3. 실행 결과의 artifact **packages** 안에 `badapple.fwsc`

설치: `pip3 install mido python-rtmidi` 후 `python3 tools/fm1_install.py badapple.fwsc`.
복구: OCT−를 누른 채 전원 → USB 복구, 또는 M-VAVE 공식 업데이터로 순정 복귀.

패키지 식별자 `FM-1_951`은 임시로 고른 번호. 겹치면 워크플로에서 바꿀 것.

## pack 만들기

```sh
python3 modules/badapple/tools/extract.py badapple.mov 80 60 1 frames.raw     # 1비트 프레임
cc -O2 -o bav_tool modules/badapple/tools/bav_tool.c
./bav_tool enc frames.raw 80 60 30 3 video.bav                                 # 무손실 압축
python3 modules/badapple/tools/badapple_pack.py video.bav song_drums_split.mid modules/badapple/data/badapple.bin
```

- MIDI는 드럼을 키트별 트랙으로 나눈 것. 트랙 이름으로 악기를 고름(`badapple_pack.py`의 `INSTRUMENTS`)
- `--delay-ms 1400`: 음악을 영상에 맞추는 지연(영상 소리와 화성 비교로 측정)

## PC에서 확인

```sh
modules/badapple/host/run.sh modules/badapple/data/badapple.bin   # build/bahost/badapple.wav, screen-*.png
```

모듈 전체(FM6 포함)를 가짜 호스트로 돌려 시크릿 입력 → 재생 → 종료까지 확인.

## 파일

| 파일 | 내용 |
|---|---|
| `badapple.c` | FM6를 포함하고 패널·시크릿·재생기를 덧붙인 모듈 |
| `ba_synth.h` | 재생용 신스: 2오퍼레이터 FM 10파트 + 드럼 8종, 24보이스, 시퀀서 |
| `bav.h` | 1비트 영상 디코더 (문맥 모델 + 레인지 코더, 할당 없음) |
| `tools/` | 프레임 추출, 영상 인코더, pack 빌더 |
| `host/` | PC 테스트 하네스 |
