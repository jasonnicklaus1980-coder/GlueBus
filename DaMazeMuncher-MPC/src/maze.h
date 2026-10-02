#pragma once
// The maze (an original layout, 21 x 16). Read by the game and by tools/make_skin.py, which draws the walls.
//   #  wall          .  dot            o  power record (enemies turn scared)
//   ' ' open path    P  player start   H  enemy house (enemies only)   -  house door (enemies only)
// Row 6 runs off both edges: a tunnel that wraps around. Checked by test/host_test.cpp: every row is 21 wide,
// no dead ends, every path cell reachable.
// MAZE-BEGIN
static const char* const kMaze[] = {
    "#####################",
    "#o.................o#",
    "#.##.###.###.###.##.#",
    "#...................#",
    "#.##.#.###-###.#.##.#",
    "#....#.#HHHHH#.#....#",
    "  ...#.#HHHHH#.#...  ",
    "####.#.#######.#.####",
    "#.........P.........#",
    "#.##.####.#.####.##.#",
    "#o.#.............#.o#",
    "##.#.#.#######.#.#.##",
    "#....#.........#....#",
    "#.######.###.######.#",
    "#...................#",
    "#####################",
};
// MAZE-END
