#include "core/domain/PokemonBall.hpp"

namespace pr {

int pokespriteItemIdForBallId(int ball_id) {
    switch (ball_id) {
        case 1:  // Master Ball
        case 2:  // Ultra Ball
        case 3:  // Great Ball
        case 4:  // Poke Ball
        case 5:  // Safari Ball
        case 6:  // Net Ball
        case 7:  // Dive Ball
        case 8:  // Nest Ball
        case 9:  // Repeat Ball
        case 10: // Timer Ball
        case 11: // Luxury Ball
        case 12: // Premier Ball
        case 13: // Dusk Ball
        case 14: // Heal Ball
        case 15: // Quick Ball
        case 16: // Cherish Ball
            return ball_id;
        case 17: return 492;  // Fast Ball
        case 18: return 493;  // Level Ball
        case 19: return 494;  // Lure Ball
        case 20: return 495;  // Heavy Ball
        case 21: return 496;  // Love Ball
        case 22: return 497;  // Friend Ball
        case 23: return 498;  // Moon Ball
        case 24: return 499;  // Sport Ball
        case 25: return 576;  // Dream Ball
        case 26: return 851;  // Beast Ball
        case 27: return 1785; // Strange Ball
        default:
            return -1;
    }
}

} // namespace pr
