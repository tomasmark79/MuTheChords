#!/usr/bin/env python3
"""Deploy MuTheChords plugin artifacts and the explicitly named private external pattern library."""
from pathlib import Path
import base64
import hashlib
import json
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TARGET = "producent@musicbox"
REMOTE = r"C:\MusicBox\Plugins\DotNamePlugins"

def powershell(script):
    encoded = base64.b64encode(script.encode("utf-16le")).decode("ascii")
    result = subprocess.run(["ssh", "-o", "BatchMode=yes", TARGET, "powershell", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    return result.stdout

def main():
    artifacts = ROOT / "dist/windows"
    clap = artifacts / "MuTheChords.clap"
    vst = artifacts / "MuTheChords.vst3"
    library = artifacts / "MuTheChords.patterns.json"
    if not clap.is_file() or not vst.is_dir():
        raise SystemExit("Build Windows VST3 and CLAP artifacts first")
    if not library.is_file():
        raise SystemExit("Missing external pattern library dist/windows/MuTheChords.patterns.json")
    document = json.loads(library.read_text(encoding="utf-8"))
    if document.get("version") != 1 or not document.get("patterns") or not document.get("provenance"):
        raise SystemExit("Invalid external pattern library")
    # Explicit whitelist: never package unrelated dist files or source references.
    files = [clap, library] + sorted(p for p in vst.rglob("*") if p.is_file())
    if not any(p.suffix.lower() == ".vst3" for p in files if p != clap):
        raise SystemExit("VST3 bundle contains no plugin binary")
    # Stage first, so transfer failures never overwrite the installed plugins.
    with tempfile.TemporaryDirectory(prefix="muthechords-deploy-") as tmp:
        archive = Path(tmp) / "muthechords.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
            for file in files:
                z.write(file, file.relative_to(artifacts).as_posix())
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        remote_stage = powershell("[Console]::Write((Join-Path $env:TEMP ('muthechords-' + [Guid]::NewGuid().ToString('N'))))").strip()
        powershell(f"New-Item -ItemType Directory -Path '{remote_stage}' -Force | Out-Null")
        subprocess.run(["scp", "-q", str(archive), f"{TARGET}:{remote_stage.replace(chr(92), '/')}/package.zip"], check=True)
        # Verify precisely the packaged files, including the external JSON.
        relative_names = [file.relative_to(artifacts).as_posix().replace("/", "\\") for file in files]
        expected_paths = ",".join("'" + name.replace("'", "''") + "'" for name in relative_names)
        script = f"""
$ErrorActionPreference='Stop'
$stage='{remote_stage}'
$package=Join-Path $stage 'package.zip'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $package).Hash.ToLower() -ne '{digest}') {{ throw 'Transfer hash mismatch' }}
Expand-Archive -LiteralPath $package -DestinationPath (Join-Path $stage 'files')
New-Item -ItemType Directory -Path '{REMOTE}' -Force | Out-Null
# Fail before replacing any artifact if Bitwig still has an installed file open.
@({expected_paths}) | ForEach-Object {{
    $path=Join-Path '{REMOTE}' $_
    if (Test-Path -LiteralPath $path -PathType Leaf) {{
        $handle=[System.IO.File]::Open($path,[System.IO.FileMode]::Open,[System.IO.FileAccess]::ReadWrite,[System.IO.FileShare]::None)
        $handle.Dispose()
    }}
}}
Get-ChildItem -LiteralPath (Join-Path $stage 'files') | ForEach-Object {{ Copy-Item -LiteralPath $_.FullName -Destination '{REMOTE}' -Recurse -Force }}
@({expected_paths}) | ForEach-Object {{ $path=Join-Path '{REMOTE}' $_; [PSCustomObject]@{{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()}} }} | ConvertTo-Json -Compress
Remove-Item -LiteralPath $stage -Recurse -Force
"""
        remote_files = json.loads(powershell(script))
        if isinstance(remote_files, dict):
            remote_files = [remote_files]
        hashes = {item["path"].lower(): item["sha256"] for item in remote_files}
        for file in files:
            remote_name = REMOTE + "\\" + str(file.relative_to(artifacts)).replace("/", "\\")
            if hashes.get(remote_name.lower()) != hashlib.sha256(file.read_bytes()).hexdigest():
                raise SystemExit(f"Remote verification failed: {remote_name}")
        print(f"Deployed and verified {len(files)} files in {TARGET}:{REMOTE}")

if __name__ == "__main__":
    main()
