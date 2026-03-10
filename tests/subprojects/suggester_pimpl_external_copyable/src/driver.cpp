#include "pimpl_widget_external_copyable.hpp"

using pimpl_external::copyable::WidgetExternalCopyable;

int main() {
    WidgetExternalCopyable original;
    original.set_label("copyable");
    original.add_value(3);
    original.add_value(5);

    WidgetExternalCopyable copied = original;
    WidgetExternalCopyable assigned;
    assigned = original;

    const bool totals_match =
        copied.total() == original.total() &&
        assigned.total() == original.total();
    const bool labels_match =
        copied.label() == original.label() &&
        assigned.label() == original.label();

    return (totals_match && labels_match) ? 0 : 1;
}
