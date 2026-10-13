// Recorded ESP2 mid-frame freeze entries; export_fixtures.py --rotation-entry-audit.
// Inputs are qualified by bus timing, independently of the interpreter equation.
#pragma once
#include <cstdint>
namespace espRotationEntryTests {
struct Fixture { const char* name; uint32_t before, after; uint8_t config, config2003, config2005; uint32_t mask; };
// 2026-09-16-rotation-dense41/delay9_r1.u32 sha256=afb6ffa0657c851df8010ca1c5216d3d0c9f06958f51021eeaee628b36752755
// 2026-09-16-rotation-dense41/delay21_r2.u32 sha256=084b1ac779481be7bbd050279809a94d9f8ca713727d147e268133ff3e2d29c5
// 2026-09-16-rotation-dense41/delay12_r1.u32 sha256=967f8f2bb81cc11cd95707b9d3aed132c40e0b2aeadfc7dadc01b2b3e68a04d4
// 2026-09-16-rotation-dense41/delay9_r0.u32 sha256=1e3dbdfb97bae41a537ad7c034638ba7587a2e29855bcd35abad4167fa51d98d
// 2026-09-16-rotation-dense41/delay18_r0.u32 sha256=ea0ae6338a240954bbe351018505d488d5b73b95fa555e91ed812eb9de33e1f3
// 2026-09-16-rotation-dense41/delay0_r1.u32 sha256=fc7a23f6047bfe58d7ae96161ed29b0be773764d042685c527f463b1b4d92955
// 2026-09-16-rotation-dense41/delay3_r0.u32 sha256=a9ee46500d6be0340dcaf19d9e525829b32ff25d091040c3b05064e9a0850e72
// 2026-09-16-rotation-dense41/delay6_r1.u32 sha256=ad435f5ea75ec1dcee257463b027f3ce4bc4225f27945a57eadd67843ad7dc8d
// 2026-09-16-rotation-dense41/delay21_r0.u32 sha256=a833bb1c8982f84d4b162d3a11f1f9c0c7f35e5b22e1decacce26f1b0dd2cdf1
// 2026-09-16-rotation-dense41/delay3_r1.u32 sha256=4eb6e97cfd45406a5de2527aa0293c18e35fecd870cb3354aa26861ed8c5bee6
// 2026-09-16-rotation-dense41/delay6_r2.u32 sha256=f4a8c113933d1458af215c7fe41a379c46cb8af6cdb60fa360fd92a725012693
// 2026-09-16-rotation-dense41/delay3_r2.u32 sha256=f1c90612bc59c3ef3168fbc8f8058206127ee41231989eaace560924f82108e2
// 2026-09-16-rotation-dense41/delay21_r1.u32 sha256=7cf597ee5fe48e25e4accf7603dcffe105f654bb920dd5e5a54482e02fb61dd1
// 2026-09-16-rotation-dense41/delay6_r0.u32 sha256=9ce379d4a5f3408b45a5521eb80acae5549dd43b6e0f1669928ead4fe8751851
// 2026-09-16-rotation-dense41/delay0_r2.u32 sha256=4cf2f29a51f5fb7e6dec7b1e443ab452e1b043360b8115a00f4258c8a76d959c
// 2026-09-16-rotation-offset42/delay9_r0.u32 sha256=a6eececcd068e8751aece82806a323243c70bda45a53d2c789c8ad548edf9dbf
// 2026-09-16-rotation-offset42/delay6_r0.u32 sha256=6b47287a9f94c9e894c1ef68bcc30d6bd99b4ff9cd223f26afafa7977a5e76f9
// 2026-09-16-rotation-offset42/delay3_r0.u32 sha256=7b2703c65c4e9b113f32517881b009481234c9eec72a3784d98e17d24296aaa2
// 2026-09-16-rotation-offset42/delay0_r0.u32 sha256=bdfff0a05b2512c1a303d1a454c57d360e88acb6da45fee4b042e72cb3514513
// 2026-09-16-rotation-offset42/delay21_r0.u32 sha256=ef6ef7b9957f57c13de3f003e8d6383b93d32ef6bde8e19cc73ed260691ee9fd
// 2026-09-16-rotation-offset43/delay9_r0.u32 sha256=273f9aeba8bf4636ef0e55229e30fe8db162c9a5cb47c1f822decddd742cf696
// 2026-09-16-rotation-offset43/delay6_r0.u32 sha256=0a82b0f754b194aa1dc2c60f20488dd26474ab111a868cc9b924ba9a56bbbe8c
// 2026-09-16-rotation-offset43/delay0_r0.u32 sha256=eac47571b7dc0963a0b91e87f4f32b57fdac8d905f8a549a643d3c2a53c0bc26
// 2026-09-16-rotation-offset43/delay21_r0.u32 sha256=1307113414a6260da247784f9fe1e4b1b2dbeb3a06f0b2318a3c31d7f24ceb8c
// 2026-09-16-rotation-offset44/delay9_r0.u32 sha256=5090f9172ecd0c9aa9a690aadaf7aae20f17491c5df1bd7123d413b8f3865d57
// 2026-09-16-rotation-offset44/delay3_r0.u32 sha256=9079eafc10e76dc958c0cecdbebf98bad4e894a31eee4fc783724a48bf5117c7
// 2026-09-16-rotation-offset44/delay0_r0.u32 sha256=2f6fd34cd34ca3ea9b10c6aa442f64b85af264722b93f88ff4c017feab593594
// 2026-09-16-rotation-offset44/delay21_r0.u32 sha256=88ac27bd363d87ab12615ec95c1a946bf7624eb51e9dbba20cb0e750554be6cf
// 2026-09-16-rotation-profiles51/m14_b00_delay9_r0.u32 sha256=4045928dc7c4995b0a56db153fcc289419f2292addc304eb596cf7c5f48a8d1b
// 2026-09-16-rotation-profiles51/m14_b00_delay3_r0.u32 sha256=ed26b7e8487dfae7f32e541fca103871879365c36dd72317ead2f291364a2361
// 2026-09-16-rotation-profiles51/m14_b00_delay21_r0.u32 sha256=e73508bb7b27428bcef5d61d234f7484c519d80d5154508e83551f9d35d5efa1
// 2026-09-16-rotation-profiles51/m14_b00_delay18_r0.u32 sha256=b59468f0fb433e14828a279f0e86af37fc25c4d662eaa706da8087a7e75251c0
// 2026-09-16-rotation-profiles51/m34_b00_delay12_r0.u32 sha256=ec3b22d2b8f8b83acc5ab180991034fe0309cb43e6b4c6da43c09fe50017855a
// 2026-09-16-rotation-profiles51/m34_b00_delay6_r0.u32 sha256=fb1c40993f0b105a845a7cc68db4a56676b9ec99f7fae15c992a704a61605e82
// 2026-09-16-rotation-profiles51/m34_b00_delay3_r0.u32 sha256=6050915d1877257cf7e3536462dca5b77b1aaf8b17bbfa82832d79b6df3d719b
// 2026-09-16-rotation-profiles51/m34_b00_delay0_r0.u32 sha256=5344de9e20ebb66aadd5bda6c3a4361464e66e74000400e7984042947bdc5f9c
// 2026-09-16-rotation-profiles51/m34_b00_delay18_r0.u32 sha256=19cb6280278df05d5ca35965ab8aa3baf9f0c6bfefab75b46db552d59146a490
// 2026-09-16-rotation-profiles51/m54_b00_delay12_r0.u32 sha256=3e9b941f49105502d8eb958c309473231757408701c5e6657d0adb402e7b2bff
// 2026-09-16-rotation-profiles51/m54_b00_delay6_r0.u32 sha256=68746531b1fb769426d611e91eba2648eaea5837a5b61b72808dbf62b591d422
// 2026-09-16-rotation-profiles51/m54_b00_delay3_r0.u32 sha256=6905ecc61cb3fb8fe213c2fed06a3e1e7bf0a4d3394f0e2da3389a048af653d8
// 2026-09-16-rotation-profiles51/m54_b00_delay21_r0.u32 sha256=89794c56fcf6aaad57c4529807b6b0d07e042b86c6d5d295db4bae70851e0c5d
// 2026-09-16-rotation-profiles51/m54_b00_delay18_r0.u32 sha256=31c36c7d7d86662311d487713f292dc87bb267e8f13830788da6b4060ebc073a
// 2026-09-16-rotation-profiles51/m74_b00_delay9_r0.u32 sha256=a7057440b44f36601d5aed271a26ad141241660d30b0d59a77afff1234c79654
// 2026-09-16-rotation-profiles51/m74_b00_delay12_r0.u32 sha256=06427c3982c03ae552a009b75c91844f4b2d10c23fa9e4a9e109d235e8493590
// 2026-09-16-rotation-profiles51/m74_b00_delay3_r0.u32 sha256=68ff1ea045f27d28675c507799d0ef73749b90271af1eb3b460482ce5224b675
// 2026-09-16-rotation-profiles52/m14_b0f_delay6_r0.u32 sha256=394e0409917723ac95821dc597adc313f6699fab8c50362c3b0cb3dee4cc3520
// 2026-09-16-rotation-profiles52/m14_b0f_delay3_r0.u32 sha256=940c04c9ca838c0946bbd1707610701ccf56c2942bfec3bdb07d0cee3130ec19
// 2026-09-16-rotation-profiles52/m14_b0f_delay21_r0.u32 sha256=07e8893a41ff56dd83137c4103c93dd01bb8515837a6af0fa9c4d3c4e30d003a
// 2026-09-16-rotation-profiles52/m34_b0f_delay9_r0.u32 sha256=723125512d5f5b428bbd1f13c08cf223dafffd4d21d20da69555a1e74e00f1eb
// 2026-09-16-rotation-profiles52/m34_b0f_delay6_r0.u32 sha256=aac5772fcbd3f8d0f0ccdbb19c9b834ab10f00fa9baac67bfc74190db9e83661
// 2026-09-16-rotation-profiles52/m34_b0f_delay0_r0.u32 sha256=771ace759e5ee463c5f22347c1e9958ba27e81d557f574fbd863a77384047ae5
// 2026-09-16-rotation-profiles52/m34_b0f_delay21_r0.u32 sha256=2fa4100ffbaf2ab9467216384802fda7f8b55a13dbe39a2ab40819876d0db2a4
// 2026-09-16-rotation-profiles52/m34_b0f_delay18_r0.u32 sha256=3a723fa41f3b0c04193dbc3b8df07433b02cb9d4b2eef2f1cbb007a361de197c
// 2026-09-16-rotation-profiles52/m54_b0f_delay9_r0.u32 sha256=151ed34d9c62bff53f1f0127330eddb7c187c045e380b64b4070d7433f1ee16f
// 2026-09-16-rotation-profiles52/m54_b0f_delay21_r0.u32 sha256=dbbb54688bd5123493ab51680fbb0566e96301edea7c42a8c1bf917e12d98257
// 2026-09-16-rotation-profiles52/m74_b0f_delay6_r0.u32 sha256=a6f21fbb18eaca38dd7666822a2f8f429eeccff5b8d54aa08efbfe939610545b
// 2026-09-16-rotation-profiles52/m74_b0f_delay3_r0.u32 sha256=17d51e1219b432f3e0680868ef4e807bcfa80f9cdbd3aa52d92b9f397e063efa
// 2026-09-16-rotation-profiles52/m74_b0f_delay0_r0.u32 sha256=8deb04f9dac073a78f4e01c1c0fa33d1f63f51ee8c28956017507a0544df82bf
// 2026-09-16-rotation-profiles52/m74_b0f_delay21_r0.u32 sha256=b5537d9db7ecb49ce2af211f20625bd27249d010048f9b0b11273eca69b5c518
// 2026-09-16-rotation-profiles52/m74_b0f_delay18_r0.u32 sha256=6bff42be2455bedf263bbf241eb71eb2368ed994d5f7e8f178d516dc15105d29
// 2026-09-16-rotation-profiles53/m14_b10_delay9_r0.u32 sha256=db18daf99e8128302347b62d637d9b2a325891f7a75688f33aa7f4bc6626a701
// 2026-09-16-rotation-profiles53/m14_b10_delay3_r0.u32 sha256=622c19cf4b7abdc286de5fd80efecd84c21662f55e8830fa6881645265b2aea5
// 2026-09-16-rotation-profiles53/m14_b10_delay0_r0.u32 sha256=0d80f30b019ae0d0b2168e4d5423b1dd2786d9c0b63ed08fa313bf816d772cc6
// 2026-09-16-rotation-profiles53/m14_b10_delay21_r0.u32 sha256=a34d5d96b43640e250fa598caa1965b3239134cc36a9e56eab961fcfe0431f9b
// 2026-09-16-rotation-profiles53/m34_b10_delay9_r0.u32 sha256=7edeb06e82d6f461f360a9b1db85116eeb8a9fd8cb5033988c4f08c96183416a
// 2026-09-16-rotation-profiles53/m34_b10_delay12_r0.u32 sha256=c583930751fdba88ace428a6a4bdab76ef9da66feeda4a97efb5954a589e7697
// 2026-09-16-rotation-profiles53/m34_b10_delay6_r0.u32 sha256=7591f7e56fff6287866507ceabb18530b61f66f985c071cf7e271a1cabd05d87
// 2026-09-16-rotation-profiles53/m34_b10_delay3_r0.u32 sha256=28355a5874207f6ccf34af694d1eb9786a40d0bf71ce3df114ef918534a16ef3
// 2026-09-16-rotation-profiles53/m34_b10_delay0_r0.u32 sha256=09f238fd94b4884657576fea5d7c4a5d1a20bf5f9229f002285b0db47a10e918
// 2026-09-16-rotation-profiles53/m34_b10_delay18_r0.u32 sha256=a01788c6f0b772e12176f682c7575f8bc907fbb23881420f08bf02d19b7ac9df
// 2026-09-16-rotation-profiles53/m54_b10_delay0_r0.u32 sha256=a5777e47000d28af6a53dbf4eb568020ca1c5d31270e19d0d0c1233eada53798
// 2026-09-16-rotation-profiles53/m54_b10_delay21_r0.u32 sha256=b86640065d9209192494a94b14fbcde3fcdf95336f24bc7b646c1a3334dce3b3
// 2026-09-16-rotation-profiles53/m54_b10_delay18_r0.u32 sha256=11f92adc408d89c8779cdcc4c8c70c3d2595e9e1946f5dfa597fe917ae7454ef
// 2026-09-16-rotation-profiles53/m74_b10_delay12_r0.u32 sha256=1f2d894f3d2bff34cdbdd2f9452d74f60974662979bfea015801ef3729fef856
// 2026-09-16-rotation-profiles53/m74_b10_delay0_r0.u32 sha256=8b609a49b5e509ba2308933322120e0d61abc0e688e528fd454ce42dba5a4931
// 2026-09-16-rotation-profiles53/m74_b10_delay21_r0.u32 sha256=b9661f32218fe9d75db2916c3806a3ae7ca69ba5f07f203cf6672b0a338d4b39
// 2026-09-16-rotation-profiles53/m74_b10_delay18_r0.u32 sha256=7ebc27d37aeb9da44163d6f0f25bb2fca89bbef6a74e99fef3a506b4b27121b1
// 2026-09-16-rotation-bridge54/m14_b00_delay1_r0.u32 sha256=ae2bcf2a930b92b2972d577c4bcbcd6b24c707abeb2d16e6beaba21943a411ff
// 2026-09-16-rotation-bridge54/m34_b00_delay0_r0.u32 sha256=00def9d0280c18f5a88bb12a9dfa415283a9303a3d01d0fa3f32ea511bda7a87
// 2026-09-16-rotation-bridge54/m34_b00_delay1_r0.u32 sha256=b738b11b76eeced3a35669dddb9fa5c95ed89902ba1b89cae2b8fe4d6c061f46
// 2026-09-16-rotation-bridge54/m34_b00_delay2_r0.u32 sha256=050ec7823184e605cdba070f4311974dc67fd226b834e5935bd24992da5a9fed
// 2026-09-16-rotation-bridge54/m54_b00_delay0_r0.u32 sha256=c8c50b272a7f28eb0f93ecba16e62aaf9306f1ffcd4e7efc208a744664162fb6
// 2026-09-16-rotation-bridge54/m54_b00_delay1_r0.u32 sha256=78aea1157c45f1af5af3bae9f71801081611921f67682ce885f8ccd1aa0001b6
// 2026-09-16-rotation-bridge54/m54_b00_delay3_r0.u32 sha256=2662601c24e9d6b17f4247075f7ff21860141216e59dca9e80183eb5d4d6b7f1
// 2026-09-16-rotation-bridge54/m74_b00_delay0_r0.u32 sha256=3fb9f1dd5ebfdfb3c68b0af31e79e0476fe428fed4ce468677acd05d5f6222b2
// 2026-09-16-rotation-bridge54/m74_b00_delay1_r0.u32 sha256=31d0361e9b74193ac6ac0902e13a9eae6110355671b519e1729c35b299c7b36a
// 2026-09-16-rotation-bridge54/m74_b00_delay2_r0.u32 sha256=35f745fd3e6bf5fac18ea4b690eb56e18bc793bc0ca8513a02e61b27be54d8a2
// 2026-09-16-rotation-bridge54/m74_b00_delay3_r0.u32 sha256=f930af698de515e9ad1e441f64e3437d3e35b24cbd509d9e37a47a0058c684c9
// 2026-09-16-rotation-bridge54/m14_b0f_delay0_r0.u32 sha256=71e20785818c98fd5c554d19ab8816018e36313523305a4b90314eacf498f029
// 2026-09-16-rotation-bridge54/m14_b0f_delay1_r0.u32 sha256=7608ac4e035e6d81144ffaa696cc4e1f11dcc596760a2f5331300f48d8b1b611
// 2026-09-16-rotation-bridge54/m14_b0f_delay2_r0.u32 sha256=9dcb6e6cd477f9d9f1b66076e5c8d6552caf8742146609ccf8f25621ec28256e
// 2026-09-16-rotation-bridge54/m14_b0f_delay3_r0.u32 sha256=e5a5bfbc298e48593d5f200d8e2d4639152dd3ebc484aa9c8f50702600da4e9b
// 2026-09-16-rotation-bridge54/m34_b0f_delay0_r0.u32 sha256=c8ce59ee4fe340b573f50e0def1f2c5e6d2b78ebf536906030f98f8ded217e2a
// 2026-09-16-rotation-bridge54/m34_b0f_delay1_r0.u32 sha256=a1bb0306fe486633ad47efe2d28d2c204420e3dc17882b1c7168d3ca291a426c
// 2026-09-16-rotation-bridge54/m34_b0f_delay2_r0.u32 sha256=923f643dc694fc5867d7857e06e6c57b117d935fe54578f99fd62b108613bfde
// 2026-09-16-rotation-bridge54/m54_b0f_delay0_r0.u32 sha256=9b1cb525e7c3efc1078007bb195a995598bf1498d8c1e27ddbb02071a655337a
// 2026-09-16-rotation-bridge54/m54_b0f_delay1_r0.u32 sha256=fd8cbb3e053327e3b31570f7b22b61f5c56125067456c910e3166843bcf3dd6d
// 2026-09-16-rotation-bridge54/m54_b0f_delay2_r0.u32 sha256=67edf3db015dae299585106e1cf4af2f94ce4eee3b307ce1886d5f5096aff857
// 2026-09-16-rotation-bridge54/m54_b0f_delay3_r0.u32 sha256=69c0151aa68718f1f036dd435ad29f62a173712b9785f9602afa8ad72c4631a8
// 2026-09-16-rotation-bridge54/m14_b10_delay1_r0.u32 sha256=37c290c86ca868be103977ae7768062adb2ddba08225800af14e691ca35e14f7
// 2026-09-16-rotation-bridge54/m14_b10_delay2_r0.u32 sha256=fdc6c289ce0bd32b8f9ee7594b510a2c47b77544ff20c26cfc495bc9d8d9ac43
// 2026-09-16-rotation-bridge54/m14_b10_delay3_r0.u32 sha256=fe5bc07ab21fdc28449bf77e156709fee77eb38c8c2f996e6882de808b4e8756
// 2026-09-16-rotation-bridge54/m34_b10_delay0_r0.u32 sha256=e7f299302a98ade98ba181ec46ba472e39996154ab133be61dca467fe9538865
// 2026-09-16-rotation-bridge54/m34_b10_delay3_r0.u32 sha256=f22415ba8fe09eef6fa795c7b7a5a9fab2391392f3cab3f6246eec259366d626
// 2026-09-16-rotation-bridge54/m54_b10_delay1_r0.u32 sha256=c4c11fc77b03cf124280a3fe2484face28b97ed56b397e20ad7267d601b60f54
// 2026-09-16-rotation-bridge54/m54_b10_delay3_r0.u32 sha256=f2f858e87ab17196feefe991a067a6b5f2be2619fd4868cb7f7dbd2d06aff282
// 2026-09-16-rotation-bridge54/m74_b10_delay0_r0.u32 sha256=9464e1fe46dc81331fd295a6ada0408f937234e33ccb771083ee4835a530a766
// 2026-09-16-rotation-bridge56/m14_b00_delay0_r0.u32 sha256=66b73c22bcd6e3306be8d5d77a59cbf648285cf99086d1500380bbcc2ed3f02a
// 2026-09-16-rotation-bridge56/m14_b00_delay1_r0.u32 sha256=8dc9019484b34414b8ff4cace7e07aabb455f6c6a233b7d3b887e01ec3f7253f
// 2026-09-16-rotation-bridge56/m14_b00_delay2_r0.u32 sha256=dbefb47b6820eff2d9d608d4c6335cfae9cb1d9c52488f544992d4bd148e96ba
// 2026-09-16-rotation-bridge56/m14_b00_delay3_r0.u32 sha256=69eee3435e5e84699defcf60f3ff24e66d6e2ecfaa062d71cf661ef6131523fd
// 2026-09-16-rotation-bridge56/m14_b00_delay5_r0.u32 sha256=074935fa0c237283d59342298763bd6e51719d87a8f8fe5f478b0e29fc022030
// 2026-09-16-rotation-bridge56/m14_b00_delay7_r0.u32 sha256=3bd7c7cac4c2c8cdbd79151728e450364982caab0248cfdb2d78543d9654b072
// 2026-09-16-rotation-bridge56/m74_b0f_delay1_r0.u32 sha256=f3b4759d54dd24ae929fa33f965b40739756dfab9deffe59237582f18bf1ab81
// 2026-09-16-rotation-bridge56/m74_b0f_delay2_r0.u32 sha256=37967ab9dc104c8762731704a3f2f8bce8f57c43ba56aeb942643c58a694bc4f
// 2026-09-16-rotation-bridge56/m74_b0f_delay4_r0.u32 sha256=31abc707a8660baff5da96c05b308e860877c5ab2cadc2729f96e322496f814a
// 2026-09-16-rotation-bridge56/m74_b0f_delay6_r0.u32 sha256=95d7a346ae70aa2915ceea6cf8ed6da81a89ce8c7aa0a25e1623442fdf7ca0bb
// 2026-09-16-rotation-bridge56/m74_b10_delay6_r0.u32 sha256=8b6524c7c17456d8babf671623f2d85059ca046ed92140e7a652212652ef21d8
// 2026-09-16-rotation-bridge56/m74_b10_delay7_r0.u32 sha256=57730983abb1c06e41eca3a750d6cebcafe53c6b7b64ad795d275daaa0711b97
inline constexpr Fixture fixtures[] = {
{"2026-09-16-rotation-dense41/delay9_r1",0x14bc4,0x04bb4,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay21_r2",0x633e9,0x523e9,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay12_r1",0x5214a,0x4104a,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay9_r0",0x40dd2,0x30dc2,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay18_r0",0x30b2a,0x20a2a,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay0_r1",0x1e23c,0x1d13c,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay3_r0",0x1cef2,0x1cee2,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay6_r1",0x1cc21,0x1cb11,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay21_r0",0x1c522,0x1b412,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay3_r1",0x1b11a,0x1a01a,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay6_r2",0x199be,0x199be,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay3_r2",0x19789,0x18789,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay21_r1",0x184d6,0x174c6,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay6_r0",0x05f00,0x75ef0,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-dense41/delay0_r2",0x75c07,0x65bf7,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset42/delay9_r0",0x14a43,0x04933,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset42/delay6_r0",0x62263,0x51153,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset42/delay3_r0",0x50e6e,0x40d6e,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset42/delay0_r0",0x40ae8,0x30ae8,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset42/delay21_r0",0x3080d,0x2070d,0x40,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset43/delay9_r0",0x14e09,0x04d09,0x7f,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset43/delay6_r0",0x633cf,0x523cf,0x7f,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset43/delay0_r0",0x40dbc,0x30dbc,0x7f,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset43/delay21_r0",0x30b09,0x20a09,0x7f,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset44/delay9_r0",0x1523d,0x0413d,0xc0,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset44/delay3_r0",0x5374c,0x4264c,0xc0,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset44/delay0_r0",0x4239e,0x3139e,0xc0,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-offset44/delay21_r0",0x3110e,0x2000e,0xc0,0x57,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m14_b00_delay9_r0",0x00fb5,0x00fa5,0x40,0x14,0x00,0x0ffff},
{"2026-09-16-rotation-profiles51/m14_b00_delay3_r0",0x0dfb5,0x0dfa5,0x40,0x14,0x00,0x0ffff},
{"2026-09-16-rotation-profiles51/m14_b00_delay21_r0",0x0d7e4,0x0c7d4,0x40,0x14,0x00,0x0ffff},
{"2026-09-16-rotation-profiles51/m14_b00_delay18_r0",0x0c3f2,0x0b3e2,0x40,0x14,0x00,0x0ffff},
{"2026-09-16-rotation-profiles51/m34_b00_delay12_r0",0x6a7d1,0x697c1,0x40,0x34,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m34_b00_delay6_r0",0x693bd,0x683bd,0x40,0x34,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m34_b00_delay3_r0",0x67f7a,0x57e7a,0x40,0x34,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m34_b00_delay0_r0",0x57ac0,0x47ab0,0x40,0x34,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m34_b00_delay18_r0",0x362b9,0x252b9,0x40,0x34,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m54_b00_delay12_r0",0x04498,0x73498,0x40,0x54,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m54_b00_delay6_r0",0x730f2,0x620e2,0x40,0x54,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m54_b00_delay3_r0",0x61d34,0x51c24,0x40,0x54,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m54_b00_delay21_r0",0x41356,0x30246,0x40,0x54,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m54_b00_delay18_r0",0x2fe82,0x2fe72,0x40,0x54,0x00,0x7ffff},
{"2026-09-16-rotation-profiles51/m74_b00_delay9_r0",0x0fabc,0x0fabc,0x40,0x74,0x00,0x0ffff},
{"2026-09-16-rotation-profiles51/m74_b00_delay12_r0",0x0e2e3,0x0d2d3,0x40,0x74,0x00,0x0ffff},
{"2026-09-16-rotation-profiles51/m74_b00_delay3_r0",0x0cab8,0x0cab8,0x40,0x74,0x00,0x0ffff},
{"2026-09-16-rotation-profiles52/m14_b0f_delay6_r0",0x0f018,0x0ef18,0x40,0x14,0x0f,0x0ffff},
{"2026-09-16-rotation-profiles52/m14_b0f_delay3_r0",0x0eac6,0x0eab6,0x40,0x14,0x0f,0x0ffff},
{"2026-09-16-rotation-profiles52/m14_b0f_delay21_r0",0x0d155,0x0c045,0x40,0x14,0x0f,0x0ffff},
{"2026-09-16-rotation-profiles52/m34_b0f_delay9_r0",0x6b7f0,0x6a7e0,0x40,0x34,0x0f,0x7ffff},
{"2026-09-16-rotation-profiles52/m34_b0f_delay6_r0",0x68ba8,0x68ba8,0x40,0x34,0x0f,0x7ffff},
{"2026-09-16-rotation-profiles52/m34_b0f_delay0_r0",0x673bd,0x563bd,0x40,0x34,0x0f,0x7ffff},
{"2026-09-16-rotation-profiles52/m34_b0f_delay21_r0",0x55f83,0x45f73,0x40,0x34,0x0f,0x7ffff},
{"2026-09-16-rotation-profiles52/m34_b0f_delay18_r0",0x45b2d,0x35a2d,0x40,0x34,0x0f,0x7ffff},
{"2026-09-16-rotation-profiles52/m54_b0f_delay9_r0",0x355fa,0x245fa,0x40,0x54,0x0f,0x7ffff},
{"2026-09-16-rotation-profiles52/m54_b0f_delay21_r0",0x50af6,0x40ae6,0x40,0x54,0x0f,0x7ffff},
{"2026-09-16-rotation-profiles52/m74_b0f_delay6_r0",0x0d3bd,0x0c3bd,0x40,0x74,0x0f,0x0ffff},
{"2026-09-16-rotation-profiles52/m74_b0f_delay3_r0",0x0bffb,0x0bffb,0x40,0x74,0x0f,0x0ffff},
{"2026-09-16-rotation-profiles52/m74_b0f_delay0_r0",0x0bc11,0x0bb01,0x40,0x74,0x0f,0x0ffff},
{"2026-09-16-rotation-profiles52/m74_b0f_delay21_r0",0x0b6e7,0x0a6d7,0x40,0x74,0x0f,0x0ffff},
{"2026-09-16-rotation-profiles52/m74_b0f_delay18_r0",0x0a2fb,0x092fb,0x40,0x74,0x0f,0x0ffff},
{"2026-09-16-rotation-profiles53/m14_b10_delay9_r0",0x0019b,0x0019b,0x40,0x14,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m14_b10_delay3_r0",0x00183,0x00173,0x40,0x14,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m14_b10_delay0_r0",0x00152,0x00042,0x40,0x14,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m14_b10_delay21_r0",0x0003c,0x0033c,0x40,0x14,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m34_b10_delay9_r0",0x002d0,0x002c0,0x40,0x34,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m34_b10_delay12_r0",0x002a0,0x00290,0x40,0x34,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m34_b10_delay6_r0",0x00296,0x00286,0x40,0x34,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m34_b10_delay3_r0",0x0022c,0x0012c,0x40,0x34,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m34_b10_delay0_r0",0x00114,0x00004,0x40,0x34,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m34_b10_delay18_r0",0x002c8,0x002c8,0x40,0x34,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m54_b10_delay0_r0",0x000be,0x000be,0x40,0x54,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m54_b10_delay21_r0",0x000f9,0x000f9,0x40,0x54,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m54_b10_delay18_r0",0x00152,0x00042,0x40,0x54,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m74_b10_delay12_r0",0x003f6,0x003e6,0x40,0x74,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m74_b10_delay0_r0",0x00268,0x00168,0x40,0x74,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m74_b10_delay21_r0",0x00193,0x00183,0x40,0x74,0x10,0x003ff},
{"2026-09-16-rotation-profiles53/m74_b10_delay18_r0",0x0019e,0x0019e,0x40,0x74,0x10,0x003ff},
{"2026-09-16-rotation-bridge54/m14_b00_delay1_r0",0x006b3,0x7f6a3,0x40,0x14,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m34_b00_delay0_r0",0x7da89,0x7da89,0x40,0x34,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m34_b00_delay1_r0",0x7d6fb,0x7c6fb,0x40,0x34,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m34_b00_delay2_r0",0x7c2eb,0x7b2eb,0x40,0x34,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m54_b00_delay0_r0",0x7aae8,0x7aae8,0x40,0x54,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m54_b00_delay1_r0",0x7a708,0x79608,0x40,0x54,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m54_b00_delay3_r0",0x77e29,0x67d29,0x40,0x54,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m74_b00_delay0_r0",0x67916,0x57806,0x40,0x74,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m74_b00_delay1_r0",0x57409,0x46309,0x40,0x74,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m74_b00_delay2_r0",0x45ef8,0x35ef8,0x40,0x74,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m74_b00_delay3_r0",0x35b37,0x25a27,0x40,0x74,0x00,0x7ffff},
{"2026-09-16-rotation-bridge54/m14_b0f_delay0_r0",0x2562a,0x1452a,0x40,0x14,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m14_b0f_delay1_r0",0x1414b,0x0304b,0x40,0x14,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m14_b0f_delay2_r0",0x02c46,0x72b36,0x40,0x14,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m14_b0f_delay3_r0",0x7272b,0x6162b,0x40,0x14,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m34_b0f_delay0_r0",0x6125a,0x5015a,0x40,0x34,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m34_b0f_delay1_r0",0x4fd60,0x4fc50,0x40,0x34,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m34_b0f_delay2_r0",0x4f852,0x4f742,0x40,0x34,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m54_b0f_delay0_r0",0x4de58,0x4dd58,0x40,0x54,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m54_b0f_delay1_r0",0x4d947,0x4d837,0x40,0x54,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m54_b0f_delay2_r0",0x4d44e,0x4c34e,0x40,0x54,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m54_b0f_delay3_r0",0x4bfb5,0x4bfa5,0x40,0x54,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge54/m14_b10_delay1_r0",0x4868c,0x4768c,0x40,0x14,0x10,0x7ffff},
{"2026-09-16-rotation-bridge54/m14_b10_delay2_r0",0x472c3,0x362b3,0x40,0x14,0x10,0x7ffff},
{"2026-09-16-rotation-bridge54/m14_b10_delay3_r0",0x35f33,0x25e23,0x40,0x14,0x10,0x7ffff},
{"2026-09-16-rotation-bridge54/m34_b10_delay0_r0",0x25a11,0x15901,0x40,0x34,0x10,0x7ffff},
{"2026-09-16-rotation-bridge54/m34_b10_delay3_r0",0x73ae5,0x63ad5,0x40,0x34,0x10,0x7ffff},
{"2026-09-16-rotation-bridge54/m54_b10_delay1_r0",0x521a9,0x411a9,0x40,0x54,0x10,0x7ffff},
{"2026-09-16-rotation-bridge54/m54_b10_delay3_r0",0x309a1,0x20991,0x40,0x54,0x10,0x7ffff},
{"2026-09-16-rotation-bridge54/m74_b10_delay0_r0",0x2057b,0x1f47b,0x40,0x74,0x10,0x7ffff},
{"2026-09-16-rotation-bridge56/m14_b00_delay0_r0",0x10e43,0x00d33,0x40,0x14,0x00,0x7ffff},
{"2026-09-16-rotation-bridge56/m14_b00_delay1_r0",0x00943,0x70833,0x40,0x14,0x00,0x7ffff},
{"2026-09-16-rotation-bridge56/m14_b00_delay2_r0",0x7041d,0x6f31d,0x40,0x14,0x00,0x7ffff},
{"2026-09-16-rotation-bridge56/m14_b00_delay3_r0",0x6eefc,0x6eefc,0x40,0x14,0x00,0x7ffff},
{"2026-09-16-rotation-bridge56/m14_b00_delay5_r0",0x6e710,0x6d600,0x40,0x14,0x00,0x7ffff},
{"2026-09-16-rotation-bridge56/m14_b00_delay7_r0",0x6bd8d,0x6bd8d,0x40,0x14,0x00,0x7ffff},
{"2026-09-16-rotation-bridge56/m74_b0f_delay1_r0",0x6b562,0x6a452,0x40,0x74,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge56/m74_b0f_delay2_r0",0x6a02f,0x69f2f,0x40,0x74,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge56/m74_b0f_delay4_r0",0x69661,0x68551,0x40,0x74,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge56/m74_b0f_delay6_r0",0x66c5b,0x56b5b,0x40,0x74,0x0f,0x7ffff},
{"2026-09-16-rotation-bridge56/m74_b10_delay6_r0",0x6168f,0x5068f,0x40,0x74,0x10,0x7ffff},
{"2026-09-16-rotation-bridge56/m74_b10_delay7_r0",0x5028c,0x4f28c,0x40,0x74,0x10,0x7ffff},
};
}
