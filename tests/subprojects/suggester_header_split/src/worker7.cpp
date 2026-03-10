#include "monolith.hpp"

int worker7() {
    splitproj::CompileUnit unit("w7");
    splitproj::Record7 rec{};
    return unit.weight() + rec.score();
}
