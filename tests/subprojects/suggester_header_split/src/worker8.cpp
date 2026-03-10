#include "monolith.hpp"

int worker8() {
    splitproj::CompileUnit unit("w8");
    splitproj::Record8 rec{};
    return unit.weight() + rec.score();
}
