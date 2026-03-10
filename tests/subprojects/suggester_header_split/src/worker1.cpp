#include "monolith.hpp"

int worker1() {
    splitproj::CompileUnit unit("w1");
    splitproj::Record1 rec{};
    return unit.weight() + rec.score();
}
