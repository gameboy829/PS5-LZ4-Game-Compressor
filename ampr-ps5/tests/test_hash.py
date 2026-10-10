#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib,subprocess,tempfile,hashlib
root=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='lz4-hash-') as tmp:
    tmp=pathlib.Path(tmp)
    source=tmp/'hash.cpp'
    source.write_text('''#include <vector>
#include <iostream>
#include <iomanip>
using Bytes=std::vector<unsigned char>;
#include "portable_sha256.h"
int main(int argc,char**argv){size_t step=std::stoull(argv[1]);Sha h;Bytes data(step);while(std::cin){std::cin.read((char*)data.data(),data.size());h.add(data.data(),size_t(std::cin.gcount()));}for(auto c:h.finish())std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<int(c);}
''')
    exe=tmp/'hash'
    subprocess.run(['g++','-std=c++17','-O2','-I'+str(root/'ampr-native/src'),str(source),'-o',str(exe)],check=True)
    cases=[b'',b'abc',b'a'*1000000]+[bytes(i%251 for i in range(n)) for n in (1,55,56,63,64,65,127,128,129,1048579)]
    count=0
    for data in cases:
        for step in (1,7,55,64,65,8192):
            got=subprocess.run([str(exe),str(step)],input=data,capture_output=True,check=True).stdout.decode()
            assert got==hashlib.sha256(data).hexdigest(),(len(data),step,got)
            count+=1
    print(f'PASS: {count} SHA-256 checks against hashlib, including empty input, million-a vector and fragmented block boundaries')
