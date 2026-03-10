#include "monolith.hpp"

int worker3() {
    splitproj::CompileUnit unit("w3");
    splitproj::Record3 rec{};
    return unit.weight() + rec.score();
}
