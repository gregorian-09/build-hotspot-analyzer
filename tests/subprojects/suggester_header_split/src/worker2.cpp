#include "monolith.hpp"

int worker2() {
    splitproj::CompileUnit unit("w2");
    splitproj::Record2 rec{};
    return unit.weight() + rec.score();
}
