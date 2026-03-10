#include "widget.hpp"
#include "heavy.hpp"

int worker() {
    Heavy heavy;
    Widget widget;
    return widget.run(heavy);
}
