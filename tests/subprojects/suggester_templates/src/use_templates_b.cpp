#include "templates.hpp"

namespace templates {

template<int N>
int use_big() {
    BigTemplate<N> item{};
    return item.sum();
}

int use_templates_b() {
    Big128 c{};
    Big192 d{};
    Matrix<int, 24, 24> m{};
    Matrix<int, 48, 48> m2{};
    Rec160 r{};
    int total = c.sum() + d.sum() + m.trace() + m2.trace() + r.value();

#define USE_BIG(N) total += use_big<N>();
    USE_BIG(51)
    USE_BIG(52)
    USE_BIG(53)
    USE_BIG(54)
    USE_BIG(55)
    USE_BIG(56)
    USE_BIG(57)
    USE_BIG(58)
    USE_BIG(59)
    USE_BIG(60)
    USE_BIG(61)
    USE_BIG(62)
    USE_BIG(63)
    USE_BIG(64)
    USE_BIG(65)
    USE_BIG(66)
    USE_BIG(67)
    USE_BIG(68)
    USE_BIG(69)
    USE_BIG(70)
    USE_BIG(71)
    USE_BIG(72)
    USE_BIG(73)
    USE_BIG(74)
    USE_BIG(75)
    USE_BIG(76)
    USE_BIG(77)
    USE_BIG(78)
    USE_BIG(79)
    USE_BIG(80)
    USE_BIG(81)
    USE_BIG(82)
    USE_BIG(83)
    USE_BIG(84)
    USE_BIG(85)
    USE_BIG(86)
    USE_BIG(87)
    USE_BIG(88)
    USE_BIG(89)
    USE_BIG(90)
    USE_BIG(91)
    USE_BIG(92)
    USE_BIG(93)
    USE_BIG(94)
    USE_BIG(95)
    USE_BIG(96)
    USE_BIG(97)
    USE_BIG(98)
    USE_BIG(99)
    USE_BIG(100)
#undef USE_BIG

    return total;
}

}  // namespace templates
