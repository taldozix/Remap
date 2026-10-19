#include "Vila/Events.h"

namespace vila {

Events& events() {
	static Events e;
	return e;
}

void Events::clear_all() { *this = Events(); }

}  // namespace vila
