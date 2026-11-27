#!/usr/bin/env python3
"""Fetch the Intellifex LTD's EPROM and user's manual into `inputs/`.

The repository holds neither file. When neither the project root nor `inputs/`
has a file with the EPROM's SHA-256, this script downloads the zip that holds
the image from the Internet Archive's copies of dbwbp.com's synth firmware
library, checks it and writes the image into `inputs/`; the manual comes the
same way from an archived copy on handelima.net. Any file with the right
SHA-256 counts, whatever its name, the zip included. When the EPROM cannot be
downloaded the script lists what to find by hand; the manual is a reference
only, so without it the build goes on.

Standard library only.
"""

import io
import sys
import time
import urllib.request
import zipfile
from typing import NamedTuple

from build_workspace import (
    EPROM_SHA256,
    EPROM_SIZE,
    INPUT_DIRS,
    MANUAL_SHA256,
    digest,
    find_input,
)

INPUTS = INPUT_DIRS[1]
TIMEOUT = 60
ATTEMPTS = 3
RETRY_SECONDS = 10


class Source(NamedTuple):
    name: str
    size: int
    sha256: str
    what: str
    origin: str
    captures: tuple[str, ...]

    @property
    def urls(self) -> tuple[str, ...]:
        # `id_` asks the Wayback Machine for the bytes as they were served,
        # without its page rewriting.
        archived = tuple(
            f"https://web.archive.org/web/{capture}id_/{self.origin}"
            for capture in self.captures
        )
        return (*archived, self.origin)


# The 2025 capture of the zip is the site's bot check, not the file.
EPROM_ZIP = Source(
    name="Rocktron Intellifex LDT firmware v1.0 - Jan-1995 eprom 27C256.zip",
    size=17270,
    sha256="bfab15b9db9b90aa4ec6bf60cc7ad140086bf1dce472885ce0fadd6a8c0f8b56",
    what="the zip holding the 27C256 EPROM image of firmware v1.0, January 1995",
    origin="https://dbwbp.com/synthbin/Rocktron%20Intellifex%20LDT%20firmware%20v1.0"
    "%20-%20Jan-1995%20eprom%2027C256.zip",
    captures=("20240922074930", "20221130141027", "20181026234338", "20170711203911"),
)
EPROM_IMAGE = "Rocktron Intellifex LDT firmware v1.0 - Jan-1995 eprom 27C256.BIN"
MANUAL = Source(
    name="intellifex_ltd.pdf",
    size=1256827,
    sha256=MANUAL_SHA256,
    what="Rocktron's Intellifex LTD user's manual, 1995 (60 pages)",
    origin="https://www.handelima.net/studiogear/manuals/Rocktron/ROCKTRON%20INTELLIFEX"
    "/ifxltd.pdf",
    captures=("20231010135543", "20210519162717", "20140327064220"),
)


def download(source: Source) -> bytes | None:
    request_headers = {"User-Agent": "ifx-fetch-inputs/1"}
    for url in source.urls:
        for attempt in range(1, ATTEMPTS + 1):
            try:
                request = urllib.request.Request(url, headers=request_headers)
                with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
                    data = response.read()
            except OSError as error:
                if attempt < ATTEMPTS:
                    time.sleep(RETRY_SECONDS)
                    continue
                print(f"  {url}: {error}")
                break
            if len(data) == source.size and digest(data) == source.sha256:
                return data
            print(f"  {url}: {len(data)} bytes that are not {source.name}")
            break
    return None


def write_input(name: str, data: bytes) -> None:
    INPUTS.mkdir(parents=True, exist_ok=True)
    partial = INPUTS / f"{name}.part"
    partial.write_bytes(data)
    partial.replace(INPUTS / name)
    print(f"  written to {INPUTS / name}")


def fetch_eprom(online: bool) -> bool:
    path = find_input(EPROM_SHA256)
    if path is not None:
        print(f"EPROM: {path}")
        return True
    path = find_input(EPROM_ZIP.sha256)
    if path is not None:
        print(f"EPROM: extracting {path}")
        archive = path.read_bytes()
    elif online:
        print(f"EPROM: downloading {EPROM_ZIP.name}")
        archive = download(EPROM_ZIP)
        if archive is None:
            return False
    else:
        return False
    with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
        image = zipped.read(EPROM_IMAGE)
    if len(image) != EPROM_SIZE or digest(image) != EPROM_SHA256:
        raise SystemExit(f"{EPROM_IMAGE}: does not match its SHA-256")
    write_input(EPROM_IMAGE, image)
    return True


def fetch_manual() -> bool:
    path = find_input(MANUAL.sha256)
    if path is not None:
        print(f"manual: {path}")
        return True
    print(f"manual: downloading {MANUAL.name}")
    data = download(MANUAL)
    if data is None:
        return False
    write_input(MANUAL.name, data)
    return True


def ask_for(source: Source, *also: str) -> None:
    print(f"\n{source.name}: could not be downloaded")
    print(f"  {source.what}, {source.size} bytes")
    print(f"  sha256 {source.sha256}")
    print(f"  address: {source.origin}")
    for line in also:
        print(f"  {line}")
    print(
        f"Find it elsewhere, in an archive or a mirror, and put it into {INPUTS}/\n"
        "under any name: a file counts when its SHA-256 matches.\n",
        flush=True,
    )


def main() -> int:
    if not fetch_manual():
        ask_for(MANUAL)
        print("manual: missing; it is a reference only, so the build goes on")
    if fetch_eprom(online=True):
        return 0
    ask_for(
        EPROM_ZIP,
        f"or the image inside it, {EPROM_IMAGE}, {EPROM_SIZE} bytes",
        f"  sha256 {EPROM_SHA256}",
    )
    if not sys.stdin.isatty():
        raise SystemExit("then run the make target again")
    while True:
        try:
            input("Press Enter once it is there, or Ctrl-C to stop. ")
        except (EOFError, KeyboardInterrupt):
            raise SystemExit("\nstopped") from None
        if fetch_eprom(online=False):
            return 0
        print("  still missing: the EPROM or its zip")


if __name__ == "__main__":
    sys.exit(main())
