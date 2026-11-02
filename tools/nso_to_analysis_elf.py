"""Create a local analysis ELF from a user-supplied NSO (never distribute it).

Addresses remain module-relative. This does not apply dynamic relocations,
resolve imports, or claim to produce a standalone runnable ELF.
"""
import argparse
from pathlib import Path
import struct
from nso import Nso


def convert(source, destination):
    n=Nso(source)
    out=bytearray(4096)
    out[:16]=b'\x7fELF\x02\x01\x01'+b'\0'*9
    struct.pack_into('<HHIQQQIHHHHHH',out,16,2,183,1,0,64,0,0,64,56,3,0,0,0)
    for i,(address,size) in enumerate((n.text,n.rodata,n.data)):
        offset=(len(out)+4095)&~4095
        out.extend(b'\0'*(offset-len(out)))
        out.extend(n.image[address:address+size])
        mem_size=size+(n.bss_size if i==2 else 0)
        struct.pack_into('<IIQQQQQQ',out,64+i*56,1,(5,4,6)[i],offset,address,address,size,mem_size,4096)
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_bytes(out)
    print('Analysis ELF:',destination,'build:',n.build_id)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('destination',type=Path)
    a=p.parse_args();convert(a.source,a.destination)
