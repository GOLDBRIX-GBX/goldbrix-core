// IDEE V — transition rules against real transactions.
// The question: can a thief spend a curve reserve and keep the money?
#include <consensus/gbx_launchpad.h>
#include <primitives/transaction.h>
#include <cstdio>
#include <cstring>
using namespace gbx;
static int fails=0;
#define CHECK(cond,msg) do{ if(!(cond)){printf("  FAIL: %s\n",msg);++fails;} else printf("  ok  : %s\n",msg);}while(0)

static uint256 COIN_ID = uint256::ONE;
static const int64_t SAT = 100000000LL;

// Build the OP_RETURN carrying an intent.
static CTxOut IntentOut(CurveOp op, const uint256& id, int64_t amount)
{
    std::vector<unsigned char> d;
    const char* tag = "GBX:C:";
    d.insert(d.end(), tag, tag+6);
    d.push_back((unsigned char)op);
    d.insert(d.end(), id.begin(), id.end());
    for (int i=7;i>=0;--i) d.push_back((unsigned char)((amount >> (i*8)) & 0xff)); // big-endian
    CScript spk; spk << OP_RETURN << d;
    return CTxOut(0, spk);
}
static CTxOut CurveOut(int64_t value){ return CTxOut(value, CurveScriptPubKey(COIN_ID)); }
static CTxOut BurnOut(int64_t value){ return CTxOut(value, CurveBurnScript()); }
static CTxOut UserOut(int64_t value){ CScript s; s << OP_0 << std::vector<unsigned char>(20,0x11); return CTxOut(value,s); }

static CurveError Run(const std::vector<CTxOut>& outs, CurveOp op, int64_t amount, int64_t reserve_in,
                      int curve_h=0, int spend_h=1000000)
{
    CMutableTransaction mtx;
    mtx.vin.emplace_back();                                   // the curve input (value comes from reserve_in)
    for (const auto& o : outs) mtx.vout.push_back(o);
    mtx.vout.push_back(IntentOut(op, COIN_ID, amount));
    const CTransaction tx(mtx);
    auto intent = ParseCurveIntent(tx);
    if (!intent) return CurveError::NO_INTENT;
    return CheckCurveTransition(tx, *intent, reserve_in, curve_h, spend_h);
}

int main(){
    printf("\n[A] An honest buy is accepted\n");
    {
        const int64_t gross = 100*SAT, fee = CurveFee(gross), net = gross - fee;
        int64_t tok=0, newres=0; CurveBuy(0, net, tok, newres);
        CHECK(Run({CurveOut(newres), BurnOut(fee)}, CurveOp::BUY, gross, 0) == CurveError::OK,
              "buy with correct reserve and burned fee ACCEPTED");
    }

    printf("\n[B] A thief cannot keep the reserve\n");
    {
        const int64_t gross = 100*SAT, fee = CurveFee(gross), net = gross-fee;
        int64_t tok=0, newres=0; CurveBuy(0, net, tok, newres);
        CHECK(Run({CurveOut(newres - 50*SAT), UserOut(50*SAT), BurnOut(fee)}, CurveOp::BUY, gross, 0) == CurveError::BAD_AMOUNT,
              "buy that siphons 50 GBX into the buyer's pocket REJECTED");
        CHECK(Run({UserOut(newres), BurnOut(fee)}, CurveOp::BUY, gross, 0) == CurveError::BAD_AMOUNT,
              "buy that sends the whole reserve to the buyer REJECTED");
    }

    printf("\n[C] The fee cannot be pocketed\n");
    {
        const int64_t gross = 100*SAT, fee = CurveFee(gross), net = gross-fee;
        int64_t tok=0, newres=0; CurveBuy(0, net, tok, newres);
        CHECK(Run({CurveOut(newres), UserOut(fee)}, CurveOp::BUY, gross, 0) == CurveError::BAD_FEE,
              "buy that pays the fee to a wallet instead of burning it REJECTED");
        CHECK(Run({CurveOut(newres)}, CurveOp::BUY, gross, 0) == CurveError::BAD_FEE,
              "buy with no fee burned at all REJECTED");
    }

    printf("\n[D] A sell must follow the curve\n");
    {
        const int64_t start = 1000*SAT;
        const int64_t tokens = CurveTokensSold(start) / 2;
        int64_t gross=0, newres=0; CurveSell(start, tokens, gross, newres);
        const int64_t fee = CurveFee(gross);
        CHECK(Run({CurveOut(newres), UserOut(gross-fee), BurnOut(fee)}, CurveOp::SELL, tokens, start) == CurveError::OK,
              "honest sell ACCEPTED");
        CHECK(Run({CurveOut(newres - 100*SAT), UserOut(gross-fee+100*SAT), BurnOut(fee)}, CurveOp::SELL, tokens, start) == CurveError::BAD_AMOUNT,
              "sell that takes 100 GBX extra from the reserve REJECTED");
    }

    printf("\n[E] Refund: only for a dead coin, only pro-rata\n");
    {
        const int64_t start = 1000*SAT;
        const int64_t sold = CurveTokensSold(start);
        const int64_t mine = sold / 4;                         // I hold a quarter of the tokens
        const int64_t out  = (int64_t)(((u128)start * (u128)mine) / (u128)sold);
        CHECK(Run({CurveOut(start-out), UserOut(out)}, CurveOp::REFUND, mine, start, 0, 100) == CurveError::NOT_IDLE,
              "refund on a coin that traded recently REJECTED");
        CHECK(Run({CurveOut(start-out), UserOut(out)}, CurveOp::REFUND, mine, start, 0, CURVE_REFUND_IDLE_BLOCKS+1) == CurveError::OK,
              "refund after 30 days of silence ACCEPTED — pro-rata, no fee");
        CHECK(Run({CurveOut(start-out-1*SAT), UserOut(out+1*SAT)}, CurveOp::REFUND, mine, start, 0, CURVE_REFUND_IDLE_BLOCKS+1) == CurveError::BAD_AMOUNT,
              "refund that grabs more than the holder's share REJECTED");
    }

    printf("\n[F] The last holder empties the curve — money is never stranded\n");
    {
        const int64_t start = 1000*SAT;
        const int64_t sold = CurveTokensSold(start);
        CHECK(Run({UserOut(start)}, CurveOp::REFUND, sold, start, 0, CURVE_REFUND_IDLE_BLOCKS+1) == CurveError::OK,
              "last holder takes the whole reserve and the curve closes ACCEPTED");
    }

    printf("\n%s  (%d failures)\n\n", fails?"TESTS FAILED":"ALL TRANSITION TESTS PASSED", fails);
    return fails?1:0;
}
