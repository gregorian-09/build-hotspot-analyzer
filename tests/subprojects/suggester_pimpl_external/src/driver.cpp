#include "pimpl_widget_external.hpp"

int main() {
    pimpl_external::WidgetExternal widget;
    widget.add_value(3);
    widget.add_value(7);
    return widget.total();
}
