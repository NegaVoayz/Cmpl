/* test_ir_type_cache.c -- B-28: the composite type interning cache must
 * grow past its initial 128 slots instead of silently dropping entries.
 *
 * 100 distinct structs, each used as a local pointer variable in touch(),
 * create 100 distinct (IR_PTR, inner, 0) keys — past the 70 %-load growth
 * trigger (90 of 128).  If grow/rehash is lossy or crashy, this file fails
 * to compile or produces invalid IR.  touch() is external so it is always
 * code-generated (the pointer types are interned during alloca building). */

struct S00 { int a; };  struct S01 { int a; };  struct S02 { int a; };
struct S03 { int a; };  struct S04 { int a; };  struct S05 { int a; };
struct S06 { int a; };  struct S07 { int a; };  struct S08 { int a; };
struct S09 { int a; };  struct S10 { int a; };  struct S11 { int a; };
struct S12 { int a; };  struct S13 { int a; };  struct S14 { int a; };
struct S15 { int a; };  struct S16 { int a; };  struct S17 { int a; };
struct S18 { int a; };  struct S19 { int a; };  struct S20 { int a; };
struct S21 { int a; };  struct S22 { int a; };  struct S23 { int a; };
struct S24 { int a; };  struct S25 { int a; };  struct S26 { int a; };
struct S27 { int a; };  struct S28 { int a; };  struct S29 { int a; };
struct S30 { int a; };  struct S31 { int a; };  struct S32 { int a; };
struct S33 { int a; };  struct S34 { int a; };  struct S35 { int a; };
struct S36 { int a; };  struct S37 { int a; };  struct S38 { int a; };
struct S39 { int a; };  struct S40 { int a; };  struct S41 { int a; };
struct S42 { int a; };  struct S43 { int a; };  struct S44 { int a; };
struct S45 { int a; };  struct S46 { int a; };  struct S47 { int a; };
struct S48 { int a; };  struct S49 { int a; };  struct S50 { int a; };
struct S51 { int a; };  struct S52 { int a; };  struct S53 { int a; };
struct S54 { int a; };  struct S55 { int a; };  struct S56 { int a; };
struct S57 { int a; };  struct S58 { int a; };  struct S59 { int a; };
struct S60 { int a; };  struct S61 { int a; };  struct S62 { int a; };
struct S63 { int a; };  struct S64 { int a; };  struct S65 { int a; };
struct S66 { int a; };  struct S67 { int a; };  struct S68 { int a; };
struct S69 { int a; };  struct S70 { int a; };  struct S71 { int a; };
struct S72 { int a; };  struct S73 { int a; };  struct S74 { int a; };
struct S75 { int a; };  struct S76 { int a; };  struct S77 { int a; };
struct S78 { int a; };  struct S79 { int a; };  struct S80 { int a; };
struct S81 { int a; };  struct S82 { int a; };  struct S83 { int a; };
struct S84 { int a; };  struct S85 { int a; };  struct S86 { int a; };
struct S87 { int a; };  struct S88 { int a; };  struct S89 { int a; };
struct S90 { int a; };  struct S91 { int a; };  struct S92 { int a; };
struct S93 { int a; };  struct S94 { int a; };  struct S95 { int a; };
struct S96 { int a; };  struct S97 { int a; };  struct S98 { int a; };
struct S99 { int a; };

int
touch(void)
{
    struct S00* p00;  struct S01* p01;  struct S02* p02;  struct S03* p03;
    struct S04* p04;  struct S05* p05;  struct S06* p06;  struct S07* p07;
    struct S08* p08;  struct S09* p09;  struct S10* p10;  struct S11* p11;
    struct S12* p12;  struct S13* p13;  struct S14* p14;  struct S15* p15;
    struct S16* p16;  struct S17* p17;  struct S18* p18;  struct S19* p19;
    struct S20* p20;  struct S21* p21;  struct S22* p22;  struct S23* p23;
    struct S24* p24;  struct S25* p25;  struct S26* p26;  struct S27* p27;
    struct S28* p28;  struct S29* p29;  struct S30* p30;  struct S31* p31;
    struct S32* p32;  struct S33* p33;  struct S34* p34;  struct S35* p35;
    struct S36* p36;  struct S37* p37;  struct S38* p38;  struct S39* p39;
    struct S40* p40;  struct S41* p41;  struct S42* p42;  struct S43* p43;
    struct S44* p44;  struct S45* p45;  struct S46* p46;  struct S47* p47;
    struct S48* p48;  struct S49* p49;  struct S50* p50;  struct S51* p51;
    struct S52* p52;  struct S53* p53;  struct S54* p54;  struct S55* p55;
    struct S56* p56;  struct S57* p57;  struct S58* p58;  struct S59* p59;
    struct S60* p60;  struct S61* p61;  struct S62* p62;  struct S63* p63;
    struct S64* p64;  struct S65* p65;  struct S66* p66;  struct S67* p67;
    struct S68* p68;  struct S69* p69;  struct S70* p70;  struct S71* p71;
    struct S72* p72;  struct S73* p73;  struct S74* p74;  struct S75* p75;
    struct S76* p76;  struct S77* p77;  struct S78* p78;  struct S79* p79;
    struct S80* p80;  struct S81* p81;  struct S82* p82;  struct S83* p83;
    struct S84* p84;  struct S85* p85;  struct S86* p86;  struct S87* p87;
    struct S88* p88;  struct S89* p89;  struct S90* p90;  struct S91* p91;
    struct S92* p92;  struct S93* p93;  struct S94* p94;  struct S95* p95;
    struct S96* p96;  struct S97* p97;  struct S98* p98;  struct S99* p99;

    (void)p00;  (void)p01;  (void)p02;  (void)p03;  (void)p04;  (void)p05;
    (void)p06;  (void)p07;  (void)p08;  (void)p09;  (void)p10;  (void)p11;
    (void)p12;  (void)p13;  (void)p14;  (void)p15;  (void)p16;  (void)p17;
    (void)p18;  (void)p19;  (void)p20;  (void)p21;  (void)p22;  (void)p23;
    (void)p24;  (void)p25;  (void)p26;  (void)p27;  (void)p28;  (void)p29;
    (void)p30;  (void)p31;  (void)p32;  (void)p33;  (void)p34;  (void)p35;
    (void)p36;  (void)p37;  (void)p38;  (void)p39;  (void)p40;  (void)p41;
    (void)p42;  (void)p43;  (void)p44;  (void)p45;  (void)p46;  (void)p47;
    (void)p48;  (void)p49;  (void)p50;  (void)p51;  (void)p52;  (void)p53;
    (void)p54;  (void)p55;  (void)p56;  (void)p57;  (void)p58;  (void)p59;
    (void)p60;  (void)p61;  (void)p62;  (void)p63;  (void)p64;  (void)p65;
    (void)p66;  (void)p67;  (void)p68;  (void)p69;  (void)p70;  (void)p71;
    (void)p72;  (void)p73;  (void)p74;  (void)p75;  (void)p76;  (void)p77;
    (void)p78;  (void)p79;  (void)p80;  (void)p81;  (void)p82;  (void)p83;
    (void)p84;  (void)p85;  (void)p86;  (void)p87;  (void)p88;  (void)p89;
    (void)p90;  (void)p91;  (void)p92;  (void)p93;  (void)p94;  (void)p95;
    (void)p96;  (void)p97;  (void)p98;  (void)p99;
    return 0;
}

int
main(void)
{
    return 0;
}
