"""Build an allowlisted portable Windows preview, without ROMs or local evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1]
VERSION="0.1.0-preview"


def package(destination):
    destination=Path(destination).resolve()
    destination.mkdir(parents=True,exist_ok=True)
    folder=destination/f"Alleyway-PC-{VERSION}-windows-x64"
    if folder.exists():
        raise ValueError(f"Output already exists: {folder}")
    subprocess.run([sys.executable,str(ROOT/"tools/build.py")],check=True,cwd=ROOT)
    folder.mkdir()
    for source,name in ((ROOT/"build/alleyway-lab.exe","Alleyway-PC.exe"),
                        (ROOT/"docs/RELEASE_README.txt","LEEME.txt"),
                        (ROOT/"LICENSE","LICENSE"),(ROOT/"CHANGELOG.md","CHANGELOG.md")):
        shutil.copyfile(source,folder/name)
    revision=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True).strip())
    manifest={"version":VERSION,"source_revision":revision,"source_dirty":dirty,"files":{}}
    for file in sorted(folder.iterdir()):
        manifest["files"][file.name]={"bytes":file.stat().st_size,"sha256":hashlib.sha256(file.read_bytes()).hexdigest()}
    (folder/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    archive=destination/(folder.name+".zip")
    with zipfile.ZipFile(archive,"w",compression=zipfile.ZIP_DEFLATED) as z:
        for file in sorted(folder.iterdir()):
            info=zipfile.ZipInfo(folder.name+"/"+file.name,date_time=(2026,1,1,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            z.writestr(info,file.read_bytes())
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n",encoding="ascii")
    print(json.dumps({"archive":str(archive),"sha256":digest,"source_dirty":dirty}))
    return archive


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=ROOT/"build/release")
    package(parser.parse_args().output)
