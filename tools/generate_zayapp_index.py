#!/usr/bin/env python3
"""배포할 파일의 실제 바이트 크기로 ZayApp 인덱스를 생성한다.

Python 3.9 이상과 전체 이력을 받은 Git 작업 트리가 필요하다. Pages 빌드가
끝난 다음, docs를 업로드하기 직전에 실행한다. 원본 파일은 수정하지 않는다.

추적 중인 파일의 mtime/ctime은 각각 마지막/최초 Git 커밋의 committer time이다.
체크아웃이나 읽기 때문에 캐시 버전이 바뀌지 않도록 atime은 mtime과 같게 둔다.
미추적 파일은 HEAD 시각을 사용하며, 빌드 등으로 변경된 추적 파일도 mtime에
HEAD 시각을 사용한다. 이 값은 Windows FILETIME의 100ns 단위로 기록한다.

Git 시각은 초 단위다. 같은 초에 같은 길이로 바뀐 파일이나 같은 HEAD에서
다시 만든 같은 길이의 빌드 결과는 기존 인덱스 형식만으로 구분할 수 없다.
"""

import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile


FILETIME_OFFSET = 116444736000000000
UINT64_MAX = (1 << 64) - 1
EXCLUDED_NAMES = {".git", ".github"}


class IndexError(Exception):
    """생성 실패 시 이전 인덱스를 보존하고 원인을 표시하기 위한 오류."""


def git(directory, *arguments):
    command = ["git", "--literal-pathspecs", "-c", "log.showSignature=false",
               "-C", str(directory), *arguments]
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False)
    except OSError as error:
        raise IndexError("Git 실행 실패: " + str(error)) from error
    if result.returncode:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise IndexError("Git 명령 실패: " + message)
    try:
        return result.stdout.decode("utf-8")
    except UnicodeDecodeError as error:
        raise IndexError("Git 파일명은 UTF-8이어야 합니다.") from error


def filetime(seconds):
    value = FILETIME_OFFSET + int(seconds) * 10000000
    if not 0 <= value <= UINT64_MAX:
        raise IndexError("FILETIME 범위를 벗어난 Git 시각: " + str(seconds))
    return str(value)


def validate_path(relative):
    # PC 캐시의 SafeDiskPath 규칙과 JSON 문자열 해석에 맞는 경로만 허용한다.
    if "\\" in relative or ":" in relative:
        raise IndexError("PC 캐시에 사용할 수 없는 경로: " + relative)
    for part in relative.split("/"):
        if not part or part in (".", "..") or part.endswith((".", " ")):
            raise IndexError("PC 캐시에 사용할 수 없는 경로: " + relative)
        if any(ord(char) < 32 or char in '\"<>|?*' for char in part):
            raise IndexError("PC 캐시에 사용할 수 없는 경로: " + relative)
        stem = part.split(".", 1)[0].upper()
        if stem in {"CON", "PRN", "AUX", "NUL"} or (
                len(stem) == 4 and stem[:3] in {"COM", "LPT"} and stem[3].isdigit()):
            raise IndexError("PC 예약 이름을 포함한 경로: " + relative)


def collect_files(root, output):
    def scan_error(error):
        raise error

    files = []
    names = set()
    for current, directories, filenames in os.walk(root, followlinks=False, onerror=scan_error):
        # upload-pages-artifact가 항상 제외하는 Git 메타데이터와 일치시킨다.
        directories[:] = sorted(name for name in directories if name not in EXCLUDED_NAMES)
        filenames = sorted(name for name in filenames if name not in EXCLUDED_NAMES)
        for name in directories + filenames:
            path = Path(current) / name
            if path.is_symlink():
                raise IndexError("Pages에 심볼릭 링크를 배포할 수 없습니다: " + str(path))
        for name in filenames:
            path = Path(current) / name
            relative = path.relative_to(root).as_posix()
            if relative in {output, "assetsindex.json"}:
                continue
            status = path.stat()
            if not stat.S_ISREG(status.st_mode):
                raise IndexError("일반 파일이 아닙니다: " + str(path))
            validate_path(relative)
            folded = relative.casefold()
            if folded in names:
                raise IndexError("대소문자가 충돌하는 파일 경로: " + relative)
            names.add(folded)
            files.append((relative, path, status.st_size))
    return sorted(files, key=lambda item: item[0])


def generate(root, output, version):
    if root.is_symlink():
        raise IndexError("배포 루트는 심볼릭 링크일 수 없습니다: " + str(root))
    if not root.is_dir():
        raise IndexError("배포 폴더가 없습니다: " + str(root))
    root = root.resolve()
    if not output or output in (".", "..") or "/" in output or "\\" in output:
        raise IndexError("--output에는 배포 루트에 둘 파일 이름만 지정하세요.")
    validate_path(output)
    if not version:
        raise IndexError("--version은 비어 있을 수 없습니다.")
    repository = Path(git(root, "rev-parse", "--show-toplevel").rstrip("\r\n")).resolve()
    if git(repository, "rev-parse", "--is-shallow-repository").strip() == "true":
        raise IndexError("전체 Git 이력이 필요합니다. checkout의 fetch-depth를 0으로 설정하세요.")
    head = git(repository, "log", "-1", "--format=%ct", "HEAD").strip()
    if not head:
        raise IndexError("HEAD 커밋이 없습니다.")
    tracked = set(git(repository, "ls-files", "-z").split("\0"))
    changed = set(git(repository, "diff", "--name-only", "-z", "--no-renames", "HEAD", "--").split("\0"))
    entries = []
    fallback_count = 0
    for relative, path, size in collect_files(root, output):
        repo_path = path.relative_to(repository).as_posix()
        history = []
        if repo_path in tracked:
            history = git(repository, "log", "--follow", "--format=%ct", "--", repo_path).splitlines()
        if history:
            created = history[-1]
            modified = head if repo_path in changed else history[0]
            if repo_path in changed:
                fallback_count += 1
        else:
            created = modified = head
            fallback_count += 1
        entries.append({"atime": filetime(modified), "ctime": filetime(created),
                        "mtime": filetime(modified), "path": relative, "size": str(size)})
    if not entries:
        raise IndexError("인덱스에 넣을 배포 파일이 없습니다.")
    document = {"list": entries, "version": version}
    data = (json.dumps(document, ensure_ascii=False, indent="\t") + "\n").encode("utf-8")
    if len(data) > 16 * 1024 * 1024:
        raise IndexError("인덱스가 런타임의 16 MiB 제한을 넘습니다.")
    destination = root / output
    if destination.exists() and destination.read_bytes() == data:
        return destination, len(entries), fallback_count, False
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix="." + output + ".", suffix=".tmp",
                                         dir=root, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, 0o644)
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink()
    return destination, len(entries), fallback_count, True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path("docs"), help="업로드할 폴더 (기본값: docs)")
    parser.add_argument("--output", default="zayapp_index.json", help="루트에 생성할 인덱스 파일 이름")
    parser.add_argument("--version", default="zayapp_index_v1", help="런타임과 동일한 인덱스 버전")
    args = parser.parse_args()
    try:
        destination, count, fallback_count, written = generate(args.root, args.output, args.version)
    except (IndexError, OSError, ValueError) as error:
        print("인덱스 생성 실패: " + str(error), file=sys.stderr)
        return 1
    state = "생성" if written else "유지"
    print(f"{destination}: {count}개 파일, HEAD 시각 사용 {fallback_count}개, {state}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
