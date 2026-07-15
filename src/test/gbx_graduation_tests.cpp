// IDEE X — a market graduates by its OWN depth. Unit proofs impossible on live
// regtest: full curve, mainnet-scale arithmetic, exact frontiers, h_M slack.
#include <consensus/gbx_launchpad.h>
#include <consensus/gbx_curve.h>
#include <consensus/gbx_token.h>
#include <consensus/params.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <cstdio>
#include <cstring>
#include <util/translation.h>
const TranslateFn G_TRANSLATION_FUN{nullptr};
using namespace gbx;

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){printf("  FAIL: %s\n",m);++fails;} else printf("  ok  : %s\n",m);}while(0)

static uint256 CID = [](){ uint256 h; for(int i=0;i<32;i++) h.begin()[i]=(unsigned char)(i+7); return h; }();
static const std::vector<unsigned char> PK(33, 0x02);

static CMutableTransaction Base(){
    CMutableTransaction m;
    uint256 prev; prev.begin()[0]=0xAA;
    m.vin.push_back(CTxIn(COutPoint(Txid::FromUint256(prev), 0)));
    return m;
}
static CurveIntent I(CurveOp op, int64_t amount=0, int64_t tok=0){
    CurveIntent i; i.op=op; i.coin_id=CID; i.amount=amount; i.tokens_out=tok; i.pubkey=PK; return i;
}
// GRADUATE tx: whole reserve -> pool, pool_tokens derived exactly as the validator does.
static CTransaction GradTx(int64_t reserve_in){
    auto m = Base();
    const int64_t sold = CurveTokensSold(reserve_in);
    const int64_t pool_tokens = CURVE_LP_TOKENS + (CURVE_TOKENS - sold);
    m.vout.emplace_back(reserve_in, PoolScriptPubKey(CID, pool_tokens));
    return CTransaction(m);
}
static CurveError Grad(int64_t R, int64_t m_in, uint32_t hm_in, int spend_h, int win){
    return CheckCurveTransition(GradTx(R), I(CurveOp::GRADUATE), R, m_in, hm_in, 1, spend_h, 100, win);
}
// BUY tx with an explicitly declared h_M stamp on the new curve output.
static CurveError Buy(int64_t R, int64_t m_in, uint32_t hm_in, int64_t gross,
                      int64_t m_decl, uint32_t hm_decl, int spend_h, int win){
    const int64_t fee = CurveFee(gross), net = gross - fee;
    int64_t tok=0, newR=0;
    if(!CurveBuy(R, net, tok, newR)){ printf("  FAIL: CurveBuy refused a test buy\n"); ++fails; return CurveError::BAD_AMOUNT; }
    auto m = Base();
    m.vout.emplace_back(newR, CurveScriptPubKey(CID, m_decl, hm_decl));
    m.vout.emplace_back(0,    TokenScriptPubKey(CID, tok, PK));
    m.vout.emplace_back(fee,  CurveBurnScript());
    return CheckCurveTransition(CTransaction(m), I(CurveOp::BUY, gross, tok), R, m_in, hm_in, 1, spend_h, 100, win);
}
// REFUND tx: holder proves tokens in the input witness; declared (M,h) on the output.
static CurveError Refund(int64_t R, int64_t m_in, uint32_t hm_in, int64_t amount,
                         int64_t m_decl, uint32_t hm_decl, int spend_h){
    const int64_t sold = CurveTokensSold(R);
    const int64_t out  = (int64_t)(((u128)R * (u128)amount) / (u128)sold);
    auto m = Base();
    const CScript tws = TokenWitnessScript(CID, amount, PK);
    m.vin[0].scriptWitness.stack.push_back(std::vector<unsigned char>(tws.begin(), tws.end()));
    m.vout.emplace_back(R - out, CurveScriptPubKey(CID, m_decl, hm_decl));
    return CheckCurveTransition(CTransaction(m), I(CurveOp::REFUND, amount, 0), R, m_in, hm_in, spend_h-100, spend_h, 100, 30);
}

int main(){
    printf("\n== IDEE X · graduation by the coin's own depth (unit proofs) ==\n");
    const int H = 100000, K = 30;
    const int64_t N = CURVE_GRAD_DEPTH_N, RMIN = CURVE_GRAD_MIN_SAT;

    printf("\n[1] FULL CURVE beats any live M\n");
    int64_t Rf = (int64_t)(CURVE_K / (u128)(CURVE_V_TOKENS - CURVE_TOKENS)) - CURVE_V_GBX_SAT;
    while (CurveTokensSold(Rf) <  CURVE_TOKENS) ++Rf;
    while (CurveTokensSold(Rf-1) >= CURVE_TOKENS) --Rf;
    CHECK(CurveTokensSold(Rf)==CURVE_TOKENS && CurveTokensSold(Rf-1)<CURVE_TOKENS, "exact full-curve reserve found");
    printf("      R_full = %lld sat (~%lld GBX)\n",(long long)Rf,(long long)(Rf/COIN));
    CHECK(Grad(Rf, Rf*10, (uint32_t)H, H, K)==CurveError::OK, "full curve graduates even with a giant LIVE M");
    CHECK(Grad(Rf-1, Rf*10, (uint32_t)H, H, K)==CurveError::NOT_GRADUATED, "one token short of full + giant M = not graduated");

    printf("\n[2] frontier R vs N*M (M live)\n");
    const int64_t M1 = 500LL*COIN, BAR = M1*N;
    CHECK(Grad(BAR,   M1, (uint32_t)H, H, K)==CurveError::OK,             "R == N*M exactly -> legal");
    CHECK(Grad(BAR-1, M1, (uint32_t)H, H, K)==CurveError::NOT_GRADUATED,  "R == N*M - 1 sat -> illegal");

    printf("\n[3] floor R_MIN, M == 0\n");
    CHECK(Grad(RMIN,   0, 0, H, K)==CurveError::OK,            "M==0: R == R_MIN exactly -> legal");
    CHECK(Grad(RMIN-1, 0, 0, H, K)==CurveError::NOT_GRADUATED, "M==0: R == R_MIN - 1 sat -> illegal");

    printf("\n[4] window: K binds, K+1 forgets\n");
    const int64_t Rmid = 5000LL*COIN;   // >= R_MIN, < N*M1
    CHECK(Grad(Rmid, M1, (uint32_t)(H-K),   H, K)==CurveError::NOT_GRADUATED, "age == K: M still binds");
    CHECK(Grad(Rmid, M1, (uint32_t)(H-K-1), H, K)==CurveError::OK,            "age == K+1: M forgotten, bar breathes down");

    printf("\n[5] mainnet scale: no int64 overflow on N*M\n");
    CHECK(MAX_MONEY <= std::numeric_limits<int64_t>::max()/N, "N*MAX_MONEY fits int64 (written proof, not assumed)");
    CHECK(Grad(MAX_MONEY, MAX_MONEY, (uint32_t)H, H, 201600)==CurveError::OK, "R=MAX_MONEY (full curve) + M=MAX_MONEY live, K mainnet -> legal, no overflow");
    const int64_t Mbig = 4000LL*COIN;   // N*M = 80k GBX, below full-curve reserve
    CHECK(Grad(Mbig*N,   Mbig, (uint32_t)H, H, 201600)==CurveError::OK,            "mainnet K: R == N*M -> legal");
    CHECK(Grad(Mbig*N-1, Mbig, (uint32_t)H, H, 201600)==CurveError::NOT_GRADUATED, "mainnet K: R == N*M - 1 -> illegal");

    printf("\n[6] h_M slack on a record BUY (client declares, consensus demands freshness)\n");
    const int64_t R0 = 1000LL*COIN, gross = 100LL*COIN;
    const int64_t trade = gross - CurveFee(gross);           // the new record M
    CHECK(Buy(R0, 1*COIN, (uint32_t)(H-5), gross, trade, (uint32_t)H,     H, K)==CurveError::OK, "stamp == spend_height -> accepted");
    CHECK(Buy(R0, 1*COIN, (uint32_t)(H-5), gross, trade, (uint32_t)(H-100), H, K)==CurveError::OK, "stamp == spend-100 -> accepted");
    CHECK(Buy(R0, 1*COIN, (uint32_t)(H-5), gross, trade, (uint32_t)(H-101), H, K)!=CurveError::OK, "stamp == spend-101 -> REJECTED (stale)");
    CHECK(Buy(R0, 1*COIN, (uint32_t)(H-5), gross, trade, (uint32_t)(H+1),   H, K)!=CurveError::OK, "stamp in the FUTURE -> REJECTED");

    printf("\n[7] non-record BUY cannot touch (M, h_M); expired M breathes on BUY too\n");
    const int64_t Mhuge = 900LL*COIN;                         // > trade
    CHECK(Buy(R0, Mhuge, (uint32_t)(H-5), gross, Mhuge, (uint32_t)(H-5), H, K)==CurveError::OK,  "non-record: (M,h) carried unchanged -> accepted");
    CHECK(Buy(R0, Mhuge, (uint32_t)(H-5), gross, Mhuge, (uint32_t)H,     H, K)!=CurveError::OK,  "non-record: re-stamping M fresh -> REJECTED");
    CHECK(Buy(R0, Mhuge, (uint32_t)(H-K-1), gross, trade, (uint32_t)H,   H, K)==CurveError::OK,  "expired M: small trade becomes the new M -> accepted");
    CHECK(Buy(R0, Mhuge, (uint32_t)(H-K-1), gross, Mhuge, (uint32_t)(H-K-1), H, K)!=CurveError::OK, "expired M kept alive unchanged -> REJECTED");

    printf("\n[8] REFUND leaves (M, h_M) untouched — THE LAW is not a trade\n");
    const int64_t sold = CurveTokensSold(R0), half = sold/2;
    CHECK(Refund(R0, 7*COIN, (uint32_t)(H-3), half, 7*COIN, (uint32_t)(H-3), H)==CurveError::OK, "refund with (M,h) unchanged -> accepted");
    CHECK(Refund(R0, 7*COIN, (uint32_t)(H-3), half, 0,      (uint32_t)H,     H)!=CurveError::OK, "refund that rewrites M -> REJECTED");
    CHECK(Refund(R0, 7*COIN, (uint32_t)(H-K-90), half, 7*COIN, (uint32_t)(H-K-90), H)==CurveError::OK, "refund with EXPIRED M still carries it unchanged -> accepted");

    printf("\n%s (fails=%d)\n\n", fails? "== RED ==":"== ALL GREEN ==", fails);
    return fails?1:0;
}
