// IDEE V — consensus curve rules: adversarial tests.
// Every test answers one question: can anyone steal, strand, or fake money?
#include <consensus/gbx_curve.h>
#include <cstdio>
#include <cassert>
using namespace gbx;
static int fails = 0;
#define CHECK(cond, msg) do { if(!(cond)) { printf("  FAIL: %s\n", msg); ++fails; } else printf("  ok  : %s\n", msg); } while(0)

int main(){
    const int64_t SAT = 100000000LL;
    int64_t tok=0, res=0, out=0;

    printf("\n[1] Buy is monotone and bounded\n");
    CHECK(CurveBuy(0, 100*SAT, tok, res), "first buy of 100 GBX accepted");
    CHECK(res == 100*SAT, "reserve equals what was paid in");
    CHECK(tok > 0 && tok < CURVE_TOKENS, "tokens emitted within the curve supply");
    int64_t tok2=0,res2=0;
    CHECK(CurveBuy(res, 100*SAT, tok2, res2), "second identical buy accepted");
    CHECK(tok2 < tok, "later buyers get fewer tokens: the price rises");

    printf("\n[2] Nobody can drain more than the reserve holds\n");
    int64_t big_tokens = CurveTokensSold(res2);
    CHECK(CurveSell(res2, big_tokens, out, res), "selling every token ever emitted is allowed");
    CHECK(out <= res2, "payout never exceeds the reserve");
    CHECK(res >= 0, "reserve never goes negative");
    CHECK(!CurveSell(res2, big_tokens + 1, out, res), "selling more tokens than exist is REJECTED");

    printf("\n[3] Round-trip cannot mint money (no free lunch)\n");
    int64_t r=0,t=0;
    CurveBuy(0, 1000*SAT, t, r);
    int64_t back=0, r2=0;
    CurveSell(r, t, back, r2);
    CHECK(back <= 1000*SAT, "buying then selling never returns more than was paid");
    printf("       paid=%lld got_back=%lld dust_left=%lld\n", (long long)(1000*SAT), (long long)back, (long long)r2);

    printf("\n[4] Dust and zero are rejected\n");
    CHECK(!CurveBuy(0, 0, t, r), "zero buy REJECTED");
    CHECK(!CurveBuy(0, -1, t, r), "negative buy REJECTED");
    CHECK(!CurveSell(0, 10, out, r), "selling into an empty curve REJECTED");
    CHECK(!CurveSell(100*SAT, 0, out, r), "zero sell REJECTED");

    printf("\n[5] Fee is exact, integer, and cannot be gamed\n");
    CHECK(CurveFee(10000) == 50, "0.50%% of 10000 = 50");
    CHECK(CurveFee(0) == 0, "fee of zero is zero");
    CHECK(CurveFee(1) == 0, "fee floors down on dust (never rounds up against the user)");

    printf("\n[6] Determinism: the same input always gives the same output\n");
    int64_t a1=0,b1=0,a2=0,b2=0;
    CurveBuy(123456789, 987654321, a1, b1);
    CurveBuy(123456789, 987654321, a2, b2);
    CHECK(a1==a2 && b1==b2, "identical calls produce identical results");

    printf("\n%s  (%d failures)\n\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails);
    return fails ? 1 : 0;
}
