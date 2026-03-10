#include "templates.hpp"

namespace templates {

template<int N>
int use_big() {
    BigTemplate<N> item{};
    return item.sum();
}

int use_templates_a() {
    Big64 a{};
    Big96 b{};
    Matrix<int, 16, 16> m{};
    Matrix<int, 32, 32> m2{};
    Rec128 r{};
    int total = a.sum() + b.sum() + m.trace() + m2.trace() + r.value();

#define USE_BIG(N) total += use_big<N>();
    USE_BIG(5)
    USE_BIG(6)
    USE_BIG(7)
    USE_BIG(8)
    USE_BIG(9)
    USE_BIG(10)
    USE_BIG(11)
    USE_BIG(12)
    USE_BIG(13)
    USE_BIG(14)
    USE_BIG(15)
    USE_BIG(16)
    USE_BIG(17)
    USE_BIG(18)
    USE_BIG(19)
    USE_BIG(20)
    USE_BIG(21)
    USE_BIG(22)
    USE_BIG(23)
    USE_BIG(24)
    USE_BIG(25)
    USE_BIG(26)
    USE_BIG(27)
    USE_BIG(28)
    USE_BIG(29)
    USE_BIG(30)
    USE_BIG(31)
    USE_BIG(32)
    USE_BIG(33)
    USE_BIG(34)
    USE_BIG(35)
    USE_BIG(36)
    USE_BIG(37)
    USE_BIG(38)
    USE_BIG(39)
    USE_BIG(40)
    USE_BIG(41)
    USE_BIG(42)
    USE_BIG(43)
    USE_BIG(44)
    USE_BIG(45)
    USE_BIG(46)
    USE_BIG(47)
    USE_BIG(48)
    USE_BIG(49)
    USE_BIG(50)
#undef USE_BIG

    return total;
}

}  // namespace templates
