// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/ses_noise_module.h, ses_noise_module.cpp.
#pragma once

#include "module.h"

#include <string>
#include <vector>

namespace hgps::model {

/// @brief Draws each person's socio-economic status from a normal distribution.
class SesNoiseModule final : public UpdatableModule {
  public:
    SesNoiseModule() : SesNoiseModule("normal", {0.0, 1.0}) {}

    /// @throws std::invalid_argument for a function other than "normal", or the wrong number of
    ///         parameters.
    SesNoiseModule(std::string function, std::vector<double> parameters);

    ModuleType type() const noexcept override { return ModuleType::SES; }
    const std::string &name() const noexcept override { return name_; }

    void initialise_population(RuntimeContext &context) override;

    /// @brief Draws for newborns only; everyone else keeps the status they were born with.
    void update_population(RuntimeContext &context) override;

  private:
    std::string function_;
    std::vector<double> parameters_;
    std::string name_{"SES"};
};

} // namespace hgps::model
