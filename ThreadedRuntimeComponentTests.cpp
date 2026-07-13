#include <cassert>
#include "ThreadedRuntimeComponent.h"

int main() {
    int starts = 0;
    int stops = 0;
    ThreadedRuntimeComponent component(RuntimeComponent::Collectors,
        [&] { ++starts; return RuntimeStatus{}; },
        [&] { ++stops; });
    assert(component.Start().Succeeded());
    assert(component.Start().code == RuntimeStatusCode::AlreadyRunning);
    component.Stop();
    component.Stop();
    assert(starts == 1 && stops == 1);
    return 0;
}