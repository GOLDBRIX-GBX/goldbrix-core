// IDEE W — a coin is born of work. Does the proof-of-work rule actually bite at
// mainnet difficulty (where regtest's max target cannot exercise "hash > target")?
#include <consensus/gbx_launchpad.h>
#include <consensus/params.h>
#include <arith_uint256.h>
#include <cstdio>
#include <cstring>
using namespace gbx;

// Every Bitcoin binary defines this itself; a standalone test must too.
#include <util/translation.h>
const TranslateFn G_TRANSLATION_FUN{nullptr};

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){printf("  FAIL: %s\n",m);++fails;} else printf("  ok  : %s\n",m);}while(0)

static uint256 COIN_ID = [](){ uint256 h; for(int i=0;i<32;i++) h.begin()[i]=(unsigned char)(i+1); return h; }();

// Assemble an 80-byte header. merkle defaults to coin_id. Caller sets version+nonce.
static std::vector<unsigned char> Header(const uint256& merkle, uint32_t version, uint32_t nBits, uint32_t nonce){
    std::vector<unsigned char> h(80,0);
    auto put32=[&](int off,uint32_t v){ h[off]=v&0xff; h[off+1]=(v>>8)&0xff; h[off+2]=(v>>16)&0xff; h[off+3]=(v>>24)&0xff; };
    put32(0, version);
    // prevhash 4..35 left zero (caller passes prev_height; lookup is done by validation, not here)
    std::memcpy(h.data()+36, merkle.begin(), 32);
    put32(68, 0);          // time
    put32(72, nBits);
    put32(76, nonce);
    return h;
}

int main(){
    printf("\n== IDEE W · CheckCreatePoW (mainnet difficulty) ==\n");
    Consensus::Params p;
    p.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    p.nLaunchpadHeight = 1;
    // mainnet-like bits: 0x1d1ccbbb (today's live difficulty)
    const uint32_t NBITS = 0x1d1ccbbb;
    const int prev_h = 1000, spend_h = 1001;   // fresh

    // 1) wrong length
    CHECK(!CheckCreatePoW(std::vector<unsigned char>(79,0), COIN_ID, NBITS, prev_h, spend_h, p),
          "79-byte proof REJECTED (must be exactly 80)");

    // 2) merkle != coin_id
    uint256 other = COIN_ID; other.begin()[0]^=0xff;
    CHECK(!CheckCreatePoW(Header(other,0x20000000,NBITS,0), COIN_ID, NBITS, prev_h, spend_h, p),
          "merkle != coin_id REJECTED (proof bound to its coin)");

    // 3) AuxPoW version bit set
    CHECK(!CheckCreatePoW(Header(COIN_ID,0x20000000|0x100,NBITS,0), COIN_ID, NBITS, prev_h, spend_h, p),
          "AuxPoW bit REJECTED (proof cannot be a merged-mining by-product)");

    // 4) stale: spend_h - prev_h beyond the window
    CHECK(!CheckCreatePoW(Header(COIN_ID,0x20000000,NBITS,0), COIN_ID, NBITS, 1, 1+CREATE_POW_MAX_AGE+1, p),
          "stale proof REJECTED (work cannot be stockpiled)");

    // 5) INSUFFICIENT WORK: a header we did NOT mine (nonce 0) almost surely hashes ABOVE
    //    the eased mainnet target -> must be rejected. This is the path regtest cannot test.
    bool anyAccepted=false;
    for(uint32_t nonce=0; nonce<8; ++nonce){
        if(CheckCreatePoW(Header(COIN_ID,0x20000000,NBITS,nonce), COIN_ID, NBITS, prev_h, spend_h, p)) anyAccepted=true;
    }
    CHECK(!anyAccepted, "un-mined header (hash > eased target) REJECTED at mainnet difficulty");

    // 6) determinism: same inputs, same verdict, twice
    bool a=CheckCreatePoW(Header(COIN_ID,0x20000000,NBITS,5), COIN_ID, NBITS, prev_h, spend_h, p);
    bool b=CheckCreatePoW(Header(COIN_ID,0x20000000,NBITS,5), COIN_ID, NBITS, prev_h, spend_h, p);
    CHECK(a==b, "deterministic (same header -> same verdict)");

    printf("\n%s  (%d failures)\n\n", fails?"TESTS FAILED":"ALL CREATE-POW TESTS PASSED", fails);
    return fails?1:0;
}
