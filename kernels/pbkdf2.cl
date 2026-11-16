// Compile the same byte-oriented kernel through OpenCL C or HIPRTC.
#ifdef WIFI_GPU_HIP
typedef unsigned int uint;
typedef unsigned char uchar;
#define WIFI_DEVICE_INLINE __device__ __forceinline__
#define __kernel extern "C" __global__ __launch_bounds__(256)
#define __global
#define get_global_id(dimension) (static_cast<size_t>(blockIdx.x)*blockDim.x+threadIdx.x)
#define WIFI_GPU_AMD_OPENCL 1
WIFI_DEVICE_INLINE uint rotate(uint value,uint bits) {
    return (value<<(bits&31U))|(value>>((0U-bits)&31U));
}
WIFI_DEVICE_INLINE uint bitselect(uint a,uint b,uint mask) {
    return (a&~mask)|(b&mask);
}
#else
#define WIFI_DEVICE_INLINE inline
#endif

// PBKDF2-HMAC-SHA1 for WPA passphrases: two output blocks, 32-byte PMK.
// Independent prototype, not copied from Hashcat. OpenCL C 1.2.
typedef struct { uchar bytes[64]; uint length; } Password;
typedef struct { uint ipad[5], opad[5], u[10], sum[10]; } State;

#ifdef WIFI_GPU_HIP
// ROCm 7.2 HIP lowers these forms more efficiently than the OpenCL bitselects.
#define SHA1_CH(b,c,d) ((d)^((b)&((c)^(d))))
#define SHA1_MAJ(b,c,d) (((b)&(c))|((d)&((b)|(c))))
#elif defined(WIFI_GPU_AMD_OPENCL)
#define SHA1_CH(b,c,d) bitselect((d),(c),(b))
#define SHA1_MAJ(b,c,d) bitselect((b),(c),(b)^(d))
#else
#define SHA1_CH(b,c,d) (((b)&(c))|(~(b)&(d)))
#define SHA1_MAJ(b,c,d) (((b)&(c))|((b)&(d))|((c)&(d)))
#endif

WIFI_DEVICE_INLINE void sha1_initial(uint s[5]) {
    s[0]=0x67452301U; s[1]=0xefcdab89U; s[2]=0x98badcfeU;
    s[3]=0x10325476U; s[4]=0xc3d2e1f0U;
}

WIFI_DEVICE_INLINE void sha1_block(uint s[5], uint w[16]) {
    uint a=s[0], b=s[1], c=s[2], d=s[3], e=s[4];
    #pragma unroll
    for (uint i=0; i<80; ++i) {
        if (i>=16) w[i&15]=rotate(w[(i-3)&15]^w[(i-8)&15]^w[(i-14)&15]^w[i&15],1U);
        uint f, k;
        if (i<20) { f=(b&c)|(~b&d); k=0x5a827999U; }
        else if (i<40) { f=b^c^d; k=0x6ed9eba1U; }
        else if (i<60) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdcU; }
        else { f=b^c^d; k=0xca62c1d6U; }
        uint t=rotate(a,5U)+f+e+k+w[i&15];
        e=d; d=c; c=rotate(b,30U); b=a; a=t;
    }
    s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d; s[4]+=e;
}

WIFI_DEVICE_INLINE void finish_hmac(const uint opad[5], uint digest[5]) {
    uint w[16]={0}, outer[5];
    for (uint i=0;i<5;++i) { w[i]=digest[i]; outer[i]=opad[i]; }
    w[5]=0x80000000U; w[15]=(64U+20U)*8U;
    sha1_block(outer,w);
    for (uint i=0;i<5;++i) digest[i]=outer[i];
}

// SHA-1 compression of a 20-byte digest following a 64-byte HMAC pad.
// Fixed padding and scalar words keep the schedule out of indexed private arrays.
// BEGIN GENERATED SHA1_20
#if defined(WIFI_GPU_HIP)
WIFI_DEVICE_INLINE void sha1_20(const uint base[5],uint digest[5]) {
    uint a=base[0],b=base[1],c=base[2],d=base[3],e=base[4];
    uint w0=digest[0];
    uint w1=digest[1];
    uint w2=digest[2];
    uint w3=digest[3];
    uint w4=digest[4];
    uint w5=0x80000000u;
    uint w6=0u;
    uint w7=0u;
    uint w8=0u;
    uint w9=0u;
    uint w10=0u;
    uint w11=0u;
    uint w12=0u;
    uint w13=0u;
    uint w14=0u;
    uint w15=672u;
    uint w16=0u;
    uint w17=0u;
    uint w18=0u;
    uint w19=0u;
    uint w20=0u;
    uint w21=0u;
    uint w22=0u;
    uint w23=0u;
    uint w24=0u;
    uint w25=0u;
    uint w26=0u;
    uint w27=0u;
    uint w28=0u;
    uint w29=0u;
    uint w30=0u;
    uint w31=0u;
    uint w32=0u;
    uint w33=0u;
    uint w34=0u;
    uint w35=0u;
    uint w36=0u;
    uint w37=0u;
    uint w38=0u;
    uint w39=0u;
    uint w40=0u;
    uint w41=0u;
    uint w42=0u;
    uint w43=0u;
    uint w44=0u;
    uint w45=0u;
    uint w46=0u;
    uint w47=0u;
    uint w48=0u;
    uint w49=0u;
    uint w50=0u;
    uint w51=0u;
    uint w52=0u;
    uint w53=0u;
    uint w54=0u;
    uint w55=0u;
    uint w56=0u;
    uint w57=0u;
    uint w58=0u;
    uint w59=0u;
    uint w60=0u;
    uint w61=0u;
    uint w62=0u;
    uint w63=0u;
    uint w64=0u;
    uint w65=0u;
    uint w66=0u;
    uint w67=0u;
    uint w68=0u;
    uint w69=0u;
    uint w70=0u;
    uint w71=0u;
    uint w72=0u;
    uint w73=0u;
    uint w74=0u;
    uint w75=0u;
    uint w76=0u;
    uint w77=0u;
    uint w78=0u;
    uint w79=0u;
    w16=rotate(w0^w2,1u);
    w17=rotate(w1^w3,1u);
    w18=rotate(w2^w4^0x000002a0u,1u);
    w19=rotate(w3^w16^0x80000000u,1u);
    w20=rotate(w4^w17,1u);
    w21=rotate(w2^w4^0x400002a0u,2u);
    w22=rotate(w3^w16^0x80000000u,2u);
    w23=rotate(w4^w17^0x00000150u,2u);
    w24=rotate(w16^w21,1u);
    w25=rotate(w17^w22,1u);
    w26=rotate(w18^w23,1u);
    w27=rotate(w19^w24,1u);
    w28=rotate(w4^w22,2u);
    w29=rotate(w4^w17^0x20000104u,4u);
    w30=rotate(w0^w2^w24,2u);
    w31=rotate(w1^w3^w25^0x000003f0u,2u);
    w32=rotate(w26^w16^w4^w0,2u);
    w33=rotate(w1^w17^w27^0x80000000u,2u);
    w34=rotate(w2^w18^w28,2u);
    w35=rotate(w3^w19^w29,2u);
    w36=rotate(w4^w20^w30,2u);
    w37=rotate(w21^w31^0x80000000u,2u);
    w38=rotate(w22^w32,2u);
    w39=rotate(w23^w33,2u);
    w40=rotate(w24^w34,2u);
    w41=rotate(w1^w29,4u);
    w42=rotate(w2^w30,4u);
    w43=rotate(w3^w31^0x000000a8u,4u);
    w44=rotate(w16^w28^w38,2u);
    w45=rotate(w17^w29^w39,2u);
    w46=rotate(w18^w30^w40,2u);
    w47=rotate(w19^w31^w41^0x000002a0u,2u);
    w48=rotate(w42^w32^w20^w16,2u);
    w49=rotate(w43^w33^w21^w17,2u);
    w50=rotate(w44^w34^w22^w18,2u);
    w51=rotate(w45^w35^w23^w19,2u);
    w52=rotate(w46^w36^w24^w20,2u);
    w53=rotate(w47^w37^w25^w21,2u);
    w54=rotate(w48^w38^w26^w22,2u);
    w55=rotate(w49^w39^w27^w23,2u);
    w56=rotate(w50^w40^w28^w24,2u);
    w57=rotate(w51^w41^w29^w25,2u);
    w58=rotate(w52^w42^w30^w26,2u);
    w59=rotate(w1^w27^w47^0x80000150u,4u);
    w60=rotate(w0^w28^w48,4u);
    w61=rotate(w1^w29^w49^0x00000150u,4u);
    w62=rotate(w0^w30^w50,4u);
    w63=rotate(w1^w31^w51^0x00000150u,4u);
    w64=rotate(w0^w32^w52,4u);
    w65=rotate(w1^w33^w53,4u);
    w66=rotate(w2^w34^w54,4u);
    w67=rotate(w3^w35^w55,4u);
    w68=rotate(w4^w36^w56,4u);
    w69=rotate(w2^w4^w45^0xe8000000u,8u);
    w70=rotate(w38^w58,4u);
    w71=rotate(w4^w47^0x0000002au,8u);
    w72=rotate(w16^w40^w60,4u);
    w73=rotate(w17^w41^w61,4u);
    w74=rotate(w18^w42^w62,4u);
    w75=rotate(w19^w43^w63,4u);
    w76=rotate(w20^w44^w64,4u);
    w77=rotate(w21^w45^w65,4u);
    w78=rotate(w22^w46^w66,4u);
    w79=rotate(w23^w47^w67^0x000002a0u,4u);
    e=(e+w0+0x5a827999u)+(rotate(a,5u)+SHA1_CH(b,c,d)); b=rotate(b,30u);
    d=(d+w1+0x5a827999u)+(rotate(e,5u)+SHA1_CH(a,b,c)); a=rotate(a,30u);
    c=(c+w2+0x5a827999u)+(rotate(d,5u)+SHA1_CH(e,a,b)); e=rotate(e,30u);
    b=(b+w3+0x5a827999u)+(rotate(c,5u)+SHA1_CH(d,e,a)); d=rotate(d,30u);
    a=(a+w4+0x5a827999u)+(rotate(b,5u)+SHA1_CH(c,d,e)); c=rotate(c,30u);
    e=(e+w5+0x5a827999u)+(rotate(a,5u)+SHA1_CH(b,c,d)); b=rotate(b,30u);
    d=(d+w6+0x5a827999u)+(rotate(e,5u)+SHA1_CH(a,b,c)); a=rotate(a,30u);
    c=(c+w7+0x5a827999u)+(rotate(d,5u)+SHA1_CH(e,a,b)); e=rotate(e,30u);
    b=(b+w8+0x5a827999u)+(rotate(c,5u)+SHA1_CH(d,e,a)); d=rotate(d,30u);
    a=(a+w9+0x5a827999u)+(rotate(b,5u)+SHA1_CH(c,d,e)); c=rotate(c,30u);
    e=(e+w10+0x5a827999u)+(rotate(a,5u)+SHA1_CH(b,c,d)); b=rotate(b,30u);
    d=(d+w11+0x5a827999u)+(rotate(e,5u)+SHA1_CH(a,b,c)); a=rotate(a,30u);
    c=(c+w12+0x5a827999u)+(rotate(d,5u)+SHA1_CH(e,a,b)); e=rotate(e,30u);
    b=(b+w13+0x5a827999u)+(rotate(c,5u)+SHA1_CH(d,e,a)); d=rotate(d,30u);
    a=(a+w14+0x5a827999u)+(rotate(b,5u)+SHA1_CH(c,d,e)); c=rotate(c,30u);
    e=(e+w15+0x5a827999u)+(rotate(a,5u)+SHA1_CH(b,c,d)); b=rotate(b,30u);
    d=(d+w16+0x5a827999u)+(rotate(e,5u)+SHA1_CH(a,b,c)); a=rotate(a,30u);
    c=(c+w17+0x5a827999u)+(rotate(d,5u)+SHA1_CH(e,a,b)); e=rotate(e,30u);
    b=(b+w18+0x5a827999u)+(rotate(c,5u)+SHA1_CH(d,e,a)); d=rotate(d,30u);
    a=(a+w19+0x5a827999u)+(rotate(b,5u)+SHA1_CH(c,d,e)); c=rotate(c,30u);
    e=(e+w20+0x6ed9eba1u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w21+0x6ed9eba1u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w22+0x6ed9eba1u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w23+0x6ed9eba1u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w24+0x6ed9eba1u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w25+0x6ed9eba1u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w26+0x6ed9eba1u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w27+0x6ed9eba1u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w28+0x6ed9eba1u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w29+0x6ed9eba1u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w30+0x6ed9eba1u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w31+0x6ed9eba1u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w32+0x6ed9eba1u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w33+0x6ed9eba1u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w34+0x6ed9eba1u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w35+0x6ed9eba1u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w36+0x6ed9eba1u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w37+0x6ed9eba1u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w38+0x6ed9eba1u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w39+0x6ed9eba1u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w40+0x8f1bbcdcu)+(rotate(a,5u)+SHA1_MAJ(b,c,d)); b=rotate(b,30u);
    d=(d+w41+0x8f1bbcdcu)+(rotate(e,5u)+SHA1_MAJ(a,b,c)); a=rotate(a,30u);
    c=(c+w42+0x8f1bbcdcu)+(rotate(d,5u)+SHA1_MAJ(e,a,b)); e=rotate(e,30u);
    b=(b+w43+0x8f1bbcdcu)+(rotate(c,5u)+SHA1_MAJ(d,e,a)); d=rotate(d,30u);
    a=(a+w44+0x8f1bbcdcu)+(rotate(b,5u)+SHA1_MAJ(c,d,e)); c=rotate(c,30u);
    e=(e+w45+0x8f1bbcdcu)+(rotate(a,5u)+SHA1_MAJ(b,c,d)); b=rotate(b,30u);
    d=(d+w46+0x8f1bbcdcu)+(rotate(e,5u)+SHA1_MAJ(a,b,c)); a=rotate(a,30u);
    c=(c+w47+0x8f1bbcdcu)+(rotate(d,5u)+SHA1_MAJ(e,a,b)); e=rotate(e,30u);
    b=(b+w48+0x8f1bbcdcu)+(rotate(c,5u)+SHA1_MAJ(d,e,a)); d=rotate(d,30u);
    a=(a+w49+0x8f1bbcdcu)+(rotate(b,5u)+SHA1_MAJ(c,d,e)); c=rotate(c,30u);
    e=(e+w50+0x8f1bbcdcu)+(rotate(a,5u)+SHA1_MAJ(b,c,d)); b=rotate(b,30u);
    d=(d+w51+0x8f1bbcdcu)+(rotate(e,5u)+SHA1_MAJ(a,b,c)); a=rotate(a,30u);
    c=(c+w52+0x8f1bbcdcu)+(rotate(d,5u)+SHA1_MAJ(e,a,b)); e=rotate(e,30u);
    b=(b+w53+0x8f1bbcdcu)+(rotate(c,5u)+SHA1_MAJ(d,e,a)); d=rotate(d,30u);
    a=(a+w54+0x8f1bbcdcu)+(rotate(b,5u)+SHA1_MAJ(c,d,e)); c=rotate(c,30u);
    e=(e+w55+0x8f1bbcdcu)+(rotate(a,5u)+SHA1_MAJ(b,c,d)); b=rotate(b,30u);
    d=(d+w56+0x8f1bbcdcu)+(rotate(e,5u)+SHA1_MAJ(a,b,c)); a=rotate(a,30u);
    c=(c+w57+0x8f1bbcdcu)+(rotate(d,5u)+SHA1_MAJ(e,a,b)); e=rotate(e,30u);
    b=(b+w58+0x8f1bbcdcu)+(rotate(c,5u)+SHA1_MAJ(d,e,a)); d=rotate(d,30u);
    a=(a+w59+0x8f1bbcdcu)+(rotate(b,5u)+SHA1_MAJ(c,d,e)); c=rotate(c,30u);
    e=(e+w60+0xca62c1d6u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w61+0xca62c1d6u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w62+0xca62c1d6u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w63+0xca62c1d6u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w64+0xca62c1d6u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w65+0xca62c1d6u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w66+0xca62c1d6u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w67+0xca62c1d6u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w68+0xca62c1d6u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w69+0xca62c1d6u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w70+0xca62c1d6u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w71+0xca62c1d6u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w72+0xca62c1d6u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w73+0xca62c1d6u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w74+0xca62c1d6u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w75+0xca62c1d6u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w76+0xca62c1d6u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w77+0xca62c1d6u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w78+0xca62c1d6u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w79+0xca62c1d6u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    digest[0]=a+base[0];digest[1]=b+base[1];digest[2]=c+base[2];digest[3]=d+base[3];digest[4]=e+base[4];
}
#if defined(WIFI_GPU_HIP_PAIR)
WIFI_DEVICE_INLINE void sha1_pair(const uint base[5],uint u0[5],uint u1[5]) {
    uint a_0=base[0],b_0=base[1],c_0=base[2],d_0=base[3],e_0=base[4];
    uint w0_0=u0[0];
    uint w1_0=u0[1];
    uint w2_0=u0[2];
    uint w3_0=u0[3];
    uint w4_0=u0[4];
    uint w5_0=0x80000000u;
    uint w6_0=0u;
    uint w7_0=0u;
    uint w8_0=0u;
    uint w9_0=0u;
    uint w10_0=0u;
    uint w11_0=0u;
    uint w12_0=0u;
    uint w13_0=0u;
    uint w14_0=0u;
    uint w15_0=672u;
    e_0=(e_0+w0_0+0x5a827999u)+(rotate(a_0,5u)+SHA1_CH(b_0,c_0,d_0)); b_0=rotate(b_0,30u);
    d_0=(d_0+w1_0+0x5a827999u)+(rotate(e_0,5u)+SHA1_CH(a_0,b_0,c_0)); a_0=rotate(a_0,30u);
    c_0=(c_0+w2_0+0x5a827999u)+(rotate(d_0,5u)+SHA1_CH(e_0,a_0,b_0)); e_0=rotate(e_0,30u);
    b_0=(b_0+w3_0+0x5a827999u)+(rotate(c_0,5u)+SHA1_CH(d_0,e_0,a_0)); d_0=rotate(d_0,30u);
    a_0=(a_0+w4_0+0x5a827999u)+(rotate(b_0,5u)+SHA1_CH(c_0,d_0,e_0)); c_0=rotate(c_0,30u);
    e_0=(e_0+w5_0+0x5a827999u)+(rotate(a_0,5u)+SHA1_CH(b_0,c_0,d_0)); b_0=rotate(b_0,30u);
    d_0=(d_0+w6_0+0x5a827999u)+(rotate(e_0,5u)+SHA1_CH(a_0,b_0,c_0)); a_0=rotate(a_0,30u);
    c_0=(c_0+w7_0+0x5a827999u)+(rotate(d_0,5u)+SHA1_CH(e_0,a_0,b_0)); e_0=rotate(e_0,30u);
    b_0=(b_0+w8_0+0x5a827999u)+(rotate(c_0,5u)+SHA1_CH(d_0,e_0,a_0)); d_0=rotate(d_0,30u);
    a_0=(a_0+w9_0+0x5a827999u)+(rotate(b_0,5u)+SHA1_CH(c_0,d_0,e_0)); c_0=rotate(c_0,30u);
    e_0=(e_0+w10_0+0x5a827999u)+(rotate(a_0,5u)+SHA1_CH(b_0,c_0,d_0)); b_0=rotate(b_0,30u);
    d_0=(d_0+w11_0+0x5a827999u)+(rotate(e_0,5u)+SHA1_CH(a_0,b_0,c_0)); a_0=rotate(a_0,30u);
    c_0=(c_0+w12_0+0x5a827999u)+(rotate(d_0,5u)+SHA1_CH(e_0,a_0,b_0)); e_0=rotate(e_0,30u);
    b_0=(b_0+w13_0+0x5a827999u)+(rotate(c_0,5u)+SHA1_CH(d_0,e_0,a_0)); d_0=rotate(d_0,30u);
    a_0=(a_0+w14_0+0x5a827999u)+(rotate(b_0,5u)+SHA1_CH(c_0,d_0,e_0)); c_0=rotate(c_0,30u);
    e_0=(e_0+w15_0+0x5a827999u)+(rotate(a_0,5u)+SHA1_CH(b_0,c_0,d_0)); b_0=rotate(b_0,30u);
    uint w16_0=rotate(w0_0^w2_0,1u);
    d_0=(d_0+w16_0+0x5a827999u)+(rotate(e_0,5u)+SHA1_CH(a_0,b_0,c_0)); a_0=rotate(a_0,30u);
    uint w17_0=rotate(w1_0^w3_0,1u);
    c_0=(c_0+w17_0+0x5a827999u)+(rotate(d_0,5u)+SHA1_CH(e_0,a_0,b_0)); e_0=rotate(e_0,30u);
    uint w18_0=rotate(w2_0^w4_0^0x000002a0u,1u);
    b_0=(b_0+w18_0+0x5a827999u)+(rotate(c_0,5u)+SHA1_CH(d_0,e_0,a_0)); d_0=rotate(d_0,30u);
    uint w19_0=rotate(w3_0^w16_0^0x80000000u,1u);
    a_0=(a_0+w19_0+0x5a827999u)+(rotate(b_0,5u)+SHA1_CH(c_0,d_0,e_0)); c_0=rotate(c_0,30u);
    uint w20_0=rotate(w4_0^w17_0,1u);
    e_0=(e_0+w20_0+0x6ed9eba1u)+(rotate(a_0,5u)+(b_0^c_0^d_0)); b_0=rotate(b_0,30u);
    uint w21_0=rotate(w2_0^w4_0^0x400002a0u,2u);
    d_0=(d_0+w21_0+0x6ed9eba1u)+(rotate(e_0,5u)+(a_0^b_0^c_0)); a_0=rotate(a_0,30u);
    uint w22_0=rotate(w3_0^w16_0^0x80000000u,2u);
    c_0=(c_0+w22_0+0x6ed9eba1u)+(rotate(d_0,5u)+(e_0^a_0^b_0)); e_0=rotate(e_0,30u);
    uint w23_0=rotate(w4_0^w17_0^0x00000150u,2u);
    b_0=(b_0+w23_0+0x6ed9eba1u)+(rotate(c_0,5u)+(d_0^e_0^a_0)); d_0=rotate(d_0,30u);
    uint w24_0=rotate(w16_0^w21_0,1u);
    a_0=(a_0+w24_0+0x6ed9eba1u)+(rotate(b_0,5u)+(c_0^d_0^e_0)); c_0=rotate(c_0,30u);
    uint w25_0=rotate(w17_0^w22_0,1u);
    e_0=(e_0+w25_0+0x6ed9eba1u)+(rotate(a_0,5u)+(b_0^c_0^d_0)); b_0=rotate(b_0,30u);
    uint w26_0=rotate(w18_0^w23_0,1u);
    d_0=(d_0+w26_0+0x6ed9eba1u)+(rotate(e_0,5u)+(a_0^b_0^c_0)); a_0=rotate(a_0,30u);
    uint w27_0=rotate(w19_0^w24_0,1u);
    c_0=(c_0+w27_0+0x6ed9eba1u)+(rotate(d_0,5u)+(e_0^a_0^b_0)); e_0=rotate(e_0,30u);
    uint w28_0=rotate(w4_0^w22_0,2u);
    b_0=(b_0+w28_0+0x6ed9eba1u)+(rotate(c_0,5u)+(d_0^e_0^a_0)); d_0=rotate(d_0,30u);
    uint w29_0=rotate(w4_0^w17_0^0x20000104u,4u);
    a_0=(a_0+w29_0+0x6ed9eba1u)+(rotate(b_0,5u)+(c_0^d_0^e_0)); c_0=rotate(c_0,30u);
    uint w30_0=rotate(w0_0^w2_0^w24_0,2u);
    e_0=(e_0+w30_0+0x6ed9eba1u)+(rotate(a_0,5u)+(b_0^c_0^d_0)); b_0=rotate(b_0,30u);
    uint w31_0=rotate(w1_0^w3_0^w25_0^0x000003f0u,2u);
    d_0=(d_0+w31_0+0x6ed9eba1u)+(rotate(e_0,5u)+(a_0^b_0^c_0)); a_0=rotate(a_0,30u);
    uint w32_0=rotate(w26_0^w16_0^w4_0^w0_0,2u);
    c_0=(c_0+w32_0+0x6ed9eba1u)+(rotate(d_0,5u)+(e_0^a_0^b_0)); e_0=rotate(e_0,30u);
    uint w33_0=rotate(w1_0^w17_0^w27_0^0x80000000u,2u);
    b_0=(b_0+w33_0+0x6ed9eba1u)+(rotate(c_0,5u)+(d_0^e_0^a_0)); d_0=rotate(d_0,30u);
    uint w34_0=rotate(w2_0^w18_0^w28_0,2u);
    a_0=(a_0+w34_0+0x6ed9eba1u)+(rotate(b_0,5u)+(c_0^d_0^e_0)); c_0=rotate(c_0,30u);
    uint w35_0=rotate(w3_0^w19_0^w29_0,2u);
    e_0=(e_0+w35_0+0x6ed9eba1u)+(rotate(a_0,5u)+(b_0^c_0^d_0)); b_0=rotate(b_0,30u);
    uint w36_0=rotate(w4_0^w20_0^w30_0,2u);
    d_0=(d_0+w36_0+0x6ed9eba1u)+(rotate(e_0,5u)+(a_0^b_0^c_0)); a_0=rotate(a_0,30u);
    uint w37_0=rotate(w21_0^w31_0^0x80000000u,2u);
    c_0=(c_0+w37_0+0x6ed9eba1u)+(rotate(d_0,5u)+(e_0^a_0^b_0)); e_0=rotate(e_0,30u);
    uint w38_0=rotate(w22_0^w32_0,2u);
    b_0=(b_0+w38_0+0x6ed9eba1u)+(rotate(c_0,5u)+(d_0^e_0^a_0)); d_0=rotate(d_0,30u);
    uint w39_0=rotate(w23_0^w33_0,2u);
    uint a_1=base[0],b_1=base[1],c_1=base[2],d_1=base[3],e_1=base[4];
    uint w0_1=u1[0];
    uint w1_1=u1[1];
    uint w2_1=u1[2];
    uint w3_1=u1[3];
    uint w4_1=u1[4];
    uint w5_1=0x80000000u;
    uint w6_1=0u;
    uint w7_1=0u;
    uint w8_1=0u;
    uint w9_1=0u;
    uint w10_1=0u;
    uint w11_1=0u;
    uint w12_1=0u;
    uint w13_1=0u;
    uint w14_1=0u;
    uint w15_1=672u;
    e_1=(e_1+w0_1+0x5a827999u)+(rotate(a_1,5u)+SHA1_CH(b_1,c_1,d_1)); b_1=rotate(b_1,30u);
    d_1=(d_1+w1_1+0x5a827999u)+(rotate(e_1,5u)+SHA1_CH(a_1,b_1,c_1)); a_1=rotate(a_1,30u);
    c_1=(c_1+w2_1+0x5a827999u)+(rotate(d_1,5u)+SHA1_CH(e_1,a_1,b_1)); e_1=rotate(e_1,30u);
    b_1=(b_1+w3_1+0x5a827999u)+(rotate(c_1,5u)+SHA1_CH(d_1,e_1,a_1)); d_1=rotate(d_1,30u);
    a_1=(a_1+w4_1+0x5a827999u)+(rotate(b_1,5u)+SHA1_CH(c_1,d_1,e_1)); c_1=rotate(c_1,30u);
    e_1=(e_1+w5_1+0x5a827999u)+(rotate(a_1,5u)+SHA1_CH(b_1,c_1,d_1)); b_1=rotate(b_1,30u);
    d_1=(d_1+w6_1+0x5a827999u)+(rotate(e_1,5u)+SHA1_CH(a_1,b_1,c_1)); a_1=rotate(a_1,30u);
    c_1=(c_1+w7_1+0x5a827999u)+(rotate(d_1,5u)+SHA1_CH(e_1,a_1,b_1)); e_1=rotate(e_1,30u);
    b_1=(b_1+w8_1+0x5a827999u)+(rotate(c_1,5u)+SHA1_CH(d_1,e_1,a_1)); d_1=rotate(d_1,30u);
    a_1=(a_1+w9_1+0x5a827999u)+(rotate(b_1,5u)+SHA1_CH(c_1,d_1,e_1)); c_1=rotate(c_1,30u);
    e_1=(e_1+w10_1+0x5a827999u)+(rotate(a_1,5u)+SHA1_CH(b_1,c_1,d_1)); b_1=rotate(b_1,30u);
    d_1=(d_1+w11_1+0x5a827999u)+(rotate(e_1,5u)+SHA1_CH(a_1,b_1,c_1)); a_1=rotate(a_1,30u);
    c_1=(c_1+w12_1+0x5a827999u)+(rotate(d_1,5u)+SHA1_CH(e_1,a_1,b_1)); e_1=rotate(e_1,30u);
    b_1=(b_1+w13_1+0x5a827999u)+(rotate(c_1,5u)+SHA1_CH(d_1,e_1,a_1)); d_1=rotate(d_1,30u);
    a_1=(a_1+w14_1+0x5a827999u)+(rotate(b_1,5u)+SHA1_CH(c_1,d_1,e_1)); c_1=rotate(c_1,30u);
    e_1=(e_1+w15_1+0x5a827999u)+(rotate(a_1,5u)+SHA1_CH(b_1,c_1,d_1)); b_1=rotate(b_1,30u);
    uint w16_1=rotate(w0_1^w2_1,1u);
    d_1=(d_1+w16_1+0x5a827999u)+(rotate(e_1,5u)+SHA1_CH(a_1,b_1,c_1)); a_1=rotate(a_1,30u);
    uint w17_1=rotate(w1_1^w3_1,1u);
    c_1=(c_1+w17_1+0x5a827999u)+(rotate(d_1,5u)+SHA1_CH(e_1,a_1,b_1)); e_1=rotate(e_1,30u);
    uint w18_1=rotate(w2_1^w4_1^0x000002a0u,1u);
    b_1=(b_1+w18_1+0x5a827999u)+(rotate(c_1,5u)+SHA1_CH(d_1,e_1,a_1)); d_1=rotate(d_1,30u);
    uint w19_1=rotate(w3_1^w16_1^0x80000000u,1u);
    a_1=(a_1+w19_1+0x5a827999u)+(rotate(b_1,5u)+SHA1_CH(c_1,d_1,e_1)); c_1=rotate(c_1,30u);
    uint w20_1=rotate(w4_1^w17_1,1u);
    e_1=(e_1+w20_1+0x6ed9eba1u)+(rotate(a_1,5u)+(b_1^c_1^d_1)); b_1=rotate(b_1,30u);
    uint w21_1=rotate(w2_1^w4_1^0x400002a0u,2u);
    d_1=(d_1+w21_1+0x6ed9eba1u)+(rotate(e_1,5u)+(a_1^b_1^c_1)); a_1=rotate(a_1,30u);
    uint w22_1=rotate(w3_1^w16_1^0x80000000u,2u);
    c_1=(c_1+w22_1+0x6ed9eba1u)+(rotate(d_1,5u)+(e_1^a_1^b_1)); e_1=rotate(e_1,30u);
    uint w23_1=rotate(w4_1^w17_1^0x00000150u,2u);
    b_1=(b_1+w23_1+0x6ed9eba1u)+(rotate(c_1,5u)+(d_1^e_1^a_1)); d_1=rotate(d_1,30u);
    uint w24_1=rotate(w16_1^w21_1,1u);
    a_1=(a_1+w24_1+0x6ed9eba1u)+(rotate(b_1,5u)+(c_1^d_1^e_1)); c_1=rotate(c_1,30u);
    uint w25_1=rotate(w17_1^w22_1,1u);
    e_1=(e_1+w25_1+0x6ed9eba1u)+(rotate(a_1,5u)+(b_1^c_1^d_1)); b_1=rotate(b_1,30u);
    uint w26_1=rotate(w18_1^w23_1,1u);
    d_1=(d_1+w26_1+0x6ed9eba1u)+(rotate(e_1,5u)+(a_1^b_1^c_1)); a_1=rotate(a_1,30u);
    uint w27_1=rotate(w19_1^w24_1,1u);
    c_1=(c_1+w27_1+0x6ed9eba1u)+(rotate(d_1,5u)+(e_1^a_1^b_1)); e_1=rotate(e_1,30u);
    uint w28_1=rotate(w4_1^w22_1,2u);
    b_1=(b_1+w28_1+0x6ed9eba1u)+(rotate(c_1,5u)+(d_1^e_1^a_1)); d_1=rotate(d_1,30u);
    uint w29_1=rotate(w4_1^w17_1^0x20000104u,4u);
    a_1=(a_1+w29_1+0x6ed9eba1u)+(rotate(b_1,5u)+(c_1^d_1^e_1)); c_1=rotate(c_1,30u);
    uint w30_1=rotate(w0_1^w2_1^w24_1,2u);
    e_1=(e_1+w30_1+0x6ed9eba1u)+(rotate(a_1,5u)+(b_1^c_1^d_1)); b_1=rotate(b_1,30u);
    uint w31_1=rotate(w1_1^w3_1^w25_1^0x000003f0u,2u);
    d_1=(d_1+w31_1+0x6ed9eba1u)+(rotate(e_1,5u)+(a_1^b_1^c_1)); a_1=rotate(a_1,30u);
    uint w32_1=rotate(w26_1^w16_1^w4_1^w0_1,2u);
    c_1=(c_1+w32_1+0x6ed9eba1u)+(rotate(d_1,5u)+(e_1^a_1^b_1)); e_1=rotate(e_1,30u);
    uint w33_1=rotate(w1_1^w17_1^w27_1^0x80000000u,2u);
    b_1=(b_1+w33_1+0x6ed9eba1u)+(rotate(c_1,5u)+(d_1^e_1^a_1)); d_1=rotate(d_1,30u);
    uint w34_1=rotate(w2_1^w18_1^w28_1,2u);
    a_1=(a_1+w34_1+0x6ed9eba1u)+(rotate(b_1,5u)+(c_1^d_1^e_1)); c_1=rotate(c_1,30u);
    uint w35_1=rotate(w3_1^w19_1^w29_1,2u);
    e_1=(e_1+w35_1+0x6ed9eba1u)+(rotate(a_1,5u)+(b_1^c_1^d_1)); b_1=rotate(b_1,30u);
    uint w36_1=rotate(w4_1^w20_1^w30_1,2u);
    d_1=(d_1+w36_1+0x6ed9eba1u)+(rotate(e_1,5u)+(a_1^b_1^c_1)); a_1=rotate(a_1,30u);
    uint w37_1=rotate(w21_1^w31_1^0x80000000u,2u);
    c_1=(c_1+w37_1+0x6ed9eba1u)+(rotate(d_1,5u)+(e_1^a_1^b_1)); e_1=rotate(e_1,30u);
    uint w38_1=rotate(w22_1^w32_1,2u);
    b_1=(b_1+w38_1+0x6ed9eba1u)+(rotate(c_1,5u)+(d_1^e_1^a_1)); d_1=rotate(d_1,30u);
    uint w39_1=rotate(w23_1^w33_1,2u);
    a_0=(a_0+w39_0+0x6ed9eba1u)+(rotate(b_0,5u)+(c_0^d_0^e_0)); c_0=rotate(c_0,30u);
    uint w40_0=rotate(w24_0^w34_0,2u);
    e_0=(e_0+w40_0+0x8f1bbcdcu)+(rotate(a_0,5u)+SHA1_MAJ(b_0,c_0,d_0)); b_0=rotate(b_0,30u);
    uint w41_0=rotate(w1_0^w29_0,4u);
    d_0=(d_0+w41_0+0x8f1bbcdcu)+(rotate(e_0,5u)+SHA1_MAJ(a_0,b_0,c_0)); a_0=rotate(a_0,30u);
    uint w42_0=rotate(w2_0^w30_0,4u);
    c_0=(c_0+w42_0+0x8f1bbcdcu)+(rotate(d_0,5u)+SHA1_MAJ(e_0,a_0,b_0)); e_0=rotate(e_0,30u);
    uint w43_0=rotate(w3_0^w31_0^0x000000a8u,4u);
    b_0=(b_0+w43_0+0x8f1bbcdcu)+(rotate(c_0,5u)+SHA1_MAJ(d_0,e_0,a_0)); d_0=rotate(d_0,30u);
    uint w44_0=rotate(w16_0^w28_0^w38_0,2u);
    a_0=(a_0+w44_0+0x8f1bbcdcu)+(rotate(b_0,5u)+SHA1_MAJ(c_0,d_0,e_0)); c_0=rotate(c_0,30u);
    uint w45_0=rotate(w17_0^w29_0^w39_0,2u);
    e_0=(e_0+w45_0+0x8f1bbcdcu)+(rotate(a_0,5u)+SHA1_MAJ(b_0,c_0,d_0)); b_0=rotate(b_0,30u);
    uint w46_0=rotate(w18_0^w30_0^w40_0,2u);
    d_0=(d_0+w46_0+0x8f1bbcdcu)+(rotate(e_0,5u)+SHA1_MAJ(a_0,b_0,c_0)); a_0=rotate(a_0,30u);
    uint w47_0=rotate(w19_0^w31_0^w41_0^0x000002a0u,2u);
    c_0=(c_0+w47_0+0x8f1bbcdcu)+(rotate(d_0,5u)+SHA1_MAJ(e_0,a_0,b_0)); e_0=rotate(e_0,30u);
    uint w48_0=rotate(w42_0^w32_0^w20_0^w16_0,2u);
    b_0=(b_0+w48_0+0x8f1bbcdcu)+(rotate(c_0,5u)+SHA1_MAJ(d_0,e_0,a_0)); d_0=rotate(d_0,30u);
    uint w49_0=rotate(w43_0^w33_0^w21_0^w17_0,2u);
    a_0=(a_0+w49_0+0x8f1bbcdcu)+(rotate(b_0,5u)+SHA1_MAJ(c_0,d_0,e_0)); c_0=rotate(c_0,30u);
    uint w50_0=rotate(w44_0^w34_0^w22_0^w18_0,2u);
    e_0=(e_0+w50_0+0x8f1bbcdcu)+(rotate(a_0,5u)+SHA1_MAJ(b_0,c_0,d_0)); b_0=rotate(b_0,30u);
    uint w51_0=rotate(w45_0^w35_0^w23_0^w19_0,2u);
    d_0=(d_0+w51_0+0x8f1bbcdcu)+(rotate(e_0,5u)+SHA1_MAJ(a_0,b_0,c_0)); a_0=rotate(a_0,30u);
    uint w52_0=rotate(w46_0^w36_0^w24_0^w20_0,2u);
    c_0=(c_0+w52_0+0x8f1bbcdcu)+(rotate(d_0,5u)+SHA1_MAJ(e_0,a_0,b_0)); e_0=rotate(e_0,30u);
    uint w53_0=rotate(w47_0^w37_0^w25_0^w21_0,2u);
    b_0=(b_0+w53_0+0x8f1bbcdcu)+(rotate(c_0,5u)+SHA1_MAJ(d_0,e_0,a_0)); d_0=rotate(d_0,30u);
    uint w54_0=rotate(w48_0^w38_0^w26_0^w22_0,2u);
    a_0=(a_0+w54_0+0x8f1bbcdcu)+(rotate(b_0,5u)+SHA1_MAJ(c_0,d_0,e_0)); c_0=rotate(c_0,30u);
    uint w55_0=rotate(w49_0^w39_0^w27_0^w23_0,2u);
    e_0=(e_0+w55_0+0x8f1bbcdcu)+(rotate(a_0,5u)+SHA1_MAJ(b_0,c_0,d_0)); b_0=rotate(b_0,30u);
    uint w56_0=rotate(w50_0^w40_0^w28_0^w24_0,2u);
    d_0=(d_0+w56_0+0x8f1bbcdcu)+(rotate(e_0,5u)+SHA1_MAJ(a_0,b_0,c_0)); a_0=rotate(a_0,30u);
    uint w57_0=rotate(w51_0^w41_0^w29_0^w25_0,2u);
    c_0=(c_0+w57_0+0x8f1bbcdcu)+(rotate(d_0,5u)+SHA1_MAJ(e_0,a_0,b_0)); e_0=rotate(e_0,30u);
    uint w58_0=rotate(w52_0^w42_0^w30_0^w26_0,2u);
    b_0=(b_0+w58_0+0x8f1bbcdcu)+(rotate(c_0,5u)+SHA1_MAJ(d_0,e_0,a_0)); d_0=rotate(d_0,30u);
    uint w59_0=rotate(w1_0^w27_0^w47_0^0x80000150u,4u);
    a_0=(a_0+w59_0+0x8f1bbcdcu)+(rotate(b_0,5u)+SHA1_MAJ(c_0,d_0,e_0)); c_0=rotate(c_0,30u);
    uint w60_0=rotate(w0_0^w28_0^w48_0,4u);
    e_0=(e_0+w60_0+0xca62c1d6u)+(rotate(a_0,5u)+(b_0^c_0^d_0)); b_0=rotate(b_0,30u);
    uint w61_0=rotate(w1_0^w29_0^w49_0^0x00000150u,4u);
    d_0=(d_0+w61_0+0xca62c1d6u)+(rotate(e_0,5u)+(a_0^b_0^c_0)); a_0=rotate(a_0,30u);
    uint w62_0=rotate(w0_0^w30_0^w50_0,4u);
    c_0=(c_0+w62_0+0xca62c1d6u)+(rotate(d_0,5u)+(e_0^a_0^b_0)); e_0=rotate(e_0,30u);
    uint w63_0=rotate(w1_0^w31_0^w51_0^0x00000150u,4u);
    b_0=(b_0+w63_0+0xca62c1d6u)+(rotate(c_0,5u)+(d_0^e_0^a_0)); d_0=rotate(d_0,30u);
    uint w64_0=rotate(w0_0^w32_0^w52_0,4u);
    a_0=(a_0+w64_0+0xca62c1d6u)+(rotate(b_0,5u)+(c_0^d_0^e_0)); c_0=rotate(c_0,30u);
    uint w65_0=rotate(w1_0^w33_0^w53_0,4u);
    e_0=(e_0+w65_0+0xca62c1d6u)+(rotate(a_0,5u)+(b_0^c_0^d_0)); b_0=rotate(b_0,30u);
    uint w66_0=rotate(w2_0^w34_0^w54_0,4u);
    d_0=(d_0+w66_0+0xca62c1d6u)+(rotate(e_0,5u)+(a_0^b_0^c_0)); a_0=rotate(a_0,30u);
    uint w67_0=rotate(w3_0^w35_0^w55_0,4u);
    c_0=(c_0+w67_0+0xca62c1d6u)+(rotate(d_0,5u)+(e_0^a_0^b_0)); e_0=rotate(e_0,30u);
    uint w68_0=rotate(w4_0^w36_0^w56_0,4u);
    b_0=(b_0+w68_0+0xca62c1d6u)+(rotate(c_0,5u)+(d_0^e_0^a_0)); d_0=rotate(d_0,30u);
    uint w69_0=rotate(w2_0^w4_0^w45_0^0xe8000000u,8u);
    a_0=(a_0+w69_0+0xca62c1d6u)+(rotate(b_0,5u)+(c_0^d_0^e_0)); c_0=rotate(c_0,30u);
    uint w70_0=rotate(w38_0^w58_0,4u);
    e_0=(e_0+w70_0+0xca62c1d6u)+(rotate(a_0,5u)+(b_0^c_0^d_0)); b_0=rotate(b_0,30u);
    uint w71_0=rotate(w4_0^w47_0^0x0000002au,8u);
    d_0=(d_0+w71_0+0xca62c1d6u)+(rotate(e_0,5u)+(a_0^b_0^c_0)); a_0=rotate(a_0,30u);
    uint w72_0=rotate(w16_0^w40_0^w60_0,4u);
    c_0=(c_0+w72_0+0xca62c1d6u)+(rotate(d_0,5u)+(e_0^a_0^b_0)); e_0=rotate(e_0,30u);
    uint w73_0=rotate(w17_0^w41_0^w61_0,4u);
    b_0=(b_0+w73_0+0xca62c1d6u)+(rotate(c_0,5u)+(d_0^e_0^a_0)); d_0=rotate(d_0,30u);
    uint w74_0=rotate(w18_0^w42_0^w62_0,4u);
    a_0=(a_0+w74_0+0xca62c1d6u)+(rotate(b_0,5u)+(c_0^d_0^e_0)); c_0=rotate(c_0,30u);
    uint w75_0=rotate(w19_0^w43_0^w63_0,4u);
    e_0=(e_0+w75_0+0xca62c1d6u)+(rotate(a_0,5u)+(b_0^c_0^d_0)); b_0=rotate(b_0,30u);
    uint w76_0=rotate(w20_0^w44_0^w64_0,4u);
    d_0=(d_0+w76_0+0xca62c1d6u)+(rotate(e_0,5u)+(a_0^b_0^c_0)); a_0=rotate(a_0,30u);
    uint w77_0=rotate(w21_0^w45_0^w65_0,4u);
    c_0=(c_0+w77_0+0xca62c1d6u)+(rotate(d_0,5u)+(e_0^a_0^b_0)); e_0=rotate(e_0,30u);
    uint w78_0=rotate(w22_0^w46_0^w66_0,4u);
    b_0=(b_0+w78_0+0xca62c1d6u)+(rotate(c_0,5u)+(d_0^e_0^a_0)); d_0=rotate(d_0,30u);
    uint w79_0=rotate(w23_0^w47_0^w67_0^0x000002a0u,4u);
    a_1=(a_1+w39_1+0x6ed9eba1u)+(rotate(b_1,5u)+(c_1^d_1^e_1)); c_1=rotate(c_1,30u);
    uint w40_1=rotate(w24_1^w34_1,2u);
    e_1=(e_1+w40_1+0x8f1bbcdcu)+(rotate(a_1,5u)+SHA1_MAJ(b_1,c_1,d_1)); b_1=rotate(b_1,30u);
    uint w41_1=rotate(w1_1^w29_1,4u);
    d_1=(d_1+w41_1+0x8f1bbcdcu)+(rotate(e_1,5u)+SHA1_MAJ(a_1,b_1,c_1)); a_1=rotate(a_1,30u);
    uint w42_1=rotate(w2_1^w30_1,4u);
    c_1=(c_1+w42_1+0x8f1bbcdcu)+(rotate(d_1,5u)+SHA1_MAJ(e_1,a_1,b_1)); e_1=rotate(e_1,30u);
    uint w43_1=rotate(w3_1^w31_1^0x000000a8u,4u);
    b_1=(b_1+w43_1+0x8f1bbcdcu)+(rotate(c_1,5u)+SHA1_MAJ(d_1,e_1,a_1)); d_1=rotate(d_1,30u);
    uint w44_1=rotate(w16_1^w28_1^w38_1,2u);
    a_1=(a_1+w44_1+0x8f1bbcdcu)+(rotate(b_1,5u)+SHA1_MAJ(c_1,d_1,e_1)); c_1=rotate(c_1,30u);
    uint w45_1=rotate(w17_1^w29_1^w39_1,2u);
    e_1=(e_1+w45_1+0x8f1bbcdcu)+(rotate(a_1,5u)+SHA1_MAJ(b_1,c_1,d_1)); b_1=rotate(b_1,30u);
    uint w46_1=rotate(w18_1^w30_1^w40_1,2u);
    d_1=(d_1+w46_1+0x8f1bbcdcu)+(rotate(e_1,5u)+SHA1_MAJ(a_1,b_1,c_1)); a_1=rotate(a_1,30u);
    uint w47_1=rotate(w19_1^w31_1^w41_1^0x000002a0u,2u);
    c_1=(c_1+w47_1+0x8f1bbcdcu)+(rotate(d_1,5u)+SHA1_MAJ(e_1,a_1,b_1)); e_1=rotate(e_1,30u);
    uint w48_1=rotate(w42_1^w32_1^w20_1^w16_1,2u);
    b_1=(b_1+w48_1+0x8f1bbcdcu)+(rotate(c_1,5u)+SHA1_MAJ(d_1,e_1,a_1)); d_1=rotate(d_1,30u);
    uint w49_1=rotate(w43_1^w33_1^w21_1^w17_1,2u);
    a_1=(a_1+w49_1+0x8f1bbcdcu)+(rotate(b_1,5u)+SHA1_MAJ(c_1,d_1,e_1)); c_1=rotate(c_1,30u);
    uint w50_1=rotate(w44_1^w34_1^w22_1^w18_1,2u);
    e_1=(e_1+w50_1+0x8f1bbcdcu)+(rotate(a_1,5u)+SHA1_MAJ(b_1,c_1,d_1)); b_1=rotate(b_1,30u);
    uint w51_1=rotate(w45_1^w35_1^w23_1^w19_1,2u);
    d_1=(d_1+w51_1+0x8f1bbcdcu)+(rotate(e_1,5u)+SHA1_MAJ(a_1,b_1,c_1)); a_1=rotate(a_1,30u);
    uint w52_1=rotate(w46_1^w36_1^w24_1^w20_1,2u);
    c_1=(c_1+w52_1+0x8f1bbcdcu)+(rotate(d_1,5u)+SHA1_MAJ(e_1,a_1,b_1)); e_1=rotate(e_1,30u);
    uint w53_1=rotate(w47_1^w37_1^w25_1^w21_1,2u);
    b_1=(b_1+w53_1+0x8f1bbcdcu)+(rotate(c_1,5u)+SHA1_MAJ(d_1,e_1,a_1)); d_1=rotate(d_1,30u);
    uint w54_1=rotate(w48_1^w38_1^w26_1^w22_1,2u);
    a_1=(a_1+w54_1+0x8f1bbcdcu)+(rotate(b_1,5u)+SHA1_MAJ(c_1,d_1,e_1)); c_1=rotate(c_1,30u);
    uint w55_1=rotate(w49_1^w39_1^w27_1^w23_1,2u);
    e_1=(e_1+w55_1+0x8f1bbcdcu)+(rotate(a_1,5u)+SHA1_MAJ(b_1,c_1,d_1)); b_1=rotate(b_1,30u);
    uint w56_1=rotate(w50_1^w40_1^w28_1^w24_1,2u);
    d_1=(d_1+w56_1+0x8f1bbcdcu)+(rotate(e_1,5u)+SHA1_MAJ(a_1,b_1,c_1)); a_1=rotate(a_1,30u);
    uint w57_1=rotate(w51_1^w41_1^w29_1^w25_1,2u);
    c_1=(c_1+w57_1+0x8f1bbcdcu)+(rotate(d_1,5u)+SHA1_MAJ(e_1,a_1,b_1)); e_1=rotate(e_1,30u);
    uint w58_1=rotate(w52_1^w42_1^w30_1^w26_1,2u);
    b_1=(b_1+w58_1+0x8f1bbcdcu)+(rotate(c_1,5u)+SHA1_MAJ(d_1,e_1,a_1)); d_1=rotate(d_1,30u);
    uint w59_1=rotate(w1_1^w27_1^w47_1^0x80000150u,4u);
    a_1=(a_1+w59_1+0x8f1bbcdcu)+(rotate(b_1,5u)+SHA1_MAJ(c_1,d_1,e_1)); c_1=rotate(c_1,30u);
    uint w60_1=rotate(w0_1^w28_1^w48_1,4u);
    e_1=(e_1+w60_1+0xca62c1d6u)+(rotate(a_1,5u)+(b_1^c_1^d_1)); b_1=rotate(b_1,30u);
    uint w61_1=rotate(w1_1^w29_1^w49_1^0x00000150u,4u);
    d_1=(d_1+w61_1+0xca62c1d6u)+(rotate(e_1,5u)+(a_1^b_1^c_1)); a_1=rotate(a_1,30u);
    uint w62_1=rotate(w0_1^w30_1^w50_1,4u);
    c_1=(c_1+w62_1+0xca62c1d6u)+(rotate(d_1,5u)+(e_1^a_1^b_1)); e_1=rotate(e_1,30u);
    uint w63_1=rotate(w1_1^w31_1^w51_1^0x00000150u,4u);
    b_1=(b_1+w63_1+0xca62c1d6u)+(rotate(c_1,5u)+(d_1^e_1^a_1)); d_1=rotate(d_1,30u);
    uint w64_1=rotate(w0_1^w32_1^w52_1,4u);
    a_1=(a_1+w64_1+0xca62c1d6u)+(rotate(b_1,5u)+(c_1^d_1^e_1)); c_1=rotate(c_1,30u);
    uint w65_1=rotate(w1_1^w33_1^w53_1,4u);
    e_1=(e_1+w65_1+0xca62c1d6u)+(rotate(a_1,5u)+(b_1^c_1^d_1)); b_1=rotate(b_1,30u);
    uint w66_1=rotate(w2_1^w34_1^w54_1,4u);
    d_1=(d_1+w66_1+0xca62c1d6u)+(rotate(e_1,5u)+(a_1^b_1^c_1)); a_1=rotate(a_1,30u);
    uint w67_1=rotate(w3_1^w35_1^w55_1,4u);
    c_1=(c_1+w67_1+0xca62c1d6u)+(rotate(d_1,5u)+(e_1^a_1^b_1)); e_1=rotate(e_1,30u);
    uint w68_1=rotate(w4_1^w36_1^w56_1,4u);
    b_1=(b_1+w68_1+0xca62c1d6u)+(rotate(c_1,5u)+(d_1^e_1^a_1)); d_1=rotate(d_1,30u);
    uint w69_1=rotate(w2_1^w4_1^w45_1^0xe8000000u,8u);
    a_1=(a_1+w69_1+0xca62c1d6u)+(rotate(b_1,5u)+(c_1^d_1^e_1)); c_1=rotate(c_1,30u);
    uint w70_1=rotate(w38_1^w58_1,4u);
    e_1=(e_1+w70_1+0xca62c1d6u)+(rotate(a_1,5u)+(b_1^c_1^d_1)); b_1=rotate(b_1,30u);
    uint w71_1=rotate(w4_1^w47_1^0x0000002au,8u);
    d_1=(d_1+w71_1+0xca62c1d6u)+(rotate(e_1,5u)+(a_1^b_1^c_1)); a_1=rotate(a_1,30u);
    uint w72_1=rotate(w16_1^w40_1^w60_1,4u);
    c_1=(c_1+w72_1+0xca62c1d6u)+(rotate(d_1,5u)+(e_1^a_1^b_1)); e_1=rotate(e_1,30u);
    uint w73_1=rotate(w17_1^w41_1^w61_1,4u);
    b_1=(b_1+w73_1+0xca62c1d6u)+(rotate(c_1,5u)+(d_1^e_1^a_1)); d_1=rotate(d_1,30u);
    uint w74_1=rotate(w18_1^w42_1^w62_1,4u);
    a_1=(a_1+w74_1+0xca62c1d6u)+(rotate(b_1,5u)+(c_1^d_1^e_1)); c_1=rotate(c_1,30u);
    uint w75_1=rotate(w19_1^w43_1^w63_1,4u);
    e_1=(e_1+w75_1+0xca62c1d6u)+(rotate(a_1,5u)+(b_1^c_1^d_1)); b_1=rotate(b_1,30u);
    uint w76_1=rotate(w20_1^w44_1^w64_1,4u);
    d_1=(d_1+w76_1+0xca62c1d6u)+(rotate(e_1,5u)+(a_1^b_1^c_1)); a_1=rotate(a_1,30u);
    uint w77_1=rotate(w21_1^w45_1^w65_1,4u);
    c_1=(c_1+w77_1+0xca62c1d6u)+(rotate(d_1,5u)+(e_1^a_1^b_1)); e_1=rotate(e_1,30u);
    uint w78_1=rotate(w22_1^w46_1^w66_1,4u);
    b_1=(b_1+w78_1+0xca62c1d6u)+(rotate(c_1,5u)+(d_1^e_1^a_1)); d_1=rotate(d_1,30u);
    uint w79_1=rotate(w23_1^w47_1^w67_1^0x000002a0u,4u);
    a_0=(a_0+w79_0+0xca62c1d6u)+(rotate(b_0,5u)+(c_0^d_0^e_0)); c_0=rotate(c_0,30u);
    u0[0]=a_0+base[0];u0[1]=b_0+base[1];u0[2]=c_0+base[2];u0[3]=d_0+base[3];u0[4]=e_0+base[4];
    a_1=(a_1+w79_1+0xca62c1d6u)+(rotate(b_1,5u)+(c_1^d_1^e_1)); c_1=rotate(c_1,30u);
    u1[0]=a_1+base[0];u1[1]=b_1+base[1];u1[2]=c_1+base[2];u1[3]=d_1+base[3];u1[4]=e_1+base[4];
}
#endif
#elif defined(WIFI_GPU_AMD_OPENCL)
WIFI_DEVICE_INLINE void sha1_20(const uint base[5],uint digest[5]) {
    uint a=base[0],b=base[1],c=base[2],d=base[3],e=base[4];
    uint w0=digest[0];
    uint w1=digest[1];
    uint w2=digest[2];
    uint w3=digest[3];
    uint w4=digest[4];
    uint w5=0x80000000u;
    uint w6=0u;
    uint w7=0u;
    uint w8=0u;
    uint w9=0u;
    uint w10=0u;
    uint w11=0u;
    uint w12=0u;
    uint w13=0u;
    uint w14=0u;
    uint w15=672u;
    uint w16=0u;
    uint w17=0u;
    uint w18=0u;
    uint w19=0u;
    uint w20=0u;
    uint w21=0u;
    uint w22=0u;
    uint w23=0u;
    uint w24=0u;
    uint w25=0u;
    uint w26=0u;
    uint w27=0u;
    uint w28=0u;
    uint w29=0u;
    uint w30=0u;
    uint w31=0u;
    uint w32=0u;
    uint w33=0u;
    uint w34=0u;
    uint w35=0u;
    uint w36=0u;
    uint w37=0u;
    uint w38=0u;
    uint w39=0u;
    uint w40=0u;
    uint w41=0u;
    uint w42=0u;
    uint w43=0u;
    uint w44=0u;
    uint w45=0u;
    uint w46=0u;
    uint w47=0u;
    uint w48=0u;
    uint w49=0u;
    uint w50=0u;
    uint w51=0u;
    uint w52=0u;
    uint w53=0u;
    uint w54=0u;
    uint w55=0u;
    uint w56=0u;
    uint w57=0u;
    uint w58=0u;
    uint w59=0u;
    uint w60=0u;
    uint w61=0u;
    uint w62=0u;
    uint w63=0u;
    uint w64=0u;
    uint w65=0u;
    uint w66=0u;
    uint w67=0u;
    uint w68=0u;
    uint w69=0u;
    uint w70=0u;
    uint w71=0u;
    uint w72=0u;
    uint w73=0u;
    uint w74=0u;
    uint w75=0u;
    uint w76=0u;
    uint w77=0u;
    uint w78=0u;
    uint w79=0u;
    w16=rotate(w13^w8^w2^w0,1u);
    w17=rotate(w14^w9^w3^w1,1u);
    w18=rotate(w15^w10^w4^w2,1u);
    w19=rotate(w16^w11^w5^w3,1u);
    w20=rotate(w17^w12^w6^w4,1u);
    w21=rotate(w18^w13^w7^w5,1u);
    w22=rotate(w19^w14^w8^w6,1u);
    w23=rotate(w20^w15^w9^w7,1u);
    w24=rotate(w21^w16^w10^w8,1u);
    w25=rotate(w22^w17^w11^w9,1u);
    w26=rotate(w23^w18^w12^w10,1u);
    w27=rotate(w24^w19^w13^w11,1u);
    w28=rotate(w25^w20^w14^w12,1u);
    w29=rotate(w26^w21^w15^w13,1u);
    w30=rotate(w27^w22^w16^w14,1u);
    w31=rotate(w28^w23^w17^w15,1u);
    w32=rotate(w26^w16^w4^w0,2u);
    w33=rotate(w27^w17^w5^w1,2u);
    w34=rotate(w28^w18^w6^w2,2u);
    w35=rotate(w29^w19^w7^w3,2u);
    w36=rotate(w30^w20^w8^w4,2u);
    w37=rotate(w31^w21^w9^w5,2u);
    w38=rotate(w32^w22^w10^w6,2u);
    w39=rotate(w33^w23^w11^w7,2u);
    w40=rotate(w34^w24^w12^w8,2u);
    w41=rotate(w35^w25^w13^w9,2u);
    w42=rotate(w36^w26^w14^w10,2u);
    w43=rotate(w37^w27^w15^w11,2u);
    w44=rotate(w38^w28^w16^w12,2u);
    w45=rotate(w39^w29^w17^w13,2u);
    w46=rotate(w40^w30^w18^w14,2u);
    w47=rotate(w41^w31^w19^w15,2u);
    w48=rotate(w42^w32^w20^w16,2u);
    w49=rotate(w43^w33^w21^w17,2u);
    w50=rotate(w44^w34^w22^w18,2u);
    w51=rotate(w45^w35^w23^w19,2u);
    w52=rotate(w46^w36^w24^w20,2u);
    w53=rotate(w47^w37^w25^w21,2u);
    w54=rotate(w48^w38^w26^w22,2u);
    w55=rotate(w49^w39^w27^w23,2u);
    w56=rotate(w50^w40^w28^w24,2u);
    w57=rotate(w51^w41^w29^w25,2u);
    w58=rotate(w52^w42^w30^w26,2u);
    w59=rotate(w53^w43^w31^w27,2u);
    w60=rotate(w54^w44^w32^w28,2u);
    w61=rotate(w55^w45^w33^w29,2u);
    w62=rotate(w56^w46^w34^w30,2u);
    w63=rotate(w57^w47^w35^w31,2u);
    w64=rotate(w52^w32^w8^w0,4u);
    w65=rotate(w53^w33^w9^w1,4u);
    w66=rotate(w54^w34^w10^w2,4u);
    w67=rotate(w55^w35^w11^w3,4u);
    w68=rotate(w56^w36^w12^w4,4u);
    w69=rotate(w57^w37^w13^w5,4u);
    w70=rotate(w58^w38^w14^w6,4u);
    w71=rotate(w59^w39^w15^w7,4u);
    w72=rotate(w60^w40^w16^w8,4u);
    w73=rotate(w61^w41^w17^w9,4u);
    w74=rotate(w62^w42^w18^w10,4u);
    w75=rotate(w63^w43^w19^w11,4u);
    w76=rotate(w64^w44^w20^w12,4u);
    w77=rotate(w65^w45^w21^w13,4u);
    w78=rotate(w66^w46^w22^w14,4u);
    w79=rotate(w67^w47^w23^w15,4u);
    e=(e+w0+0x5a827999u)+(rotate(a,5u)+SHA1_CH(b,c,d)); b=rotate(b,30u);
    d=(d+w1+0x5a827999u)+(rotate(e,5u)+SHA1_CH(a,b,c)); a=rotate(a,30u);
    c=(c+w2+0x5a827999u)+(rotate(d,5u)+SHA1_CH(e,a,b)); e=rotate(e,30u);
    b=(b+w3+0x5a827999u)+(rotate(c,5u)+SHA1_CH(d,e,a)); d=rotate(d,30u);
    a=(a+w4+0x5a827999u)+(rotate(b,5u)+SHA1_CH(c,d,e)); c=rotate(c,30u);
    e=(e+w5+0x5a827999u)+(rotate(a,5u)+SHA1_CH(b,c,d)); b=rotate(b,30u);
    d=(d+w6+0x5a827999u)+(rotate(e,5u)+SHA1_CH(a,b,c)); a=rotate(a,30u);
    c=(c+w7+0x5a827999u)+(rotate(d,5u)+SHA1_CH(e,a,b)); e=rotate(e,30u);
    b=(b+w8+0x5a827999u)+(rotate(c,5u)+SHA1_CH(d,e,a)); d=rotate(d,30u);
    a=(a+w9+0x5a827999u)+(rotate(b,5u)+SHA1_CH(c,d,e)); c=rotate(c,30u);
    e=(e+w10+0x5a827999u)+(rotate(a,5u)+SHA1_CH(b,c,d)); b=rotate(b,30u);
    d=(d+w11+0x5a827999u)+(rotate(e,5u)+SHA1_CH(a,b,c)); a=rotate(a,30u);
    c=(c+w12+0x5a827999u)+(rotate(d,5u)+SHA1_CH(e,a,b)); e=rotate(e,30u);
    b=(b+w13+0x5a827999u)+(rotate(c,5u)+SHA1_CH(d,e,a)); d=rotate(d,30u);
    a=(a+w14+0x5a827999u)+(rotate(b,5u)+SHA1_CH(c,d,e)); c=rotate(c,30u);
    e=(e+w15+0x5a827999u)+(rotate(a,5u)+SHA1_CH(b,c,d)); b=rotate(b,30u);
    d=(d+w16+0x5a827999u)+(rotate(e,5u)+SHA1_CH(a,b,c)); a=rotate(a,30u);
    c=(c+w17+0x5a827999u)+(rotate(d,5u)+SHA1_CH(e,a,b)); e=rotate(e,30u);
    b=(b+w18+0x5a827999u)+(rotate(c,5u)+SHA1_CH(d,e,a)); d=rotate(d,30u);
    a=(a+w19+0x5a827999u)+(rotate(b,5u)+SHA1_CH(c,d,e)); c=rotate(c,30u);
    e=(e+w20+0x6ed9eba1u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w21+0x6ed9eba1u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w22+0x6ed9eba1u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w23+0x6ed9eba1u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w24+0x6ed9eba1u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w25+0x6ed9eba1u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w26+0x6ed9eba1u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w27+0x6ed9eba1u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w28+0x6ed9eba1u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w29+0x6ed9eba1u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w30+0x6ed9eba1u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w31+0x6ed9eba1u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w32+0x6ed9eba1u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w33+0x6ed9eba1u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w34+0x6ed9eba1u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w35+0x6ed9eba1u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w36+0x6ed9eba1u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w37+0x6ed9eba1u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w38+0x6ed9eba1u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w39+0x6ed9eba1u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w40+0x8f1bbcdcu)+(rotate(a,5u)+SHA1_MAJ(b,c,d)); b=rotate(b,30u);
    d=(d+w41+0x8f1bbcdcu)+(rotate(e,5u)+SHA1_MAJ(a,b,c)); a=rotate(a,30u);
    c=(c+w42+0x8f1bbcdcu)+(rotate(d,5u)+SHA1_MAJ(e,a,b)); e=rotate(e,30u);
    b=(b+w43+0x8f1bbcdcu)+(rotate(c,5u)+SHA1_MAJ(d,e,a)); d=rotate(d,30u);
    a=(a+w44+0x8f1bbcdcu)+(rotate(b,5u)+SHA1_MAJ(c,d,e)); c=rotate(c,30u);
    e=(e+w45+0x8f1bbcdcu)+(rotate(a,5u)+SHA1_MAJ(b,c,d)); b=rotate(b,30u);
    d=(d+w46+0x8f1bbcdcu)+(rotate(e,5u)+SHA1_MAJ(a,b,c)); a=rotate(a,30u);
    c=(c+w47+0x8f1bbcdcu)+(rotate(d,5u)+SHA1_MAJ(e,a,b)); e=rotate(e,30u);
    b=(b+w48+0x8f1bbcdcu)+(rotate(c,5u)+SHA1_MAJ(d,e,a)); d=rotate(d,30u);
    a=(a+w49+0x8f1bbcdcu)+(rotate(b,5u)+SHA1_MAJ(c,d,e)); c=rotate(c,30u);
    e=(e+w50+0x8f1bbcdcu)+(rotate(a,5u)+SHA1_MAJ(b,c,d)); b=rotate(b,30u);
    d=(d+w51+0x8f1bbcdcu)+(rotate(e,5u)+SHA1_MAJ(a,b,c)); a=rotate(a,30u);
    c=(c+w52+0x8f1bbcdcu)+(rotate(d,5u)+SHA1_MAJ(e,a,b)); e=rotate(e,30u);
    b=(b+w53+0x8f1bbcdcu)+(rotate(c,5u)+SHA1_MAJ(d,e,a)); d=rotate(d,30u);
    a=(a+w54+0x8f1bbcdcu)+(rotate(b,5u)+SHA1_MAJ(c,d,e)); c=rotate(c,30u);
    e=(e+w55+0x8f1bbcdcu)+(rotate(a,5u)+SHA1_MAJ(b,c,d)); b=rotate(b,30u);
    d=(d+w56+0x8f1bbcdcu)+(rotate(e,5u)+SHA1_MAJ(a,b,c)); a=rotate(a,30u);
    c=(c+w57+0x8f1bbcdcu)+(rotate(d,5u)+SHA1_MAJ(e,a,b)); e=rotate(e,30u);
    b=(b+w58+0x8f1bbcdcu)+(rotate(c,5u)+SHA1_MAJ(d,e,a)); d=rotate(d,30u);
    a=(a+w59+0x8f1bbcdcu)+(rotate(b,5u)+SHA1_MAJ(c,d,e)); c=rotate(c,30u);
    e=(e+w60+0xca62c1d6u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w61+0xca62c1d6u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w62+0xca62c1d6u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w63+0xca62c1d6u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w64+0xca62c1d6u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w65+0xca62c1d6u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w66+0xca62c1d6u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w67+0xca62c1d6u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w68+0xca62c1d6u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w69+0xca62c1d6u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w70+0xca62c1d6u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w71+0xca62c1d6u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w72+0xca62c1d6u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w73+0xca62c1d6u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w74+0xca62c1d6u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    e=(e+w75+0xca62c1d6u)+(rotate(a,5u)+(b^c^d)); b=rotate(b,30u);
    d=(d+w76+0xca62c1d6u)+(rotate(e,5u)+(a^b^c)); a=rotate(a,30u);
    c=(c+w77+0xca62c1d6u)+(rotate(d,5u)+(e^a^b)); e=rotate(e,30u);
    b=(b+w78+0xca62c1d6u)+(rotate(c,5u)+(d^e^a)); d=rotate(d,30u);
    a=(a+w79+0xca62c1d6u)+(rotate(b,5u)+(c^d^e)); c=rotate(c,30u);
    digest[0]=a+base[0];digest[1]=b+base[1];digest[2]=c+base[2];digest[3]=d+base[3];digest[4]=e+base[4];
}
#else
WIFI_DEVICE_INLINE void sha1_20(const uint base[5],uint digest[5]) {
    uint a=base[0],b=base[1],c=base[2],d=base[3],e=base[4];
    uint w0=digest[0];
    uint w1=digest[1];
    uint w2=digest[2];
    uint w3=digest[3];
    uint w4=digest[4];
    uint w5=0x80000000u;
    uint w6=0u;
    uint w7=0u;
    uint w8=0u;
    uint w9=0u;
    uint w10=0u;
    uint w11=0u;
    uint w12=0u;
    uint w13=0u;
    uint w14=0u;
    uint w15=672u;
    e+=rotate(a,5u)+SHA1_CH(b,c,d)+0x5a827999u+w0; b=rotate(b,30u);
    d+=rotate(e,5u)+SHA1_CH(a,b,c)+0x5a827999u+w1; a=rotate(a,30u);
    c+=rotate(d,5u)+SHA1_CH(e,a,b)+0x5a827999u+w2; e=rotate(e,30u);
    b+=rotate(c,5u)+SHA1_CH(d,e,a)+0x5a827999u+w3; d=rotate(d,30u);
    a+=rotate(b,5u)+SHA1_CH(c,d,e)+0x5a827999u+w4; c=rotate(c,30u);
    e+=rotate(a,5u)+SHA1_CH(b,c,d)+0x5a827999u+w5; b=rotate(b,30u);
    d+=rotate(e,5u)+SHA1_CH(a,b,c)+0x5a827999u+w6; a=rotate(a,30u);
    c+=rotate(d,5u)+SHA1_CH(e,a,b)+0x5a827999u+w7; e=rotate(e,30u);
    b+=rotate(c,5u)+SHA1_CH(d,e,a)+0x5a827999u+w8; d=rotate(d,30u);
    a+=rotate(b,5u)+SHA1_CH(c,d,e)+0x5a827999u+w9; c=rotate(c,30u);
    e+=rotate(a,5u)+SHA1_CH(b,c,d)+0x5a827999u+w10; b=rotate(b,30u);
    d+=rotate(e,5u)+SHA1_CH(a,b,c)+0x5a827999u+w11; a=rotate(a,30u);
    c+=rotate(d,5u)+SHA1_CH(e,a,b)+0x5a827999u+w12; e=rotate(e,30u);
    b+=rotate(c,5u)+SHA1_CH(d,e,a)+0x5a827999u+w13; d=rotate(d,30u);
    a+=rotate(b,5u)+SHA1_CH(c,d,e)+0x5a827999u+w14; c=rotate(c,30u);
    e+=rotate(a,5u)+SHA1_CH(b,c,d)+0x5a827999u+w15; b=rotate(b,30u);
    w0=rotate(w13^w8^w2^w0,1u);
    d+=rotate(e,5u)+SHA1_CH(a,b,c)+0x5a827999u+w0; a=rotate(a,30u);
    w1=rotate(w14^w9^w3^w1,1u);
    c+=rotate(d,5u)+SHA1_CH(e,a,b)+0x5a827999u+w1; e=rotate(e,30u);
    w2=rotate(w15^w10^w4^w2,1u);
    b+=rotate(c,5u)+SHA1_CH(d,e,a)+0x5a827999u+w2; d=rotate(d,30u);
    w3=rotate(w0^w11^w5^w3,1u);
    a+=rotate(b,5u)+SHA1_CH(c,d,e)+0x5a827999u+w3; c=rotate(c,30u);
    w4=rotate(w1^w12^w6^w4,1u);
    e+=rotate(a,5u)+(b^c^d)+0x6ed9eba1u+w4; b=rotate(b,30u);
    w5=rotate(w2^w13^w7^w5,1u);
    d+=rotate(e,5u)+(a^b^c)+0x6ed9eba1u+w5; a=rotate(a,30u);
    w6=rotate(w3^w14^w8^w6,1u);
    c+=rotate(d,5u)+(e^a^b)+0x6ed9eba1u+w6; e=rotate(e,30u);
    w7=rotate(w4^w15^w9^w7,1u);
    b+=rotate(c,5u)+(d^e^a)+0x6ed9eba1u+w7; d=rotate(d,30u);
    w8=rotate(w5^w0^w10^w8,1u);
    a+=rotate(b,5u)+(c^d^e)+0x6ed9eba1u+w8; c=rotate(c,30u);
    w9=rotate(w6^w1^w11^w9,1u);
    e+=rotate(a,5u)+(b^c^d)+0x6ed9eba1u+w9; b=rotate(b,30u);
    w10=rotate(w7^w2^w12^w10,1u);
    d+=rotate(e,5u)+(a^b^c)+0x6ed9eba1u+w10; a=rotate(a,30u);
    w11=rotate(w8^w3^w13^w11,1u);
    c+=rotate(d,5u)+(e^a^b)+0x6ed9eba1u+w11; e=rotate(e,30u);
    w12=rotate(w9^w4^w14^w12,1u);
    b+=rotate(c,5u)+(d^e^a)+0x6ed9eba1u+w12; d=rotate(d,30u);
    w13=rotate(w10^w5^w15^w13,1u);
    a+=rotate(b,5u)+(c^d^e)+0x6ed9eba1u+w13; c=rotate(c,30u);
    w14=rotate(w11^w6^w0^w14,1u);
    e+=rotate(a,5u)+(b^c^d)+0x6ed9eba1u+w14; b=rotate(b,30u);
    w15=rotate(w12^w7^w1^w15,1u);
    d+=rotate(e,5u)+(a^b^c)+0x6ed9eba1u+w15; a=rotate(a,30u);
    w0=rotate(w13^w8^w2^w0,1u);
    c+=rotate(d,5u)+(e^a^b)+0x6ed9eba1u+w0; e=rotate(e,30u);
    w1=rotate(w14^w9^w3^w1,1u);
    b+=rotate(c,5u)+(d^e^a)+0x6ed9eba1u+w1; d=rotate(d,30u);
    w2=rotate(w15^w10^w4^w2,1u);
    a+=rotate(b,5u)+(c^d^e)+0x6ed9eba1u+w2; c=rotate(c,30u);
    w3=rotate(w0^w11^w5^w3,1u);
    e+=rotate(a,5u)+(b^c^d)+0x6ed9eba1u+w3; b=rotate(b,30u);
    w4=rotate(w1^w12^w6^w4,1u);
    d+=rotate(e,5u)+(a^b^c)+0x6ed9eba1u+w4; a=rotate(a,30u);
    w5=rotate(w2^w13^w7^w5,1u);
    c+=rotate(d,5u)+(e^a^b)+0x6ed9eba1u+w5; e=rotate(e,30u);
    w6=rotate(w3^w14^w8^w6,1u);
    b+=rotate(c,5u)+(d^e^a)+0x6ed9eba1u+w6; d=rotate(d,30u);
    w7=rotate(w4^w15^w9^w7,1u);
    a+=rotate(b,5u)+(c^d^e)+0x6ed9eba1u+w7; c=rotate(c,30u);
    w8=rotate(w5^w0^w10^w8,1u);
    e+=rotate(a,5u)+SHA1_MAJ(b,c,d)+0x8f1bbcdcu+w8; b=rotate(b,30u);
    w9=rotate(w6^w1^w11^w9,1u);
    d+=rotate(e,5u)+SHA1_MAJ(a,b,c)+0x8f1bbcdcu+w9; a=rotate(a,30u);
    w10=rotate(w7^w2^w12^w10,1u);
    c+=rotate(d,5u)+SHA1_MAJ(e,a,b)+0x8f1bbcdcu+w10; e=rotate(e,30u);
    w11=rotate(w8^w3^w13^w11,1u);
    b+=rotate(c,5u)+SHA1_MAJ(d,e,a)+0x8f1bbcdcu+w11; d=rotate(d,30u);
    w12=rotate(w9^w4^w14^w12,1u);
    a+=rotate(b,5u)+SHA1_MAJ(c,d,e)+0x8f1bbcdcu+w12; c=rotate(c,30u);
    w13=rotate(w10^w5^w15^w13,1u);
    e+=rotate(a,5u)+SHA1_MAJ(b,c,d)+0x8f1bbcdcu+w13; b=rotate(b,30u);
    w14=rotate(w11^w6^w0^w14,1u);
    d+=rotate(e,5u)+SHA1_MAJ(a,b,c)+0x8f1bbcdcu+w14; a=rotate(a,30u);
    w15=rotate(w12^w7^w1^w15,1u);
    c+=rotate(d,5u)+SHA1_MAJ(e,a,b)+0x8f1bbcdcu+w15; e=rotate(e,30u);
    w0=rotate(w13^w8^w2^w0,1u);
    b+=rotate(c,5u)+SHA1_MAJ(d,e,a)+0x8f1bbcdcu+w0; d=rotate(d,30u);
    w1=rotate(w14^w9^w3^w1,1u);
    a+=rotate(b,5u)+SHA1_MAJ(c,d,e)+0x8f1bbcdcu+w1; c=rotate(c,30u);
    w2=rotate(w15^w10^w4^w2,1u);
    e+=rotate(a,5u)+SHA1_MAJ(b,c,d)+0x8f1bbcdcu+w2; b=rotate(b,30u);
    w3=rotate(w0^w11^w5^w3,1u);
    d+=rotate(e,5u)+SHA1_MAJ(a,b,c)+0x8f1bbcdcu+w3; a=rotate(a,30u);
    w4=rotate(w1^w12^w6^w4,1u);
    c+=rotate(d,5u)+SHA1_MAJ(e,a,b)+0x8f1bbcdcu+w4; e=rotate(e,30u);
    w5=rotate(w2^w13^w7^w5,1u);
    b+=rotate(c,5u)+SHA1_MAJ(d,e,a)+0x8f1bbcdcu+w5; d=rotate(d,30u);
    w6=rotate(w3^w14^w8^w6,1u);
    a+=rotate(b,5u)+SHA1_MAJ(c,d,e)+0x8f1bbcdcu+w6; c=rotate(c,30u);
    w7=rotate(w4^w15^w9^w7,1u);
    e+=rotate(a,5u)+SHA1_MAJ(b,c,d)+0x8f1bbcdcu+w7; b=rotate(b,30u);
    w8=rotate(w5^w0^w10^w8,1u);
    d+=rotate(e,5u)+SHA1_MAJ(a,b,c)+0x8f1bbcdcu+w8; a=rotate(a,30u);
    w9=rotate(w6^w1^w11^w9,1u);
    c+=rotate(d,5u)+SHA1_MAJ(e,a,b)+0x8f1bbcdcu+w9; e=rotate(e,30u);
    w10=rotate(w7^w2^w12^w10,1u);
    b+=rotate(c,5u)+SHA1_MAJ(d,e,a)+0x8f1bbcdcu+w10; d=rotate(d,30u);
    w11=rotate(w8^w3^w13^w11,1u);
    a+=rotate(b,5u)+SHA1_MAJ(c,d,e)+0x8f1bbcdcu+w11; c=rotate(c,30u);
    w12=rotate(w9^w4^w14^w12,1u);
    e+=rotate(a,5u)+(b^c^d)+0xca62c1d6u+w12; b=rotate(b,30u);
    w13=rotate(w10^w5^w15^w13,1u);
    d+=rotate(e,5u)+(a^b^c)+0xca62c1d6u+w13; a=rotate(a,30u);
    w14=rotate(w11^w6^w0^w14,1u);
    c+=rotate(d,5u)+(e^a^b)+0xca62c1d6u+w14; e=rotate(e,30u);
    w15=rotate(w12^w7^w1^w15,1u);
    b+=rotate(c,5u)+(d^e^a)+0xca62c1d6u+w15; d=rotate(d,30u);
    w0=rotate(w13^w8^w2^w0,1u);
    a+=rotate(b,5u)+(c^d^e)+0xca62c1d6u+w0; c=rotate(c,30u);
    w1=rotate(w14^w9^w3^w1,1u);
    e+=rotate(a,5u)+(b^c^d)+0xca62c1d6u+w1; b=rotate(b,30u);
    w2=rotate(w15^w10^w4^w2,1u);
    d+=rotate(e,5u)+(a^b^c)+0xca62c1d6u+w2; a=rotate(a,30u);
    w3=rotate(w0^w11^w5^w3,1u);
    c+=rotate(d,5u)+(e^a^b)+0xca62c1d6u+w3; e=rotate(e,30u);
    w4=rotate(w1^w12^w6^w4,1u);
    b+=rotate(c,5u)+(d^e^a)+0xca62c1d6u+w4; d=rotate(d,30u);
    w5=rotate(w2^w13^w7^w5,1u);
    a+=rotate(b,5u)+(c^d^e)+0xca62c1d6u+w5; c=rotate(c,30u);
    w6=rotate(w3^w14^w8^w6,1u);
    e+=rotate(a,5u)+(b^c^d)+0xca62c1d6u+w6; b=rotate(b,30u);
    w7=rotate(w4^w15^w9^w7,1u);
    d+=rotate(e,5u)+(a^b^c)+0xca62c1d6u+w7; a=rotate(a,30u);
    w8=rotate(w5^w0^w10^w8,1u);
    c+=rotate(d,5u)+(e^a^b)+0xca62c1d6u+w8; e=rotate(e,30u);
    w9=rotate(w6^w1^w11^w9,1u);
    b+=rotate(c,5u)+(d^e^a)+0xca62c1d6u+w9; d=rotate(d,30u);
    w10=rotate(w7^w2^w12^w10,1u);
    a+=rotate(b,5u)+(c^d^e)+0xca62c1d6u+w10; c=rotate(c,30u);
    w11=rotate(w8^w3^w13^w11,1u);
    e+=rotate(a,5u)+(b^c^d)+0xca62c1d6u+w11; b=rotate(b,30u);
    w12=rotate(w9^w4^w14^w12,1u);
    d+=rotate(e,5u)+(a^b^c)+0xca62c1d6u+w12; a=rotate(a,30u);
    w13=rotate(w10^w5^w15^w13,1u);
    c+=rotate(d,5u)+(e^a^b)+0xca62c1d6u+w13; e=rotate(e,30u);
    w14=rotate(w11^w6^w0^w14,1u);
    b+=rotate(c,5u)+(d^e^a)+0xca62c1d6u+w14; d=rotate(d,30u);
    w15=rotate(w12^w7^w1^w15,1u);
    a+=rotate(b,5u)+(c^d^e)+0xca62c1d6u+w15; c=rotate(c,30u);
    digest[0]=a+base[0]; digest[1]=b+base[1]; digest[2]=c+base[2]; digest[3]=d+base[3]; digest[4]=e+base[4];
}
#endif
// END GENERATED SHA1_20

__kernel void pbkdf2_init(__global const Password* passwords,
                         __global const uchar* ssid, uint ssid_len,
                         __global State* states, uint count) {
    size_t id=get_global_id(0);
    if (id>=count) return;
    State s;
    uint key[16]={0}, w[16];
    for (uint i=0;i<passwords[id].length;++i)
        key[i>>2] |= ((uint)passwords[id].bytes[i]) << (24U-8U*(i&3));
    sha1_initial(s.ipad); sha1_initial(s.opad);
    for (uint i=0;i<16;++i) w[i]=key[i]^0x36363636U;
    sha1_block(s.ipad,w);
    for (uint i=0;i<16;++i) w[i]=key[i]^0x5c5c5c5cU;
    sha1_block(s.opad,w);
    for (uint block=0;block<2;++block) {
        uint inner[5];
        for (uint i=0;i<16;++i) w[i]=0;
        for (uint i=0;i<ssid_len;++i) w[i>>2] |= ((uint)ssid[i]) << (24U-8U*(i&3));
        uint last=ssid_len+3U;
        w[last>>2] |= (block+1U) << (24U-8U*(last&3));
        uint pad=ssid_len+4U;
        w[pad>>2] |= 0x80U << (24U-8U*(pad&3));
        w[15]=(64U+ssid_len+4U)*8U;
        for (uint i=0;i<5;++i) inner[i]=s.ipad[i];
        sha1_block(inner,w);
        finish_hmac(s.opad,inner);
        for (uint i=0;i<5;++i) s.u[block*5+i]=s.sum[block*5+i]=inner[i];
    }
    states[id]=s;
}

#if defined(WIFI_GPU_HIP) && defined(WIFI_GPU_HIP_PAIR)
// Interleave both independent SHA-1 chains on gfx1200.
__kernel void pbkdf2_loop_pair(__global State* states,uint rounds,uint count) {
    size_t id=get_global_id(0);
    if(id>=count) return;
    uint ip[5],op[5],u0[5],u1[5],acc0[5],acc1[3];
    for(uint i=0;i<5;++i) {
        ip[i]=states[id].ipad[i];op[i]=states[id].opad[i];
        u0[i]=states[id].u[i];u1[i]=states[id].u[i+5];
        acc0[i]=states[id].sum[i];if(i<3) acc1[i]=states[id].sum[i+5];
    }
    #pragma unroll 1
    for(uint step=0;step<rounds;++step) {
        sha1_pair(ip,u0,u1); sha1_pair(op,u0,u1);
        for(uint i=0;i<5;++i) {acc0[i]^=u0[i];if(i<3) acc1[i]^=u1[i];}
    }
    for(uint i=0;i<5;++i) {
        states[id].u[i]=u0[i];states[id].u[i+5]=u1[i];
        states[id].sum[i]=acc0[i];if(i<3) states[id].sum[i+5]=acc1[i];
    }
}
#else
// Two work items per candidate independently derive the two PBKDF2 blocks.
// The split entry-point name prevents an older one-item host from using this ABI.
// Each item writes only its own u/sum words; init and finish still use one item.
__kernel void pbkdf2_loop_split(__global State* states, uint rounds, uint count) {
    size_t id=get_global_id(0);
    if(id>=count*2U) return;
    uint block=(uint)(id&1U);
    id>>=1;
    uint ip[5],op[5],u[5],acc[5];
    for(uint i=0;i<5;++i) {
        ip[i]=states[id].ipad[i]; op[i]=states[id].opad[i];
        u[i]=states[id].u[block*5+i]; acc[i]=states[id].sum[block*5+i];
    }
    // AMD benefits from separate inner/outer bodies and bitselect boolean ops.
#ifdef WIFI_GPU_AMD_OPENCL
    #pragma unroll 1
    for(uint step=0;step<rounds;++step) {
        sha1_20(ip,u);
        sha1_20(op,u);
        for(uint i=0;i<5;++i) acc[i]^=u[i];
    }
#else
    // Share one compression body between inner and outer HMAC. Two steps
    // restore the pad order; only outer steps contribute to the accumulator.
    #pragma unroll 1
    for(uint step=0;step<rounds*2;++step) {
        sha1_20(ip,u);
        for(uint i=0;i<5;++i) { uint t=ip[i]; ip[i]=op[i]; op[i]=t; }
        if(step&1U) for(uint i=0;i<5;++i) acc[i]^=u[i];
    }
#endif
    for(uint i=0;i<5;++i) { states[id].u[block*5+i]=u[i]; states[id].sum[block*5+i]=acc[i]; }
}

#endif

__kernel void pbkdf2_finish(__global const State* states, __global uchar* pmks, uint count) {
    size_t id=get_global_id(0);
    if (id>=count) return;
    for (uint i=0;i<32;++i)
        pmks[id*32+i]=(uchar)(states[id].sum[i>>2] >> (24U-8U*(i&3)));
}

// A full HMAC-SHA1 with a key of at most 32 bytes. Messages are padded on the
// host, including the 64-byte HMAC prefix in the encoded length.
static WIFI_DEVICE_INLINE void hmac_sha1_blocks(const uint key[8], __global const uint* blocks,
                             uint block_count, uint digest[5]) {
    uint inner[5],outer[5],w[16];
    sha1_initial(inner); sha1_initial(outer);
    for(uint i=0;i<16;++i) w[i]=(i<8 ? key[i] : 0U)^0x36363636U;
    sha1_block(inner,w);
    for(uint i=0;i<16;++i) w[i]=(i<8 ? key[i] : 0U)^0x5c5c5c5cU;
    sha1_block(outer,w);
    for(uint block=0;block<block_count;++block) {
        for(uint i=0;i<16;++i) w[i]=blocks[block*16+i];
        sha1_block(inner,w);
    }
    sha1_20(outer,inner);
    for(uint i=0;i<5;++i) digest[i]=inner[i];
}

// WPA2 EAPOL v2 with one exact nonce context. Other record types and nonce
// correction sets use the CPU verifier. Every reported hit is CPU rederived.
__kernel void wpa2_sha1_filter(__global const uchar* pmks,
                              __global const uint* ptk, uint ptk_blocks,
                              __global const uint* eapol, uint eapol_blocks,
                              __global const uint* mic, __global uchar* matches,
                              uint count) {
    size_t id=get_global_id(0);
    if(id>=count) return;
    uint key[8],digest[5];
    for(uint i=0;i<8;++i) {
        size_t at=id*32+i*4;
        key[i]=((uint)pmks[at]<<24)|((uint)pmks[at+1]<<16)|
               ((uint)pmks[at+2]<<8)|(uint)pmks[at+3];
    }
    hmac_sha1_blocks(key,ptk,ptk_blocks,digest);
    for(uint i=0;i<8;++i) key[i]=i<4 ? digest[i] : 0U;
    hmac_sha1_blocks(key,eapol,eapol_blocks,digest);
    matches[id]=(digest[0]==mic[0] && digest[1]==mic[1] &&
                 digest[2]==mic[2] && digest[3]==mic[3]);
}
