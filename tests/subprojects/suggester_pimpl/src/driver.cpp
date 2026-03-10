#include "pimpl_widget.hpp"

namespace pimpl {

int build_widget_total() {
    Widget widget;
    for (int i = 0; i < 200; ++i) {
        widget.add_value(i);
    }
    return widget.total();
}

}  // namespace pimpl
