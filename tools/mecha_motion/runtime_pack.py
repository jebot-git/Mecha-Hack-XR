"""Pack verified native poses for client-side sampling without a training runtime."""
import argparse
import json
from pathlib import Path

import numpy as np
from .verify import file_hash


def pack(library, dest):
    manifest = library / "actions.json"
    dest.mkdir(parents=True, exist_ok=True)
    index = {"version": 1, "fps": 24, "source": file_hash(manifest), "actors": {}}
    with (dest / "poses.bin").open("wb") as stream:
        for actor, row in sorted(json.loads(manifest.read_bytes()).items()):
            source = library / row["native"]
            if file_hash(source) != row["files"][source.name] or row["fps"] != index["fps"]:
                raise ValueError(f"Unverified poses for {actor}")
            with np.load(source, allow_pickle=False) as clip:
                parts = json.loads(clip["parts_json"].item())
                values = clip["poses"].copy()
                # Horizontal displacement and collision stay with the simulation.
                # The caller applies vertical travel only to locally presented gestures.
                height = clip["root"][:, 1]
                names, offsets = clip["names"].tolist(), clip["offsets"].tolist()
                entry = {"parts": [x["name"] for x in parts], "kind":row["kind"],
                         "family":row["family"], "clips":{}}
                if actor in ("mecha_ace","mecha_ace_alt"):
                    entry["elbows"] = parts
                for name,a,b in zip(names,offsets[:-1],offsets[1:],strict=True):
                    poses = values[a:b]
                    scale = max(.001,float(abs(poses[:,:,4:]).max()),float(abs(height[a:b]).max()))/32767
                    q = np.clip(poses[:,:,:4],-1,1)*32767
                    data = np.concatenate((q,poses[:,:,4:]/scale), axis=-1)
                    data = np.column_stack((data.reshape(b-a,-1), height[a:b]/scale))
                    packed = np.rint(data).astype("<i2")
                    if abs(data-packed).max() > .502:
                        raise ValueError(f"Quantization overflow: {actor}/{name}")
                    entry["clips"][name] = [stream.tell()//2,b-a,scale]
                    stream.write(packed.tobytes())
                index["actors"][actor] = entry
    (dest/"library.json").write_text(json.dumps(index,separators=(",",":"),allow_nan=False)+"\n")

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("library",type=Path)
    p.add_argument("dest",type=Path)
    args=p.parse_args()
    pack(args.library,args.dest)
