#pragma once
#include <string>
#include <unordered_map>

namespace makellm {

// Curated GD object catalog (name -> object ID), verified against real 2.2 IDs.
inline const std::unordered_map<std::string, int>& objectIds() {
    static const std::unordered_map<std::string, int> ids = {
        {"block_black_gradient_square", 1},
        {"block_grid_patterned_top_square", 2},
        {"block_grid_patterned_inner_square", 5},
        {"block_grid_patterned_pillar_square", 7},
        {"spike_black_gradient_spike", 8},
        {"portal_cube_portal", 12},
        {"portal_ship_portal", 13},
        {"decor_tall_rod", 15},
        {"decor_medium_rod", 16},
        {"decor_short_rod", 17},
        {"jump_pad_yellow_jump_pad", 35},
        {"jump_orb_yellow_jump_orb", 36},
        {"spike_half_black_gradient_spike", 39},
        {"block_black_gradient_single_slab", 40},
        {"decor_tall_chain", 41},
        {"portal_ball_portal", 47},
        {"hazard_non_colorable_wavy_black_pit_hazard", 61},
        {"block_mechanical_square", 76},
        {"block_mechanical_top_square", 77},
        {"block_grid_patterned_square", 83},
        {"decor_large_decorative_gear", 85},
        {"decor_medium_decorative_gear", 86},
        {"decor_small_decorative_gear", 87},
        {"spike_large_black_sawblade", 88},
        {"spike_medium_black_sawblade", 89},
        {"block_black_square", 90},
        {"block_black_top_square", 91},
        {"spike_small_black_sawblade", 98},
        {"spike_small_black_gradient_spike", 103},
        {"decor_small_chain", 110},
        {"portal_ufo_portal", 111},
        {"block_brick_square", 116},
        {"block_brick_top_square", 117},
        {"spike_thorn_black_pit_hazard", 135},
        {"decor_large_wheel", 137},
        {"decor_medium_wheel", 138},
        {"decor_small_wheel", 139},
        {"jump_pad_pink_jump_pad", 140},
        {"jump_orb_pink_jump_orb", 141},
        {"block_breakable_brick", 143},
        {"block_invisible_square", 146},
        {"block_invisible_slab", 147},
        {"block_pulsing_square", 148},
        {"decor_large_blade", 183},
        {"decor_medium_blade", 184},
        {"decor_small_blade", 185},
        {"spike_fake_black_spike", 191},
        {"block_black_gradient_small_square", 195},
        {"spike_fake_black_half_spike", 198},
        {"portal_yellow_slow_speed_portal", 200},
        {"portal_blue_normal_speed_portal", 201},
        {"portal_green_fast_speed_portal", 202},
        {"portal_pink_fast_speed_portal", 203},
        {"spike_colored_spike", 216},
        {"spike_colored_half_spike", 217},
        {"spike_colored_small_spike", 218},
        {"block_black_gradient_slab_middle", 369},
        {"block_black_gradient_slab_side", 370},
        {"spike_black_gradient_tiny_spike", 392},
        {"hazard_non_colorable_round_black_hazard", 446},
        {"portal_wave_portal", 660},
        {"hazard_non_colorable_square_black_hazard", 667},
        {"portal_robot_portal", 745},
        {"portal_spider_portal", 1331},
        {"jump_pad_red_jump_pad", 1332},
        {"jump_orb_red_jump_orb", 1333},
        {"portal_red_fast_speed_portal", 1334},
        {"spike_black_pit_hazard", 1715},
        {"block_beveled_black_slope", 1745},
        {"portal_swing_portal", 1933},
    };
    return ids;
}

inline int idFor(const std::string& name) {
    auto& m = objectIds();
    auto it = m.find(name);
    return it == m.end() ? 0 : it->second;
}

} // namespace makellm
