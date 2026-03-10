#include "pimpl_widget.hpp"

namespace pimpl {

using VeryHeavy = heavy::Expander<200>;
using ExtraSeq = heavy::MakeSeq<900>::type;

static const VeryHeavy kHeavyInstance{};
static const ExtraSeq kExtraSeq{};

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

Widget::Widget() = default;
Widget::~Widget() = default;

void Widget::add_value(int value) {
    values_.push_back(value);
    counters_["total"] += value;
    fast_lookup_["total"] = counters_["total"];
}

int Widget::total() const {
    int sum = 0;
    for (int value : values_) {
        sum += value;
    }
#define USE_EXPANDER(N) sum += use_expander<N>();
    USE_EXPANDER(120)
    USE_EXPANDER(121)
    USE_EXPANDER(122)
    USE_EXPANDER(123)
    USE_EXPANDER(124)
    USE_EXPANDER(125)
    USE_EXPANDER(126)
    USE_EXPANDER(127)
    USE_EXPANDER(128)
    USE_EXPANDER(129)
    USE_EXPANDER(130)
    USE_EXPANDER(131)
    USE_EXPANDER(132)
    USE_EXPANDER(133)
    USE_EXPANDER(134)
    USE_EXPANDER(135)
    USE_EXPANDER(136)
    USE_EXPANDER(137)
    USE_EXPANDER(138)
    USE_EXPANDER(139)
    USE_EXPANDER(140)
    USE_EXPANDER(141)
    USE_EXPANDER(142)
    USE_EXPANDER(143)
    USE_EXPANDER(144)
    USE_EXPANDER(145)
    USE_EXPANDER(146)
    USE_EXPANDER(147)
    USE_EXPANDER(148)
    USE_EXPANDER(149)
    USE_EXPANDER(150)
    USE_EXPANDER(151)
    USE_EXPANDER(152)
    USE_EXPANDER(153)
    USE_EXPANDER(154)
    USE_EXPANDER(155)
    USE_EXPANDER(156)
    USE_EXPANDER(157)
    USE_EXPANDER(158)
    USE_EXPANDER(159)
    USE_EXPANDER(160)
    USE_EXPANDER(161)
    USE_EXPANDER(162)
    USE_EXPANDER(163)
    USE_EXPANDER(164)
    USE_EXPANDER(165)
    USE_EXPANDER(166)
    USE_EXPANDER(167)
    USE_EXPANDER(168)
    USE_EXPANDER(169)
    USE_EXPANDER(170)
    USE_EXPANDER(171)
    USE_EXPANDER(172)
    USE_EXPANDER(173)
    USE_EXPANDER(174)
    USE_EXPANDER(175)
    USE_EXPANDER(176)
    USE_EXPANDER(177)
    USE_EXPANDER(178)
    USE_EXPANDER(179)
    USE_EXPANDER(180)
    USE_EXPANDER(181)
    USE_EXPANDER(182)
    USE_EXPANDER(183)
    USE_EXPANDER(184)
    USE_EXPANDER(185)
    USE_EXPANDER(186)
    USE_EXPANDER(187)
    USE_EXPANDER(188)
    USE_EXPANDER(189)
    USE_EXPANDER(190)
    USE_EXPANDER(191)
    USE_EXPANDER(192)
    USE_EXPANDER(193)
    USE_EXPANDER(194)
    USE_EXPANDER(195)
    USE_EXPANDER(196)
    USE_EXPANDER(197)
    USE_EXPANDER(198)
    USE_EXPANDER(199)
    USE_EXPANDER(200)
    USE_EXPANDER(201)
    USE_EXPANDER(202)
    USE_EXPANDER(203)
    USE_EXPANDER(204)
    USE_EXPANDER(205)
    USE_EXPANDER(206)
    USE_EXPANDER(207)
    USE_EXPANDER(208)
    USE_EXPANDER(209)
    USE_EXPANDER(210)
    USE_EXPANDER(211)
    USE_EXPANDER(212)
    USE_EXPANDER(213)
    USE_EXPANDER(214)
    USE_EXPANDER(215)
    USE_EXPANDER(216)
    USE_EXPANDER(217)
    USE_EXPANDER(218)
    USE_EXPANDER(219)
    USE_EXPANDER(220)
    USE_EXPANDER(221)
    USE_EXPANDER(222)
    USE_EXPANDER(223)
    USE_EXPANDER(224)
    USE_EXPANDER(225)
    USE_EXPANDER(226)
    USE_EXPANDER(227)
    USE_EXPANDER(228)
    USE_EXPANDER(229)
    USE_EXPANDER(230)
    USE_EXPANDER(231)
    USE_EXPANDER(232)
    USE_EXPANDER(233)
    USE_EXPANDER(234)
    USE_EXPANDER(235)
    USE_EXPANDER(236)
    USE_EXPANDER(237)
    USE_EXPANDER(238)
    USE_EXPANDER(239)
    USE_EXPANDER(240)
#undef USE_EXPANDER
    return sum + heavy::Fib<19>::value + expander_.value;
}

std::string Widget::label() const {
    return label_ + std::to_string(expander_.value);
}

}  // namespace pimpl
