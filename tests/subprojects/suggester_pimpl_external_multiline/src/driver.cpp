#include "pimpl_widget_external_multiline.hpp"

int main() {
    pimpl_external_multiline::WidgetExternalMultiline widget;
    widget.add_value(3);
    widget.add_value(5);
    return widget.total();
}
