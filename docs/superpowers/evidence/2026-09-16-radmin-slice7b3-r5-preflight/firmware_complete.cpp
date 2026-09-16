#include "firmware_action_effects.h"
#include "remote_session.h"
static_assert(sizeof(mrfw::ActionPlan)==2);
struct ProposedRecord { mrfw::ActionPlan plan; };
static_assert(sizeof(ProposedRecord)==2);
