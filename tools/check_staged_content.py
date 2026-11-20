"""Reject local game content and oversized blobs from the Git index."""

import subprocess
import sys
from pathlib import PurePosixPath


MAX_BYTES = 50 * 1024 * 1024


def main():
    diff = subprocess.check_output(
        ["git", "diff", "--cached", "--raw", "--no-abbrev", "--diff-filter=ACMRT", "-z"]
    )
    records = iter(diff.split(b"\0"))
    errors = []
    for metadata in records:
        if not metadata:
            break
        _, mode, _, blob, status = metadata.split()
        path = next(records).decode("utf-8", "replace")
        if status[:1] in (b"R", b"C"):
            path = next(records).decode("utf-8", "replace")
        parts = PurePosixPath(path.lower()).parts
        if parts[0] in {"build", "extracted", "generated", "save"} or parts[0].startswith(
            ("extracted-", "generated-")
        ) or parts[0] in {f"aoc{i}" for i in range(1, 9)} or parts[-1] in {
            "savagewind.exe", "savagewind-config.exe", "swgame.dll", "swgame.dll.tmp",
        }:
            errors.append(f"{path}: local game data or build output")
        elif mode != b"160000":
            size = int(subprocess.check_output(["git", "cat-file", "-s", blob.decode("ascii")]))
            if size > MAX_BYTES:
                errors.append(f"{path}: staged blob exceeds 50 MiB")
    for error in errors:
        print(error, file=sys.stderr)
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
