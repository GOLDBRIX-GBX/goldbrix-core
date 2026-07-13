// IDEE V — graduation and the ownerless pool. Can anyone pull the liquidity?
#include <consensus/gbx_launchpad.h>
#include <cstdio>
using namespace gbx;
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){printf("  FAIL: %s\n",m);++fails;} else printf("  ok  : %s\n",m);}while(0)
static uint256 CID = uint256::ONE;
static const int64_t SAT = 100000000LL;
static std::vector<unsigned char> PK = [](){ std::vector<unsigned char> v(33,0x02); v[1]=0xaa; return v; }();

static CTxOut Intent(CurveOp op,int64_t amount,int64_t tok_out){
    std::vector<unsigned char> d; const char* t="GBX:C:"; d.insert(d.end(),t,t+6);
    d.push_back((unsigned char)op); d.insert(d.end(),CID.begin(),CID.end());
    for(int i=7;i>=0;--i) d.push_back((unsigned char)((amount>>(i*8))&0xff));
    for(int i=7;i>=0;--i) d.push_back((unsigned char)((tok_out>>(i*8))&0xff));
    d.insert(d.end(),PK.begin(),PK.end());
    CScript s; s<<OP_RETURN<<d; return CTxOut(0,s);
}
static CTxOut Pool(int64_t gbx,int64_t tok){ return CTxOut(gbx, PoolScriptPubKey(CID,tok)); }
static CTxOut Burn(int64_t v){ return CTxOut(v, CurveBurnScript()); }
static CTxOut User(int64_t v){ CScript s; s<<OP_0<<std::vector<unsigned char>(20,0x11); return CTxOut(v,s); }
static CTxOut Tok(int64_t a){ return CTxOut(546, TokenScriptPubKey(CID,a,PK)); }

static CurveError RunG(std::vector<CTxOut> outs,int64_t reserve_in){
    CMutableTransaction m; m.vin.emplace_back();
    for(auto&o:outs) m.vout.push_back(o);
    m.vout.push_back(Intent(CurveOp::GRADUATE,0,0));
    CTransaction tx(m); auto i=ParseCurveIntent(tx); if(!i) return CurveError::NO_INTENT;
    return CheckCurveTransition(tx,*i,reserve_in,0,1000000);
}
static CurveError RunP(std::vector<CTxOut> outs,CurveOp op,int64_t amount,int64_t tok_out,
                       int64_t pg,int64_t pt,int64_t held=0){
    CMutableTransaction m; m.vin.emplace_back();
    if(held>0){ CTxIn in; CScript ws=TokenWitnessScript(CID,held,PK);
        in.scriptWitness.stack.push_back({}); in.scriptWitness.stack.push_back(std::vector<unsigned char>(ws.begin(),ws.end()));
        m.vin.push_back(in); }
    for(auto&o:outs) m.vout.push_back(o);
    m.vout.push_back(Intent(op,amount,tok_out));
    CTransaction tx(m); auto i=ParseCurveIntent(tx); if(!i) return CurveError::NO_INTENT;
    return CheckPoolTransition(tx,*i,pg,pt);
}

int main(){
    const int64_t FULL = CURVE_GRADUATION_SAT;                 // 80,000 GBX
    const int64_t sold = CurveTokensSold(FULL);
    const int64_t ptok = CURVE_LP_TOKENS + (CURVE_TOKENS - sold);

    printf("\n[1] Graduation: every unit follows into the pool\n");
    CHECK(RunG({Pool(FULL,ptok)}, FULL)==CurveError::OK, "full curve graduates into a pool ACCEPTED");
    CHECK(RunG({Pool(FULL,ptok)}, FULL-1)==CurveError::NOT_GRADUATED, "graduation one unit early REJECTED");
    CHECK(RunG({Pool(FULL-1000*SAT,ptok), User(1000*SAT)}, FULL)==CurveError::BAD_AMOUNT,
          "graduation that skims 1000 GBX REJECTED");
    CHECK(RunG({Pool(FULL,ptok*2)}, FULL)==CurveError::BAD_AMOUNT, "graduation inflating pool tokens REJECTED");
    CHECK(RunG({User(FULL)}, FULL)==CurveError::BAD_AMOUNT, "graduation that pays the reserve to a wallet REJECTED");

    printf("\n[2] The pool cannot be drained — there is no owner to drain it\n");
    {
        int64_t pg=FULL, pt=ptok;
        int64_t in=100*SAT, fee=PoolFee(in), net=in-fee, out=0, ng=0, nt=0;
        PoolBuy(pg,pt,net,out,ng,nt);
        CHECK(RunP({Pool(ng,nt), Burn(fee), Tok(out)}, CurveOp::POOL_BUY, in, out, pg, pt)==CurveError::OK,
              "honest pool buy ACCEPTED");
        CHECK(RunP({Pool(ng-50*SAT,nt), Burn(fee), Tok(out), User(50*SAT)}, CurveOp::POOL_BUY, in, out, pg, pt)==CurveError::BAD_AMOUNT,
              "pool buy that siphons 50 GBX out of the pool REJECTED");
        CHECK(RunP({User(pg), Burn(fee), Tok(out)}, CurveOp::POOL_BUY, in, out, pg, pt)==CurveError::BAD_AMOUNT,
              "attempt to take the ENTIRE pool REJECTED (this is the rug-pull, and it cannot happen)");
        CHECK(RunP({Pool(ng,nt), Tok(out)}, CurveOp::POOL_BUY, in, out, pg, pt)==CurveError::BAD_FEE,
              "pool buy without burning the fee REJECTED");
    }

    printf("\n[3] Pool sell must prove the holding\n");
    {
        int64_t pg=FULL, pt=ptok, sell=1000000, gross=0, ng=0, nt=0;
        PoolSell(pg,pt,sell,gross,ng,nt); int64_t fee=PoolFee(gross);
        CHECK(RunP({Pool(ng,nt), Burn(fee), User(gross-fee)}, CurveOp::POOL_SELL, sell, 0, pg, pt, sell)==CurveError::OK,
              "pool sell backed by a holding ACCEPTED");
        CHECK(RunP({Pool(ng,nt), Burn(fee), User(gross-fee)}, CurveOp::POOL_SELL, sell, 0, pg, pt, 0)==CurveError::BAD_TOKENS,
              "pool sell with no tokens REJECTED");
    }

    printf("\n[4] Constant product holds: no free money\n");
    {
        int64_t pg=FULL, pt=ptok, in=1000*SAT, out=0, ng=0, nt=0;
        PoolBuy(pg,pt,in,out,ng,nt);
        int64_t back=0, g2=0, t2=0;
        PoolSell(ng,nt,out,back,g2,t2);
        CHECK(back <= in, "buy then sell in the pool never returns more than was paid");
        printf("       in=%lld back=%lld (pool keeps %lld)\n",(long long)in,(long long)back,(long long)(in-back));
    }

    printf("\n%s  (%d failures)\n\n", fails?"TESTS FAILED":"ALL POOL TESTS PASSED", fails);
    return fails?1:0;
}
