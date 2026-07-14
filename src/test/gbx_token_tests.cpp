// IDEE V — token conservation. Can anyone conjure or steal memecoin tokens?
#include <consensus/gbx_launchpad.h>
#include <consensus/gbx_token.h>
#include <cstdio>
using namespace gbx;
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){printf("  FAIL: %s\n",m);++fails;} else printf("  ok  : %s\n",m);}while(0)

static uint256 CID = uint256::ONE;
static const int64_t SAT = 100000000LL;
static std::vector<unsigned char> PK  = [](){ std::vector<unsigned char> v(33,0x02); v[1]=0xaa; return v; }();
static std::vector<unsigned char> PK2 = [](){ std::vector<unsigned char> v(33,0x03); v[1]=0xbb; return v; }();

static CTxOut Intent(CurveOp op,int64_t amount,int64_t tokens_out,const std::vector<unsigned char>& pk){
    std::vector<unsigned char> d; const char* t="GBX:C:"; d.insert(d.end(),t,t+6);
    d.push_back((unsigned char)op);
    d.insert(d.end(), CID.begin(), CID.end());
    for(int i=7;i>=0;--i) d.push_back((unsigned char)((amount>>(i*8))&0xff));
    for(int i=7;i>=0;--i) d.push_back((unsigned char)((tokens_out>>(i*8))&0xff));
    d.insert(d.end(), pk.begin(), pk.end());
    CScript s; s<<OP_RETURN<<d; return CTxOut(0,s);
}
static CTxOut Curve(int64_t v){ return CTxOut(v, CurveScriptPubKey(CID)); }
static CTxOut Burn(int64_t v){ return CTxOut(v, CurveBurnScript()); }
static CTxOut User(int64_t v){ CScript s; s<<OP_0<<std::vector<unsigned char>(20,0x11); return CTxOut(v,s); }
static CTxOut Tok(int64_t amt,const std::vector<unsigned char>& pk){ return CTxOut(546, TokenScriptPubKey(CID,amt,pk)); }

//! Build a tx: outputs + optional token input (spending a holding of `held` tokens).
static CurveError Run(std::vector<CTxOut> outs, CurveOp op, int64_t amount, int64_t tokens_out,
                      int64_t reserve_in, int64_t held=0, const std::vector<unsigned char>& pk=PK,
                      int spend_h=1000000)
{
    CMutableTransaction m;
    m.vin.emplace_back();                                  // the curve input
    if (held > 0) {                                        // the holder's token input, witness reveals the script
        CTxIn in; CScript ws = TokenWitnessScript(CID, held, pk);
        in.scriptWitness.stack.push_back(std::vector<unsigned char>()); // sig
        in.scriptWitness.stack.push_back(std::vector<unsigned char>(ws.begin(), ws.end()));
        m.vin.push_back(in);
    }
    for (auto& o : outs) m.vout.push_back(o);
    m.vout.push_back(Intent(op, amount, tokens_out, pk));
    CTransaction tx(m);
    auto in_ = ParseCurveIntent(tx);
    if(!in_) return CurveError::NO_INTENT;
    return CheckCurveTransition(tx, *in_, reserve_in, 0, spend_h, CURVE_REFUND_IDLE_BLOCKS);
}

int main(){
    printf("\n[1] Buy mints exactly what the curve says\n");
    {
        int64_t gross=100*SAT, fee=CurveFee(gross), net=gross-fee, tok=0, nr=0;
        CurveBuy(0, net, tok, nr);
        CHECK(Run({Curve(nr), Burn(fee), Tok(tok,PK)}, CurveOp::BUY, gross, tok, 0)==CurveError::OK,
              "honest buy with the right token output ACCEPTED");
        CHECK(Run({Curve(nr), Burn(fee), Tok(tok*2,PK)}, CurveOp::BUY, gross, tok*2, 0)==CurveError::BAD_TOKENS,
              "buy claiming DOUBLE the tokens REJECTED");
        CHECK(Run({Curve(nr), Burn(fee)}, CurveOp::BUY, gross, tok, 0)==CurveError::BAD_TOKENS,
              "buy with no token output at all REJECTED");
        CHECK(Run({Curve(nr), Burn(fee), Tok(tok,PK2)}, CurveOp::BUY, gross, tok, 0)==CurveError::BAD_TOKENS,
              "buy whose token output pays a DIFFERENT key REJECTED");
    }

    printf("\n[2] You cannot sell what you do not hold\n");
    {
        int64_t start=1000*SAT, sold=CurveTokensSold(start), sell=sold/4;
        int64_t gross=0,nr=0; CurveSell(start, sell, gross, nr); int64_t fee=CurveFee(gross);
        CHECK(Run({Curve(nr), User(gross-fee), Burn(fee)}, CurveOp::SELL, sell, 0, start, sell)==CurveError::OK,
              "sell backed by a real holding ACCEPTED");
        CHECK(Run({Curve(nr), User(gross-fee), Burn(fee)}, CurveOp::SELL, sell, 0, start, 0)==CurveError::BAD_TOKENS,
              "sell with NO tokens spent REJECTED (thin air)");
        CHECK(Run({Curve(nr), User(gross-fee), Burn(fee)}, CurveOp::SELL, sell, 0, start, sell-1)==CurveError::BAD_TOKENS,
              "sell of more than you hold REJECTED");
    }

    printf("\n[3] Change comes back, or it is destroyed — never silently kept\n");
    {
        int64_t start=1000*SAT, sold=CurveTokensSold(start), held=sold/2, sell=held/2, change=held-sell;
        int64_t gross=0,nr=0; CurveSell(start, sell, gross, nr); int64_t fee=CurveFee(gross);
        CHECK(Run({Curve(nr), User(gross-fee), Burn(fee), Tok(change,PK)}, CurveOp::SELL, sell, change, start, held)==CurveError::OK,
              "partial sell with correct change ACCEPTED");
        CHECK(Run({Curve(nr), User(gross-fee), Burn(fee), Tok(change*2,PK)}, CurveOp::SELL, sell, change*2, start, held)==CurveError::BAD_TOKENS,
              "partial sell inflating the change REJECTED");
        CHECK(Run({Curve(nr), User(gross-fee), Burn(fee)}, CurveOp::SELL, sell, change, start, held)==CurveError::BAD_TOKENS,
              "partial sell that declares change but pays none REJECTED");
    }

    printf("\n[4] Refund needs the same proof of holding\n");
    {
        int64_t start=1000*SAT, sold=CurveTokensSold(start), mine=sold/4;
        int64_t out=(int64_t)(((u128)start*(u128)mine)/(u128)sold);
        CHECK(Run({Curve(start-out), User(out)}, CurveOp::REFUND, mine, 0, start, mine, PK, CURVE_REFUND_IDLE_BLOCKS+1)==CurveError::OK,
              "refund backed by a holding ACCEPTED");
        CHECK(Run({Curve(start-out), User(out)}, CurveOp::REFUND, mine, 0, start, 0, PK, CURVE_REFUND_IDLE_BLOCKS+1)==CurveError::BAD_TOKENS,
              "refund with no holding REJECTED (nobody drains a dead coin they never bought)");
    }

    printf("\n%s  (%d failures)\n\n", fails?"TESTS FAILED":"ALL TOKEN TESTS PASSED", fails);
    return fails?1:0;
}
