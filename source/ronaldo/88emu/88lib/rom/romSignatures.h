#pragma once

#include <cstdint>

#include "baseLib/md5.h"

namespace emu88Lib
{
    // The first four bytes of the images the registry catalogues by MD5, in the layout that MD5
    // describes, taken from the dumps themselves. The scanner reads only these four bytes of a
    // file of the right size and hashes it only if they can begin one of the rows of that size,
    // as the file is or word swapped. Many images share a signature (wave ROMs start with
    // "Roland"), so this only spares the hashing of files that cannot match; the MD5 still
    // decides. A row missing here is always hashed: a missing signature costs time, a wrong one
    // a ROM that is never found, so only ever add one read off a dump whose MD5 matches.
    struct RomSignature
    {
        baseLib::MD5 hash;
        // Byte 0 in the top bits, so it reads like a hex dump of the file
        uint32_t head;
    };

    inline constexpr RomSignature g_romSignatures[] = {
        {baseLib::MD5("bfff32b6144c1d706109accb6e6b1113"), 0xffffffff}, // 1.02
        {baseLib::MD5("099552dcbc6a94bb0d2abf1281873fa1"), 0xffffffff}, // 1.00, LAPC-I
        {baseLib::MD5("88f459121acf32923b1d85742c67e9c9"), 0xffffffff}, // 1.00, CM-32LN/CM-500/LAPC-N
        {baseLib::MD5("08cdcfa0ed93e9cb16afa76e6ac5f0a4"), 0x00000000}, // R15449121+R15179945
        {baseLib::MD5("46d3bb96193004181a4db38c0ba92b25"), 0xfb133f07}, // R15179917
        {baseLib::MD5("89e42e386e82e0cacb4a2704a03706ca"), 0x00000000}, // R15449121
        {baseLib::MD5("f7909bed95b04d8dc4cc47970e2bc3e6"), 0x0000175e}, // R15179945
        {baseLib::MD5("5626206284b22c2734f3e9efefcd2675"), 0xa100f018}, // 1.07, 10 Oct 87
        {baseLib::MD5("b7180ac1be56e740b0f635062c791b5a"), 0xa100f018}, // M-9, ver1.07 10 Oct 87
        {baseLib::MD5("d241220ed9c171ffcf347413eaf5969b"), 0xa1f0a180}, // 1.07 A, IC27
        {baseLib::MD5("346d6c8ef954c1901822d349dd658aef"), 0x00180078}, // 1.07 B, IC26
        {baseLib::MD5("2a2e1a6f83d3bfd5799b1b835b68784f"), 0xa1f0a180}, // 1.06 A, IC27
        {baseLib::MD5("04d9852b98409301ec59f300f6e3d0e9"), 0x00180078}, // 1.06 B, IC26
        {baseLib::MD5("0dfb58615f6789f9ba382988d88d2014"), 0xa1f0a180}, // 1.05 A, IC27
        {baseLib::MD5("cd83d3b708887a5822d5155782b922d0"), 0x00180078}, // 1.05 B, IC26
        {baseLib::MD5("1eddb73995829e4befecd40b13dfb408"), 0xa1f0a180}, // 1.04 A, IC27
        {baseLib::MD5("87546da1c1b9ad1354baea23a28e6981"), 0x00180078}, // 1.04 B, IC26
        {baseLib::MD5("c6c8ba9fef6923e1788b8e5e7308df68"), 0xa1f0a180}, // Blue Ridge A
        {baseLib::MD5("d5ed9d93730d625df29e910527082a0b"), 0x00180078}, // Blue Ridge B
        {baseLib::MD5("4194e653a5ebb2e3217318fc5ea8b9ea"), 0xa1f0a180}, // M-9 A, IC27
        {baseLib::MD5("65cf18bdf8569abe30892be9e12976d0"), 0x00180078}, // M-9 B, IC26
        {baseLib::MD5("499539a02b726aa43f9a22cf05c48e7b"), 0x00000000}, // R15179844
        {baseLib::MD5("0d4908e119ddfa6f0283b46bc9fe744d"), 0x00000000}, // R15179845
        {baseLib::MD5("21efb00e7881f7fa0b6fcdfadd2d33c3"), 0x00000000}, // BAD_DUMP: R15449121
        {baseLib::MD5("181499301ad745424fee81ad61102588"), 0xfb133f07}, // R15179857
        {baseLib::MD5("8c4b2c7708be8e88a3ac14777575629c"), 0xffffffff}, // 2.07
        {baseLib::MD5("615a866337ecb752768ac03ed4ddf85c"), 0x46816781}, // 2.04, 88-11-11
        {baseLib::MD5("ccd61a220f433a62a53b48eb9bb36827"), 0xffffffff}, // 1.00
        {baseLib::MD5("21efdc888020fe99303ef34265349021"), 0x526f6c61}, // R15179970
        {baseLib::MD5("69e39536391fe79250f67cd3c7794217"), 0x526f6c61}, // R15179971
        {baseLib::MD5("512bee180e2390b896484adb01f1d215"), 0x526f6c61}, // R15179972
        {baseLib::MD5("d49350c3b4e63a890c7719c0ada10fa5"), 0xffffffff}, // boot 1.03
        {baseLib::MD5("29a74bbbca5a25e6071ff7de3df78341"), 0x526f6c61}, // D
        {baseLib::MD5("53da9e576c406a68d22791f2e59bd3b2"), 0x526f6c61}, // E
        {baseLib::MD5("b308e81824827691a53037fb15fee317"), 0x526f6c61}, // F
        {baseLib::MD5("732a497464f8f69ccd480dbf61ff0a03"), 0x00000000}, // T6963C CG ROM 0101
        {baseLib::MD5("b6c7e3ae2abdd31720c61f07bc9f71b6"), 0x00000000}, // 1.091
        {baseLib::MD5("bea309a1fefa5ca4a43a3bf9417516db"), 0x00000000}, // 1.08
        {baseLib::MD5("359d35b9238cf7492218a681fd3003e0"), 0x42d8048e}, // 1.00
        {baseLib::MD5("ddf9ba6a5690558107e81f10b2e5dc0d"), 0xe63206c4}, // IC6
        {baseLib::MD5("5fded9504cfd4142f6bfa8a3c194902a"), 0xd4560785}, // IC7
        {baseLib::MD5("c5caa57249fcbc56f65c18a15373a0fa"), 0xbf8b118e}, // IC17
        {baseLib::MD5("035f6449672d4c9e82a890adfe03d1af"), 0xfe101285}, // IC18
        {baseLib::MD5("0ac771782ea58a53af590ebdf140d517"), 0x00000200}, // 1.01
        {baseLib::MD5("25e016e93c8a44ba3c35584462b56d72"), 0x00000200}, // 1.04
        {baseLib::MD5("2e6d39474b66c7a6a48f7a1a3e5dc403"), 0x00000200}, // G-800 GS-64 3.00 RES-1
        {baseLib::MD5("b2c30824c92c214cb7314d2a58409423"), 0x526f6c61}, // XP-GS E-10
        {baseLib::MD5("6a92b7de3ac7b8205d29ec4497644beb"), 0x526f6c61}, // IC325
        {baseLib::MD5("d98f4b255d3a7dc830d92c71c25ce2eb"), 0x526f6c61}, // IC326
        {baseLib::MD5("c05b103d4db110b3962431173cc72967"), 0x526f6c61}, // IC327
        {baseLib::MD5("bccc26c34cac0d5509e8efb645043b5b"), 0x526f6c61}, // IC328
        {baseLib::MD5("9d4c2f123b4451d8ee75c3b982760f28"), 0x00008d1a}, // 1.02
        {baseLib::MD5("784b3ea762b5f96cabdceb33d121d5e4"), 0x00008d1a}, // SC-GS A '96
        {baseLib::MD5("bd33b20bb5e8f51e436e141f81a75c14"), 0x526f6c61}, // R01567167, XP-GS 1.01
        {baseLib::MD5("125ea2056dc208f04a141b3dcdffdb1b"), 0x526f6c61}, // R01567178, SC-GS 1.00
        {baseLib::MD5("48c3887c9a2a574b907242640fa0a320"), 0x526f6c61}, // R01233667, SC-GS 1.00
        {baseLib::MD5("decc8a499b69e68ee8b121d73410c36b"), 0x526f6c61}, // CS0, first half of R01567167, derived reference
        {baseLib::MD5("69933f0a2a3f6f4ab53932f40ba63f74"), 0x526f6c61}, // CS1, second half of R01567167, derived reference
        {baseLib::MD5("bda725bd1cf8c3911f906314ca162bed"), 0x526f6c61}, // CS2, first half of R01567178, derived reference
        {baseLib::MD5("94e94038993600555b738e8ccc24d8b8"), 0x526f6c61}, // CS3, second half of R01567178, derived reference
        {baseLib::MD5("f836d9491075c28c1c0587d4876cec65"), 0x00008d1a}, // R01780078, SC-GS B '97
        {baseLib::MD5("b55b02cfcd6b971cc8c4ee10bce50019"), 0x00008d1a}, // 1.01
        {baseLib::MD5("052707d6a9ec7c4f03d60d1f10464bf0"), 0x00008d1a}, // 1.04
        {baseLib::MD5("efe1ffb0ccbe1b2ec454692c522494fc"), 0x000020c0}, // SH7016 boot
        {baseLib::MD5("554d5997dcd9ce6fa0777092ff48f6fa"), 0x00220c38}, // XP-GS 1.01 / SC-GS 1.00
        {baseLib::MD5("06eee65647b66109efb01eabd6d71248"), 0xffff0002}, // tone flash
        {baseLib::MD5("e8eb6ac0e394997dfbc17064f9f4a70e"), 0xa4eba52b}, // IC53+IC54, unscrambled
        {baseLib::MD5("d5016082b2d0f12c235bacb9469acb07"), 0xa4eba52b}, // IC54, decoded
        {baseLib::MD5("aeac599d472090323bf17a1905cfb166"), 0x526f6c61}, // R01891445, XP dump layout
        {baseLib::MD5("960e4c3d9195968a22567d36c763988f"), 0x526f6c61}, // R01891456, XP dump layout (derived reference)
        {baseLib::MD5("d1be6db8885cb9e56fe52cd587e9d639"), 0x00001e58}, // SH7016 boot
        {baseLib::MD5("b03dc0c554fae77bb1782da4ccfadfbd"), 0x00001a00}, // BAD_DUMP: reconstructed CPU ROM
        {baseLib::MD5("79f7d20691ef98b4408592e0e70132d5"), 0x00e9df38}, // program and tone flash, 2000-11-28
        {baseLib::MD5("2ce0dfb99b0fbe4313b225d37d68ac95"), 0xa4eba52b}, // CS0
        {baseLib::MD5("35551cfb0cb36b95de6301a617249178"), 0xa4eba52b}, // CS1
        {baseLib::MD5("4ca058f7db05f51e97bb30a162e9610a"), 0x0000016c}, // H8/532 boot
        {baseLib::MD5("63b24c7193ce34afefce9cec32ac39f0"), 0x00034050}, // 1.01, R00233567
        {baseLib::MD5("fe2ba2027fa06dca43f2379f18b24a4d"), 0x00034050}, // 1.00, R00561423
        {baseLib::MD5("30df645acf7f1b621d5f2891ea53e00b"), 0x526f6c61}, // PCM 1
        {baseLib::MD5("f86e0433a11707a048a8a79e8d98a3be"), 0x524f4c41}, // PCM 2
        {baseLib::MD5("462cb3a2ce9e54f4e54a52f643931ffd"), 0x0000016c}, // 1.20/1.21, R15199778
        {baseLib::MD5("6b61186953b50d900e430ae6a996bda7"), 0x1027e010}, // 1.21, R15209363
        {baseLib::MD5("181b86f1c5a81caa65b6cebbf7c2678b"), 0x1027e010}, // 1.20, R15209337
        {baseLib::MD5("6b74988a79c2a48239809f07a54175b3"), 0x0000016c}, // 2.00, R15199799
        {baseLib::MD5("d79cf9c9aa88416372cb0fd318f79beb"), 0x102ff810}, // 2.00, R15209387
        {baseLib::MD5("b301bfdaac9650ac97322f533396fdfb"), 0x0000016c}, // 1.10, R15199736
        {baseLib::MD5("c0f0c8c2ffcfaf9a56c7e0d2dd440857"), 0x10278610}, // 1.10, R15209275
        {baseLib::MD5("c4e923344906aa630ab1b950720db386"), 0x0000016c}, // 1.00, R15199748
        {baseLib::MD5("7e90b34c777e2c5e1277ae559d959c26"), 0x10255210}, // 1.00, R15449258
        {baseLib::MD5("5f40d5297f47358ddea43b8d225874cc"), 0x524f4c41}, // R15209276
        {baseLib::MD5("f78fb079a7d8c5f1eb04e0fd5aedbc9e"), 0x524f4c41}, // R15209277
        {baseLib::MD5("53c013d4bad0337385cfda2631e5b78e"), 0x524f4c41}, // R15209281
        {baseLib::MD5("1e8c539412480c3c261d82e049a8bb10"), 0x524f4c41}, // R15279806
        {baseLib::MD5("fb28743cef3fa73542554f68a8e2297f"), 0x524f4c41}, // R15279807
        {baseLib::MD5("437d9575678bdcc30c2f0e1a32765027"), 0x10025a10}, // 1.20, R15279812
        {baseLib::MD5("b2c215cdf5325ec84ecde86a5d3d1eb8"), 0x10025a10}, // 1.10, R15279809
        {baseLib::MD5("c40042f1469eec55870391f131c96230"), 0x0000016c}, // R00128523
        {baseLib::MD5("cfbd838e3fe6bcfb92f2587fafee048e"), 0x10025a10}, // 1.30, R00128567
        {baseLib::MD5("31883de733ad34858bfd262b8fcf2fb6"), 0x0000016c}, // R15199827
        {baseLib::MD5("89d8263a413326249c042da8bcd0fda1"), 0xffffffff}, // R15279828
        {baseLib::MD5("16c605f5c3e7c6870706b8b23dcca269"), 0x526f6c61}, // R15279824
        {baseLib::MD5("5c7c6ab34ef6da079b05a36fd23c9c91"), 0x102fe810}, // SC-155 control
        {baseLib::MD5("e81ac462597571c9cde1739e92c08da2"), 0x000007f8}, // R00677323
        {baseLib::MD5("6a950fe278c88d33b3b60f936abcedca"), 0x0000000a}, // R00678167
        {baseLib::MD5("3a2b61cf66ed3edc447a762accd5fc40"), 0x526f6c61}, // PCM 1
        {baseLib::MD5("ab91066ef997237039655ad676195746"), 0x526f6c61}, // PCM 2
        {baseLib::MD5("d42c1efacde5cff241e9e5be0d183d4e"), 0x526f6c61}, // PCM 3
        {baseLib::MD5("f6448c563ce0df27d18bb59abc58ab72"), 0x526f6c61}, // PCM 4
        {baseLib::MD5("d44e423549b50f2f7d5cd6b71067e3b0"), 0x000086f0}, // SH7042A mask ROM
        {baseLib::MD5("34cd67b9ba09a1f4ad078f1595e76b50"), 0x4d494947}, // 1.11
        {baseLib::MD5("54731f82ec7fba29baf8ad1ee043fb17"), 0xa4eb64a5}, // Ver001 1999-10-13, decoded
    };

    // Rows without a signature, as no dump of them was at hand:
    //   abe0982e4f662f881affa7d8a932c739 1.06, 31 Aug 87
    //   5de47ca37a3712ed32993f865ee9fb5b 1.05, 06 Aug 87
    //   c5233e4594f9b7bc4f773ae6ba5aa87d 1.04, 14 July 87
    //   9513fec4f09a7d327748340ce3a2a59b Blue Ridge, verX.XX 30 Sep 88
    //   daa3ea8e557b85e1ae25a1348db37343 2.06
    //   ef4978c0a278f284d12b1632aa0b804b 2.03, MT-100
    //   34e5802afd4c12230e775e0c94c4873b GS-28 2.00, CTF patch: SC-55 tones, SC-55 v1.21 drums
    //   c3db7620a8438c48e96f4b343da48943 GS-28 2.00, CTF patch: SC-55 tones, SC-55 v2.00 drums
    //   08fc3433479b1d60c7070ecec25ac532 GS-28 2.00, CTF patch: strict SC-55 tones, SC-55 v1.21 drums
    //   40c1447c07aca6f299c7722d09791c56 GS-28 2.00, CTF patch: strict SC-55 tones, SC-55 v2.00 drums
    //   1bfee46852377ada090dd2b2e76fd6ed GS-28 2.00, CTF patch: SC-55mkII tones, SC-55 v1.21 drums
    //   f365d0131fe381c82f99117ca08cc953 GS-28 2.00, CTF patch: SC-55mkII tones, SC-55 v2.00 drums
    //
    // Rows known only by a published SHA-1 or SHA-256 have no MD5 to key a signature by, and no
    // dump here either; every file of their size is hashed for them. Today these are the CM-300
    // internal ROM R15199774 (32 KiB) and the RLP-3237 program 2.01, R15209486 (256 KiB).
} // namespace emu88Lib
