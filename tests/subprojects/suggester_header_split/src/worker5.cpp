#include "monolith.hpp"

int worker5() {
    splitproj::CompileUnit unit("w5");
    splitproj::Record5 rec{};
    return unit.weight() + rec.score();
}
