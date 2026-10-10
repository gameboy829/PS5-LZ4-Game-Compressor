// SPDX-License-Identifier: GPL-3.0-or-later
// Application adapter for the MIT-licensed PicoSHA2 dependency.
#pragma once
#include "../third_party/picosha2/picosha2.h"
struct sha256_ctx_t { picosha2::hash256_one_by_one hash; };
static inline void sha256_init(sha256_ctx_t* c) { c->hash.init(); }
static inline void sha256_update(sha256_ctx_t* c,const void* data,size_t size) {
    if(size){const auto* p=static_cast<const unsigned char*>(data);c->hash.process(p,p+size);}
}
static inline void sha256_final(sha256_ctx_t* c,unsigned char out[32]) {
    c->hash.finish();c->hash.get_hash_bytes(out,out+32);
}
struct Sha {
    sha256_ctx_t context;
    Sha(){sha256_init(&context);}
    void add(const void* data,size_t size){sha256_update(&context,data,size);}
    Bytes finish(){Bytes out(32);sha256_final(&context,out.data());return out;}
};
