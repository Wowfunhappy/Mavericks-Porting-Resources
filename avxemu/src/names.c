/* Mnemonic strings for diagnostics and unimplemented-op reporting. */
#include "vexops.h"

static const char *const names[VEX_OP_COUNT] = {
    [VEX_INVALID]="<invalid>",
    [VPADDB]="vpaddb",[VPADDW]="vpaddw",[VPADDD]="vpaddd",[VPADDQ]="vpaddq",
    [VPSUBB]="vpsubb",[VPSUBW]="vpsubw",[VPSUBD]="vpsubd",[VPSUBQ]="vpsubq",
    [VPADDSB]="vpaddsb",[VPADDSW]="vpaddsw",[VPADDUSB]="vpaddusb",[VPADDUSW]="vpaddusw",
    [VPSUBSB]="vpsubsb",[VPSUBSW]="vpsubsw",[VPSUBUSB]="vpsubusb",[VPSUBUSW]="vpsubusw",
    [VPAND]="vpand",[VPANDN]="vpandn",[VPOR]="vpor",[VPXOR]="vpxor",
    [VPCMPEQB]="vpcmpeqb",[VPCMPEQW]="vpcmpeqw",[VPCMPEQD]="vpcmpeqd",[VPCMPEQQ]="vpcmpeqq",
    [VPCMPGTB]="vpcmpgtb",[VPCMPGTW]="vpcmpgtw",[VPCMPGTD]="vpcmpgtd",[VPCMPGTQ]="vpcmpgtq",
    [VPMINUB]="vpminub",[VPMINUW]="vpminuw",[VPMINUD]="vpminud",
    [VPMINSB]="vpminsb",[VPMINSW]="vpminsw",[VPMINSD]="vpminsd",
    [VPMAXUB]="vpmaxub",[VPMAXUW]="vpmaxuw",[VPMAXUD]="vpmaxud",
    [VPMAXSB]="vpmaxsb",[VPMAXSW]="vpmaxsw",[VPMAXSD]="vpmaxsd",
    [VPMULLW]="vpmullw",[VPMULLD]="vpmulld",[VPMULHW]="vpmulhw",[VPMULHUW]="vpmulhuw",
    [VPMULHRSW]="vpmulhrsw",[VPMULDQ]="vpmuldq",[VPMULUDQ]="vpmuludq",
    [VPMADDWD]="vpmaddwd",[VPMADDUBSW]="vpmaddubsw",
    [VPAVGB]="vpavgb",[VPAVGW]="vpavgw",[VPSADBW]="vpsadbw",
    [VPABSB]="vpabsb",[VPABSW]="vpabsw",[VPABSD]="vpabsd",
    [VPSIGNB]="vpsignb",[VPSIGNW]="vpsignw",[VPSIGND]="vpsignd",[VPHADDD]="vphaddd",
    [VPSLLW]="vpsllw",[VPSLLD]="vpslld",[VPSLLQ]="vpsllq",
    [VPSRLW]="vpsrlw",[VPSRLD]="vpsrld",[VPSRLQ]="vpsrlq",[VPSRAW]="vpsraw",[VPSRAD]="vpsrad",
    [VPSLLVD]="vpsllvd",[VPSLLVQ]="vpsllvq",[VPSRLVD]="vpsrlvd",[VPSRLVQ]="vpsrlvq",[VPSRAVD]="vpsravd",
    [VPSLLDQ]="vpslldq",[VPSRLDQ]="vpsrldq",
    [VPSHUFB]="vpshufb",[VPSHUFD]="vpshufd",[VPSHUFLW]="vpshuflw",[VPSHUFHW]="vpshufhw",
    [VPACKSSWB]="vpacksswb",[VPACKSSDW]="vpackssdw",[VPACKUSWB]="vpackuswb",[VPACKUSDW]="vpackusdw",
    [VPUNPCKLBW]="vpunpcklbw",[VPUNPCKHBW]="vpunpckhbw",[VPUNPCKLWD]="vpunpcklwd",[VPUNPCKHWD]="vpunpckhwd",
    [VPUNPCKLDQ]="vpunpckldq",[VPUNPCKHDQ]="vpunpckhdq",[VPUNPCKLQDQ]="vpunpcklqdq",[VPUNPCKHQDQ]="vpunpckhqdq",
    [VPALIGNR]="vpalignr",[VPBLENDW]="vpblendw",[VPBLENDD]="vpblendd",[VPBLENDVB]="vpblendvb",[VPMOVMSKB]="vpmovmskb",
    [VPBROADCASTB]="vpbroadcastb",[VPBROADCASTW]="vpbroadcastw",[VPBROADCASTD]="vpbroadcastd",[VPBROADCASTQ]="vpbroadcastq",
    [VBROADCASTI128]="vbroadcasti128",
    [VPMOVZXBW]="vpmovzxbw",[VPMOVZXBD]="vpmovzxbd",[VPMOVZXBQ]="vpmovzxbq",
    [VPMOVZXWD]="vpmovzxwd",[VPMOVZXWQ]="vpmovzxwq",[VPMOVZXDQ]="vpmovzxdq",
    [VPMOVSXBW]="vpmovsxbw",[VPMOVSXBD]="vpmovsxbd",[VPMOVSXBQ]="vpmovsxbq",
    [VPMOVSXWD]="vpmovsxwd",[VPMOVSXWQ]="vpmovsxwq",[VPMOVSXDQ]="vpmovsxdq",
    [VEXTRACTI128]="vextracti128",[VINSERTI128]="vinserti128",[VPERM2I128]="vperm2i128",
    [VPERMQ]="vpermq",[VPERMD]="vpermd",[VPERMPD]="vpermpd",[VPERMPS]="vpermps",
    [VCVTPH2PS]="vcvtph2ps",
    [VFMADD132]="vfmadd132",[VFMADD213]="vfmadd213",[VFMADD231]="vfmadd231",
    [VFMSUB132]="vfmsub132",[VFMSUB213]="vfmsub213",[VFMSUB231]="vfmsub231",
    [VFNMADD132]="vfnmadd132",[VFNMADD213]="vfnmadd213",[VFNMADD231]="vfnmadd231",
    [VFNMSUB132]="vfnmsub132",[VFNMSUB213]="vfnmsub213",[VFNMSUB231]="vfnmsub231",
    [BMI_ANDN]="andn",[BMI_BLSI]="blsi",[BMI_BLSR]="blsr",[BMI_BLSMSK]="blsmsk",
    [BMI_BZHI]="bzhi",[BMI_BEXTR]="bextr",[BMI_MULX]="mulx",[BMI_PDEP]="pdep",
    [BMI_PEXT]="pext",[BMI_RORX]="rorx",[BMI_SARX]="sarx",[BMI_SHLX]="shlx",
    [BMI_SHRX]="shrx",[BMI_TZCNT]="tzcnt",[BMI_LZCNT]="lzcnt",[BMI_MOVBE]="movbe",
};

const char *vex_op_name(vex_op op) {
    if ((unsigned)op < VEX_OP_COUNT && names[op]) return names[op];
    return "<unknown>";
}
