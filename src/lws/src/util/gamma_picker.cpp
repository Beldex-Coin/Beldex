#include "gamma_picker.h"

#include <algorithm>
#include <stdexcept>

#include "crypto/crypto.h"
#include "cryptonote_config.h"

namespace lws
{
  namespace
  {
    constexpr const double gamma_shape = 19.28;
    constexpr const double gamma_scale = 1 / double(1.61);
    constexpr const std::size_t blocks_in_a_year = BLOCKS_EXPECTED_IN_YEARS(1, 17);  // need to change
    constexpr const std::size_t default_unlock_time = CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE_V17 * DIFFICULTY_TARGET_V17;
    constexpr const std::size_t recent_spend_window = 2 * DIFFICULTY_TARGET_V17;  // need to change
  }

  gamma_picker::gamma_picker(std::vector<uint64_t> rct_offsets)
    : gamma_picker(std::move(rct_offsets), gamma_shape, gamma_scale)
  {}

  gamma_picker::gamma_picker(std::vector<std::uint64_t> offsets_in, double shape, double scale)
    : rct_offsets(std::move(offsets_in)),
      gamma(shape, scale),
      outputs_per_second(0)
  {
    if (!rct_offsets.empty())
    {
      const std::size_t blocks_to_consider = std::min(rct_offsets.size(), blocks_in_a_year);
      const std::uint64_t initial = blocks_to_consider < rct_offsets.size() ?
        rct_offsets[rct_offsets.size() - blocks_to_consider - 1] : 0;
      const std::size_t outputs_to_consider = rct_offsets.back() - initial;

      static_assert(0 < DIFFICULTY_TARGET_V17, "block target time cannot be zero");
      // this assumes constant target over the whole rct range
      outputs_per_second = outputs_to_consider / double(DIFFICULTY_TARGET_V17 * blocks_to_consider);
    }
  }

  bool gamma_picker::is_valid() const noexcept
  {
    return CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE_V17 < rct_offsets.size();
  }

  std::uint64_t gamma_picker::spendable_upper_bound() const noexcept
  {
    if (!is_valid())
      return 0;
    return *(rct_offsets.end() - CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE_V17 - 1);
  }

  std::uint64_t gamma_picker::operator()()
  {
    if (!is_valid())
      throw std::logic_error{"Cannot select random output - blockchain height too small"};

    static_assert(std::is_empty<crypto::random_device>(), "random_device is no longer cheap to construct");
    static constexpr const crypto::random_device engine{};
    const auto end = offsets().end() - CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE_V17;
    const uint64_t num_rct_outputs = spendable_upper_bound();

    for (unsigned tries = 0; tries < 100; ++tries)
    {
      double output_age_in_seconds = std::exp(gamma(engine));

      // shift output back by unlock time to apply distribution from chain tip
      if (output_age_in_seconds > default_unlock_time)
        output_age_in_seconds -= default_unlock_time;
      else
        output_age_in_seconds = crypto::rand_idx(recent_spend_window);

      std::uint64_t output_index = output_age_in_seconds * outputs_per_second;
      if (num_rct_outputs <= output_index)
        continue; // gamma selected older than blockchain height (rare)

      output_index = num_rct_outputs - 1 - output_index;
      const auto selection = std::lower_bound(offsets().begin(), end, output_index);
      if (selection == end)
        throw std::logic_error{"Unable to select random output - output not found in range (should never happen)"};

      const std::uint64_t first_rct = offsets().begin() == selection ? 0 : *(selection - 1);
      const std::uint64_t n_rct = *selection - first_rct;
      if (n_rct != 0)
        return first_rct + crypto::rand_idx(n_rct);
      // block had zero outputs (miner didn't collect XMR?)
    }
    throw std::runtime_error{"Unable to select random output in spendable range using gamma distribution after 100 attempts"};
  }

  std::vector<std::uint64_t> gamma_picker::take_offsets()
  {
    return std::vector<std::uint64_t>{std::move(rct_offsets)};
  }
} // lws
