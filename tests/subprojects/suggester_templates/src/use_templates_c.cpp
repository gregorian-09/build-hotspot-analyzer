#include "templates.hpp"

namespace templates {

template<int N>
int use_big() {
    BigTemplate<N> item{};
    return item.sum();
}

int use_templates_c() {
    Big64 a{};
    Big128 b{};
    Big192 c{};
    Matrix<int, 32, 32> m{};
    Matrix<int, 64, 64> m2{};
    Rec160 r{};
    Rec200 s{};
    int total = a.sum() + b.sum() + c.sum() + m.trace() + m2.trace() + r.value() + s.value();

#define USE_BIG(N) total += use_big<N>();
    USE_BIG(101)
    USE_BIG(102)
    USE_BIG(103)
    USE_BIG(104)
    USE_BIG(105)
    USE_BIG(106)
    USE_BIG(107)
    USE_BIG(108)
    USE_BIG(109)
    USE_BIG(110)
    USE_BIG(111)
    USE_BIG(112)
    USE_BIG(113)
    USE_BIG(114)
    USE_BIG(115)
    USE_BIG(116)
    USE_BIG(117)
    USE_BIG(118)
    USE_BIG(119)
    USE_BIG(120)
    USE_BIG(121)
    USE_BIG(122)
    USE_BIG(123)
    USE_BIG(124)
    USE_BIG(125)
    USE_BIG(126)
    USE_BIG(127)
    USE_BIG(128)
    USE_BIG(129)
    USE_BIG(130)
    USE_BIG(131)
    USE_BIG(132)
    USE_BIG(133)
    USE_BIG(134)
    USE_BIG(135)
    USE_BIG(136)
    USE_BIG(137)
    USE_BIG(138)
    USE_BIG(139)
    USE_BIG(140)
    USE_BIG(141)
    USE_BIG(142)
    USE_BIG(143)
    USE_BIG(144)
    USE_BIG(145)
    USE_BIG(146)
    USE_BIG(147)
    USE_BIG(148)
    USE_BIG(149)
    USE_BIG(150)
    USE_BIG(151)
    USE_BIG(152)
    USE_BIG(153)
    USE_BIG(154)
    USE_BIG(155)
    USE_BIG(156)
    USE_BIG(157)
    USE_BIG(158)
    USE_BIG(159)
    USE_BIG(160)
    USE_BIG(161)
    USE_BIG(162)
    USE_BIG(163)
    USE_BIG(164)
    USE_BIG(165)
    USE_BIG(166)
    USE_BIG(167)
    USE_BIG(168)
    USE_BIG(169)
    USE_BIG(170)
    USE_BIG(171)
    USE_BIG(172)
    USE_BIG(173)
    USE_BIG(174)
    USE_BIG(175)
    USE_BIG(176)
    USE_BIG(177)
    USE_BIG(178)
    USE_BIG(179)
    USE_BIG(180)
    USE_BIG(181)
    USE_BIG(182)
    USE_BIG(183)
    USE_BIG(184)
    USE_BIG(185)
    USE_BIG(186)
    USE_BIG(187)
    USE_BIG(188)
    USE_BIG(189)
    USE_BIG(190)
    USE_BIG(191)
    USE_BIG(192)
    USE_BIG(193)
    USE_BIG(194)
    USE_BIG(195)
    USE_BIG(196)
    USE_BIG(197)
    USE_BIG(198)
    USE_BIG(199)
    USE_BIG(200)
#undef USE_BIG

    return total;
}

}  // namespace templates
