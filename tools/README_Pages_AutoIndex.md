# ZayApp: Pages 배포 직전에 인덱스 자동 생성

## 적용

이 자료는 Git 저장소 루트에 `docs/`가 있는 구성을 기준으로 합니다.

| 파일 | 저장소 안의 위치 |
| --- | --- |
| Python 생성기 | `tools/generate_zayapp_index.py` |
| 배포 워크플로 | `.github/workflows/zayapp-pages.yml` |

1. 두 파일을 위 위치에 넣습니다. 이미 Pages 배포 워크플로가 있으면 기존 배포를 이 워크플로로 교체하거나, 생성 단계를 기존 워크플로의 최종 파일 업로드 직전에 합칩니다. 같은 사이트를 두 워크플로에서 중복 배포하지 않도록 합니다.
2. GitHub 저장소의 **Settings → Pages → Build and deployment → Source**를 **GitHub Actions**로 선택합니다.
3. 저장소의 기본 브랜치에 push합니다. 워크플로는 `main`이나 `master`를 고정하지 않고 저장소의 실제 기본 브랜치를 확인합니다.
4. **Actions → Deploy ZayApp Pages**에서 실행 결과를 확인합니다. 설정을 바꾼 뒤 수동으로 실행할 때는 **Run workflow**에서 기본 브랜치를 선택합니다.

감시 경로는 `docs/**`, 생성기, 워크플로 자신, 루트 `.gitattributes`입니다. 다른 소스 파일만 변경한 push에는 배포하지 않습니다.

## 실행 순서

전체 Git 이력으로 checkout한 뒤, Python이 `docs/`를 읽어 `docs/zayapp_index.json`을 생성합니다. 그 디렉터리를 Pages 배포 artifact로 업로드하고 배포합니다. 생성 이후 파일의 바이트를 바꾸는 빌드 단계는 없습니다.

```bash
python3 tools/generate_zayapp_index.py \
  --root docs \
  --output zayapp_index.json \
  --version zayapp_index_v1
```

생성기는 Python 3.9 이상과 Git만 사용하며 추가 패키지는 필요 없습니다. 실제 Git 저장소 안에서 실행해야 하며 전체 이력이 필요합니다. 워크플로는 이를 위해 `fetch-depth: 0`을 설정합니다.

`docs/`가 아닌 다른 경로를 배포한다면 `--root`, artifact의 `path`, push 감시 경로를 함께 변경합니다. 나중에 WASM/JS 빌드 단계를 추가할 때는 **빌드 → 인덱스 생성 → 업로드** 순서를 지킵니다.

## 생성 기준

- `version`은 `zayapp_index_v1`, 파일 목록 키는 `list`로 기존 런타임과 같습니다.
- `size`는 checkout 이후 실제 배포할 파일의 바이트 크기입니다. CRLF가 LF로 변환됐으면 LF 파일의 크기를 기록합니다.
- `mtime`은 파일의 마지막 Git 커밋 시각, `ctime`은 파일 이력의 최초 Git 커밋 시각입니다. 둘 다 Windows FILETIME의 100ns 단위인 10진 문자열로 변환합니다.
- `atime`은 `mtime`과 같게 기록합니다. checkout하거나 인덱스를 읽는 것만으로 모든 캐시가 갱신되지 않게 하기 위한 정책입니다.
- Git에 아직 추적되지 않은 파일은 HEAD 커밋 시각을 사용합니다. 빌드 등으로 checkout 후 변경된 추적 파일의 `mtime`도 HEAD 커밋 시각을 사용합니다.
- 파일 목록은 실제 디렉터리를 재귀적으로 조사합니다. 이전 인덱스의 항목을 재사용하지 않습니다.
- 생성 대상 인덱스 자신과 기존 `assetsindex.json`은 목록에서 제외합니다. 업로드 Action이 제외하는 `.git`과 `.github`도 제외합니다. `.nojekyll` 등 다른 숨김 파일은 포함하며 업로드에도 포함합니다.
- 원본 파일의 내용과 줄바꿈은 수정하지 않습니다. 출력만 임시 파일에 완성한 다음 교체합니다. 같은 결과면 다시 쓰지 않습니다.
- 경로 충돌, 지원하지 않는 링크, 디렉터리 조사 실패, 얕은 Git 이력 등의 오류는 배포 전에 실패로 처리합니다.

배포본의 인덱스가 Git에 커밋된 인덱스와 달라도 정상입니다. GitHub Actions 작업 공간에서 새로 생성한 인덱스를 배포에 사용합니다. 저장소에 자동으로 커밋하거나 push하지 않습니다.

로컬 DEBUG 실행에서 `docs/`를 조사해 인덱스를 만드는 C++ 기능은 그대로 사용할 수 있습니다. Pages의 최종 인덱스는 배포 직전에 다시 생성하므로, CI 방식에서는 줄바꿈 보존용 `.gitattributes` 변경이 필수는 아닙니다.

## 캐시 버전의 범위

기존 런타임은 `size + mtime + ctime`을 파일 버전으로 사용합니다. Git 커밋 시각은 초 단위이므로 같은 초에 같은 길이로 변경한 파일, 또는 같은 HEAD에서 다시 만든 같은 길이의 빌드 결과는 이 조합만으로 구분할 수 없습니다. 이번 작업에서는 기존 인덱스 형식과 런타임 API를 유지했으며 콘텐츠 해시 필드는 추가하지 않았습니다.

## 확인한 동작

로컬 Git 저장소에서 CRLF 파일이 checkout 후 LF로 변환되는 사례를 만들고, 워크플로의 실제 Python 명령을 실행했습니다. 인덱스에 기록된 크기가 업로드용 tar 안의 실제 크기와 일치했고, `.nojekyll` 포함, 반복 생성 동일, 원본 바이트 무변경, 자동 커밋 없음도 확인했습니다.

이 자료는 실행 가능한 설정과 생성기입니다. 사용자의 GitHub 설정 변경이나 실제 Actions 실행·Pages 배포를 대신 수행한 결과는 아닙니다.

## 공식 참고 자료

- [GitHub Pages의 사용자 지정 워크플로](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
- [Pages 게시 원본을 GitHub Actions로 설정](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site)
- [actions/upload-pages-artifact](https://github.com/actions/upload-pages-artifact)
