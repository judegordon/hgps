#pragma once

#include "analysis_module.h"
#include "demographic.h"
#include "disease.h"
#include "disease_table.h"
#include "modulefactory.h"
#include "riskfactor.h"
#include "ses_noise_module.h"

namespace hgps {

SimulationModuleFactory get_default_simulation_module_factory(Repository &manager);
} // namespace hgps
